#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <string>
#include <cstring>

#include <core/servertypes.hpp>
#include <logger/logger.hpp>
#include <network/net_platform.hpp>
#include <storage/storage_manager.hpp>
#include <format/csv/csv_formatter.hpp>
#include <misc/packet_validator.hpp>
#include <misc/stats_analyzer.hpp>
#include <transport/isubscriber.hpp>
#include <misc/help.hpp>

#ifdef HAVE_CONFPARSER
extern "C"
{
    #include <confparser.h>
}
#include <conf/confloader.hpp>
#endif

#include <conf/cliargsparser.hpp>

#ifdef HAVE_WEBSOCKET
    #include <transport/websocket/ws_subscriber.hpp>
#endif

#ifdef HAVE_MQTT
    #include <transport/mqtt/mqtt_subscriber.hpp>
#endif

#include <webif/web_interface.hpp>

// ---------------------------------------------------------------------------
// Global signal flag — set by SIGINT / SIGTERM to break the main loop.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running = false; }

// Unix epoch wall-clock time in milliseconds.
// system_clock is required so that recv_ms shares the same epoch as
// DataPacket::timestamp_ms (which the client writes as Unix epoch ms).
// Latency = recv_ms - pkt.timestamp_ms is only meaningful when both values
// are on the same (Unix) epoch.  steady_clock starts at an arbitrary point
// (system boot) and would produce ~47-year offsets against client timestamps.
// For the interval comparisons (last_stats_ms, last_log_ms) the potential
// NTP micro-adjustment is negligible.
static uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

static server::LogLevel parse_log_level(const std::string& s)
{
    if (s == "DEBUG") return server::LogLevel::DEBUG;
    if (s == "WARN")  return server::LogLevel::WARN;
    if (s == "ERROR") return server::LogLevel::ERR;
    return server::LogLevel::INFO;
}

#ifdef HAVE_CONFPARSER
static const char* extract_config_path(int argc, char* argv[])
{
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
    return nullptr;
}
#endif

// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    server::Config cfg;

    // Step 1: load INI config file (if found / specified).
#ifdef HAVE_CONFPARSER
    {
        const char* explicit_path = extract_config_path(argc, argv);
        char found_path[512] = {};
        bool found = conf_find_config("gaccelserver", explicit_path,
                                     found_path, sizeof(found_path));
        if (explicit_path && !found) {
            std::cerr << "[ERROR] Config file not found: " << explicit_path << "\n";
            return 1;
        }
        if (found) {
            std::cout << "[config] Loading: " << found_path << "\n";
            conf_result_t* conf = conf_load(found_path, nullptr);
            if (!conf) {
                std::cerr << "[ERROR] conf_load: out of memory\n";
                return 1;
            }
            if (conf_error(conf)[0] != '\0')
                std::cerr << "[config] Warning: " << conf_error(conf) << "\n";
            apply_conf(conf, cfg);
            conf_free(conf);
        }
    }
#endif

    // Step 2: CLI arguments override the config file.
    cfg = parse_args(argc, argv, cfg);

    // --dump-config: print resolved values and exit.
#ifdef HAVE_CONFPARSER
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-config") == 0) {
            dump_config(cfg);
            return 0;
        }
    }
#endif

    // Step 3: initialise logger.
    {
        std::string lp = cfg.log_file;
        if (lp == "default") lp = server::Logger::default_log_path();
        server::Logger::instance().configure(
            lp, parse_log_level(cfg.log_level), cfg.log_also_stderr);
        if (!lp.empty() && lp != "stderr" && lp != "stdout")
            LOG_INFOF("[Server] Log: %s", lp.c_str());
    }

    LOG_INFOF("[Server] Device: %s  range=+/-%.1fg",
              cfg.device_model.c_str(), cfg.device_range_g);

    // Step 4: platform network init.
    if (!server::platform::net_init()) {
        LOG_ERRF("[Server] net_init: %s",
                 server::platform::last_socket_error().c_str());
        return 1;
    }

    // Step 5: auto-size ring buffer when no explicit --buf was given.
    // Minimum safe capacity = rate_hz * flush_interval_ms / 1000.
    // A 2x safety factor absorbs bursts and writer latency spikes.
    if (cfg.auto_buffer) {
        const double assumed_rate_hz = 200.0;
        std::size_t min_buf = static_cast<std::size_t>(
            assumed_rate_hz * static_cast<double>(cfg.flush_interval_ms) / 1000.0 * 2.0);
        if (min_buf < 64) min_buf = 64;
        if (cfg.buffer_capacity < min_buf) {
            LOG_INFOF("[Storage] auto_buffer: capacity %zu -> %zu "
                      "(2 x %.0fHz x %zums flush)",
                      cfg.buffer_capacity, min_buf,
                      assumed_rate_hz, cfg.flush_interval_ms);
            cfg.buffer_capacity = min_buf;
        }
    }

    // Step 6: build ValidatorConfig from the resolved Config.
    server::ValidatorConfig vcfg;
    vcfg.timesource  = server::PacketValidator::parse_timesource(cfg.timesource.c_str());
    vcfg.seq_optional = cfg.seq_optional;
    // When max_acc_ms2 is zero, derive it from the configured device range.
    vcfg.max_acc_ms2 = (cfg.max_acc_ms2 > 0.0)
                       ? cfg.max_acc_ms2
                       : static_cast<double>(cfg.device_range_g) * 9.80665;

    LOG_INFOF("[Validator] timesource=%s  max_acc=%.4f m/s^2  seq_optional=%s",
              server::PacketValidator::timesource_str(vcfg.timesource),
              vcfg.max_acc_ms2,
              cfg.seq_optional ? "yes" : "no");

    server::PacketValidator validator(vcfg);

    // Step 7: stats analyzer.
    server::StatsAnalyzer stats(200, cfg.web_stats_interval_ms);

    // Step 8: web interface (browser dashboard).
    std::unique_ptr<server::web::WebInterface> webif;
    if (cfg.web_enabled) {
        webif.reset(new server::web::WebInterface());
        if (!webif->start(cfg.web_host, cfg.web_port)) {
            LOG_WARN("[Server] Web interface failed to start — dashboard disabled");
            webif.reset();
        } else {
            // set_device_info here so the first connecting browser gets it
            webif->set_device_info(cfg.device_model, cfg.device_range_g);
        }
    } else {
        LOG_INFO("[Server] Web interface disabled (set webinterface=true in [transport])");
    }

    // Step 9: resolve output file path.
    // If output_file is set it is used as-is (exact path or directory prefix).
    // If store_path is set, append a trailing separator so StorageManager
    // treats it as a directory and generates a timestamped filename inside it.
    // StorageManager creates the directory if it does not exist.
    std::string out_path = cfg.output_file;
    if (out_path.empty() && !cfg.store_path.empty()) {
        out_path = cfg.store_path;
        char last = out_path.back();
        if (last != '/' && last != '\\') out_path += '/';
    }

    auto formatter = std::unique_ptr<server::IFormatter>(
        new server::CsvFormatter(cfg.csv_separator));
    server::StorageManager storage(std::move(formatter), out_path,
                                   cfg.buffer_capacity, cfg.flush_interval_ms);
    storage.start();

    // Step 10: create transport subscriber.
    std::unique_ptr<server::ISubscriber> sub;

    if (cfg.transport == server::TransportType::WebSocket) {
#ifdef HAVE_WEBSOCKET
        sub.reset(new server::WsSubscriber());
#else
        LOG_ERR("[Server] WebSocket support not compiled; rebuild with -DENABLE_WEBSOCKET=ON");
        storage.stop();
        server::platform::net_cleanup();
        return 1;
#endif
    } else {
#ifdef HAVE_MQTT
        server::MqttWill will;
        // If mqtt_will_topic is empty use "<topic>/status" as the default.
        will.topic   = cfg.mqtt_will_topic.empty()
                       ? cfg.mqtt_topic + "/status"
                       : cfg.mqtt_will_topic;
        will.payload = cfg.mqtt_will_payload;
        will.qos     = cfg.mqtt_will_qos;
        will.retain  = cfg.mqtt_will_retain;
        sub.reset(new server::MqttSubscriber(
            cfg.mqtt_client_id, cfg.mqtt_topic, cfg.mqtt_qos,
            cfg.mqtt_username, cfg.mqtt_password, will,
            cfg.mqtt_keepalive));
#else
        LOG_ERR("[Server] MQTT support not compiled; rebuild with -DENABLE_MQTT=ON");
        storage.stop();
        server::platform::net_cleanup();
        return 1;
#endif
    }

    // Step 11: register packet callback — runs in the transport thread.
    uint64_t last_stats_ms = now_ms();

    sub->set_callback([&](const std::string& payload) {
        uint64_t recv_ms = now_ms();

        auto parsed = validator.parse(payload);
        if (parsed.result != server::ParseResult::OK) {
            const char* reason = "unknown";
            if      (parsed.result == server::ParseResult::MISSING_FIELDS)  reason = "missing fields";
            else if (parsed.result == server::ParseResult::OUT_OF_RANGE)    reason = "out of range";
            else if (parsed.result == server::ParseResult::INVALID_FORMAT)  reason = "invalid format";
            LOG_WARNF("[Validator] Rejected (%s): %.80s", reason, payload.c_str());
            return;
        }

        const server::DataPacket& pkt = parsed.packet;
        stats.record(recv_ms, pkt.timestamp_ms, pkt.sequence_id);
        server::StatsSnapshot snap = stats.snapshot();

        if (webif) {
            double lat = static_cast<double>(recv_ms)
                       - static_cast<double>(pkt.timestamp_ms);
            webif->broadcast_data(pkt, lat, snap.avg_jitter_ms);

            uint64_t now = recv_ms;
            if (now - last_stats_ms >= cfg.web_stats_interval_ms) {
                webif->broadcast_stats(snap);
                last_stats_ms = now;
            }
        }
        (void)last_stats_ms;

        // Periodic stats log every 1000 packets.
        if (pkt.sequence_id > 0 && pkt.sequence_id % 1000 == 0)
            LOG_INFOF("[Stats] %s", server::StatsAnalyzer::format(snap).c_str());

        if (!storage.push(pkt))
            LOG_WARN("[Server] Ring buffer full -- packet dropped");
    });

    // Step 12: connect / bind.
    if (!sub->connect(cfg.host, cfg.port)) {
        LOG_ERRF("[Server] Cannot start on %s:%u",
                 cfg.host.c_str(), (unsigned)cfg.port);
        storage.stop();
        server::platform::net_cleanup();
        return 1;
    }

    LOG_INFOF("[%s] Running  output=%s  buf=%zu  flush=%zums  sep='%s'",
              sub->name(),
              storage.filepath().c_str(),
              cfg.buffer_capacity,
              cfg.flush_interval_ms,
              cfg.csv_separator.c_str());

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::thread sub_thread([&sub]{ sub->run(); });

    // Main loop: periodic stats log.
    uint64_t last_log_ms = now_ms();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        uint64_t now = now_ms();
        if (now - last_log_ms >= 10000) {
            LOG_INFOF("[Stats] %s",
                      server::StatsAnalyzer::format(stats.snapshot()).c_str());
            last_log_ms = now;
        }
    }

    LOG_INFO("[Server] Shutting down...");
    sub->stop();
    if (sub_thread.joinable()) sub_thread.join();

    if (webif) webif->stop();

    storage.stop();
    server::platform::net_cleanup();

    auto final_snap = stats.snapshot();
    LOG_INFOF("[Server] Done  written=%llu  dropped=%llu  lost=%llu(%.2f%%)  output=%s",
              (unsigned long long)storage.written_packets(),
              (unsigned long long)storage.dropped_packets(),
              (unsigned long long)final_snap.packets_lost,
              final_snap.loss_rate_pct,
              storage.filepath().c_str());
    return 0;
}
