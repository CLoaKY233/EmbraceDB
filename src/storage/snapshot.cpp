#include "storage/snapshot.hpp"
#include "core/common.hpp"
#include "core/status.hpp"
#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include "storage/checksum.hpp"
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace embrace::storage {

    static auto write_le32_to_fd(int fd, uint32_t val) -> core::Status {
        char buf[4];
        buf[0] = static_cast<char>(val & 0xFF);
        buf[1] = static_cast<char>((val >> 8) & 0xFF);
        buf[2] = static_cast<char>((val >> 16) & 0xFF);
        buf[3] = static_cast<char>((val >> 24) & 0xFF);

        ssize_t written = ::write(fd, buf, 4);
        if (written != 4) {
            return core::Status::IOError(
                fmt::format("Failed to write uint32: {}", strerror(errno)));
        }
        return core::Status::Ok();
    }

    static auto read_le32_from_fd(int fd) -> std::pair<core::Status, uint32_t> {
        char buf[4];
        ssize_t n = ::read(fd, buf, 4);
        if (n != 4) {
            return {core::Status::IOError("Failed to read uint32"), 0};
        }
        uint32_t val = static_cast<uint32_t>(static_cast<unsigned char>(buf[0])) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 8) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 16) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(buf[3])) << 24);
        return {core::Status::Ok(), val};
    }

    static auto write_string_to_fd(int fd, const std::string &str) -> core::Status {
        auto status = write_le32_to_fd(fd, static_cast<uint32_t>(str.size()));
        if (!status.ok()) {
            return status;
        }

        if (!str.empty()) {
            ssize_t written = ::write(fd, str.data(), str.size());
            if (written != static_cast<ssize_t>(str.size())) {
                return core::Status::IOError("Failed to write string data");
            }
        }
        return core::Status::Ok();
    }

    static auto read_string_from_fd(int fd) -> std::pair<core::Status, std::string> {
        auto [len_status, len] = read_le32_from_fd(fd);
        if (!len_status.ok()) {
            return {len_status, ""};
        }

        if (len > core::MAX_KEY_SIZE * 10) {
            return {core::Status::Corruption("String length too large"), ""};
        }

        std::string result(len, '\0');

        if (len > 0) {
            ssize_t n = ::read(fd, &result[0], len);
            if (n != static_cast<ssize_t>(len)) {
                return {core::Status::IOError("Failed to read string data"), ""};
            }
        }
        return {core::Status::Ok(), result};
    }

    Snapshotter::Snapshotter(const std::string &snapshot_path) : snapshot_path_(snapshot_path) {}

    auto Snapshotter::exists() const -> bool {
        struct stat buffer;
        return (stat(snapshot_path_.c_str(), &buffer) == 0);
    }

    auto Snapshotter::create_snapshot(const indexing::Btree &tree) -> core::Status {
        std::string temp_path = snapshot_path_ + ".tmp";

        const auto snapshot_start = std::chrono::steady_clock::now();
        int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (fd < 0) {
            return core::Status::IOError(
                fmt::format("Failed to create snapshot temp file: {}", strerror(errno)));
        }
        FileHandle file(fd);

        // Count entries first
        size_t entry_count = 0;
        tree.iterate_all([&](const core::Key &, const core::Value &) { entry_count++; });

        // Write header
        auto status = write_le32_to_fd(fd, SNAPSHOT_MAGIC);
        if (!status.ok())
            return status;

        status = write_le32_to_fd(fd, SNAPSHOT_VERSION);
        if (!status.ok())
            return status;

        status = write_le32_to_fd(fd, static_cast<uint32_t>(entry_count));
        if (!status.ok())
            return status;

        char header_data[12];
        for (int i = 0; i < 4; i++) {
            header_data[i] = static_cast<char>((SNAPSHOT_MAGIC >> (i * 8)) & 0xFF);
            header_data[i + 4] = static_cast<char>((SNAPSHOT_VERSION >> (i * 8)) & 0xFF);
            header_data[i + 8] = static_cast<char>((entry_count >> (i * 8)) & 0xFF);
        }
        uint32_t header_crc = compute_crc32(header_data, 12);
        status = write_le32_to_fd(fd, header_crc);
        if (!status.ok())
            return status;

        core::Status write_status = core::Status::Ok();
        tree.iterate_all([&](const core::Key &key, const core::Value &value) {
            if (!write_status.ok())
                return;

            std::vector<char> entry_data;
            entry_data.reserve(8 + key.size() + value.size());

            for (int i = 0; i < 4; i++) {
                entry_data.push_back(static_cast<char>((key.size() >> (i * 8)) & 0xFF));
            }

            entry_data.insert(entry_data.end(), key.begin(), key.end());

            for (int i = 0; i < 4; i++) {
                entry_data.push_back(static_cast<char>((value.size() >> (i * 8)) & 0xFF));
            }

            entry_data.insert(entry_data.end(), value.begin(), value.end());

            uint32_t entry_crc = compute_crc32(entry_data.data(), entry_data.size());

            write_status = write_string_to_fd(fd, key);
            if (!write_status.ok())
                return;

            write_status = write_string_to_fd(fd, value);
            if (!write_status.ok())
                return;

            write_status = write_le32_to_fd(fd, entry_crc);
        });

        if (!write_status.ok()) {
            ::unlink(temp_path.c_str());
            return write_status;
        }

        if (::fsync(fd) != 0) {
            ::unlink(temp_path.c_str());
            return core::Status::IOError(
                fmt::format("Failed to sync snapshot: {}", strerror(errno)));
        }

        if (::rename(temp_path.c_str(), snapshot_path_.c_str()) != 0) {
            ::unlink(temp_path.c_str());
            return core::Status::IOError(
                fmt::format("Failed to rename snapshot: {}", strerror(errno)));
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - snapshot_start)
                                    .count();

        LOG_INFO("Snapshot created successfully: path='{}', entries={}, elapsed_ms={}",
                 snapshot_path_, entry_count, elapsed_ms);
        return core::Status::Ok();
    }

    auto Snapshotter::validate_snapshot_header(int fd) -> core::Status {
        auto [magic_status, magic] = read_le32_from_fd(fd);
        if (!magic_status.ok())
            return magic_status;

        if (magic != SNAPSHOT_MAGIC) {
            return core::Status::Corruption(fmt::format("Invalid snapshot magic: {:#x}", magic));
        }

        auto [ver_status, version] = read_le32_from_fd(fd);
        if (!ver_status.ok())
            return ver_status;

        if (version != SNAPSHOT_VERSION) {
            return core::Status::Corruption(
                fmt::format("Unsupported snapshot version: {}", version));
        }

        return core::Status::Ok();
    }

    auto Snapshotter::load_snapshot(indexing::Btree &tree) -> core::Status {
        if (!exists()) {
            LOG_DEBUG("Snapshot not found; skipping load for path='{}'", snapshot_path_);
            return core::Status::Ok();
        }

        const auto load_start = std::chrono::steady_clock::now();
        int fd = ::open(snapshot_path_.c_str(), O_RDONLY);
        if (fd < 0) {
            return core::Status::IOError(
                fmt::format("Failed to open snapshot: {}", strerror(errno)));
        }
        FileHandle file(fd);

        auto status = validate_snapshot_header(fd);
        if (!status.ok())
            return status;

        auto [count_status, entry_count] = read_le32_from_fd(fd);
        if (!count_status.ok())
            return count_status;

        // Read header CRC
        auto [crc_status, stored_header_crc] = read_le32_from_fd(fd);
        if (!crc_status.ok())
            return crc_status;

        char header_data[12];
        for (int i = 0; i < 4; i++) {
            header_data[i] = static_cast<char>((SNAPSHOT_MAGIC >> (i * 8)) & 0xFF);
            header_data[i + 4] = static_cast<char>((SNAPSHOT_VERSION >> (i * 8)) & 0xFF);
            header_data[i + 8] = static_cast<char>((entry_count >> (i * 8)) & 0xFF);
        }

        uint32_t computed_header_crc = compute_crc32(header_data, 12);
        if (stored_header_crc != computed_header_crc) {
            return core::Status::Corruption("Snapshot header CRC mismatch");
        }

        for (uint32_t i = 0; i < entry_count; i++) {
            auto [key_status, key] = read_string_from_fd(fd);
            if (!key_status.ok()) {
                return core::Status::Corruption(fmt::format("Failed to read key at entry {}", i));
            }

            auto [val_status, value] = read_string_from_fd(fd);
            if (!val_status.ok()) {
                return core::Status::Corruption(fmt::format("Failed to read value at entry {}", i));
            }

            auto [entry_crc_status, stored_entry_crc] = read_le32_from_fd(fd);
            if (!entry_crc_status.ok()) {
                return core::Status::Corruption(
                    fmt::format("Failed to read entry CRC at entry {}", i));
            }

            std::vector<char> entry_data;
            entry_data.reserve(8 + key.size() + value.size());

            for (int j = 0; j < 4; j++) {
                entry_data.push_back(static_cast<char>((key.size() >> (j * 8)) & 0xFF));
            }
            entry_data.insert(entry_data.end(), key.begin(), key.end());

            for (int j = 0; j < 4; j++) {
                entry_data.push_back(static_cast<char>((value.size() >> (j * 8)) & 0xFF));
            }
            entry_data.insert(entry_data.end(), value.begin(), value.end());

            uint32_t computed_entry_crc = compute_crc32(entry_data.data(), entry_data.size());

            if (stored_entry_crc != computed_entry_crc) {
                return core::Status::Corruption(fmt::format("Entry CRC mismatch at entry {}", i));
            }

            auto put_status = tree.put(key, value);
            if (!put_status.ok()) {
                return put_status;
            }
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - load_start)
                                    .count();

        LOG_INFO("Snapshot loaded successfully: path='{}', entries={}, elapsed_ms={}",
                 snapshot_path_, entry_count, elapsed_ms);
        return core::Status::Ok();
    }
} // namespace embrace::storage
