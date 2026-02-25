#ifndef __STORAGEMANAGER_H__
#define __STORAGEMANAGER_H__

#include <format/iformatter.h>
#include <core/common/statuscode.h>
#include <sctl/ringbuffer.h>

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstddef>

namespace server
{
    namespace storage
    {
        class StorageManager
        {
            public:
                StorageManager(
                    std::unique_ptr<server::format::IFormatter> formatter,
                               const std::string& output_filepath,
                               /*std::*/size_t buffer_capacity,
                               /*std::*/size_t flush_interval_ms);

                ~StorageManager();

                // Non-copyable
                StorageManager(const StorageManager&) = delete;
                StorageManager& operator=(const StorageManager&) = delete;
                //Start write thread. Must called before push()
                void start();
                //Stop write thread, wait to shutdown and sync rest
                void stop();

                /// Потокобезопасно добавить пакет в буфер.
                /// Возвращает false, если буфер переполнен (пакет отброшен).
                // add packet to buffer
                // return false if buffer overflow (packet dropped)
                bool push(const server::format::types::data_packet_t& packet);

                uint64_t dropped_packets() const noexcept { return _dropped_packets.load(); }
                uint64_t written_packets() const noexcept { return _written_packets.load(); }
            private:
                void flush_loop();
                void flush_now();
            private:

                std::unique_ptr<server::format::IFormatter>  _formatter;
                std::string                  _output_filepath_;
                std::size_t                  _flush_interval_ms;
                //RingBuffer<server::format::types::data_packet_t> _buffer;
                server::sctl::ringbuffer<server::format::types::data_packet_t> _buffer;

                std::thread                  _writer_thread;
                std::atomic<bool>            _running;
                std::mutex                   _stop_mutex;
                std::condition_variable      _stop_cv;

                std::atomic<uint64_t>        _dropped_packets;
                std::atomic<uint64_t>        _written_packets;
        };
    }
}

#endif
