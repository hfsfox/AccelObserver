#pragma once
// =============================================================================
// transport/ws_subscriber.hpp
// WebSocket-сервер (RFC 6455).
// Приложение слушает порт, принимает подключения издателей данных,
// выполняет HTTP Upgrade Handshake и читает фреймы.
//
// SHA-1 и Base64 реализованы локально — без внешних зависимостей.
// =============================================================================
#include <transport/isubscriber.hpp>
#include <network/net_platform.hpp>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace server
{

class WsSubscriber : public ISubscriber {
public:
    WsSubscriber();
    ~WsSubscriber() override;

    // Non-copyable
    WsSubscriber(const WsSubscriber&) = delete;
    WsSubscriber& operator=(const WsSubscriber&) = delete;

    bool        connect(const std::string& host, uint16_t port) override;
    void        set_callback(MessageCallback cb) override;
    void        run() override;
    void        stop() override;
    const char* name() const override { return "WebSocket"; }

private:
    bool        do_handshake(socket_t client);
    std::string compute_accept_key(const std::string& client_key);

    // Читает один полный WebSocket-фрейм и записывает payload в out.
    // Возвращает false при ошибке или закрытии соединения.
    bool read_frame(socket_t client, std::string& payload);

    // Отправить WS-фрейм Pong
    void send_pong(socket_t client,
                   const std::vector<uint8_t>& ping_payload);

    // Читать строку до \r\n из сокета (для HTTP-запроса)
    static bool recv_line(socket_t sock, std::string& line);
    // Получить ровно len байт
    static bool recv_exact(socket_t sock, uint8_t* buf, std::size_t len);
    // Отправить ровно len байт
    static bool send_exact(socket_t sock, const void* buf, std::size_t len);

    static void sha1(const uint8_t* data, std::size_t len, uint8_t out[20]);
    static std::string base64_encode(const uint8_t* data, std::size_t len);

    socket_t          server_sock_;
    MessageCallback   callback_;
    std::atomic<bool> running_;
};

} // namespace subscriber
