#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE  1
#endif

#include <webif/web_interface.hpp>
#include <core/servertypes.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define CLOSE_SOCKET(s) closesocket(s)
#  define SEND_FLAGS      0
typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  ifdef __linux__
#    define SEND_FLAGS     MSG_NOSIGNAL
#  else
#    define SEND_FLAGS     0
#  endif
#  define CLOSE_SOCKET(s) ::close(s)
#  define INVALID_SOCKET  (-1)
#endif

extern "C" {
#include <crypto/sha1.h>
#include <crypto/base64.h>
}

namespace server {
namespace web {

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";


WebInterface::WebInterface()  = default;
WebInterface::~WebInterface() { stop(); }


bool WebInterface::start(const std::string& host, uint16_t port) {
    host_ = host;
    port_ = port;

    listen_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == INVALID_SOCKET) {
        std::perror("[WebIf] socket");
        return false;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                 (const char*)&opt, sizeof(opt));
#if defined(__APPLE__)
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = (host.empty() || host == "0.0.0.0")
                           ? INADDR_ANY : inet_addr(host.c_str());

    if (::bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::perror("[WebUI] bind");
        CLOSE_SOCKET(listen_fd_); listen_fd_ = INVALID_SOCKET;
        return false;
    }
    if (::listen(listen_fd_, 16) < 0) {
        std::perror("[WebUI] listen");
        CLOSE_SOCKET(listen_fd_); listen_fd_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&WebInterface::accept_loop, this);

    std::printf("[WebUI] WebSocket endpoint: ws://%s:%u/\n"
                "[WebUI] Info endpoint:      http://%s:%u/\n",
                host_.empty() ? "0.0.0.0" : host_.c_str(), port_,
                host_.empty() ? "0.0.0.0" : host_.c_str(), port_);
    return true;
}

void WebInterface::stop() {
    if (!running_.exchange(false)) return;

    if (listen_fd_ != INVALID_SOCKET) {
        CLOSE_SOCKET(listen_fd_);
        listen_fd_ = INVALID_SOCKET;
    }
    if (accept_thread_.joinable())
        accept_thread_.join();

    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (int fd : client_fds_) CLOSE_SOCKET(fd);
        client_fds_.clear();
    }
    for (int i = 0; i < 50 && active_clients_.load() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void WebInterface::accept_loop() {
    while (running_.load()) {
        struct sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int cfd = (int)::accept(listen_fd_, (struct sockaddr*)&caddr, &clen);
        if (cfd < 0) break;
        ++active_clients_;
        std::thread([this, cfd]{
            handle_client(cfd);
            --active_clients_;
        }).detach();
    }
}


std::string WebInterface::read_request(int fd) {
    char buf[4096];
    int  total = 0;
    while (total < (int)sizeof(buf) - 1) {
        ssize_t n = ::recv(fd, buf + total,
                           sizeof(buf) - 1 - (size_t)total, 0);
        if (n <= 0) break;
        total += (int)n;
        buf[total] = '\0';
        if (std::strstr(buf, "\r\n\r\n")) break;
    }
    return std::string(buf, (size_t)total);
}

std::string WebInterface::extract_path(const std::string& request) {
    // First line: "GET /path HTTP/1.1"
    auto sp1 = request.find(' ');
    if (sp1 == std::string::npos) return "/";
    auto sp2 = request.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return "/";
    return request.substr(sp1 + 1, sp2 - sp1 - 1);
}


void WebInterface::handle_client(int fd) {
    std::string req = read_request(fd);
    if (req.empty()) { CLOSE_SOCKET(fd); return; }

    bool is_ws = (req.find("Upgrade: websocket") != std::string::npos ||
                  req.find("Upgrade: WebSocket") != std::string::npos);

    if (!is_ws) {
        // Plain HTTP: return a JSON info response with CORS headers.
        std::string path = extract_path(req);
        do_http_response(fd, path);
        CLOSE_SOCKET(fd);
        return;
    }

    if (!do_ws_upgrade(fd, req)) {
        CLOSE_SOCKET(fd);
        return;
    }

    // Send cached device info immediately after upgrade.
    {
        std::lock_guard<std::mutex> lk(device_mutex_);
        if (!device_model_.empty()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "{\"type\":\"device\",\"model\":\"%s\",\"range_g\":%.1f}",
                device_model_.c_str(), device_range_g_);
            ws_send_one(fd, buf);
        }
    }

    // Register for broadcasts.
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        client_fds_.push_back(fd);
    }

    while (running_.load()) {
        if (!ws_discard_frame(fd)) break;
    }

    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), fd),
            client_fds_.end());
    }
    CLOSE_SOCKET(fd);
}


bool WebInterface::do_http_response(int fd, const std::string& path) {
    std::string body;
    int code = 200;
    const char* ctype = "application/json";

    if (path == "/health") {
        body = "{\"status\":\"ok\"}";
    } else {
        // Root: return server info so Vue dev can discover the WebSocket URL.
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"server\":\"data_subscriber\","
            "\"version\":\"3\","
            "\"ws\":\"ws://%s:%u/\","
            "\"clients\":%zu}",
            host_.empty() ? "localhost" : host_.c_str(),
            port_,
            client_count());
        body = buf;
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 " << code << " OK\r\n"
         << "Content-Type: " << ctype << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         // CORS headers allow the Vue dev server (different port) to call this endpoint.
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
    std::string r = resp.str();
    return send_raw(fd, r.c_str(), r.size());
}


bool WebInterface::do_ws_upgrade(int fd, const std::string& request) {
    const char* key_hdr = "Sec-WebSocket-Key: ";
    auto pos = request.find(key_hdr);
    if (pos == std::string::npos) return false;
    pos += std::strlen(key_hdr);
    auto end_pos = request.find("\r\n", pos);
    if (end_pos == std::string::npos) return false;

    std::string client_key = request.substr(pos, end_pos - pos);
    while (!client_key.empty() &&
           (client_key.front() == ' ' || client_key.front() == '\r'))
        client_key.erase(client_key.begin());
    while (!client_key.empty() &&
           (client_key.back() == ' ' || client_key.back() == '\r'))
        client_key.pop_back();

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << ws_accept_key(client_key) << "\r\n"
         // Allow cross-origin WebSocket from Vite dev server.
         << "Access-Control-Allow-Origin: *\r\n"
         << "\r\n";
    std::string r = resp.str();
    return send_raw(fd, r.c_str(), r.size());
}


bool WebInterface::send_raw(int fd, const void* data, size_t len) {
    const char* p   = static_cast<const char*>(data);
    size_t      rem = len;
    while (rem > 0) {
        ssize_t n = ::send(fd, p, rem, SEND_FLAGS);
        if (n <= 0) return false;
        p   += n;
        rem -= (size_t)n;
    }
    return true;
}

void WebInterface::ws_send_one(int fd, const std::string& json) {
    std::string frame = ws_encode_frame(json);
    send_raw(fd, frame.data(), frame.size());
}

void WebInterface::ws_broadcast(const std::string& json) {
    std::string frame = ws_encode_frame(json);

    std::vector<int> snapshot;
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        snapshot = client_fds_;
    }

    std::vector<int> dead;
    for (int fd : snapshot) {
        if (!send_raw(fd, frame.data(), frame.size()))
            dead.push_back(fd);
    }

    if (!dead.empty()) {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (int fd : dead) {
            client_fds_.erase(
                std::remove(client_fds_.begin(), client_fds_.end(), fd),
                client_fds_.end());
            CLOSE_SOCKET(fd);
        }
    }
}

bool WebInterface::ws_discard_frame(int fd) {
    uint8_t hdr[2];
    if (::recv(fd, (char*)hdr, 2, MSG_WAITALL) != 2) return false;

    uint8_t  opcode      = hdr[0] & 0x0F;
    bool     masked      = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (opcode == 0x8) return false;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (::recv(fd, (char*)ext, 2, MSG_WAITALL) != 2) return false;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (::recv(fd, (char*)ext, 8, MSG_WAITALL) != 8) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i)
            payload_len = (payload_len << 8) | ext[i];
    }

    if (masked) {
        uint8_t mask[4];
        if (::recv(fd, (char*)mask, 4, MSG_WAITALL) != 4) return false;
    }

    uint64_t rem = payload_len;
    char     trash[512];
    while (rem > 0) {
        size_t  chunk = (rem > sizeof(trash)) ? sizeof(trash) : (size_t)rem;
        ssize_t got   = ::recv(fd, trash, chunk, 0);
        if (got <= 0) return false;
        rem -= (uint64_t)got;
    }
    return true;
}

std::string WebInterface::ws_encode_frame(const std::string& payload) {
    std::string frame;
    frame.reserve(payload.size() + 10);
    size_t len = payload.size();
    frame.push_back('\x81');
    if (len < 126) {
        frame.push_back((char)(uint8_t)len);
    } else if (len < 65536) {
        frame.push_back('\x7E');
        frame.push_back((char)((len >> 8) & 0xFF));
        frame.push_back((char)(len        & 0xFF));
    } else {
        frame.push_back('\x7F');
        for (int i = 7; i >= 0; --i)
            frame.push_back((char)((len >> (i * 8)) & 0xFF));
    }
    frame.append(payload);
    return frame;
}

std::string WebInterface::ws_accept_key(const std::string& client_key) {
    std::string combined = client_key + WS_GUID;
    uint8_t digest[20];
    sha1_compute(reinterpret_cast<const uint8_t*>(combined.c_str()),
                 combined.size(), digest);
    char encoded[32];
    base64_encode(digest, 20, encoded);
    return std::string(encoded);
}


void WebInterface::set_device_info(const std::string& model, float range_g) {
    std::lock_guard<std::mutex> lk(device_mutex_);
    device_model_   = model;
    device_range_g_ = range_g;
}

void WebInterface::broadcast_data(const DataPacket& pkt,
                                    double latency_ms,
                                    double jitter_ms)
{
    if (client_count() == 0) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"type\":\"data\","
        "\"ts\":%llu,\"seq\":%u,"
        "\"ax\":%.6f,\"ay\":%.6f,\"az\":%.6f,"
        "\"lat\":%.3f,\"jitter\":%.3f}",
        (unsigned long long)pkt.timestamp_ms,
        (unsigned)pkt.sequence_id,
        pkt.acc_x, pkt.acc_y, pkt.acc_z,
        latency_ms, jitter_ms);
    ws_broadcast(buf);
}

void WebInterface::broadcast_stats(const StatsSnapshot& snap) {
    if (client_count() == 0) return;
    ws_broadcast(StatsAnalyzer::to_json(snap));
}

size_t WebInterface::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return client_fds_.size();
}

} /* namespace web */
} /* namespace subscriber */
