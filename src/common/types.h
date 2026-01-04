#pragma once
#include <cstdint>
#include <chrono>
#include <string>

namespace cmse {

    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = uint64_t;
    using timestamp_t = std::chrono::system_clock::time_point;

    // Constants
    constexpr int PAGE_SIZE = 4096;
    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr version_t INVALID_VERSION = 0;

    // Common types for keys and values to avoid template complexity across modules
    using KeyType = std::string;
    using ValueType = std::string;

    // Structure to hold version metadata
    struct VersionInfo {
        version_t version;
        page_id_t root_page_id;
        timestamp_t committed_at;
    };

} // namespace cmse