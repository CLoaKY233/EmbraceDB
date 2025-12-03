#pragma once

#include <cstdint>
#include <string>

namespace embrace::core {

    // Constants
    constexpr uint32_t PAGE_SIZE = 4096;
    constexpr uint32_t MAX_KEY_SIZE = 128;
    constexpr uint32_t MAX_VALUE_SIZE = 1024;

    using PageId = uint32_t;
    using TransactionId = uint64_t;

    // TODO : move to raw btye views (std::span) for performance.
    using Key = std::string;
    using Value = std::string;

} // namespace embrace::core
