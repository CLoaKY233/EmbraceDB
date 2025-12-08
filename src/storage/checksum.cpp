#include "storage/checksum.hpp"
#include <array>

namespace embrace::storage {

    static constexpr auto generate_crc32_table() -> std::array<uint32_t, 256> {
        std::array<uint32_t, 256> table{};

        constexpr uint32_t polynomial = 0xEDB88320;

        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
            }
            table[i] = crc;
        }
        return table;
    }

    static constexpr auto CRC32_TABLE = generate_crc32_table();

    auto compute_crc32(const void *data, size_t len) -> uint32_t {
        const auto *bytes = static_cast<const uint8_t *>(data);

        uint32_t crc = 0xFFFFFFFF;

        for (size_t i = 0; i < len; i++) {
            crc = CRC32_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }
} // namespace embrace::storage
