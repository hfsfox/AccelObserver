// =============================================================================
// transport/ws_subscriber.cpp
// WebSocket Server Subscriber — реализация RFC 6455
// SHA-1: RFC 3174, Base64: RFC 4648
// =============================================================================
#include <transport/websocket/ws_subscriber.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstddef>

// Удалить предупреждение «unreferenced parameter» в MSVC/GCC для unused переменных
#ifdef _WIN32
#  define UNUSED(x) (void)(x)
#else
#  define UNUSED(x) (void)(x)
#endif

//#define NOMINMAX

// ============================================================================
// SHA-1 (RFC 3174) — реализован локально
// ============================================================================
namespace {

// Circular left shift
inline uint32_t rotl32(uint32_t v, unsigned n) {
    return (v << n) | (v >> (32u - n));
}

// Вычисляет SHA-1 дайджест для произвольного буфера данных.
// out должен быть массивом из 20 байт.
void sha1_compute(const uint8_t* data, std::size_t len, uint8_t out[20]) {
    // Начальные значения хеш-буфера (§6.1 RFC 3174)
    uint32_t h[5] = {
        0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u
    };

    // Паддинг: добавляем 0x80, нули, и длину сообщения в битах (big-endian uint64)
    const std::size_t padded_len = ((len + 8u) / 64u + 1u) * 64u;
    std::vector<uint8_t> padded(padded_len, 0u);
    std::memcpy(padded.data(), data, len);
    padded[len] = 0x80u;

    uint64_t bit_len = static_cast<uint64_t>(len) * 8u;
    // Записываем big-endian в последние 8 байт
    for (int i = 7; i >= 0; --i) {
        padded[padded_len - 8u + static_cast<std::size_t>(7 - i)] =
            static_cast<uint8_t>(bit_len >> (static_cast<unsigned>(i) * 8u));
    }

    // Обработка каждого блока по 512 бит (64 байта)
    for (std::size_t blk = 0; blk < padded_len; blk += 64u) {
        uint32_t W[80];

        // Шаг 1: разбить блок на 16 слов по 32 бита (big-endian)
        for (int t = 0; t < 16; ++t) {
            W[t] = (static_cast<uint32_t>(padded[blk + static_cast<std::size_t>(t)*4u    ]) << 24u) |
                   (static_cast<uint32_t>(padded[blk + static_cast<std::size_t>(t)*4u + 1u]) << 16u) |
                   (static_cast<uint32_t>(padded[blk + static_cast<std::size_t>(t)*4u + 2u]) <<  8u) |
                   (static_cast<uint32_t>(padded[blk + static_cast<std::size_t>(t)*4u + 3u]));
        }
        // Шаг 2: расширить до 80 слов
        for (int t = 16; t < 80; ++t)
            W[t] = rotl32(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16], 1u);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int t = 0; t < 80; ++t) {
            uint32_t f, k;
            if      (t < 20) { f = (b & c) | (~b & d);              k = 0x5A827999u; }
            else if (t < 40) { f = b ^ c ^ d;                        k = 0x6ED9EBA1u; }
            else if (t < 60) { f = (b & c) | (b & d) | (c & d);     k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                        k = 0xCA62C1D6u; }

            uint32_t tmp = rotl32(a, 5u) + f + e + k + W[t];
            e = d; d = c; c = rotl32(b, 30u); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    // Результат — 20 байт big-endian
    for (int i = 0; i < 5; ++i) {
        out[i*4    ] = static_cast<uint8_t>(h[i] >> 24u);
        out[i*4 + 1] = static_cast<uint8_t>(h[i] >> 16u);
        out[i*4 + 2] = static_cast<uint8_t>(h[i] >>  8u);
        out[i*4 + 3] = static_cast<uint8_t>(h[i]       );
    }
}

// ============================================================================
// Base64 encode (RFC 4648 §4)
// ============================================================================
static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode_impl(const uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2u) / 3u) * 4u);

    for (std::size_t i = 0; i < len; i += 3u) {
        uint32_t b  = static_cast<uint32_t>(data[i]) << 16u;
        if (i + 1u < len) b |= static_cast<uint32_t>(data[i + 1u]) << 8u;
        if (i + 2u < len) b |= static_cast<uint32_t>(data[i + 2u]);

        out += BASE64_CHARS[(b >> 18u) & 0x3Fu];
        out += BASE64_CHARS[(b >> 12u) & 0x3Fu];
        out += (i + 1u < len) ? BASE64_CHARS[(b >>  6u) & 0x3Fu] : '=';
        out += (i + 2u < len) ? BASE64_CHARS[(b       ) & 0x3Fu] : '=';
    }
    return out;
}

} // anonymous namespace

// ============================================================================
// WsSubscriber
// ============================================================================
namespace server {

WsSubscriber::WsSubscriber()
    : server_sock_(INVALID_SOCK)
    , running_(false)
{}

WsSubscriber::~WsSubscriber() {
    stop();
    if (server_sock_ != INVALID_SOCK) {
        SOCK_CLOSE(server_sock_);
        server_sock_ = INVALID_SOCK;
    }
}

// ---------------------------------------------------------------------------
// connect() — для WebSocket-сервера это означает «начать слушать порт».
// Параметр host игнорируется (INADDR_ANY).
// ---------------------------------------------------------------------------
bool WsSubscriber::connect(const std::string& /*host*/, uint16_t port) {
    server_sock_ = platform::create_server_socket(port);
    if (server_sock_ == INVALID_SOCK) {
        std::cerr << "[WS] Failed to create server socket: "
                  << platform::last_socket_error() << "\n";
        return false;
    }
    std::cout << "[WS] Listening on 0.0.0.0:" << port << "\n";
    return true;
}

void WsSubscriber::set_callback(MessageCallback cb) {
    callback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// run() — основной цикл: accept → handshake → чтение фреймов.
// Поддерживает несколько последовательных клиентских подключений.
// ---------------------------------------------------------------------------
void WsSubscriber::run() {
    running_ = true;

    while (running_.load()) {
        // Используем select с таймаутом для проверки running_ без блокировки
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_sock_, &rfds);

        struct timeval tv;
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        // На Windows первый аргумент select игнорируется
        int sel = ::select(static_cast<int>(server_sock_ + 1),
                           &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;  // timeout или EINTR

        socket_t client = platform::accept_client(server_sock_);
        if (client == INVALID_SOCK) continue;

        std::cout << "[WS] Client connected\n";

        // ---- Handshake ----
        if (!do_handshake(client)) {
            std::cerr << "[WS] Handshake failed\n";
            SOCK_CLOSE(client);
            continue;
        }
        std::cout << "[WS] Handshake OK\n";

        // ---- Receive frames ----
        std::string payload;
        while (running_.load()) {
            bool ok = read_frame(client, payload);
            if (!ok) {
                std::cout << "[WS] Client disconnected\n";
                break;
            }
            if (!payload.empty() && callback_) {
                callback_(payload);
            }
        }

        SOCK_CLOSE(client);
    }
}

void WsSubscriber::stop() {
    running_ = false;
}

// ---------------------------------------------------------------------------
// HTTP Upgrade Handshake (RFC 6455 §4)
// ---------------------------------------------------------------------------
bool WsSubscriber::do_handshake(socket_t client) {
    std::string line;
    std::string ws_key;
    bool upgrade_websocket = false;
    bool connection_upgrade = false;

    // Первая строка HTTP-запроса (GET / HTTP/1.1)
    if (!recv_line(client, line)) return false;
    // Не проверяем URI/метод строго — достаточно наличия ключа

    // Читаем заголовки до пустой строки
    while (recv_line(client, line) && !line.empty()) {
        // Поиск нечувствительный к регистру (простая версия — toLowerCase первого символа)
        // Sec-WebSocket-Key
        if (line.find("Sec-WebSocket-Key:") != std::string::npos ||
            line.find("Sec-Websocket-Key:") != std::string::npos)
        {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                ws_key = line.substr(colon + 1u);
                // Обрезать пробелы
                const std::string ws_chars = " \t\r\n";
                auto start = ws_key.find_first_not_of(ws_chars);
                auto end   = ws_key.find_last_not_of(ws_chars);
                ws_key = (start == std::string::npos) ? "" : ws_key.substr(start, end - start + 1u);
            }
        }
        // Upgrade: websocket
        if (line.find("websocket") != std::string::npos ||
            line.find("WebSocket") != std::string::npos) {
            upgrade_websocket = true;
        }
        // Connection: Upgrade
        if (line.find("Upgrade") != std::string::npos) {
            connection_upgrade = true;
        }
    }

    UNUSED(upgrade_websocket);
    UNUSED(connection_upgrade);

    if (ws_key.empty()) return false;

    std::string accept = compute_accept_key(ws_key);

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << accept << "\r\n"
         << "\r\n";

    std::string r = resp.str();
    return send_exact(client, r.c_str(), r.size());
}

// ---------------------------------------------------------------------------
// RFC 6455 §4.2.2 — вычислить Sec-WebSocket-Accept
// accept = Base64( SHA1( client_key + GUID ) )
// ---------------------------------------------------------------------------
std::string WsSubscriber::compute_accept_key(const std::string& client_key) {
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input = client_key + GUID;
    uint8_t digest[20];
    sha1_compute(reinterpret_cast<const uint8_t*>(input.c_str()), input.size(), digest);
    return base64_encode_impl(digest, 20u);
}

// ---------------------------------------------------------------------------
// Чтение одного WebSocket-фрейма (RFC 6455 §5.2)
// Возвращает false при закрытии соединения или ошибке.
// payload заполняется только для текстовых (0x1) и бинарных (0x2) фреймов.
// ---------------------------------------------------------------------------
bool WsSubscriber::read_frame(socket_t client, std::string& payload) {
    payload.clear();

    // Байты заголовка (минимальные 2 байта)
    uint8_t hdr[2];
    if (!recv_exact(client, hdr, 2u)) return false;

    // const bool fin     = (hdr[0] & 0x80u) != 0u;
    const uint8_t opcode   = hdr[0] & 0x0Fu;
    const bool    masked   = (hdr[1] & 0x80u) != 0u;
    uint64_t      pay_len  = hdr[1] & 0x7Fu;

    // Расширенная длина полезной нагрузки
    if (pay_len == 126u) {
        uint8_t ext[2];
        if (!recv_exact(client, ext, 2u)) return false;
        pay_len = (static_cast<uint64_t>(ext[0]) << 8u) |
                   static_cast<uint64_t>(ext[1]);
    } else if (pay_len == 127u) {
        uint8_t ext[8];
        if (!recv_exact(client, ext, 8u)) return false;
        pay_len = 0u;
        for (int i = 0; i < 8; ++i)
            pay_len = (pay_len << 8u) | static_cast<uint64_t>(ext[i]);
    }

    // Маскирующий ключ (клиент → сервер всегда маскирует, §5.3)
    uint8_t mask_key[4] = {0u, 0u, 0u, 0u};
    if (masked) {
        if (!recv_exact(client, mask_key, 4u)) return false;
    }

    // ---- Обработка управляющих фреймов ----

    if (opcode == 0x08u) {
        // Close frame
        return false;
    }

    if (opcode == 0x09u) {
        // Ping → ответить Pong (RFC 6455 §5.5.2)
        std::vector<uint8_t> ping_data(static_cast<std::size_t>(pay_len));
        if (pay_len > 0u && !recv_exact(client, ping_data.data(),
                                        static_cast<std::size_t>(pay_len)))
            return false;
        if (masked)
            for (std::size_t i = 0; i < ping_data.size(); ++i)
                ping_data[i] ^= mask_key[i & 3u];
        send_pong(client, ping_data);
        return true;  // продолжить чтение
    }

    if (opcode == 0x0Au) {
        // Pong — игнорируем
        std::vector<uint8_t> tmp(static_cast<std::size_t>(pay_len));
        if (pay_len > 0u)
            recv_exact(client, tmp.data(), static_cast<std::size_t>(pay_len));
        return true;
    }

    // ---- Данные (text=0x01, binary=0x02, continuation=0x00) ----
    if (pay_len > 0u) {
        std::vector<uint8_t> data(static_cast<std::size_t>(pay_len));
        if (!recv_exact(client, data.data(), static_cast<std::size_t>(pay_len)))
            return false;
        if (masked)
            for (std::size_t i = 0; i < data.size(); ++i)
                data[i] ^= mask_key[i & 3u];
        payload.assign(reinterpret_cast<const char*>(data.data()), data.size());
    }

    return true;
}

void WsSubscriber::send_pong(socket_t client,
                              const std::vector<uint8_t>& ping_payload)
{
    // Pong frame: FIN=1, opcode=0xA, no mask, payload length
    const std::size_t len = ping_payload.size();
    if (len > 125u) return;  // управляющие фреймы ≤ 125 байт (§5.5)

    uint8_t frame[2] = {
        0x8Au,                          // FIN | opcode=Pong
        static_cast<uint8_t>(len)       // no mask bit, length
    };
    send_exact(client, frame, 2u);
    if (len > 0u)
        send_exact(client, ping_payload.data(), len);
}

// ---------------------------------------------------------------------------
// Вспомогательные методы I/O
// ---------------------------------------------------------------------------
bool WsSubscriber::recv_line(socket_t sock, std::string& line) {
    line.clear();
    char c;
    while (true) {
        int n = ::recv(sock, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        line += c;
    }
}

bool WsSubscriber::recv_exact(socket_t sock, uint8_t* buf, std::size_t len)
{
    std::size_t received = 0u;

    while (received < len) {

        // Ограничиваем размер одного вызова recv размером int
        const int chunk =
        static_cast<int>/*(int)*/(
            std::min(
                len - received,
                     static_cast<std::size_t>/*(std::size_t)*/(65536)
                     )
        );
        int n = ::recv(sock,
                       reinterpret_cast<char*>(buf + received),
                       chunk, 0);
        if (n <= 0) return false;
        received += static_cast<std::size_t>(n);
    }
    return true;
}

bool WsSubscriber::send_exact(socket_t sock, const void* buf, std::size_t len) {
    std::size_t sent = 0u;
    const auto* ptr = static_cast<const char*>(buf);
    while (sent < len)
    {

        //const int chunk = NULL;
        const int chunk = static_cast<int>/*(int)*/(
            std::min((std::size_t)(len - sent), static_cast<std::size_t>/*(std::size_t)*/(65536)
            )
        );
        int n = ::send(sock, ptr + sent, chunk, 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);

    }
    return true;
}

// Делегируем к локальным функциям
void WsSubscriber::sha1(const uint8_t* data, std::size_t len, uint8_t out[20]) {
    sha1_compute(data, len, out);
}

std::string WsSubscriber::base64_encode(const uint8_t* data, std::size_t len) {
    return base64_encode_impl(data, len);
}

} // namespace subscriber
