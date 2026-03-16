#include <conf/cliargsparser.hpp>
#include <misc/help.hpp>
#include <iostream>
#include <string>
#include <cstring>

server::Config
parse_args(int argc, char* argv[], server::Config cfg)
{
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : "";
        };

        if      (a == "--help" || a == "-h")    { print_usage(argv[0]); std::exit(0); }
        // FIX: --config and --dump-config must be silently consumed regardless
        // of whether HAVE_CONFPARSER is defined. Without this, passing --config
        // when the feature is disabled (or when HAVE_CONFPARSER != ENABLE_CONFPARSER)
        // prints a false "[WARN] Unknown argument: --config".
        else if (a == "--config")               { next(); /* path consumed by extract_config_path() */ }
        else if (a == "--dump-config")          { /* handled after parse_args in main() */ }
        // protocol selection
        else if (a == "--ws")                   cfg.transport = server::TransportType::WebSocket;
        else if (a == "--mqtt")                 cfg.transport = server::TransportType::MQTT;
        else if (a == "--protocol") {
            std::string p = next();
            if      (p == "mqtt") cfg.transport = server::TransportType::MQTT;
            else if (p == "ws")   cfg.transport = server::TransportType::WebSocket;
            else std::cerr << "[WARN] Unknown protocol: " << p << "\n";
        }
        // connection
        else if (a == "--host")                 cfg.host              = next();
        else if (a == "--port")                 cfg.port              = (uint16_t)std::atoi(next().c_str());
        // mqtt
        else if (a == "--topic")                cfg.mqtt_topic        = next();
        else if (a == "--mqtt-id")              cfg.mqtt_client_id    = next();
        else if (a == "--mqtt-user")            cfg.mqtt_username     = next();
        else if (a == "--mqtt-pass")            cfg.mqtt_password     = next();
        else if (a == "--mqtt-qos") {
            // FIX: validate QoS range (MQTT spec: 0, 1 or 2 only)
            int qos = std::atoi(next().c_str());
            if (qos < 0 || qos > 2) {
                std::cerr << "[WARN] Invalid --mqtt-qos " << qos
                          << "; must be 0, 1 or 2. Using 0.\n";
                qos = 0;
            }
            cfg.mqtt_qos = qos;
        }
        // storage
        else if (a == "--output")               cfg.output_file       = next();
        else if (a == "--storepath")            cfg.store_path        = next();
        else if (a == "--buf") { cfg.buffer_capacity = (std::size_t)std::atoi(next().c_str()); cfg.auto_buffer = false; }
        else if (a == "--flush-ms")             cfg.flush_interval_ms = (std::size_t)std::atoi(next().c_str());
        // logging
        else if (a == "--log")                  cfg.log_file          = next();
        else if (a == "--no-log-stderr")        cfg.log_also_stderr   = false;
        else if (a == "--log-level")            cfg.log_level         = next();
        // web interface
        else if (a == "--webiface") {
            std::string v = next();
            cfg.web_enabled = (v == "true" || v == "1" || v == "yes");
        }
        else if (a == "--webhost")              cfg.web_host = next();
        else if (a == "--webport")              cfg.web_port = (uint16_t)std::atoi(next().c_str());
        else if (a == "--timesource")           cfg.timesource   = next();
        else if (a == "--max-acc")              cfg.max_acc_ms2  = std::atof(next().c_str());
        else if (a == "--seq-optional")         cfg.seq_optional = true;
        else std::cerr << "[WARN] Unknown argument: " << a << "\n";
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
    << "  [storage] output=" << (cfg.output_file.empty() ? "(auto)" : cfg.output_file)
    << "  storepath=" << (cfg.store_path.empty() ? "(cwd)" : cfg.store_path)
    << "  buf=" << cfg.buffer_capacity
    << (cfg.auto_buffer ? "(auto)" : "(manual)")
    << "  flush=" << cfg.flush_interval_ms << "ms\n"
    << "  [logging] file=" << cfg.log_file
    << "  level=" << cfg.log_level
    << "  also_stderr=" << (cfg.log_also_stderr ? "true" : "false") << "\n"
    << "  [web]    enabled=" << (cfg.web_enabled ? "true" : "false")
    << "  host=" << cfg.web_host << "  port=" << cfg.web_port << "\n"
    << "  [device] model=" << cfg.device_model
    << "  range=+-" << cfg.device_range_g << "g\n"
    << "  [validator] timesource=" << cfg.timesource
    << "  max_acc=" << cfg.max_acc_ms2
    << "  seq_optional=" << (cfg.seq_optional?"yes":"no") << "\n";
}
