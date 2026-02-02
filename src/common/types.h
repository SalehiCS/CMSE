#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace cmse {

    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = int32_t;

    // Key: Timestamp (8 bytes)
    using KeyType = int64_t;

    // Constants
    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr int PAGE_SIZE = 4096;
    constexpr version_t INVALID_VERSION = -1;

    // --- Log Record Structure ---
    struct LogRecord {
        int64_t timestamp;
        int32_t priority;
        int32_t pid;
        char source[32];
        char host[32];
        char message[200];

        std::string toString() const {
            return "TS:" + std::to_string(timestamp) + " | MSG:" + std::string(message);
        }
    };

    // ValueType is now the Record itself (Clustered Index)
    using ValueType = LogRecord;

} // namespace cmse