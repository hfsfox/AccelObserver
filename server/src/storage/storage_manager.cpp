/*
 * Thread-safe ring buffer flushing to a CSV file.
 * If output_filepath ends with a path separator, treat it as a directory
 * prefix and generate a timestamped filename inside that directory.
 */
#include <storage/storage_manager.hpp>
#include <logger/logger.hpp>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>

namespace server
{

// Append a default timestamped filename to a directory prefix.
// prefix must end with '/' or '\\'.
static std::string make_path_in_dir(const std::string& prefix) {
    std::string name = StorageManager::generate_default_filename();
    return prefix + name;
}

StorageManager::StorageManager(std::unique_ptr<IFormatter> formatter,
                               const std::string& output_filepath,
                               std::size_t buffer_capacity,
                               std::size_t flush_interval_ms)
    : formatter_(std::move(formatter))
    , flush_interval_ms_(flush_interval_ms)
    , buffer_(buffer_capacity)
    , running_(false)
    , dropped_packets_(0)
    , written_packets_(0)
{
    if (output_filepath.empty()) {
        output_filepath_ = generate_default_filename();
    } else {
        char last = output_filepath.back();
        if (last == '/' || last == '\\') {
            // Treat as directory; append auto-generated filename.
            output_filepath_ = make_path_in_dir(output_filepath);
        } else {
            output_filepath_ = output_filepath;
        }
    }
}

StorageManager::~StorageManager() { stop(); }

void StorageManager::start() {
    running_ = true;
    if (!formatter_->write_header(output_filepath_)) {
        LOG_ERRF("[Storage] Cannot write header to: %s", output_filepath_.c_str());
    } else {
        LOG_INFOF("[Storage] Output: %s", output_filepath_.c_str());
    }
    writer_thread_ = std::thread(&StorageManager::flush_loop, this);
}

void StorageManager::stop() {
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stop_cv_.notify_all();
    }
    if (writer_thread_.joinable()) writer_thread_.join();
    flush_now(); // drain remainder
}

bool StorageManager::push(const DataPacket& packet) {
    if (!buffer_.try_push(packet)) {
        ++dropped_packets_;
        return false;
    }
    return true;
}

void StorageManager::flush_loop() {
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock,
                std::chrono::milliseconds(flush_interval_ms_),
                [this] { return !running_.load(); });
        }
        flush_now();
    }
}

void StorageManager::flush_now() {
    auto packets = buffer_.drain_nowait();
    if (packets.empty()) return;
    if (!formatter_->write_packets(output_filepath_, packets)) {
        LOG_ERRF("[Storage] Write error: %s", output_filepath_.c_str());
    } else {
        written_packets_ += static_cast<uint64_t>(packets.size());
    }
}

/* Generate filename: data-DD-MM-YYYY-HH-MM-SS-MMM.csv */
std::string StorageManager::generate_default_filename() {
    using namespace std::chrono;
    auto now    = system_clock::now();
    auto now_t  = system_clock::to_time_t(now);
    auto ms     = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now_t);
#else
    localtime_r(&now_t, &tm_buf);
#endif

    char base[64];
    std::strftime(base, sizeof(base), "data-%d-%m-%Y-%H-%M-%S", &tm_buf);

    char result[80];
    std::snprintf(result, sizeof(result), "%s-%03d.csv", base, (int)ms.count());
    return result;
}

}
