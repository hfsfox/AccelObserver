#include <transport/mqtt/mqtt_subscriber.hpp>
#include <logger/logger.hpp>
#include <mosquitto.h>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace server
{

// ---------------------------------------------------------------------------
// validate_qos
// Clamps a QoS value to the valid MQTT range [0, 2].
// Logs a warning when the value had to be clamped.
// ---------------------------------------------------------------------------
int MqttSubscriber::validate_qos(int qos, const char* context)
{
    if (qos >= 0 && qos <= 2) return qos;
    LOG_ERRF("[MQTT] QoS %d is out of range (0-2) in %s; clamping to 0", qos, context);
    return 0;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

MqttSubscriber::MqttSubscriber(const std::string& client_id,
                                const std::string& topic,
                                int qos)
    : client_id_(client_id)
    , topic_(topic)
    , qos_(validate_qos(qos, "MqttSubscriber(basic)"))
{
    init_mosquitto();
}

MqttSubscriber::MqttSubscriber(const std::string& client_id,
                                const std::string& topic,
                                int qos,
                                const std::string& username,
                                const std::string& password,
                                const MqttWill& will,
                                int keepalive)
    : client_id_(client_id)
    , topic_(topic)
    , qos_(validate_qos(qos, "MqttSubscriber(full)"))
    , username_(username)
    , password_(password)
    , will_(will)
    , keepalive_(keepalive)
{
    init_mosquitto();
}

// ---------------------------------------------------------------------------
// init_mosquitto
// ---------------------------------------------------------------------------
void MqttSubscriber::init_mosquitto()
{
    mosquitto_lib_init();

    // clean_session=true: do not persist session state on the broker.
    mosq_ = mosquitto_new(
        client_id_.empty() ? nullptr : client_id_.c_str(),
        /*clean_session=*/true,
        this);

    if (!mosq_)
        throw std::runtime_error("[MQTT] mosquitto_new failed (out of memory?)");

    mosquitto_connect_callback_set   (mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);
    mosquitto_message_callback_set   (mosq_, on_message);
    mosquitto_subscribe_callback_set (mosq_, on_subscribe);
    mosquitto_log_callback_set       (mosq_, on_log);

    if (!username_.empty()) {
        int rc = mosquitto_username_pw_set(
            mosq_,
            username_.c_str(),
            password_.empty() ? nullptr : password_.c_str());
        if (rc != MOSQ_ERR_SUCCESS)
            LOG_ERRF("[MQTT] username_pw_set: %s", mosquitto_strerror(rc));
    }

    // Last Will and Testament: published by the broker on unexpected disconnect.
    if (!will_.topic.empty()) {
        int wqos = validate_qos(will_.qos, "MqttWill.qos");
        int rc = mosquitto_will_set(
            mosq_,
            will_.topic.c_str(),
            (int)will_.payload.size(),
            will_.payload.empty() ? nullptr : will_.payload.c_str(),
            wqos,
            will_.retain);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERRF("[MQTT] will_set: %s", mosquitto_strerror(rc));
        } else {
            LOG_INFOF("[MQTT] Will set: topic=%s qos=%d retain=%d",
                      will_.topic.c_str(), wqos, (int)will_.retain);
        }
    }

    // Auto-reconnect: exponential backoff between RECONNECT_DELAY_MIN_S and
    // RECONNECT_DELAY_MAX_S seconds.
    mosquitto_reconnect_delay_set(mosq_,
        RECONNECT_DELAY_MIN_S,
        RECONNECT_DELAY_MAX_S,
        /*exponential_backoff=*/true);
}

// ---------------------------------------------------------------------------

MqttSubscriber::~MqttSubscriber()
{
    stop();
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();
}

bool MqttSubscriber::connect(const std::string& host, uint16_t port)
{
    connected_host_ = host;
    connected_port_ = port;

    LOG_INFOF("[MQTT] Connecting to %s:%u (topic=%s qos=%d client=%s)",
              host.c_str(), (unsigned)port,
              topic_.c_str(), qos_, client_id_.c_str());

    // mosquitto_connect_async does not block; connection is established inside
    // the event loop started by run().
    int rc = mosquitto_connect_async(
        mosq_,
        host.c_str(),
        static_cast<int>(port),
        keepalive_);

    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERRF("[MQTT] connect_async to %s:%u failed: %s",
                 host.c_str(), (unsigned)port, mosquitto_strerror(rc));
        return false;
    }
    return true;
}

void MqttSubscriber::set_callback(MessageCallback cb)
{
    callback_ = std::move(cb);
}

void MqttSubscriber::run()
{
    running_ = true;
    // loop_forever blocks until mosquitto_disconnect() is called from stop().
    int rc = mosquitto_loop_forever(mosq_, /*timeout_ms=*/200, /*max_packets=*/1);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN && running_.load())
        LOG_ERRF("[MQTT] loop_forever exited: %s", mosquitto_strerror(rc));
    running_ = false;
}

void MqttSubscriber::stop()
{
    // Always call mosquitto_disconnect even if running_ is false: stop() may be
    // called after connect() but before run(), leaving an open TCP connection
    // that would only be cleaned up by the destructor otherwise.
    bool was_running = running_.exchange(false);
    if (mosq_) mosquitto_disconnect(mosq_);
    (void)was_running;
}

// ---------------------------------------------------------------------------
// Callbacks (static)
// ---------------------------------------------------------------------------

void MqttSubscriber::on_connect(struct mosquitto* mosq, void* ud, int rc)
{
    auto* self = static_cast<MqttSubscriber*>(ud);
    if (rc != MOSQ_ERR_SUCCESS) {
        // rc here is a MQTT CONNACK return code (MQTT 3.1.1 §3.2.2.3),
        // NOT a MOSQ_ERR_* code.  mosquitto_strerror() does not understand
        // CONNACK codes, so we use a local string table.
        static const char* connack_str[] = {
            "accepted",           // 0
            "bad protocol version", // 1
            "client id rejected", // 2
            "server unavailable", // 3
            "bad credentials",    // 4
            "not authorized",     // 5
        };
        const char* reason = (rc >= 1 && rc <= 5) ? connack_str[rc] : "unknown";
        LOG_ERRF("[MQTT] Connection refused (connack=%d): %s", rc, reason);
        return;
    }
    self->connected_ = true;
    LOG_INFOF("[MQTT] Connected to %s:%u",
              self->connected_host_.c_str(), (unsigned)self->connected_port_);

    int sub_rc = mosquitto_subscribe(mosq, nullptr,
                                     self->topic_.c_str(), self->qos_);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
        LOG_ERRF("[MQTT] subscribe '%s' failed: %s",
                 self->topic_.c_str(), mosquitto_strerror(sub_rc));
    } else {
        LOG_INFOF("[MQTT] Subscribed: %s (qos=%d)",
                  self->topic_.c_str(), self->qos_);
    }
}

void MqttSubscriber::on_disconnect(struct mosquitto*, void* ud, int rc)
{
    auto* self = static_cast<MqttSubscriber*>(ud);
    self->connected_ = false;
    if (rc != 0)
        LOG_WARNF("[MQTT] Unexpected disconnect (rc=%d), will reconnect...", rc);
    else
        LOG_INFO("[MQTT] Disconnected (clean)");
}

void MqttSubscriber::on_message(struct mosquitto*,
                                 void* ud,
                                 const struct mosquitto_message* msg)
{
    if (!ud || !msg || !msg->payload || msg->payloadlen <= 0) return;
    auto* self = static_cast<MqttSubscriber*>(ud);
    if (self->callback_) {
        std::string payload(static_cast<const char*>(msg->payload),
                            static_cast<std::size_t>(msg->payloadlen));
        self->callback_(payload);
    }
}

void MqttSubscriber::on_subscribe(struct mosquitto*, void* ud,
                                   int /*mid*/, int qos_count,
                                   const int* granted_qos)
{
    auto* self = static_cast<MqttSubscriber*>(ud);
    // 0x80 means the broker refused the subscription.
    // Log a warning if the broker granted a lower QoS than requested.
    for (int i = 0; i < qos_count; ++i) {
        if (granted_qos[i] == 0x80) {
            LOG_ERRF("[MQTT] Subscription [%d] to '%s' refused by broker",
                     i, self->topic_.c_str());
        } else if (granted_qos[i] < self->qos_) {
            LOG_WARNF("[MQTT] Subscription [%d] to '%s': "
                      "requested QoS=%d but broker granted QoS=%d",
                      i, self->topic_.c_str(), self->qos_, granted_qos[i]);
        } else {
            LOG_INFOF("[MQTT] Subscription [%d] to '%s' granted QoS=%d",
                      i, self->topic_.c_str(), granted_qos[i]);
        }
    }
}

void MqttSubscriber::on_log(struct mosquitto*, void*, int level,
                              const char* str)
{
    if (level == MOSQ_LOG_ERR || level == MOSQ_LOG_WARNING)
        LOG_WARNF("[MQTT/lib] %s", str);
}

} // namespace server
