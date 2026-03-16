/*
 * core/stats_analyzer.cpp
 */
#include <misc/stats_analyzer.hpp>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace server
{

StatsAnalyzer::StatsAnalyzer(size_t history_size, uint64_t report_interval_ms)
    : history_size_(history_size)
    , report_interval_ms_(report_interval_ms)
{}

void StatsAnalyzer::record(uint64_t server_time_ms,
                             uint64_t packet_ts_ms,
                             uint32_t seq)
{
    std::lock_guard<std::mutex> lk(mutex_);

    double lat = static_cast<double>(server_time_ms)
               - static_cast<double>(packet_ts_ms);

    // Jitter: absolute inter-sample latency variation (RFC 3550)
    double jitter = 0.0;
    if (!first_packet_) {
        jitter = std::abs(lat - prev_latency_ms_);
        sum_jitter_ += jitter;
        if (jitter > max_jitter_) max_jitter_ = jitter;
    }
    prev_latency_ms_ = lat;
    first_packet_    = false;

    sum_latency_ += lat;
    if (lat < min_latency_) min_latency_ = lat;
    if (lat > max_latency_) max_latency_ = lat;

    latency_history_.push_back(lat);
    if (latency_history_.size() > history_size_)
        latency_history_.pop_front();

    ++packets_received_;

    // Detect loss by sequence number gaps; handle 32-bit wraparound
    if (last_seq_ != UINT32_MAX) {
        uint32_t expected = last_seq_ + 1;
        if (seq != expected) {
            uint32_t gap = (seq > expected)
                           ? (seq - expected)
                           : (UINT32_MAX - expected + seq + 1);
            if (gap < 10000)
                packets_lost_ += gap;
        }
    }
    last_seq_ = seq;

    // Maintain a sliding window for rate estimation
    if (rate_window_start_ms_ == 0) {
        rate_window_start_ms_ = server_time_ms;
        rate_window_count_    = 0;
    }
    ++rate_window_count_;
    uint64_t elapsed_ms = server_time_ms - rate_window_start_ms_;
    if (elapsed_ms >= report_interval_ms_) {
        current_rate_hz_      = static_cast<double>(rate_window_count_)
                              / (static_cast<double>(elapsed_ms) / 1000.0);
        rate_window_start_ms_ = server_time_ms;
        rate_window_count_    = 0;
    }
}

StatsSnapshot StatsAnalyzer::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    StatsSnapshot s;
    s.packets_received = packets_received_;
    s.packets_lost     = packets_lost_;
    s.last_seq         = last_seq_;
    s.rx_rate_hz       = current_rate_hz_;

    uint64_t total = packets_received_ + packets_lost_;
    s.loss_rate_pct = (total > 0)
                      ? (100.0 * static_cast<double>(packets_lost_) / total)
                      : 0.0;

    if (packets_received_ > 0)
        s.avg_latency_ms = sum_latency_ / static_cast<double>(packets_received_);
    if (packets_received_ > 1)
        s.avg_jitter_ms = sum_jitter_ / static_cast<double>(packets_received_ - 1);

    s.min_latency_ms = (min_latency_ < 1e17) ? min_latency_ : 0.0;
    s.max_latency_ms = max_latency_;
    s.max_jitter_ms  = max_jitter_;
    return s;
}

std::string StatsAnalyzer::format(const StatsSnapshot& s) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "rx=%llu lost=%llu(%.2f%%) "
        "lat=min/avg/max=%.1f/%.1f/%.1f ms "
        "jitter=avg/max=%.2f/%.2f ms "
        "rate=%.1f Hz",
        (unsigned long long)s.packets_received,
        (unsigned long long)s.packets_lost,
        s.loss_rate_pct,
        s.min_latency_ms, s.avg_latency_ms, s.max_latency_ms,
        s.avg_jitter_ms,  s.max_jitter_ms,
        s.rx_rate_hz);
    return buf;
}

std::string StatsAnalyzer::to_json(const StatsSnapshot& s) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"type\":\"stats\""
        ",\"rx\":%llu,\"lost\":%llu,\"loss_pct\":%.3f"
        ",\"lat_min\":%.3f,\"lat_avg\":%.3f,\"lat_max\":%.3f"
        ",\"jitter_avg\":%.3f,\"jitter_max\":%.3f"
        ",\"rate_hz\":%.2f,\"last_seq\":%u}",
        (unsigned long long)s.packets_received,
        (unsigned long long)s.packets_lost,
        s.loss_rate_pct,
        s.min_latency_ms, s.avg_latency_ms, s.max_latency_ms,
        s.avg_jitter_ms,  s.max_jitter_ms,
        s.rx_rate_hz,
        (unsigned)s.last_seq);
    return buf;
}

void StatsAnalyzer::reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    packets_received_     = 0;
    packets_lost_         = 0;
    last_seq_             = UINT32_MAX;
    first_packet_         = true;
    prev_latency_ms_      = 0.0;
    sum_jitter_           = 0.0;
    max_jitter_           = 0.0;
    sum_latency_          = 0.0;
    min_latency_          = 1e18;
    max_latency_          = 0.0;
    rate_window_start_ms_ = 0;
    rate_window_count_    = 0;
    current_rate_hz_      = 0.0;
    latency_history_.clear();
}

}
