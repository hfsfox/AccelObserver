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
#include <misc/help.hpp>

#ifdef ENABLE_CONFPARSER
extern "C"
{
    #include <confparser.h>
}
#include <conf/confloader.hpp>
#endif

#include <conf/cliargsparser.hpp>

#ifdef HAVE_WEBSOCKET
    //#include <transport/websocket/ws_subscriber.hpp>
#endif

#ifdef HAVE_MQTT
    //#include <transport/mqtt/mqtt_subscriber.hpp>
#endif

#ifdef HAVE_WEBUI
    //#include <web/web_interface.hpp>
#endif

// atomic running state for begin and end main event loop
static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running = false; }

// wall-clock time in milliseconds (steady_clock for monotonic latency calc)
static uint64_t
now_ms()
{
    //using namespace std::chrono;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
            );
}

static server::LogLevel
parse_log_level(const std::string& s)
{
    if (s == "DEBUG") return server::LogLevel::DEBUG;
    if (s == "WARN")  return server::LogLevel::WARN;
    if (s == "ERROR") return server::LogLevel::ERR;
    return server::LogLevel::INFO;
}

#ifdef HAVE_CONFPARSER

static const char*
extract_config_path(int argc, char* argv[])
{
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], "--config") == 0) return argv[i + 1];
        return nullptr;
}

#endif /* HAVE_CONFPARSER */


int main(int argc, char* argv[])
{
    server::Config cfg;

    // Step 1: config file
    #ifdef HAVE_CONFPARSER
    {
        const char* explicit_path = extract_config_path(argc, argv);
        char found_path[512] = {};
        bool found = conf_find_config("gaccelserveer", explicit_path,
                                     found_path, sizeof(found_path));
        if (explicit_path && !found) {
            std::cerr << "[ERROR] Config file not found: " << explicit_path << "\n";
            return 1;
        }
        if (found) {
            std::cout << "[config] Loading: " << found_path << "\n";
            char err_buf[256] = {};
            IniDoc* conf = conf_load(found_path, err_buf);
            if (!conf) {
                std::cerr << "[ERROR] Cannot read config: " << err_buf << "\n";
                return 1;
            }
            if (conf_error(conf)[0] != '\0')
                std::cerr << "[config] Warning: " << conf_error(conf) << "\n";
            apply_conf(conf, cfg);
            conf_free(conf);
        }
    }
    #endif

    // Step 2: CLI overrides config file
    cfg = parse_args(argc, argv, cfg);

    // --dump-config: print resolved values and exit
    #ifdef HAVE_CONFPARSER
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-config") == 0) {
            dump_config(cfg);
            return 0;
        }
    }
    #endif /* HAVE_CONFPARSER */

    // Init Logger
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

    /* Network */
    if (!server::platform::net_init())
    {
        LOG_ERRF("[Server] net_init: %s",
                 server::platform::last_socket_error().c_str());
        return 1;
    }

    /* Auto-size buffer_capacity based on rate and flush interval.
     * Required minimum: rate_hz * flush_interval_ms / 1000.
     * We use a 2x safety factor to absorb bursts and writer latency jitter. */

    if (cfg.auto_buffer)
    {
        // We do not have an explicit rate_hz in Config (that belongs to the client).
        // Use a conservative default assumption of 200 Hz.
        // If the user sets --buf explicitly (auto_buffer=false), skip this.
        const double assumed_rate_hz = 200.0;
        std::size_t min_buf = static_cast<std::size_t>(
            assumed_rate_hz * static_cast<double>(cfg.flush_interval_ms) / 1000.0 * 2.0);
        if (min_buf < 64) min_buf = 64;
        if (cfg.buffer_capacity < min_buf)
        {
            LOG_INFOF("[Storage] auto_buffer: capacity %zu -> %zu "
            "(2 x %.0fHz x %zums flush)",
                      cfg.buffer_capacity, min_buf,
                      assumed_rate_hz, cfg.flush_interval_ms);
            cfg.buffer_capacity = min_buf;
        }
    }


}
