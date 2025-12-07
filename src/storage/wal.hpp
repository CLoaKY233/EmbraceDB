#pragma once

#include "core/common.hpp"
#include "core/status.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace embrace::storage {
    enum class WalRecordType : uint8_t { Put = 1, Delete = 2, Update = 3, Checkpoint = 3 };

    struct WalRecord {
        WalRecordType type;
        core::Key key;
        core::Value value;

        WalRecord() = default;
        WalRecord(WalRecordType t, core::Key k, core::Value v)
            : type(t), key(std::move(k)), value(std::move(v)) {}
    };

    class WalWriter {
      public:
        explicit WalWriter(const std::string &wal_path);
        ~WalWriter();

        WalWriter(const WalWriter &) = delete;
        WalWriter &operator=(const WalWriter &) = delete;
        WalWriter(WalWriter &&) = delete;
        WalWriter &operator=(WalWriter &&) = delete;

        auto write_put(const core::Key &key, const core::Value &value) -> core::Status;
        auto write_delete(const core::Key &key) -> core::Status;
        auto write_update(const core::Key &key, const core::Value &value) -> core::Status;
        auto write_checkpoint() -> core::Status;

        auto flush() -> core::Status;
        auto sync() -> core::Status;

        [[nodiscard]] auto is_open() const -> bool {
            return fd_ >= 0;
        }

      private:
        std::string wal_path_;
        int fd_;
        std::vector<char> buffer_;
        static constexpr size_t BUFFER_SIZE = 4096;

        auto write_record(const WalRecord &record) -> core::Status;
        auto flush_buffer() -> core::Status;
    };

    class WalReader {
      public:
        explicit WalReader(const std::string &wal_path);
        ~WalReader();

        WalReader(const WalReader &) = delete;
        WalReader &operator=(const WalReader &) = delete;
        WalReader(WalReader &&) = delete;
        WalReader &operator=(WalReader &&) = delete;

        auto read_next(WalRecord &record) -> core::Status;

        [[nodiscard]] auto has_more() const -> bool;

        [[nodiscard]] auto is_open() const -> bool {
            return fd_ >= 0;
        }

      private:
        std::string wal_path_;
        int fd_;
        std::vector<char> read_buffer_;
        size_t buffer_pos_;
        size_t buffer_size_;
        static constexpr size_t READ_BUFFER_SIZE = 8192;

        auto fill_buffer() -> core::Status;
        auto read_bytes(char *dest, size_t n) -> core::Status;
    };

} // namespace embrace::storage
