/*
 * Thread-safe ring buffer flushing to a CSV file.
 * If output_filepath ends with a path separator it is treated as a directory
 * prefix and a timestamped filename is generated inside that directory.
 * If the directory does not exist and the filesystem is writable the full
 * path is created recursively before the first write attempt.
 */
#include <storage/storage_manager.hpp>
#include <logger/logger.hpp>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>

// Platform-specific directory creation helpers.
#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <errno.h>
#endif

namespace server
{

// ---------------------------------------------------------------------------
// ensure_directory_exists
// Creates all missing directories in 'path' (recursive mkdir).
// Returns true if the directory already exists or was successfully created.
// Returns false if creation failed (e.g. permission denied).
// ---------------------------------------------------------------------------
static bool ensure_directory_exists(const std::string& path)
{
    if (path.empty()) return true;

    // Iterate over all prefixes separated by '/' or '\\'.
    std::string partial;
    for (std::size_t i = 0; i <= path.size(); ++i)
    {
        const bool sep = (i == path.size())
                       || (path[i] == '/')
                       || (path[i] == '\\');
        if (!sep)
        {
            partial += path[i];
            continue;
        }
        if (partial.empty() || partial == "." || partial == "..") {
            if (i < path.size()) partial += path[i];
            continue;
        }

#ifdef _WIN32
        int rc = _mkdir(partial.c_str());
        if (rc != 0 && errno != EEXIST) return false;
#else
        int rc = ::mkdir(partial.c_str(), 0755);
        if (rc != 0 && errno != EEXIST) return false;
#endif
        if (i < path.size()) partial += path[i];
    }
    return true;
}

// ---------------------------------------------------------------------------
// extract_directory
// Returns the directory component of a file path (without trailing separator).
// Returns empty string for paths with no directory component.
// ---------------------------------------------------------------------------
static std::string extract_directory(const std::string& filepath)
{
    auto pos = filepath.find_last_of("/\\");
    if (pos == std::string::npos) return {};
    return filepath.substr(0, pos);
}

// ---------------------------------------------------------------------------
// make_path_in_dir
// Appends a default timestamped filename to a directory prefix that already
// ends with '/' or '\\'.
// ---------------------------------------------------------------------------
static std::string make_path_in_dir(const std::string& prefix)
{
    return prefix + StorageManager::generate_default_filename();
}

// ---------------------------------------------------------------------------

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
            // Treat as directory prefix; append auto-generated filename.
            output_filepath_ = make_path_in_dir(output_filepath);
        } else {
            output_filepath_ = output_filepath;
        }
    }
}

StorageManager::~StorageManager() { stop(); }

void StorageManager::start()
{
    running_ = true;

    // Create the target directory if it does not exist.
    std::string dir = extract_directory(output_filepath_);
    if (!dir.empty() && !ensure_directory_exists(dir))
    {
        LOG_ERRF("[Storage] Cannot create directory: %s", dir.c_str());
        // Continue anyway; write_header will report a more specific error.
    }

    if (!formatter_->write_header(output_filepath_))
    {
        LOG_ERRF("[Storage] Cannot write header to: %s", output_filepath_.c_str());
    }
    else
    {
        LOG_INFOF("[Storage] Output: %s", output_filepath_.c_str());
    }

    writer_thread_ = std::thread(&StorageManager::flush_loop, this);
}

void StorageManager::stop()
{
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stop_cv_.notify_all();
    }
    if (writer_thread_.joinable()) writer_thread_.join();
    flush_now(); // drain remainder
}

bool StorageManager::push(const DataPacket& packet)
{
    if (!buffer_.try_push(packet)) {
        ++dropped_packets_;
        return false;
    }
    return true;
}

void StorageManager::flush_loop()
{
    while (running_.load())
    {
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock,
                std::chrono::milliseconds(flush_interval_ms_),
                [this] { return !running_.load(); });
        }
        flush_now();
    }
}

void StorageManager::flush_now()
{
    auto packets = buffer_.drain_nowait();
    if (packets.empty()) return;
    if (!formatter_->write_packets(output_filepath_, packets))
    {
        LOG_ERRF("[Storage] Write error: %s", output_filepath_.c_str());
    }
    else
    {
        written_packets_ += static_cast<uint64_t>(packets.size());
    }
}

/* Generate filename: data-DD-MM-YYYY-HH-MM-SS-MMM.csv */
std::string StorageManager::generate_default_filename()
{
    using namespace std::chrono;
    auto now   = system_clock::now();
    auto now_t = system_clock::to_time_t(now);
    auto ms    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

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

} // namespace server
