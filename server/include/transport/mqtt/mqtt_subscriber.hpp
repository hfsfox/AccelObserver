#pragma once
// =============================================================================
// transport/mqtt_subscriber.hpp  (v2)
// MQTT-подписчик на базе libmosquitto с полной поддержкой брокера.
// Новое: аутентификация, Last Will, QoS 0/1/2, авто-реконнект с backoff,
//        произвольный хост (не только localhost), топики с wildcard.
// =============================================================================
#include <transport/isubscriber.hpp>
#include <atomic>
#include <string>
#include <cstdint>

struct mosquitto;
struct mosquitto_message;

namespace server
{


// Параметры Last Will and Testament (опционально)

struct MqttWill {
    std::string topic;    ///< пустой → Will не используется
    std::string payload;
    int         qos  = 0;
    bool        retain = false;
};


class MqttSubscriber : public ISubscriber {
public:
    // Минимальный конструктор — только client_id и topic
    explicit MqttSubscriber(const std::string& client_id,
                             const std::string& topic,
                             int qos = 0);

    // Расширенный конструктор — с аутентификацией, Will и keepalive
    // FIX: keepalive added so cfg.mqtt_keepalive is actually applied
    MqttSubscriber(const std::string& client_id,
                   const std::string& topic,
                   int qos,
                   const std::string& username,
                   const std::string& password,
                   const MqttWill& will     = MqttWill{},
                   int               keepalive = 60);

    ~MqttSubscriber() override;

    MqttSubscriber(const MqttSubscriber&) = delete;
    MqttSubscriber& operator=(const MqttSubscriber&) = delete;

    // ISubscriber interface
    bool        connect(const std::string& host, uint16_t port) override;
    void        set_callback(MessageCallback cb) override;
    void        run() override;
    void        stop() override;
    const char* name() const override { return "MQTT"; }

private:
    void init_mosquitto();

    // Статические callback-и libmosquitto
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

    // keepalive в секундах: интервал PINGREQ между клиентом и брокером
    int               keepalive_     = 60;

    // Параметры авто-реконнекта
    static constexpr int RECONNECT_DELAY_MIN_S = 1;
    static constexpr int RECONNECT_DELAY_MAX_S = 30;
};

} // namespace subscriber
