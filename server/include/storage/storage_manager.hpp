#pragma once
/*
 * Buffered, thread-safe CSV writer.
 * Accepts DataPackets via push(), flushes to disk on a background thread.
 * If output_filepath ends with '/' or '\', it is treated as a directory
 * and a timestamped filename is appended automatically.
 */
#include <core/servertypes.hpp>
#include <ringbuffer/ringbuffer.hpp>
#include <format/iformatter.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>

namespace server
{

    class StorageManager
    {
    public:
        StorageManager(std::unique_ptr<IFormatter> formatter,
                    const std::string& output_filepath,
                    std::size_t buffer_capacity,
                    std::size_t flush_interval_ms);
        ~StorageManager();

        StorageManager(const StorageManager&) = delete;
        StorageManager& operator=(const StorageManager&) = delete;

        void start();
        void stop();

        bool push(const DataPacket& packet);

        uint64_t dropped_packets()  const noexcept { return dropped_packets_.load(); }
        uint64_t written_packets()  const noexcept { return written_packets_.load(); }
        const std::string& filepath() const        { return output_filepath_; }

        // Generate a default filename: data-DD-MM-YYYY-HH-MM-SS-MMM.csv
        static std::string generate_default_filename();

    private:
        void flush_loop();
        void flush_now();

        std::unique_ptr<IFormatter>  formatter_;
        std::string                  output_filepath_;
        std::size_t                  flush_interval_ms_;
        RingBuffer<DataPacket>       buffer_;
        std::thread                  writer_thread_;
        std::atomic<bool>            running_;
        std::mutex                   stop_mutex_;
        std::condition_variable      stop_cv_;
        std::atomic<uint64_t>        dropped_packets_;
        std::atomic<uint64_t>        written_packets_;
    };

}
