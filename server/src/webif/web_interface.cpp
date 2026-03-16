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

// FIX 1: MSG_WAITALL is not available on Windows and is unreliable on macOS.
// Replace all MSG_WAITALL recv() calls with a portable loop that guarantees
// exactly `len` bytes are received before returning.
static bool recv_exact_webif(int fd, void* buf, size_t len)
{
    char*  p   = static_cast<char*>(buf);
    size_t rem = len;
    while (rem > 0) {
        // Cast rem to int safely — chunk never exceeds 65536
        int chunk = static_cast<int>(rem < 65536u ? rem : 65536u);
        ssize_t n = ::recv(fd, p, static_cast<size_t>(chunk), 0);
        if (n <= 0) return false;  // connection closed or error
        p   += n;
        rem -= static_cast<size_t>(n);
    }
    return true;
}

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

    // FIX 2: The original code spin-waited for at most 500 ms and then
    // returned while detached client threads might still be alive and
    // accessing 'this' — undefined behaviour / use-after-free.
    // Correct approach: close all client sockets above (unblocks any
    // recv() in the client threads) and then wait with a proper timeout
    // using a condition_variable notified by the thread itself.
    {
        std::unique_lock<std::mutex> lk(clients_done_mutex_);
        clients_done_cv_.wait_for(lk, std::chrono::seconds(3),
            [this]{ return active_clients_.load() == 0; });
    }
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
            // FIX 2 (cont.): decrement and notify stop() that all threads finished
            if (--active_clients_ == 0)
                clients_done_cv_.notify_all();
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
    // FIX 4a: HTTP headers are case-insensitive (RFC 7230 §3.2).
    // Search for the key header in both canonical and lowercase forms.
    auto find_header_value = [&](const std::string& req,
                                  const char* canonical) -> std::string {
        // Try exact case first, then tolower the whole header name part
        size_t pos = req.find(canonical);
        if (pos == std::string::npos) {
            // Try lowercase variant (e.g. "sec-websocket-key: ")
            std::string lower_req = req.substr(0, req.find("\r\n\r\n") + 4);
            // only lowercase the header portion for comparison
            std::string needle(canonical);
            for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : lower_req) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            pos = lower_req.find(needle);
            if (pos == std::string::npos) return {};
        }
        pos += std::strlen(canonical);
        auto end_pos = req.find("\r\n", pos);
        if (end_pos == std::string::npos) return {};
        std::string val = req.substr(pos, end_pos - pos);
        // trim leading/trailing whitespace
        while (!val.empty() && (val.front() == ' ' || val.front() == '\r')) val.erase(val.begin());
        while (!val.empty() && (val.back()  == ' ' || val.back()  == '\r')) val.pop_back();
        return val;
    };

    std::string client_key = find_header_value(request, "Sec-WebSocket-Key: ");
    if (client_key.empty()) return false;

    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << ws_accept_key(client_key) << "\r\n"
         // FIX 4b: Access-Control-Allow-Origin is NOT part of the WebSocket
         // upgrade protocol (RFC 6455 §4.2.2). Browsers use the Origin header
         // for WS access control, not ACAO. Some proxies reject non-standard
         // headers in 101 responses, causing the upgrade to silently fail.
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
    // FIX 1 (cont.): replaced MSG_WAITALL with portable recv_exact_webif()
    if (!recv_exact_webif(fd, hdr, 2)) return false;

    uint8_t  opcode      = hdr[0] & 0x0F;
    bool     masked      = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    // FIX 5: Close frame
    if (opcode == 0x08) return false;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (!recv_exact_webif(fd, ext, 2)) return false;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!recv_exact_webif(fd, ext, 8)) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i)
            payload_len = (payload_len << 8) | ext[i];
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (!recv_exact_webif(fd, mask_key, 4)) return false;
    }

    // Read payload into buffer (needed for Ping echo and for unmasking)
    std::vector<uint8_t> data(static_cast<size_t>(payload_len));
    if (payload_len > 0) {
        if (!recv_exact_webif(fd, data.data(), static_cast<size_t>(payload_len)))
            return false;
        // Unmask if needed
        if (masked)
            for (size_t i = 0; i < data.size(); ++i)
                data[i] ^= mask_key[i & 3u];
    }

    // FIX 5: Respond to Ping with Pong (RFC 6455 §5.5.2).
    // Without this the browser's WS keep-alive times out and drops the
    // connection while the server is still broadcasting data.
    if (opcode == 0x09) {
        // Pong: FIN=1, opcode=0xA, no mask, same payload as ping
        uint8_t pong[2];
        pong[0] = 0x8A;  // FIN | opcode=Pong
        pong[1] = static_cast<uint8_t>(data.size() & 0x7F);
        send_raw(fd, pong, 2);
        if (!data.empty())
            send_raw(fd, data.data(), data.size());
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
} /* namespace server */
