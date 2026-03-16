// logger singleton routine, supports store to file, streams or both
// setup via instance + configure()
#ifndef __LOGGER_H__
#define __LOGGER_H__

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace server
{

    enum class LogLevel
    {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERR = 3
    };

    class Logger
    {
    public:
        static Logger& instance();

        // Non-copyable
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        // configure() must be called before first logging call
        //filepaths settings
        // ""      : stderr only
        // "stdout": stdout only
        // path    : file+stderr(if also_stderr = true)
        //min_level:   minimal level of records
        //also_stderr: duplicate to stream file info
        void configure(const std::string& filepath,
                   LogLevel min_level   = LogLevel::INFO,
                   bool     also_stderr = true);

        void log(LogLevel level, const std::string& msg);

        void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
        void info (const std::string& msg) { log(LogLevel::INFO,  msg); }
        void warn (const std::string& msg) { log(LogLevel::WARN,  msg); }
        void error(const std::string& msg) { log(LogLevel::ERR,   msg); }

        // return path to current log file (may be empty if stderr/stdout option defined)
        const std::string& filepath() const { return filepath_; }

        // Generate path to log file in platform specific default log data dir
        // format: <log_dir>/data_subscriber_YYYY-MM-DD.log
        static std::string default_log_path();

    private:
        Logger() = default;
        ~Logger();

        mutable std::mutex mutex_;
        std::ofstream      file_;
        std::string        filepath_;
        LogLevel           min_level_   = LogLevel::INFO;
        bool               also_stderr_ = true;
        bool               to_stdout_   = false;

        static const char* level_str(LogLevel l);
        std::string        timestamp_str();
    };

    // Convenience macros used everywhere in server
    #define LOG_DEBUG(msg) ::subscriber::Logger::instance().debug(msg)
    #define LOG_INFO(msg)  ::subscriber::Logger::instance().info(msg)
    #define LOG_WARN(msg)  ::subscriber::Logger::instance().warn(msg)
    #define LOG_ERR(msg)   ::subscriber::Logger::instance().error(msg)

    // formatted version over snprintf
    std::string log_fmt(const char* fmt, ...);
    #define LOG_INFOF(...)  LOG_INFO(::subscriber::log_fmt(__VA_ARGS__))
    #define LOG_WARNF(...)  LOG_WARN(::subscriber::log_fmt(__VA_ARGS__))
    #define LOG_ERRF(...)   LOG_ERR(::subscriber::log_fmt(__VA_ARGS__))

}

#endif // __LOGGER_H__
