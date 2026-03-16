#include <transport/mqtt/mqtt_subscriber.hpp>
#include <logger/logger.hpp>
#include <mosquitto.h>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace server
{

MqttSubscriber::MqttSubscriber(const std::string& client_id,
                                const std::string& topic,
                                int qos)
    : client_id_(client_id), topic_(topic), qos_(qos)
{
    init_mosquitto();
}

MqttSubscriber::MqttSubscriber(const std::string& client_id,
                                const std::string& topic,
                                int qos,
                                const std::string& username,
                                const std::string& password,
                                const MqttWill& will)
    : client_id_(client_id)
    , topic_(topic)
    , qos_(qos)
    , username_(username)
    , password_(password)
    , will_(will)
{
    init_mosquitto();
}

void MqttSubscriber::init_mosquitto() {
    mosquitto_lib_init();

    // clean_session=true: не сохранять состояние на брокере между сессиями
    mosq_ = mosquitto_new(
        client_id_.empty() ? nullptr : client_id_.c_str(),
        /*clean_session=*/true,
        this);

    if (!mosq_)
        throw std::runtime_error("[MQTT] mosquitto_new failed (out of memory?)");

    // Регистрируем все callbacks
    mosquitto_connect_callback_set   (mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);
    mosquitto_message_callback_set   (mosq_, on_message);
    mosquitto_subscribe_callback_set (mosq_, on_subscribe);
    mosquitto_log_callback_set       (mosq_, on_log);

    // Аутентификация
    if (!username_.empty()) {
        int rc = mosquitto_username_pw_set(
            mosq_,
            username_.c_str(),
            password_.empty() ? nullptr : password_.c_str());
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERRF("[MQTT] username_pw_set: %s", mosquitto_strerror(rc));
        }
    }

    // Last Will and Testament
    if (!will_.topic.empty()) {
        int rc = mosquitto_will_set(
            mosq_,
            will_.topic.c_str(),
            (int)will_.payload.size(),
            will_.payload.empty() ? nullptr : will_.payload.c_str(),
            will_.qos,
            will_.retain);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERRF("[MQTT] will_set: %s", mosquitto_strerror(rc));
        } else {
            LOG_INFOF("[MQTT] Will set: topic=%s qos=%d retain=%d",
                      will_.topic.c_str(), will_.qos, (int)will_.retain);
        }
    }

    // Авто-реконнект: min=1с, max=30с, экспоненциальный backoff
    mosquitto_reconnect_delay_set(mosq_,
        RECONNECT_DELAY_MIN_S,
        RECONNECT_DELAY_MAX_S,
        /*exponential_backoff=*/true);
}

// ---------------------------------------------------------------------------
MqttSubscriber::~MqttSubscriber() {
    stop();
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();
}

// connect() — устанавливает TCP-соединение с брокером.
// Поддерживает любой хост (DNS или IP).
bool MqttSubscriber::connect(const std::string& host, uint16_t port) {
    connected_host_ = host;
    connected_port_ = port;

    LOG_INFOF("[MQTT] Connecting to %s:%u (topic=%s qos=%d client=%s)",
              host.c_str(), (unsigned)port,
              topic_.c_str(), qos_, client_id_.c_str());

    // mosquitto_connect_async() — не блокирует, соединение установится в loop
    int rc = mosquitto_connect_async(
        mosq_,
        host.c_str(),
        static_cast<int>(port),
        keepalive_);   /* FIX: использовать член класса, не Config */

    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERRF("[MQTT] connect_async to %s:%u failed: %s",
                 host.c_str(), (unsigned)port, mosquitto_strerror(rc));
        return false;
    }
    return true;
}

void MqttSubscriber::set_callback(MessageCallback cb) {
    callback_ = std::move(cb);
}

// run() — цикл обработки сетевых событий mosquitto.
// mosquitto_loop_forever() сам обрабатывает: reconnect, keepalive, callbacks.
void MqttSubscriber::run() {
    running_ = true;

    // loop_forever блокирует до вызова mosquitto_disconnect() из stop()
    // reconnect_delay=1 — пауза после разрыва перед попыткой реконнекта
    int rc = mosquitto_loop_forever(mosq_, /*timeout_ms=*/200,
                                    /*max_packets=*/1);

    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN && running_.load()) {
        LOG_ERRF("[MQTT] loop_forever exited: %s", mosquitto_strerror(rc));
    }
    running_ = false;
}

void MqttSubscriber::stop() {
    if (!running_.exchange(false)) return;
    if (mosq_) mosquitto_disconnect(mosq_);
}


// Callbacks

void MqttSubscriber::on_connect(struct mosquitto* mosq,
                                 void* ud, int rc)
{
    auto* self = static_cast<MqttSubscriber*>(ud);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERRF("[MQTT] Connection refused (rc=%d): %s",
                 rc, mosquitto_strerror(rc));
        return;
    }
    self->connected_ = true;
    LOG_INFOF("[MQTT] Connected to %s:%u",
              self->connected_host_.c_str(), (unsigned)self->connected_port_);

    // Подписка после успешного подключения (восстанавливается при реконнекте)
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

void MqttSubscriber::on_disconnect(struct mosquitto*, void* ud, int rc) {
    auto* self = static_cast<MqttSubscriber*>(ud);
    self->connected_ = false;
    if (rc != 0) {
        LOG_WARNF("[MQTT] Unexpected disconnect (rc=%d), will reconnect...", rc);
    } else {
        LOG_INFO("[MQTT] Disconnected (clean)");
    }
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
    if (qos_count > 0 && granted_qos[0] == 128) {
        LOG_ERRF("[MQTT] Subscription to '%s' refused by broker",
                 self->topic_.c_str());
    }
}

void MqttSubscriber::on_log(struct mosquitto*, void*, int level,
                              const char* str)
{
    // Передаём только ошибки mosquitto в наш логгер
    if (level == MOSQ_LOG_ERR || level == MOSQ_LOG_WARNING) {
        LOG_WARNF("[MQTT/lib] %s", str);
    }
}

}
