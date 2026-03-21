/*
 * core/packet_validator.cpp
 * Accepts two JSON formats:
 *
 *   Native format (own clients):
 *     {"ts":1700000000000,"seq":42,"ax":0.12,"ay":-9.81,"az":0.03}
 *
 *   Android Sensor Server format (github.com/umer0586/SensorServer):
 *     {"type":"android.sensor.accelerometer","values":[0.117,0.741,9.871]}
 *     {"type":"android.sensor.accelerometer","values":[0.117,0.741,9.871],
 *      "timestamp":123456789000000}
 *
 *   In the Android format:
 *     values[0] -> ax, values[1] -> ay, values[2] -> az
 *     timestamp field is nanoseconds since device boot (NOT Unix epoch);
 *       when timesource=host or timestamp is absent the server clock is used.
 *     seq is always absent; cfg_.seq_optional must be true or the validator
 *       config must be set accordingly (main.cpp forces seq_optional=true for
 *       Android format).
 */
#include <misc/packet_validator.hpp>
#include <logger/logger.hpp>

#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <cstdio>

namespace server
{

/* -------------------------------------------------------------------------- */

PacketValidator::PacketValidator(const ValidatorConfig& cfg)
    : cfg_(cfg)
{}

uint64_t PacketValidator::default_server_time() {
    // Must use system_clock (Unix epoch), NOT steady_clock (arbitrary epoch,
    // starts at system boot on Linux/macOS).  When timesource=host this value
    // is stored in DataPacket::timestamp_ms and written to CSV; it must be a
    // real Unix timestamp in ms.  Using steady_clock would produce values
    // ~47 years off relative to any client-side Unix timestamp and make the
    // CSV timestamp column meaningless.
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count());
}

TimeSource PacketValidator::parse_timesource(const char* s) {
    if (!s) return TimeSource::External;
    if (std::strcmp(s, "host")     == 0) return TimeSource::Host;
    if (std::strcmp(s, "external") == 0) return TimeSource::External;
    LOG_WARNF("[Validator] Unknown timesource '%s'; defaulting to 'external'", s);
    return TimeSource::External;
}

const char* PacketValidator::timesource_str(TimeSource ts) {
    return (ts == TimeSource::Host) ? "host" : "external";
}

/* -------------------------------------------------------------------------- */
/* Low-level JSON field extractors.
 * These work on flat objects only; sufficient for the two packet formats. */

bool PacketValidator::extract_uint64(const std::string& json,
                                      const char* key,
                                      uint64_t& out)
{
    std::string search = std::string("\"") + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos += search.size();

    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size()) return false;

    errno = 0;
    char* end = nullptr;
    unsigned long long val = std::strtoull(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos || errno == ERANGE) return false;
    out = static_cast<uint64_t>(val);
    return true;
}

bool PacketValidator::extract_uint32(const std::string& json,
                                      const char* key,
                                      uint32_t& out)
{
    uint64_t val = 0;
    if (!extract_uint64(json, key, val)) return false;
    if (val > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(val);
    return true;
}

bool PacketValidator::extract_double(const std::string& json,
                                      const char* key,
                                      double& out)
{
    std::string search = std::string("\"") + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos += search.size();

    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size()) return false;

    errno = 0;
    char* end = nullptr;
    double val = std::strtod(json.c_str() + pos, &end);
    if (end == json.c_str() + pos || errno == ERANGE) return false;
    out = val;
    return true;
}

/*
 * extract_array_doubles: parse up to max_count doubles from a JSON array
 * that follows the specified key.
 * Example: "values":[0.1,-9.81,0.03]  ->  out[0]=0.1, out[1]=-9.81, out[2]=0.03
 * Returns the number of values actually parsed.
 */
int PacketValidator::extract_array_doubles(const std::string& json,
                                            const char* key,
                                            double* out,
                                            int max_count)
{
    std::string search = std::string("\"") + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();

    // Skip whitespace and colon
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;

    // Expect opening bracket
    if (pos >= json.size() || json[pos] != '[') return 0;
    ++pos;

    int count = 0;
    while (count < max_count && pos < json.size()) {
        // Skip whitespace and commas between values
        while (pos < json.size() &&
               (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\t'))
            ++pos;

        if (pos >= json.size() || json[pos] == ']') break;

        errno = 0;
        char* end = nullptr;
        double val = std::strtod(json.c_str() + pos, &end);
        if (end == json.c_str() + pos || errno == ERANGE) break;

        out[count++] = val;
        pos = static_cast<size_t>(end - json.c_str());
    }
    return count;
}

/*
 * detect_android_format: returns true when the payload contains
 * a "type" field whose value starts with "android.sensor."
 * and a "values" array field.
 * This matches all messages from the Sensor Server app regardless of
 * which specific sensor type is selected.
 */
bool PacketValidator::detect_android_format(const std::string& json) {
    // Must have both "type":"android.sensor.* and "values":[
    if (json.find("\"type\"") == std::string::npos) return false;
    if (json.find("android.sensor.") == std::string::npos) return false;
    if (json.find("\"values\"") == std::string::npos) return false;
    return true;
}

bool PacketValidator::validate_range(const DataPacket& pkt) const {
    double limit = cfg_.max_acc_ms2;
    if (pkt.acc_x < -limit || pkt.acc_x > limit) return false;
    if (pkt.acc_y < -limit || pkt.acc_y > limit) return false;
    if (pkt.acc_z < -limit || pkt.acc_z > limit) return false;
    return true;
}

/* -------------------------------------------------------------------------- */
/* parse_android: handle Sensor Server app message format.
 *
 *   {"type":"android.sensor.accelerometer","values":[ax,ay,az]}
 *   {"type":"android.sensor.accelerometer","values":[ax,ay,az],"timestamp":N}
 *
 * Android timestamp semantics:
 *   The "timestamp" field, when present, is nanoseconds elapsed since device
 *   boot (CLOCK_BOOTTIME), not a Unix epoch value.  It cannot be used directly
 *   for latency measurement against the server's wall clock.  We always use
 *   the server clock for timestamp_ms regardless of whether timestamp is
 *   present, unless the caller has a custom server_time_fn.
 *
 * Sequence numbers:
 *   The Android format has no seq field.  We increment an internal per-session
 *   counter stored in cfg_.android_seq_counter (atomic, so thread-safe).
 */
ParsedPacket PacketValidator::parse_android(const std::string& payload) const {
    ParsedPacket result{};
    result.result = ParseResult::INVALID_FORMAT;
    DataPacket& pkt = result.packet;

    // Timestamp: always use server clock for Android packets because the
    // Android "timestamp" field is nanoseconds since boot, not Unix epoch.
    uint64_t (*time_fn)() = cfg_.server_time_fn
                             ? cfg_.server_time_fn
                             : &PacketValidator::default_server_time;
    pkt.timestamp_ms = time_fn();

    // Sequence number: auto-increment from the shared counter.
    pkt.sequence_id = cfg_.android_seq_counter->fetch_add(1);

    // Parse the values array: [ax, ay, az]
    double vals[3] = {0.0, 0.0, 0.0};
    int n = extract_array_doubles(payload, "values", vals, 3);
    if (n < 3) {
        result.result = ParseResult::MISSING_FIELDS;
        return result;
    }

    pkt.acc_x = vals[0];
    pkt.acc_y = vals[1];
    pkt.acc_z = vals[2];

    if (!validate_range(pkt)) {
        result.result = ParseResult::OUT_OF_RANGE;
        return result;
    }

    result.result = ParseResult::OK;
    return result;
}

/* -------------------------------------------------------------------------- */
/* parse: main entry point.
 * Detects the packet format and dispatches to the appropriate parser. */
ParsedPacket PacketValidator::parse(const std::string& payload) const {
    // Dispatch to Android format parser when the message looks like one.
    if (detect_android_format(payload))
        return parse_android(payload);

    // Native format: {"ts":...,"seq":...,"ax":...,"ay":...,"az":...}
    ParsedPacket result{};
    result.result = ParseResult::INVALID_FORMAT;
    DataPacket& pkt = result.packet;

    /* ---- Timestamp ---- */
    uint64_t ts_from_packet = 0;
    bool has_ts = extract_uint64(payload, "ts", ts_from_packet)
                  && ts_from_packet != 0;

    if (!has_ts) {
        if (cfg_.timesource == TimeSource::External) {
            result.result = ParseResult::MISSING_FIELDS;
            return result;
        }
        uint64_t (*time_fn)() = cfg_.server_time_fn
                                 ? cfg_.server_time_fn
                                 : &PacketValidator::default_server_time;
        pkt.timestamp_ms = time_fn();
    } else {
        pkt.timestamp_ms = ts_from_packet;
    }

    /* ---- Sequence number ---- */
    if (!extract_uint32(payload, "seq", pkt.sequence_id)) {
        if (!cfg_.seq_optional) {
            result.result = ParseResult::MISSING_FIELDS;
            return result;
        }
        pkt.sequence_id = 0;
    }

    /* ---- Acceleration axes ---- */
    if (!extract_double(payload, "ax", pkt.acc_x)) {
        result.result = ParseResult::MISSING_FIELDS;
        return result;
    }
    if (!extract_double(payload, "ay", pkt.acc_y)) {
        result.result = ParseResult::MISSING_FIELDS;
        return result;
    }
    if (!extract_double(payload, "az", pkt.acc_z)) {
        result.result = ParseResult::MISSING_FIELDS;
        return result;
    }

    if (!validate_range(pkt)) {
        result.result = ParseResult::OUT_OF_RANGE;
        return result;
    }

    result.result = ParseResult::OK;
    return result;
}

}
