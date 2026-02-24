#include <core/core.h>
#include <format/iformatter.h>
#include <format/impl/csvformatter.h>
#include <memory>

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

    auto foramtter = std::unique_ptr<server::format::IFormatter>(
        new server::format::CsvFormatter());

    // ---- Создание StorageManager (буфер + поток записи) -------------------
    /*
    auto formatter = std::unique_ptr<subscriber::IFormatter>(
        new subscriber::CsvFormatter());

    subscriber::StorageManager storage(
        std::move(formatter),
                                       cfg.output_file,
                                       cfg.buffer_capacity,
                                       cfg.flush_interval_ms
    );
    storage.start();
    */

    return 0;
}
