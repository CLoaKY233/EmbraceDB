#pragma once

#include <fmt/core.h>
#include <string>
#include <utility>

namespace embrace::core {

    enum class StatusCode {
        Ok = 0,
        NotFound = 1,
        Corruption = 2,
        NotSupported = 3,
        InvalidArgument = 4,
        IOError = 5
    };

    class Status {
      public:
        // default constructor : OK status (fast path)
        Status() : code_(StatusCode::Ok), msg_("") {}
        Status(StatusCode code, std::string msg) : code_(code), msg_(std::move(msg)) {}

        // static helpers
        static auto Ok() -> Status {
            return Status();
        }
        static auto NotFound(const std::string &msg) -> Status {
            return Status(StatusCode::NotFound, msg);
        }
        static auto Corruption(const std::string &msg) -> Status {
            return Status(StatusCode::Corruption, msg);
        }
        static auto IOError(const std::string &msg) -> Status {
            return Status(StatusCode::IOError, msg);
        }
        static auto InvalidArgument(const std::string &msg) -> Status {
            return Status(StatusCode::InvalidArgument, msg);
        }

        // checkers
        [[nodiscard]] auto ok() const -> bool {
            return code_ == StatusCode::Ok;
        }
        [[nodiscard]] auto is_not_found() const -> bool {
            return code_ == StatusCode::NotFound;
        }

        // formatting for logging
        [[nodiscard]] auto to_string() const -> std::string {
            if (ok())
                return "OK";
            return fmt::format("{}: {}", code_to_string(code_), msg_);
        }

      private:
        StatusCode code_;
        std::string msg_;

        static auto code_to_string(StatusCode code) -> std::string {
            switch (code) {
            case StatusCode::Ok:
                return "Ok";
            case StatusCode::NotFound:
                return "NotFound";
            case StatusCode::Corruption:
                return "Corruption";
            case StatusCode::NotSupported:
                return "NotSupported";
            case StatusCode::InvalidArgument:
                return "InvalidArgument";
            case StatusCode::IOError:
                return "IOError";
            default:
                return "Unknown";
            }
        }
    };
} // namespace embrace::core
