#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace cmse {

    // --- Type Definitions ---
    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = int32_t;

    // Using standard chrono for precise time management
    using timestamp_t = std::chrono::system_clock::time_point;

    // KeyType is int64_t to support both ResourceID and Timestamp
    using KeyType = int64_t;
    using ValueType = int64_t;    // Usually RecordID or offset (RID)

    // Constants
    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr int PAGE_SIZE = 4096; // 4KB Page Size
    constexpr version_t INVALID_VERSION = -1;

    // --- Log Record Structure (Dataset) ---
    struct LogRecord {
        timestamp_t timestamp;    // High-precision timestamp
        int64_t resource_id;      // Numeric ID for the resource
        char resource_name[64];   // String name (e.g., "vm-prod-01")
        char event_type[16];      // E.g., "START", "STOP", "ERROR"

        // Helper to convert to string (CSV format)
        std::string toString() const {
            auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                timestamp.time_since_epoch()).count();

            return std::to_string(ticks) + "," +
                std::to_string(resource_id) + "," +
                std::string(resource_name) + "," +
                std::string(event_type);
        }
    };

} // namespace cmse