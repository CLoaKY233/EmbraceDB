#pragma once

#include <atomic>
#include <chrono>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>

namespace embrace::log {

    enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5, Off = 6 };

    struct LogConfig {
        Level level = Level::Info;
        bool console_output = true; // Must not be modified after init()
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
        void log(Level level, const std::source_location &loc,
                 fmt::format_string<Args...> format_str, Args &&...args) {
            if (level < current_level_.load(std::memory_order_relaxed))
                return;

            try {
                std::string msg = fmt::format(format_str, std::forward<Args>(args)...);
                enqueue_log(level, loc, std::move(msg));

            } catch (const std::exception &e) {
                enqueue_log(Level::Error, loc, fmt::format("LOG FORMAT ERROR: {}", e.what()));
            }
        }

        void set_level(Level level) {
            current_level_.store(level, std::memory_order_relaxed);
            config_.level = level; // Keep config in sync
        }

        bool is_initialized() const {
            return static_cast<bool>(impl_);
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

        mutable std::once_flag shutdown_flag_;
        struct Impl;
        std::shared_ptr<Impl> impl_;
        std::atomic<Level> current_level_{Level::Info};
        LogConfig config_;
    };
} // namespace embrace::log

// Logging level guidance (default Level::Info in production)
// - Trace: extremely verbose internals (per-record/per-node); usually off.
// - Debug: diagnostics and timings safe to disable in production.
// - Info: lifecycle milestones (startup, recovery, checkpoints) and success summaries.
// - Warn: unexpected but tolerated conditions; action may be required.
// - Error: operation failed; durability or availability likely impacted.
// - Fatal: process cannot continue; expect termination/abort.

#define LOG_TRACE(...)                                                                             \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Trace, std::source_location::current(),              \
                       __VA_ARGS__);                                                               \
        }                                                                                          \
    } while (0)

#define LOG_DEBUG(...)                                                                             \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Debug, std::source_location::current(),              \
                       __VA_ARGS__);                                                               \
        }                                                                                          \
    } while (0)

#define LOG_INFO(...)                                                                              \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Info, std::source_location::current(), __VA_ARGS__); \
        }                                                                                          \
    } while (0)

#define LOG_WARN(...)                                                                              \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Warn, std::source_location::current(), __VA_ARGS__); \
        }                                                                                          \
    } while (0)

#define LOG_ERROR(...)                                                                             \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Error, std::source_location::current(),              \
                       __VA_ARGS__);                                                               \
        }                                                                                          \
    } while (0)

#define LOG_FATAL(...)                                                                             \
    do {                                                                                           \
        auto &logger = ::embrace::log::Logger::instance();                                         \
        if (logger.is_initialized()) {                                                             \
            logger.log(::embrace::log::Level::Fatal, std::source_location::current(),              \
                       __VA_ARGS__);                                                               \
        }                                                                                          \
    } while (0)
