#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE  1
#endif

#include <webif/web_interface.hpp>
#include <core/servertypes.hpp>

#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define CLOSE_SOCKET(s)  closesocket(s)
#  define SHUT_SOCKET(s)   ::shutdown(s, SD_BOTH)
#  define SEND_FLAGS       0
typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  ifdef __linux__
#    define SEND_FLAGS     MSG_NOSIGNAL
#  else
#    define SEND_FLAGS     0
#  endif
#  define CLOSE_SOCKET(s)  ::close(s)
#  define SHUT_SOCKET(s)   ::shutdown(s, SHUT_RDWR)
#  define INVALID_SOCKET   (-1)
#endif

// ---------------------------------------------------------------------------
// recv_exact_webif
// Portable replacement for MSG_WAITALL: reads exactly `len` bytes.
// Returns false on connection close or error.
// ---------------------------------------------------------------------------
static bool recv_exact_webif(int fd, void* buf, size_t len)
{
    char*  p   = static_cast<char*>(buf);
    size_t rem = len;
    while (rem > 0) {
        int   chunk = static_cast<int>(rem < 65536u ? rem : 65536u);
        ssize_t n   = ::recv(fd, p, static_cast<size_t>(chunk), 0);
        if (n <= 0) return false;
        p   += n;
        rem -= static_cast<size_t>(n);
    }
    return true;
}

extern "C" {
#include <crypto/sha1.h>
#include <crypto/base64.h>
}

#include <logger/logger.hpp>

namespace server {
namespace web {

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WebInterface::WebInterface()  = default;
WebInterface::~WebInterface() { stop(); }

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool WebInterface::start(const std::string& host, uint16_t port)
{
    host_ = host;
    port_ = port;

    listen_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == INVALID_SOCKET)
    {
        LOG_ERRF("[WebIF] socket() failed: %s", std::strerror(errno));
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

    if (::bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LOG_ERRF("[WebIF] bind() on %s:%u failed: %s",
                 host.empty() ? "0.0.0.0" : host.c_str(), port_,
                 std::strerror(errno));
        CLOSE_SOCKET(listen_fd_); listen_fd_ = INVALID_SOCKET;
        return false;
    }
    if (::listen(listen_fd_, 16) < 0)
    {
        LOG_ERRF("[WebIF] listen() failed: %s", std::strerror(errno));
        CLOSE_SOCKET(listen_fd_); listen_fd_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&WebInterface::accept_loop, this);

    LOG_INFOF("[WebIF] ACTIVE  ws://%s:%u/  http://%s:%u/",
              host_.empty() ? "0.0.0.0" : host_.c_str(), port_,
              host_.empty() ? "0.0.0.0" : host_.c_str(), port_);
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void WebInterface::stop()
{
    if (!running_.exchange(false)) return;

    // Closing the listen socket unblocks accept() in accept_loop.
    if (listen_fd_ != INVALID_SOCKET)
    {
        CLOSE_SOCKET(listen_fd_);
        listen_fd_ = INVALID_SOCKET;
    }
    if (accept_thread_.joinable())
        accept_thread_.join();

    // Shut down all active client sockets so their recv() calls unblock.
    // The client threads themselves call CLOSE_SOCKET after exiting their loop.
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (int fd : client_fds_) SHUT_SOCKET(fd);
        client_fds_.clear();
    }

    // Wait for all client threads to finish (up to 3 s).
    {
        std::unique_lock<std::mutex> lk(clients_done_mutex_);
        clients_done_cv_.wait_for(lk, std::chrono::seconds(3),
            [this]{ return active_clients_.load() == 0; });
    }
}

// ---------------------------------------------------------------------------
// accept_loop
// ---------------------------------------------------------------------------
void WebInterface::accept_loop()
{
    while (running_.load()) {
        struct sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int cfd = (int)::accept(listen_fd_, (struct sockaddr*)&caddr, &clen);
        if (cfd < 0) break;

        // Enable TCP keep-alive so the OS detects silently dead browser
        // connections without requiring application-level pings.
        {
            int ka = 1;
            ::setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE,
                         (const char*)&ka, sizeof(ka));
#if defined(__linux__)
            int idle = 30, interval = 5, probes = 3;
            ::setsockopt(cfd, IPPROTO_TCP, TCP_KEEPIDLE,    &idle,     sizeof(idle));
            ::setsockopt(cfd, IPPROTO_TCP, TCP_KEEPINTVL,   &interval, sizeof(interval));
            ::setsockopt(cfd, IPPROTO_TCP, TCP_KEEPCNT,     &probes,   sizeof(probes));
#endif
        }

        ++active_clients_;
        std::thread([this, cfd]{
            handle_client(cfd);
            if (--active_clients_ == 0)
                clients_done_cv_.notify_all();
        }).detach();
    }
}

// ---------------------------------------------------------------------------
// read_request
// ---------------------------------------------------------------------------
std::string WebInterface::read_request(int fd)
{
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

// ---------------------------------------------------------------------------
// extract_path
// ---------------------------------------------------------------------------
std::string WebInterface::extract_path(const std::string& request)
{
    auto sp1 = request.find(' ');
    if (sp1 == std::string::npos) return "/";
    auto sp2 = request.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return "/";
    return request.substr(sp1 + 1, sp2 - sp1 - 1);
}

// ---------------------------------------------------------------------------
// handle_client
// ---------------------------------------------------------------------------
void WebInterface::handle_client(int fd)
{
    std::string req = read_request(fd);
    if (req.empty()) { CLOSE_SOCKET(fd); return; }

    bool is_ws = (req.find("Upgrade: websocket") != std::string::npos ||
                  req.find("Upgrade: WebSocket") != std::string::npos);

    if (!is_ws) {
        std::string path = extract_path(req);
        do_http_response(fd, path);
        CLOSE_SOCKET(fd);
        return;
    }

    if (!do_ws_upgrade(fd, req)) {
        CLOSE_SOCKET(fd);
        return;
    }

    // Send cached device info immediately after the upgrade.
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

    // Register fd for broadcasts BEFORE entering the read loop.
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        client_fds_.push_back(fd);
    }

    // Discard incoming frames (browser pings, close frames).
    // ws_broadcast() may shut down this fd from another thread; that will
    // cause recv() inside ws_discard_frame() to return an error, which breaks
    // this loop cleanly.  The fd is NOT closed by ws_broadcast — only this
    // thread calls CLOSE_SOCKET so there is no double-close.
    while (running_.load()) {
        if (!ws_discard_frame(fd)) break;
    }

    // Remove from broadcast list.  ws_broadcast may have already done this
    // if it detected a send error on fd; the erase is a no-op in that case.
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        client_fds_.erase(
            std::remove(client_fds_.begin(), client_fds_.end(), fd),
            client_fds_.end());
    }

    // This thread is the sole owner of fd at this point.
    CLOSE_SOCKET(fd);
}

// ---------------------------------------------------------------------------
// do_http_response
// ---------------------------------------------------------------------------
bool WebInterface::do_http_response(int fd, const std::string& path)
{
    std::string body;
    int code = 200;
    const char* ctype = "application/json";

    if (path == "/health")
    {
        body = "{\"status\":\"ok\"}";
    }
    else
    {
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
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
    std::string r = resp.str();
    return send_raw(fd, r.c_str(), r.size());
}

// ---------------------------------------------------------------------------
// do_ws_upgrade
// ---------------------------------------------------------------------------
bool WebInterface::do_ws_upgrade(int fd, const std::string& request)
{
    // HTTP headers are case-insensitive (RFC 7230 §3.2).
    // Search for the key header in canonical form first, then lowercase.
    auto find_header_value = [&](const std::string& req,
                                  const char* canonical) -> std::string
                                  {
        size_t pos = req.find(canonical);
        if (pos == std::string::npos)
        {
            std::string lower_req = req.substr(0, req.find("\r\n\r\n") + 4);
            std::string needle(canonical);
            for (char& c : needle)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : lower_req)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            pos = lower_req.find(needle);
            if (pos == std::string::npos) return {};
        }
        pos += std::strlen(canonical);
        auto end_pos = req.find("\r\n", pos);
        if (end_pos == std::string::npos) return {};
        std::string val = req.substr(pos, end_pos - pos);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\r'))
            val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '\r'))
            val.pop_back();
        return val;
    };

    std::string client_key = find_header_value(request, "Sec-WebSocket-Key: ");
    if (client_key.empty()) return false;

    // RFC 6455 §4.2.2: the 101 response must not include
    // Access-Control-Allow-Origin; non-standard headers in upgrade responses
    // can cause proxies to reject the connection.
    std::ostringstream resp;
    resp << "HTTP/1.1 101 Switching Protocols\r\n"
         << "Upgrade: websocket\r\n"
         << "Connection: Upgrade\r\n"
         << "Sec-WebSocket-Accept: " << ws_accept_key(client_key) << "\r\n"
         << "\r\n";
    std::string r = resp.str();
    return send_raw(fd, r.c_str(), r.size());
}

// ---------------------------------------------------------------------------
// send_raw
// ---------------------------------------------------------------------------
bool WebInterface::send_raw(int fd, const void* data, size_t len)
{
    const char* p   = static_cast<const char*>(data);
    size_t      rem = len;
    while (rem > 0)
    {
        ssize_t n = ::send(fd, p, rem, SEND_FLAGS);
        if (n <= 0) return false;
        p   += n;
        rem -= (size_t)n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ws_send_one
// ---------------------------------------------------------------------------
void WebInterface::ws_send_one(int fd, const std::string& json)
{
    std::string frame = ws_encode_frame(json);
    send_raw(fd, frame.data(), frame.size());
}

// ---------------------------------------------------------------------------
// ws_broadcast
// Sends a frame to all connected browser clients.
// Dead connections are shut down and removed from the list.
// The actual CLOSE_SOCKET is performed by the corresponding handle_client
// thread to avoid double-close races.
// ---------------------------------------------------------------------------
void WebInterface::ws_broadcast(const std::string& json)
{
    std::string frame = ws_encode_frame(json);

    std::vector<int> snapshot;
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        snapshot = client_fds_;
    }

    std::vector<int> dead;
    for (int fd : snapshot)
    {
        if (!send_raw(fd, frame.data(), frame.size()))
            dead.push_back(fd);
    }

    if (!dead.empty()) {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (int fd : dead) {
            client_fds_.erase(
                std::remove(client_fds_.begin(), client_fds_.end(), fd),
                client_fds_.end());
            // Shut down the socket so recv() in handle_client() unblocks and
            // that thread calls CLOSE_SOCKET.  We must NOT call CLOSE_SOCKET
            // here because handle_client always closes fd itself — doing so
            // from two threads causes a double-close and potential fd-reuse bug.
            SHUT_SOCKET(fd);
        }
    }
}

// ---------------------------------------------------------------------------
// ws_discard_frame
// Reads one incoming WebSocket frame from the browser and discards its
// payload.  Responds to Ping frames with Pong (RFC 6455 §5.5.2).
// Returns false on close frame, error, or connection drop.
// ---------------------------------------------------------------------------
bool WebInterface::ws_discard_frame(int fd)
{
    uint8_t hdr[2];
    if (!recv_exact_webif(fd, hdr, 2)) return false;

    uint8_t  opcode      = hdr[0] & 0x0F;
    bool     masked      = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (opcode == 0x08) return false;  // Close frame

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

    std::vector<uint8_t> data(static_cast<size_t>(payload_len));
    if (payload_len > 0) {
        if (!recv_exact_webif(fd, data.data(), static_cast<size_t>(payload_len)))
            return false;
        if (masked)
            for (size_t i = 0; i < data.size(); ++i)
                data[i] ^= mask_key[i & 3u];
    }

    // Respond to Ping with Pong (RFC 6455 §5.5.2).
    // Without this the browser WebSocket keep-alive times out and drops the
    // connection while the server is still broadcasting data.
    if (opcode == 0x09) {
        uint8_t pong[2];
        pong[0] = 0x8A;  // FIN=1, opcode=Pong
        pong[1] = static_cast<uint8_t>(data.size() & 0x7F);
        send_raw(fd, pong, 2);
        if (!data.empty())
            send_raw(fd, data.data(), data.size());
    }

    return true;
}

// ---------------------------------------------------------------------------
// ws_encode_frame  (RFC 6455 §5.2, server side: no masking)
// ---------------------------------------------------------------------------
std::string WebInterface::ws_encode_frame(const std::string& payload)
{
    std::string frame;
    frame.reserve(payload.size() + 10);
    size_t len = payload.size();
    frame.push_back('\x81');  // FIN=1, opcode=Text
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

// ---------------------------------------------------------------------------
// ws_accept_key  (RFC 6455 §4.2.2)
// ---------------------------------------------------------------------------
std::string WebInterface::ws_accept_key(const std::string& client_key)
{
    std::string combined = client_key + WS_GUID;
    uint8_t digest[20];
    sha1_compute(reinterpret_cast<const uint8_t*>(combined.c_str()),
                 combined.size(), digest);
    char encoded[32];
    base64_encode(digest, 20, encoded);
    return std::string(encoded);
}

// ---------------------------------------------------------------------------

void WebInterface::set_device_info(const std::string& model, float range_g)
{
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

void WebInterface::broadcast_stats(const StatsSnapshot& snap)
{
    if (client_count() == 0) return;
    ws_broadcast(StatsAnalyzer::to_json(snap));
}

size_t WebInterface::client_count() const
{
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return client_fds_.size();
}

} // namespace web
} // namespace server
