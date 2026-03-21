#include <conf/cliargsparser.hpp>
#include <misc/help.hpp>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helper: case-insensitive ASCII compare for CLI value strings.
// ---------------------------------------------------------------------------
static std::string str_lower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return out;
}

// ---------------------------------------------------------------------------
// parse_port
// Converts a string to a TCP port number [1, 65535].
// Prints a warning and returns 0 (sentinel "ignore") on any error:
//   - empty string (missing value after flag)
//   - non-numeric text
//   - value outside [1, 65535]
// The caller must check for 0 and keep the existing port.
// ---------------------------------------------------------------------------
static uint16_t parse_port(const std::string& s, const char* flag)
{
    if (s.empty()) {
        std::cerr << "[WARN] " << flag << " requires a port number argument.\n";
        return 0;
    }
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v <= 0 || v > 65535) {
        std::cerr << "[WARN] " << flag << " " << s
                  << " is not a valid port (1-65535); ignored.\n";
        return 0;
    }
    return static_cast<uint16_t>(v);
}

// ---------------------------------------------------------------------------
// parse_size
// Converts a string to a positive size_t using strtoull (avoids int overflow
// of std::atoi for large buffer/flush values).
// Returns 0 (sentinel "ignore") on empty string, non-numeric, or zero value.
// ---------------------------------------------------------------------------
static std::size_t parse_size(const std::string& s, const char* flag)
{
    if (s.empty()) {
        std::cerr << "[WARN] " << flag << " requires a numeric argument.\n";
        return 0;
    }
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v == 0) {
        std::cerr << "[WARN] " << flag << " " << s
                  << " is not a valid positive integer; ignored.\n";
        return 0;
    }
    return static_cast<std::size_t>(v);
}

// ---------------------------------------------------------------------------
// parse_nonempty
// Returns the string if non-empty, otherwise prints a warning and returns "".
// The caller must check for "" and keep the existing value.
// ---------------------------------------------------------------------------
static std::string parse_nonempty(const std::string& s, const char* flag)
{
    if (s.empty()) {
        std::cerr << "[WARN] " << flag << " requires a non-empty argument.\n";
        return "";
    }
    return s;
}

// ---------------------------------------------------------------------------
// peek_bool
// Used for flags that optionally take an explicit true/false value:
//   --webiface           =>  true  (flag form, no value consumed)
//   --webiface true      =>  true  (value consumed)
//   --webiface false     =>  false (value consumed)
//   --webiface --mqtt    =>  true  (next token is a flag, NOT consumed)
//
// argv / argc / i are passed by reference so the index can be advanced.
// default_val is returned when the next token is absent or is another flag.
// ---------------------------------------------------------------------------
static bool peek_bool(int argc, char* argv[], int& i, bool default_val)
{
    if (i + 1 >= argc) return default_val;
    const char* nxt = argv[i + 1];
    // A valid boolean literal must not start with '-' (which would make it
    // a flag, not a value).
    if (nxt[0] == '-') return default_val;
    std::string lv = str_lower(std::string(nxt));
    if (lv == "true"  || lv == "1" || lv == "yes" || lv == "on") { ++i; return true;  }
    if (lv == "false" || lv == "0" || lv == "no"  || lv == "off"){ ++i; return false; }
    // Not a recognisable boolean value — treat flag form as default_val.
    return default_val;
}

// ---------------------------------------------------------------------------

server::Config
parse_args(int argc, char* argv[], server::Config cfg)
{
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        // next() consumes the next token as a value argument.
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : "";
        };

        if      (a == "--help" || a == "-h") { print_usage(argv[0]); std::exit(0); }
        // --config and --dump-config are consumed here regardless of whether
        // HAVE_CONFPARSER is defined to prevent a spurious "unknown argument" warning.
        else if (a == "--config")     { next(); /* path consumed by extract_config_path() */ }
        else if (a == "--dump-config"){ /* handled after parse_args() in main() */ }

        // ---- transport ----
        else if (a == "--ws")  { cfg.transport = server::TransportType::WebSocket; }
        else if (a == "--mqtt"){ cfg.transport = server::TransportType::MQTT; }
        else if (a == "--protocol") {
            // Case-insensitive: --protocol MQTT, --protocol ws, --protocol WebSocket
            std::string p = str_lower(next());
            if      (p == "mqtt")                   cfg.transport = server::TransportType::MQTT;
            else if (p == "ws" || p == "websocket") cfg.transport = server::TransportType::WebSocket;
            else    std::cerr << "[WARN] --protocol " << p << " unknown (use mqtt|ws).\n";
        }

        // ---- connection ----
        else if (a == "--host") {
            std::string v = parse_nonempty(next(), "--host");
            if (!v.empty()) cfg.host = v;
        }
        else if (a == "--port") {
            uint16_t p = parse_port(next(), "--port");
            if (p) cfg.port = p;
        }

        // ---- mqtt ----
        else if (a == "--topic") {
            std::string v = parse_nonempty(next(), "--topic");
            if (!v.empty()) cfg.mqtt_topic = v;
        }
        else if (a == "--mqtt-id") {
            // Empty client_id is legal (broker assigns one), so no nonempty check.
            cfg.mqtt_client_id = next();
        }
        else if (a == "--mqtt-user") { cfg.mqtt_username = next(); }
        else if (a == "--mqtt-pass") { cfg.mqtt_password = next(); }
        else if (a == "--mqtt-qos") {
            int qos = std::atoi(next().c_str());
            if (qos < 0 || qos > 2) {
                std::cerr << "[WARN] --mqtt-qos " << qos
                          << " is out of range (0-2); using 0.\n";
                qos = 0;
            }
            cfg.mqtt_qos = qos;
        }

        // ---- Last Will and Testament ----
        else if (a == "--mqtt-will-topic") {
            std::string v = parse_nonempty(next(), "--mqtt-will-topic");
            if (!v.empty()) cfg.mqtt_will_topic = v;
        }
        else if (a == "--mqtt-will-payload") { cfg.mqtt_will_payload = next(); }
        else if (a == "--mqtt-will-qos") {
            int qos = std::atoi(next().c_str());
            if (qos < 0 || qos > 2) {
                std::cerr << "[WARN] --mqtt-will-qos " << qos
                          << " is out of range (0-2); using 0.\n";
                qos = 0;
            }
            cfg.mqtt_will_qos = qos;
        }
        // Boolean flag: --mqtt-will-retain         => retain = true
        //               --mqtt-will-retain true     => retain = true
        //               --mqtt-will-retain false    => retain = false
        // Does NOT consume the next token if it starts with '-' (another flag).
        else if (a == "--mqtt-will-retain") {
            cfg.mqtt_will_retain = peek_bool(argc, argv, i, true);
        }

        // ---- storage ----
        else if (a == "--output")    { cfg.output_file = next(); }
        else if (a == "--storepath") { cfg.store_path  = next(); }
        else if (a == "--buf") {
            std::size_t v = parse_size(next(), "--buf");
            if (v) { cfg.buffer_capacity = v; cfg.auto_buffer = false; }
        }
        else if (a == "--flush-ms") {
            std::size_t v = parse_size(next(), "--flush-ms");
            if (v) cfg.flush_interval_ms = v;
        }
        else if (a == "--csv-sep") { cfg.csv_separator = next(); }

        // ---- logging ----
        else if (a == "--log") {
            std::string v = parse_nonempty(next(), "--log");
            if (!v.empty()) cfg.log_file = v;
        }
        else if (a == "--no-log-stderr") { cfg.log_also_stderr = false; }
        else if (a == "--log-level") {
            std::string v = parse_nonempty(next(), "--log-level");
            if (!v.empty()) cfg.log_level = v;
        }

        // ---- web interface ----
        // Boolean flag: --webiface           => enabled = true
        //               --webiface true      => enabled = true
        //               --webiface false     => enabled = false
        // Does NOT consume the next token if it starts with '-' (another flag).
        else if (a == "--webiface") {
            cfg.web_enabled = peek_bool(argc, argv, i, true);
        }
        else if (a == "--webhost") {
            std::string v = parse_nonempty(next(), "--webhost");
            if (!v.empty()) cfg.web_host = v;
        }
        else if (a == "--webport") {
            uint16_t p = parse_port(next(), "--webport");
            if (p) cfg.web_port = p;
        }

        // ---- validator ----
        else if (a == "--timesource") {
            std::string v = parse_nonempty(next(), "--timesource");
            if (!v.empty()) cfg.timesource = v;
        }
        else if (a == "--max-acc") { cfg.max_acc_ms2 = std::atof(next().c_str()); }
        else if (a == "--seq-optional") { cfg.seq_optional = true; }

        else { std::cerr << "[WARN] Unknown argument: " << a << "\n"; }
    }
    return cfg;
}

void
dump_config(const server::Config& cfg)
{
    using T = server::TransportType;
    std::cout
    << "Resolved configuration:\n"
    << "  [transport]  protocol=" << (cfg.transport==T::MQTT ? "mqtt" : "ws")
    << "  host=" << cfg.host << "  port=" << cfg.port << "\n"
    << "  [mqtt]   topic=" << cfg.mqtt_topic
    << "  qos=" << cfg.mqtt_qos << "  id=" << cfg.mqtt_client_id << "\n"
    << "  [mqtt/will]  topic="
    << (cfg.mqtt_will_topic.empty() ? cfg.mqtt_topic + "/status" : cfg.mqtt_will_topic)
    << "  qos=" << cfg.mqtt_will_qos
    << "  retain=" << (cfg.mqtt_will_retain ? "true" : "false") << "\n"
    << "  [storage] output=" << (cfg.output_file.empty() ? "(auto)" : cfg.output_file)
    << "  storepath=" << (cfg.store_path.empty() ? "(cwd)" : cfg.store_path)
    << "  buf=" << cfg.buffer_capacity
    << (cfg.auto_buffer ? "(auto)" : "(manual)")
    << "  flush=" << cfg.flush_interval_ms << "ms"
    << "  sep='" << cfg.csv_separator << "'\n"
    << "  [logging] file=" << cfg.log_file
    << "  level=" << cfg.log_level
    << "  also_stderr=" << (cfg.log_also_stderr ? "true" : "false") << "\n"
    << "  [web]    enabled=" << (cfg.web_enabled ? "true" : "false")
    << "  host=" << cfg.web_host << "  port=" << cfg.web_port << "\n"
    << "  [device] model=" << cfg.device_model
    << "  range=+-" << cfg.device_range_g << "g\n"
    << "  [validator] timesource=" << cfg.timesource
    << "  max_acc=" << cfg.max_acc_ms2
    << "  seq_optional=" << (cfg.seq_optional ? "yes" : "no") << "\n";
}
