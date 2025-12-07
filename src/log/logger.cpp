#include "log/logger.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <thread>
#include <vector>

namespace embrace::log {
    struct Logger::Impl {
        std::mutex queue_mutex;
        std::condition_variable cv;
        std::deque<LogEntry> queue;
        std::thread worker_thread;
        bool exit_flag = false;
        std::ofstream log_file;
    };

    auto Logger::instance() -> Logger & {
        static Logger instance;
        return instance;
    }

    Logger::~Logger() {
        shutdown();
    }

    void Logger::init(const LogConfig &config) {
        if (impl_) {
            LOG_WARN("Logger already initialized, ignoring duplicate init() call");
            return; // Already initialized
        }
        config_ = config;
        current_level_.store(config.level, std::memory_order_relaxed);
        impl_ = std::make_shared<Impl>();
        if (!config_.file_path.empty()) {
            impl_->log_file.open(config_.file_path, std::ios::out | std::ios::app);
            if (!impl_->log_file.is_open()) {
                std::fprintf(stderr, "Failed to open log file: %s\n", config_.file_path.c_str());
            }
        }
        impl_->worker_thread = std::thread(&Logger::worker_loop, this);
    }

    void Logger::shutdown() {
        if (impl_ && impl_->worker_thread.joinable()) {
            {
                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                impl_->exit_flag = true;
            }
            impl_->cv.notify_one();
            impl_->worker_thread.join();
        }

        if (impl_ && impl_->log_file.is_open()) {
            impl_->log_file.close();
        }
        impl_.reset();
    }

    void Logger::enqueue_log(Level level, const std::source_location &loc, std::string &&msg) {
        auto impl = impl_;
        if (!impl)
            return;

        std::filesystem::path full_path(loc.file_name());
        std::string filename = full_path.filename().string();

        {
            std::lock_guard<std::mutex> lock(impl->queue_mutex);
            impl->queue.push_back(LogEntry{level, std::move(filename), static_cast<int>(loc.line()),
                                           std::move(msg), std::chrono::system_clock::now()});
        }
        impl->cv.notify_one();
    }

    static auto get_level_color(Level level) -> fmt::text_style {
        switch (level) {
        case Level::Trace:
            return fmt::fg(fmt::color::gray);
        case Level::Debug:
            return fmt::fg(fmt::color::cyan);
        case Level::Info:
            return fmt::fg(fmt::color::green);
        case Level::Warn:
            return fmt::fg(fmt::color::yellow) | fmt::emphasis::bold;
        case Level::Error:
            return fmt::fg(fmt::color::red) | fmt::emphasis::bold;
        case Level::Fatal:
            return fmt::bg(fmt::color::red) | fmt::fg(fmt::color::white) | fmt::emphasis::bold;
        default:
            return fmt::fg(fmt::color::white);
        }
    }

    static auto get_level_string(Level level) -> std::string_view {
        switch (level) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return " INFO"; // Space for alignment
        case Level::Warn:
            return " WARN";
        case Level::Error:
            return "ERROR";
        case Level::Fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    void Logger::worker_loop() {
        while (true) {
            std::vector<LogEntry> processing_batch;

            {
                std::unique_lock<std::mutex> lock(impl_->queue_mutex);
                impl_->cv.wait(lock, [this] { return !impl_->queue.empty() || impl_->exit_flag; });

                if (impl_->exit_flag && impl_->queue.empty()) {
                    break;
                }
                std::move(impl_->queue.begin(), impl_->queue.end(),
                          std::back_inserter(processing_batch));
                impl_->queue.clear();
            }

            for (const auto &entry : processing_batch) {
                // Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [file:line] message
                if (config_.console_output) {
                    auto time_t_val = std::chrono::system_clock::to_time_t(entry.timestamp);
                    auto time_fmt = fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(time_t_val));

                    fmt::print(stdout, "{} ",
                               fmt::format(fmt::fg(fmt::color::dim_gray), "[{}]", time_fmt));
                    fmt::print(stdout, "{} ",
                               fmt::format(get_level_color(entry.level), "[{}]",
                                           get_level_string(entry.level)));
                    fmt::print(stdout, "{} ",
                               fmt::format(fmt::fg(fmt::color::steel_blue), "[{}:{}]",
                                           entry.file_name, entry.line));
                    fmt::print(stdout, "{}\n", entry.message);
                }

                if (impl_->log_file.is_open()) {
                    auto time_t_val = std::chrono::system_clock::to_time_t(entry.timestamp);
                    auto time_fmt = fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(time_t_val));

                    std::string line = fmt::format("[{}] [{}] [{}:{}] {}\n", time_fmt,
                                                   get_level_string(entry.level), entry.file_name,
                                                   entry.line, entry.message);
                    impl_->log_file << line;
                }
            }

            if (impl_->log_file.is_open()) {
                impl_->log_file.flush();
            }
        }
    }
} // namespace embrace::log
