#pragma once
// =============================================================================
// transport/mqtt_subscriber.hpp
// MQTT subscriber built on libmosquitto.
// Supports authentication, Last Will and Testament, QoS 0/1/2,
// auto-reconnect with exponential backoff, arbitrary broker host,
// and topic wildcards.
// =============================================================================
#include <transport/isubscriber.hpp>
#include <atomic>
#include <string>
#include <cstdint>

struct mosquitto;
struct mosquitto_message;

namespace server
{

// Parameters for the MQTT Last Will and Testament message.
// When topic is empty the LWT feature is disabled.
struct MqttWill {
    std::string topic;
    std::string payload;
    int         qos    = 0;
    bool        retain = false;
};

class MqttSubscriber : public ISubscriber {
public:
    // Minimal constructor — only client_id and topic required.
    explicit MqttSubscriber(const std::string& client_id,
                             const std::string& topic,
                             int qos = 0);

    // Full constructor — authentication, LWT, and keepalive interval.
    MqttSubscriber(const std::string& client_id,
                   const std::string& topic,
                   int qos,
                   const std::string& username,
                   const std::string& password,
                   const MqttWill&    will      = MqttWill{},
                   int                keepalive = 60);

    ~MqttSubscriber() override;

    MqttSubscriber(const MqttSubscriber&)            = delete;
    MqttSubscriber& operator=(const MqttSubscriber&) = delete;

    // ISubscriber interface
    bool        connect(const std::string& host, uint16_t port) override;
    void        set_callback(MessageCallback cb) override;
    void        run() override;
    void        stop() override;
    const char* name() const override { return "MQTT"; }

private:
    void init_mosquitto();

    // Clamps qos to [0, 2]; logs a warning if clamping was required.
    static int validate_qos(int qos, const char* context);

    // libmosquitto static callbacks
    static void on_connect   (struct mosquitto*, void* ud, int rc);
    static void on_disconnect(struct mosquitto*, void* ud, int rc);
    static void on_message   (struct mosquitto*, void* ud,
                               const struct mosquitto_message* msg);
    static void on_subscribe (struct mosquitto*, void* ud, int mid,
                               int qos_count, const int* granted_qos);
    static void on_log       (struct mosquitto*, void* ud, int level,
                               const char* str);

    struct mosquitto* mosq_          = nullptr;
    std::string       client_id_;
    std::string       topic_;
    int               qos_           = 0;
    std::string       username_;
    std::string       password_;
    MqttWill          will_;

    std::string       connected_host_;
    uint16_t          connected_port_ = 0;

    MessageCallback   callback_;
    std::atomic<bool> running_       {false};
    std::atomic<bool> connected_     {false};

    // Keepalive interval in seconds (PINGREQ period between client and broker).
    int               keepalive_     = 60;

    // Auto-reconnect: exponential backoff between min and max seconds.
    static constexpr int RECONNECT_DELAY_MIN_S = 1;
    static constexpr int RECONNECT_DELAY_MAX_S = 30;
};

} // namespace server
