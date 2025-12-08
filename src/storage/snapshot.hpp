#pragma once

#include "core/status.hpp"
#include <cstdint>
#include <string>

namespace embrace::indexing {
    class Btree;
}

namespace embrace::storage {

    constexpr uint32_t SNAPSHOT_MAGIC = 0x454D4252;
    constexpr uint32_t SNAPSHOT_VERSION = 1;

    class Snapshotter {
      public:
        explicit Snapshotter(const std::string &snapshot_path);
        ~Snapshotter() = default;

        auto create_snapshot(const indexing::Btree &tree) -> core::Status;
        auto load_snapshot(indexing::Btree &tree) -> core::Status;
        [[nodiscard]] auto exists() const -> bool;

      private:
        std::string snapshot_path_;

        class FileHandle {
          public:
            explicit FileHandle(int fd) : fd_(fd) {}
            ~FileHandle() {
                if (fd_ >= 0)
                    ::close(fd_);
            }

            FileHandle(const FileHandle &) = delete;
            FileHandle &operator=(const FileHandle &) = delete;

            [[nodiscard]] auto get() const -> int {
                return fd_;
            }
            [[nodiscard]] auto valid() const -> bool {
                return fd_ >= 0;
            }

          private:
            int fd_;
        };

        auto validate_snapshot_header(int fd) -> core::Status;
    };
} // namespace embrace::storage
