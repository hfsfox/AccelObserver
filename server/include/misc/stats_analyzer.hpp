#pragma once
/*
 * core/stats_analyzer.hpp
 * Real-time packet statistics: latency, jitter, and packet loss.
 *
 * Latency:  server_receive_time_ms - packet_timestamp_ms
 * Jitter:   |latency[n] - latency[n-1]|  (RFC 3550 interarrival jitter)
 * Loss:     gaps detected in the sequence number stream
 */
#include <cstdint>
#include <atomic>
#include <mutex>
#include <deque>
#include <string>

namespace server
{

struct StatsSnapshot {
    uint64_t packets_received  = 0;
    uint64_t packets_lost      = 0;
    double   avg_latency_ms    = 0.0;
    double   min_latency_ms    = 0.0;
    double   max_latency_ms    = 0.0;
    double   avg_jitter_ms     = 0.0;
    double   max_jitter_ms     = 0.0;
    double   loss_rate_pct     = 0.0;
    double   rx_rate_hz        = 0.0;
    uint32_t last_seq          = 0;
};

class StatsAnalyzer {
public:
    // history_size: rolling window depth for latency samples
    // report_interval_ms: rate measurement window
    explicit StatsAnalyzer(size_t   history_size        = 200,
                            uint64_t report_interval_ms  = 1000);

    /*
     * Record one received packet.
     * server_time_ms: wall-clock time at reception (steady_clock milliseconds).
     * packet_ts_ms:   timestamp from the packet itself (client clock).
     * seq:            sequence number for loss detection.
     */
    void record(uint64_t server_time_ms,
                uint64_t packet_ts_ms,
                uint32_t seq);

    // Thread-safe copy of current statistics.
    StatsSnapshot snapshot() const;

    // One-line human-readable summary.
    static std::string format(const StatsSnapshot& s);

    // JSON object suitable for browser WebSocket delivery.
    static std::string to_json(const StatsSnapshot& s);

    void reset();

private:
    size_t    history_size_;
    uint64_t  report_interval_ms_;

    mutable std::mutex     mutex_;
    std::deque<double>     latency_history_;

    uint64_t packets_received_     = 0;
    uint64_t packets_lost_         = 0;
    uint32_t last_seq_             = UINT32_MAX;
    bool     first_packet_         = true;

    double   prev_latency_ms_      = 0.0;
    double   sum_jitter_           = 0.0;
    double   max_jitter_           = 0.0;
    double   sum_latency_          = 0.0;
    double   min_latency_          = 1e18;
    double   max_latency_          = 0.0;

    uint64_t rate_window_start_ms_ = 0;
    uint64_t rate_window_count_    = 0;
    double   current_rate_hz_      = 0.0;
};

} /* namespace server */
