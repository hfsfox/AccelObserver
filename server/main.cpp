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
#include <iostream>

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

    if (conf.transport == server::core::types::transport_type_t::TRANSPORT_WEBSOCKET)
    {
        #ifdef HAVE_WEBSOCKET
            //transport.reset(/*new subscriber::WsSubscriber()*/);
        #else
            std::cerr << "[FATAL] WebSocket support not compiled. "
            "Rebuild with -DENABLE_WEBSOCKET=ON\n";
            return 1;
        #endif
    }
    else
    {
        #ifdef HAVE_MQTT
            //sub.reset(new subscriber::MqttSubscriber("data-subscriber-1", cfg.mqtt_topic));
        #else
            std::cerr << "[FATAL] MQTT support not compiled. "
            "Install libmosquitto and rebuild with -DENABLE_MQTT=ON\n";
            return 1;
        #endif
    }

    // Callback register, validation and buffer
    /*
    transport->set_callback([&storage_manager](const std::string& payload)
    {
        //auto parsed = subscriber::PacketValidator::parse(payload);
    }*/

    // add signal handlers for user/system process interrupt
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Ждём сигнала остановки
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[INFO] Shutting down...\n";

    server::core::network::network_cleanup();

    return 0;
}
