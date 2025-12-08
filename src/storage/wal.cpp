#include "core/common.hpp"
#include "core/status.hpp"
#include "log/logger.hpp"
#include "storage/checksum.hpp"
#include "storage/wal.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fmt/core.h>
#include <string>
#include <unistd.h>

namespace embrace::storage {

    WalWriter::WalWriter(const std::string &wal_path) : wal_path_(wal_path), fd_(-1) {
        buffer_.reserve(BUFFER_SIZE);

        fd_ = open(wal_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);

        if (fd_ < 0) {
            LOG_ERROR("Failed to open WAL file: {} (errno: {})", wal_path_, strerror(errno));
        } else {
            LOG_INFO("WAL opened successfully: {} (fd={})", wal_path_, fd_);
        }
    }

    WalWriter::~WalWriter() {
        if (fd_ >= 0) {
            flush();
            sync();
            close(fd_);
            LOG_INFO("WAL closed: {}", wal_path_);
        }
    }

    auto WalWriter::write_put(const core::Key &key, const core::Value &value) -> core::Status {
        WalRecord record(WalRecordType::Put, key, value);
        return write_record(record);
    }

    auto WalWriter::write_delete(const core::Key &key) -> core::Status {
        WalRecord record(WalRecordType::Delete, key, "");
        return write_record(record);
    }

    auto WalWriter::write_update(const core::Key &key, const core::Value &value) -> core::Status {
        WalRecord record(WalRecordType::Update, key, value);
        return write_record(record);
    }

    auto WalWriter::write_checkpoint() -> core::Status {
        WalRecord record(WalRecordType::Checkpoint, "", "");
        return write_record(record);
    }

    static auto write_le32(std::vector<char> &buf, uint32_t val) -> void {
        buf.push_back(static_cast<char>(val & 0xFF));
        buf.push_back(static_cast<char>((val >> 8) & 0xFF));
        buf.push_back(static_cast<char>((val >> 16) & 0xFF));
        buf.push_back(static_cast<char>((val >> 24) & 0xFF));
    }

    auto WalWriter::write_record(const WalRecord &record) -> core::Status {
        if (fd_ < 0) {
            return core::Status::IOError("WAL file not open");
        }

        uint32_t key_len = static_cast<uint32_t>(record.key.size());
        uint32_t value_len = static_cast<uint32_t>(record.value.size());

        if (key_len > core::MAX_KEY_SIZE) {
            return core::Status::InvalidArgument("Key too large for WAL");
        }
        if (value_len > core::MAX_VALUE_SIZE) {
            return core::Status::InvalidArgument("Value too large for WAL");
        }

        size_t record_size = 1 + 4 + key_len + 4 + value_len + 4;

        if (buffer_.size() + record_size > BUFFER_SIZE) {
            auto status = flush_buffer();
            if (!status.ok()) {
                return status;
            }
        }

        std::vector<char> temp_record;
        temp_record.reserve(record_size - 4);

        temp_record.push_back(static_cast<char>(record.type));
        write_le32(temp_record, key_len);
        temp_record.insert(temp_record.end(), record.key.begin(), record.key.end());
        write_le32(temp_record, value_len);
        temp_record.insert(temp_record.end(), record.value.begin(), record.value.end());

        uint32_t crc = compute_crc32(temp_record.data(), temp_record.size());

        buffer_.insert(buffer_.end(), temp_record.begin(), temp_record.end());
        write_le32(buffer_, crc);

        return core::Status::Ok();
    }

    auto WalWriter::flush_buffer() -> core::Status {
        if (buffer_.empty()) {
            return core::Status::Ok();
        }

        size_t total_written = 0;
        const size_t total_size = buffer_.size();

        while (total_written < total_size) {
            const size_t remaining = total_size - total_written;
            ssize_t n = ::write(fd_, buffer_.data() + total_written, remaining);

            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return core::Status::IOError(
                    fmt::format("Failed to write to WAL: {}", strerror(errno)));
            }
            if (n == 0) {
                return core::Status::IOError("Short write to WAL (wrote 0 bytes)");
            }

            total_written += static_cast<size_t>(n);
        }

        buffer_.clear();
        return core::Status::Ok();
    }

    auto WalWriter::flush() -> core::Status {
        return flush_buffer();
    }

    auto WalWriter::sync() -> core::Status {
        auto status = flush();
        if (!status.ok())
            return status;

        if (fd_ < 0) {
            return core::Status::IOError("WAL file not open");
        }

        if (fsync(fd_) != 0) {
            return core::Status::IOError(fmt::format("fsync failed: {}", strerror(errno)));
        }

        return core::Status::Ok();
    }

    WalReader::WalReader(const std::string &wal_path)
        : wal_path_(wal_path), fd_(-1), buffer_pos_(0), buffer_size_(0) {
        read_buffer_.resize(READ_BUFFER_SIZE);

        fd_ = open(wal_path_.c_str(), O_RDONLY);

        if (fd_ < 0) {
            LOG_INFO("WAL file not found (fresh start): {}", wal_path_);
        } else {
            LOG_INFO("WAL reader opened: {} (fd={})", wal_path_, fd_);
        }
    }

    WalReader::~WalReader() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    static auto read_le32(const char *data) -> uint32_t {
        return static_cast<uint32_t>(static_cast<unsigned char>(data[0])) |
               (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 8) |
               (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(data[3])) << 24);
    }

    auto WalReader::fill_buffer() -> core::Status {
        ssize_t n;

        do {
            n = read(fd_, read_buffer_.data(), READ_BUFFER_SIZE);
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            return core::Status::IOError(fmt::format("Failed to read WAL: {}", strerror(errno)));
        }

        if (n == 0) {
            return core::Status::NotFound("End of WAL");
        }

        buffer_size_ = static_cast<size_t>(n);
        buffer_pos_ = 0;
        return core::Status::Ok();
    }

    auto WalReader::read_bytes(char *dest, size_t n) -> core::Status {
        size_t total_read = 0;
        while (total_read < n) {
            if (buffer_pos_ >= buffer_size_) {
                auto status = fill_buffer();
                if (!status.ok()) {
                    if (status.is_not_found() && total_read > 0) {
                        return core::Status::Corruption("Partial record at end of WAL");
                    }
                    return status;
                }
            }

            size_t available = buffer_size_ - buffer_pos_;
            size_t to_copy = std::min(available, n - total_read);

            memcpy(dest + total_read, read_buffer_.data() + buffer_pos_, to_copy);
            buffer_pos_ += to_copy;
            total_read += to_copy;
        }

        return core::Status::Ok();
    }

    auto WalReader::read_next(WalRecord &record) -> core::Status {
        if (fd_ < 0) {
            return core::Status::NotFound("WAL file not open");
        }

        std::vector<char> record_data;

        char type_byte;
        auto status = read_bytes(&type_byte, 1);
        if (!status.ok())
            return status;

        if (type_byte < 1 || type_byte > 4) {
            return core::Status::Corruption(
                fmt::format("Invalid WAL record type: {}", static_cast<int>(type_byte)));
        }

        record.type = static_cast<WalRecordType>(type_byte);
        record_data.push_back(type_byte);

        char len_buf[4];
        status = read_bytes(len_buf, 4);
        if (!status.ok()) {
            return core::Status::Corruption("Failed to read key length");
        }
        uint32_t key_len = read_le32(len_buf);
        record_data.insert(record_data.end(), len_buf, len_buf + 4);

        if (key_len > core::MAX_KEY_SIZE) {
            return core::Status::Corruption("Key length exceeds maximum");
        }

        record.key.resize(key_len);
        if (key_len > 0) {
            status = read_bytes(&record.key[0], key_len);
            if (!status.ok()) {
                return core::Status::Corruption("Failed to read key data");
            }
            record_data.insert(record_data.end(), record.key.begin(), record.key.end());
        }

        status = read_bytes(len_buf, 4);
        if (!status.ok()) {
            return core::Status::Corruption("Failed to read value length");
        }
        uint32_t value_len = read_le32(len_buf);
        record_data.insert(record_data.end(), len_buf, len_buf + 4);

        if (value_len > core::MAX_VALUE_SIZE) {
            return core::Status::Corruption("Value length exceeds maximum");
        }

        record.value.resize(value_len);
        if (value_len > 0) {
            status = read_bytes(&record.value[0], value_len);
            if (!status.ok()) {
                return core::Status::Corruption("Failed to read value data");
            }
            record_data.insert(record_data.end(), record.value.begin(), record.value.end());
        }

        status = read_bytes(len_buf, 4);
        if (!status.ok()) {
            return core::Status::Corruption("Failed to read CRC32");
        }
        uint32_t stored_crc = read_le32(len_buf);

        uint32_t computed_crc = compute_crc32(record_data.data(), record_data.size());
        if (stored_crc != computed_crc) {
            return core::Status::Corruption(
                fmt::format("CRC mismatch in WAL record (stored: {:#x}, computed: {:#x})",
                            stored_crc, computed_crc));
        }

        return core::Status::Ok();
    }

    auto WalReader::has_more() const -> bool {
        if (buffer_pos_ < buffer_size_) {
            return true;
        }
        return fd_ >= 0;
    }

} // namespace embrace::storage
