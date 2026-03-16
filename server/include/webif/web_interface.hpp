#pragma once
/*
 * web/web_interface.hpp
 * Pure WebSocket data server for the Vue.js dashboard.
 *
 * Protocol:
 *   HTTP GET /        returns a JSON info object (no HTML served).
 *   HTTP GET /health  returns {"status":"ok"} for readiness checks.
 *   WS  /            upgraded connection receives JSON frames:
 *
 *     {"type":"data","ts":N,"seq":N,"ax":f,"ay":f,"az":f,"lat":f,"jitter":f}
 *     {"type":"stats","rx":N,"lost":N,"loss_pct":f,
 *      "lat_min":f,"lat_avg":f,"lat_max":f,
 *      "jitter_avg":f,"jitter_max":f,"rate_hz":f,"last_seq":N}
 *     {"type":"device","model":"BMI160","range_g":16.0}
 *
 * The Vue.js project connects to ws://<server>:<port>/ using the URL
 * configured in the VITE_WS_URL environment variable.
 * CORS is not an issue for WebSocket connections.
 */
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <misc/stats_analyzer.hpp>

namespace server { struct DataPacket; }

namespace server {
namespace web {

class WebInterface {
public:
    WebInterface();
    ~WebInterface();

    WebInterface(const WebInterface&)            = delete;
    WebInterface& operator=(const WebInterface&) = delete;

    /* Bind and start listening; returns false on socket error. */
    bool start(const std::string& host, uint16_t port);

    /* Stop: close listen socket, wait for all client threads. */
    void stop();

    bool is_running() const { return running_.load(); }

    /* Store device info that is sent to every new browser connection. */
    void set_device_info(const std::string& model, float range_g);

    /* Broadcast one data packet to all connected browsers. */
    void broadcast_data(const DataPacket& pkt,
                         double latency_ms,
                         double jitter_ms);

    /* Broadcast a statistics snapshot to all connected browsers. */
    void broadcast_stats(const StatsSnapshot& snap);

    /* Number of currently active WebSocket connections. */
    size_t client_count() const;

private:
    void   accept_loop();
    void   handle_client(int fd);

    bool   do_http_response(int fd, const std::string& path);
    bool   do_ws_upgrade   (int fd, const std::string& request);
    bool   send_raw        (int fd, const void* data, size_t len);
    void   ws_send_one     (int fd, const std::string& json);
    void   ws_broadcast    (const std::string& json);
    bool   ws_discard_frame(int fd);

    static std::string ws_encode_frame(const std::string& payload);
    static std::string ws_accept_key  (const std::string& client_key);
    static std::string read_request   (int fd);
    static std::string extract_path   (const std::string& request);

    int               listen_fd_ = -1;
    std::string       host_;
    uint16_t          port_      = 8088;
    std::atomic<bool> running_  {false};
    std::thread       accept_thread_;
    std::atomic<int>  active_clients_{0};

    mutable std::mutex clients_mutex_;
    std::vector<int>   client_fds_;

    // FIX 2: condition_variable used by stop() to wait for detached client
    // threads to finish instead of the original 500 ms spin-wait.
    std::mutex              clients_done_mutex_;
    std::condition_variable clients_done_cv_;

    mutable std::mutex device_mutex_;
    std::string        device_model_;
    float              device_range_g_ = 0.0f;
};

} /* namespace web */
} /* namespace server */
