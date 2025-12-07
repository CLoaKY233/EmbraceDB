#pragma once

#include <chrono>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <memory>
#include <source_location>
#include <string>

namespace embrace::log {

    enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5, Off = 6 };

    struct LogConfig {
        Level level = Level::Info;
        bool console_output = true;
        std::string file_path = ""; // empty for no file output
    };

    class Logger {
      public:
        static auto instance() -> Logger &;

        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        void init(const LogConfig &config);
        void shutdown();

        template <typename... Args>
        void log(Level level, const std::source_location &loc, fmt::format_string<Args...> fmt,
                 Args &&...args) {
            if (level < config_.level)
                return;

            try {
                std::string msg = fmt::format(fmt, std::forward<Args>(args)...);
                enqueue_log(level, loc, std::move(msg));

            } catch (const std::exception &e) {
                enqueue_log(Level::Error, loc, fmt::format("LOG FORMAT ERROR: {}", e.what()));
            }
        }

        void set_level(Level level) {
            config_.level = level;
        }

      private:
        Logger() = default;
        ~Logger();

        struct LogEntry {
            Level level;
            std::string file_name;
            int line;
            std::string message;
            std::chrono::system_clock::time_point timestamp;
        };

        void enqueue_log(Level level, const std::source_location &loc, std::string &&msg);
        void worker_loop();

        struct Impl;
        std::unique_ptr<Impl> impl_;
        LogConfig config_;
    };
} // namespace embrace::log

#define LOG_TRACE(...)                                                                             \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Trace,                           \
                                           std::source_location::current(), __VA_ARGS__)

#define LOG_DEBUG(...)                                                                             \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Debug,                           \
                                           std::source_location::current(), __VA_ARGS__)

#define LOG_INFO(...)                                                                              \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Info,                            \
                                           std::source_location::current(), __VA_ARGS__)

#define LOG_WARN(...)                                                                              \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Warn,                            \
                                           std::source_location::current(), __VA_ARGS__)

#define LOG_ERROR(...)                                                                             \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Error,                           \
                                           std::source_location::current(), __VA_ARGS__)

#define LOG_FATAL(...)                                                                             \
    ::embrace::log::Logger::instance().log(::embrace::log::Level::Fatal,                           \
                                           std::source_location::current(), __VA_ARGS__)
