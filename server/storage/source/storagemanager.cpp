#include <storage/storagemanager.h>
#include <chrono>
#include <iostream>

namespace
{
}

namespace server
{
    namespace storage
    {
        StorageManager::StorageManager(
            std::unique_ptr<server::format::IFormatter> formatter,
                               const std::string& output_filepath,
                               std::size_t buffer_capacity,
                               std::size_t flush_interval_ms)
        :
        _formatter(std::move(formatter))
        ,_output_filepath(output_filepath)
        ,_flush_interval_ms(flush_interval_ms)
        ,_buffer(buffer_capacity)
        ,_running(false)
        ,_dropped_packets(0)
        ,_written_packets(0)
        {
        }

        StorageManager::~StorageManager()
        {
            stop();
        }

        void StorageManager::start()
        {
            _running = true;
            _formatter->write_header(_output_filepath);
            _writer_thread = std::thread(&StorageManager::flush_loop, this);
        }

        void StorageManager::stop()
        {
            // exchange возвращает предыдущее значение — защита от двойного вызова
            if (!_running.exchange(false)) return;

            // Разбудить поток записи, если он спит в wait_for
            {
                std::lock_guard<std::mutex> lock(_stop_mutex);
                _stop_cv.notify_all();
            }

            // Также разблокировать drain() если поток ждёт данных
            _buffer.stop();

            if (_writer_thread.joinable())
                _writer_thread.join();

            // Финальный сброс того, что накопилось за время join
            flush_now();
        }

        bool StorageManager::push(const server::format::types::data_packet_t& packet)
        {
            if (!_buffer.try_push(packet)) {
                ++_dropped_packets;
                return false;
            }
            return true;
        }

        void StorageManager::flush_loop() {
            while (_running.load()) {
                {
                    std::unique_lock<std::mutex> lock(_stop_mutex);
                    _stop_cv.wait_for(lock,
                                      std::chrono::milliseconds(_flush_interval_ms),
                                      [this] { return !_running.load(); });
                }
                flush_now();
            }
        }

        void StorageManager::flush_now()
        {
            auto packets = _buffer.drain_nowait();
            if (packets.empty()) return;

            if (_formatter->write_packets(_output_filepath, packets)) {
                _written_packets += static_cast<uint64_t>(packets.size());
            } else {
                std::cerr << "[StorageManager] Write error for file: "
                << _output_filepath << "\n";
            }
        }

    }
}
