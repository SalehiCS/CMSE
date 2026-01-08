#pragma once
#include <cstdint>
#include <string>
#include <chrono> // Added for std::chrono

namespace cmse {

    // --- Type Definitions ---
    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = int32_t;

    // Using standard chrono for precise time management
    using timestamp_t = std::chrono::system_clock::time_point;

    // KeyType is int64_t to support both ResourceID and Timestamp (converted to ticks)
    using KeyType = int64_t;
    using ValueType = int64_t;    // Usually RecordID or offset (RID)

    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr int PAGE_SIZE = 4096; // 4KB Page Size

    // --- Log Record Structure (Dataset) ---
    // Represents a single line from the log file.
    struct LogRecord {
        timestamp_t timestamp; [cite_start]// High-precision timestamp [cite: 112]
            int64_t resource_id; [cite_start]// Numeric ID for the resource [cite: 112]
            char resource_name[64]; [cite_start]// String name (e.g., "vm-prod-01") [cite: 113]
            char event_type[16]; [cite_start]// E.g., "START", "STOP", "ERROR" [cite: 113]

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