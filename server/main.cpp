#include <core/core.h>
#include <format/iformatter.h>
#include <format/impl/csvformatter.h>
#include <storage/storagemanager.h>
#include <transport/itransport.h>
#ifdef HAVE_WEBSOCKET
    #include <transport/impl/websocket.h>
#endif
#ifdef HAVE_MQTT
    #include <transport/impl/mqtt.h>
#endif
#include <memory>
#include <atomic>
#include <csignal>

namespace
{
    static std::atomic<bool> g_running{true};

    static void signal_handler(int /*sig*/)
    {
        g_running = false;
    };
}

int main(int argc, char* argv[])
{
    server::core::types::config_t conf = server::core::parse_args(argc, argv);

    if(server::core::network::network_init() != server::core::status::STATUS_OK)
    {
        std::cerr << "[FATAL] Network init failed: "
        << server::core::network::last_socket_error() << "\n";
        server::core::network::network_cleanup();
        return 1;
    }

    // create storage manger for reingbuffer and write stream

    auto formatter = std::unique_ptr<server::format::IFormatter>(
        new server::format::CsvFormatter()
    );

    server::storage::StorageManager storage_manager
    (
        std::move(formatter),
        conf.filename,
        conf.buffer_capacity,
        conf.flush_interval_ms
    );

    storage_manager.start();

    std::unique_ptr<server::transport::ITransport> transport;

    // add signal handlers for user/system process interrupt
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    return 0;
}
