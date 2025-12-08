#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace embrace::storage {
    // CRC32 using standard polynomial 0x04C11DB7 (IEEE 802.3)
    auto compute_crc32(const void *data, size_t len) -> uint32_t;

    inline auto compute_crc32(const std::string &str) -> uint32_t {
        return compute_crc32(str.data(), str.size());
    }
} // namespace embrace::storage
