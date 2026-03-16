#include <logger/logger.hpp>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>    // for SHGetKnownFolderPath
#  pragma comment(lib, "shell32.lib")
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <pwd.h>
#endif

namespace subscriber
{

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::~Logger()
{
    if (file_.is_open()) file_.close();
}

void Logger::configure(const std::string& filepath,
                       LogLevel min_level,
                       bool also_stderr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_   = min_level;
    also_stderr_ = also_stderr;
    to_stdout_   = false;
    filepath_    = filepath;

    if (file_.is_open()) file_.close();

    if (filepath.empty() || filepath == "stderr") {
        // только stderr
        also_stderr_ = true;
    } else if (filepath == "stdout") {
        to_stdout_   = true;
        also_stderr_ = false;
    } else {
        file_.open(filepath, std::ios::out | std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "[Logger] WARN: cannot open log file: "
                      << filepath << "\n";
            also_stderr_ = true;
        }
    }
}


const char* Logger::level_str(LogLevel l)
{
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
    }
    return "?????";
}

std::string Logger::timestamp_str()
{
    using namespace std::chrono;
    auto now     = system_clock::now();
    auto time_t  = system_clock::to_time_t(now);
    auto ms_part = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_buf;

    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    char full[40];
    std::snprintf(full, sizeof(full), "%s.%03d", buf, (int)ms_part.count());
    return full;
}

void Logger::log(LogLevel level, const std::string& msg)
{
    if (level < min_level_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::string ts   = timestamp_str();
    const char* lvl  = level_str(level);
    // format: 2024-01-15 14:32:01.123 [INFO ] message
    std::ostringstream line;
    line << ts << " [" << lvl << "] " << msg << "\n";
    std::string out = line.str();

    if (file_.is_open()) {
        file_ << out;
        file_.flush();
    }
    if (also_stderr_ && !to_stdout_) {
        std::cerr << out;
    }
    if (to_stdout_) {
        std::cout << out;
    }
}

std::string Logger::default_log_path()
{
    std::string dir;

    #ifdef _WIN32
        /* %LOCALAPPDATA%\data_subscriber\ */
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
            dir = std::string(path) + "\\data_subscriber\\";
            CreateDirectoryA(dir.c_str(), NULL);
        } else {
            dir = ".\\";
        }
    #else
        // /var/log/ if have rights, othervise ~/.data_subscriber/
        if (::access("/var/log", W_OK) == 0) {
            dir = "/var/log/data_subscriber/";
            ::mkdir(dir.c_str(), 0755);
        } else {
            const char* home = ::getenv("HOME");
            if (!home) {
                struct passwd* pw = ::getpwuid(::getuid());
                home = pw ? pw->pw_dir : "/tmp";
            }
            dir = std::string(home) + "/.data_subscriber/";
            ::mkdir(dir.c_str(), 0755);
        }
    #endif

    using namespace std::chrono;
    auto now    = system_clock::now();
    auto time_t = system_clock::to_time_t(now);
    struct tm tm_buf;

    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif

    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);
    return dir + "data_subscriber_" + date_buf + ".log";
}

std::string log_fmt(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

} // namespace subscriber
