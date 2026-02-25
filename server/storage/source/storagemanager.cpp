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
        StorageManager::StorageManager(std::unique_ptr<IFormatter> formatter,
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

        bool StorageManager::push(const DataPacket& packet)
        {
            if (!_buffer.try_push(packet)) {
                ++_dropped_packets;
                return false;
            }
            return true;
        }


    }
}
