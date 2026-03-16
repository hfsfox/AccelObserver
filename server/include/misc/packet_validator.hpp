#pragma once
/*
 * core/packet_validator.hpp
 * Parses and validates incoming JSON accelerometer packets.
 *
 * Accepted formats:
 *
 *   1. Native (own ws_client / mqtt_client):
 *        {"ts":1700000000000,"seq":42,"ax":0.12,"ay":-9.81,"az":0.03}
 *      ts and seq are optional depending on ValidatorConfig.
 *
 *   2. Android Sensor Server (github.com/umer0586/SensorServer):
 *        {"type":"android.sensor.accelerometer","values":[ax,ay,az]}
 *        {"type":"android.sensor.accelerometer","values":[ax,ay,az],
 *         "timestamp":123456789000000}
 *      Detected automatically by the presence of "type" and "android.sensor.".
 *      timestamp is nanoseconds since device boot (not Unix epoch) and is
 *      therefore ignored; server time is always used for Android packets.
 *      seq is auto-incremented from an internal per-validator atomic counter.
 *
 * timesource behaviour (native format only):
 *   External  ts is mandatory; MISSING_FIELDS returned when absent
 *   Host      server wall-clock substituted when ts is absent or zero
 */
#include <core/servertypes.hpp>
#include <string>
#include <cstdint>
#include <atomic>
#include <memory>

namespace server
{

enum class TimeSource
{
    External,  // ts required in every packet
    Host       // server clock used when ts is absent or zero
};

struct ValidatorConfig {
    /* Maximum absolute acceleration on any axis (m/s^2).
     * 0 means "derive from device_range_g * 9.80665" (done in main.cpp). */
    double max_acc_ms2 = 156.9064;

    TimeSource timesource   = TimeSource::External;
    bool       seq_optional = false;

    /* Replaceable clock for unit tests; nullptr -> default_server_time(). */
    uint64_t (*server_time_fn)() = nullptr;

    /* Per-validator auto-increment counter for Android packet sequences.
     * Shared_ptr so ValidatorConfig can be copied without invalidating
     * the pointer when the original goes out of scope. */
    std::shared_ptr<std::atomic<uint32_t>> android_seq_counter
        = std::make_shared<std::atomic<uint32_t>>(0);
};

class PacketValidator {
public:
    explicit PacketValidator(const ValidatorConfig& cfg = ValidatorConfig{});

    /* Parse one JSON packet; auto-detects native vs Android format. */
    ParsedPacket parse(const std::string& payload) const;

    static TimeSource   parse_timesource(const char* s);
    static const char*  timesource_str  (TimeSource ts);

    const ValidatorConfig& config() const { return cfg_; }

private:
    ValidatorConfig cfg_;

    /* Format detection */
    static bool detect_android_format(const std::string& json);

    /* Format-specific parsers */
    ParsedPacket parse_android(const std::string& payload) const;

    /* Field extractors */
    static bool extract_uint64      (const std::string& json, const char* key, uint64_t& out);
    static bool extract_uint32      (const std::string& json, const char* key, uint32_t& out);
    static bool extract_double      (const std::string& json, const char* key, double&   out);
    static int  extract_array_doubles(const std::string& json, const char* key,
                                      double* out, int max_count);

    bool validate_range(const DataPacket& pkt) const;

    static uint64_t default_server_time();
};

}
