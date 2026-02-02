#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace cmse {

    // --- Type Definitions ---
    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = int32_t;

    // KeyType: We use the raw realtime timestamp (microseconds) from the log
    // Example: 1766691147575408
    using KeyType = int64_t;

    // ValueType: In a clustered index, this could be the Record itself, 
    // or a RecordID (PageID + Slot) pointing to a HeapFile. 
    // For Phase 2 simplicity, let's store the RecordID (int64 packed).
    using ValueType = int64_t;

    // Constants
    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr int PAGE_SIZE = 4096;
    constexpr version_t INVALID_VERSION = -1;

    // --- Log Record Structure (Based on Project Guide & Real Logs) ---
    struct LogRecord {
        // 1. Timestamp [cite: 113]
        // Mapped from: _SOURCE_REALTIME_TIMESTAMP
        int64_t timestamp;

        // 2. Severity/Priority 
        // Mapped from: PRIORITY (e.g., 3, 4)
        int32_t priority;

        // 3. Process ID [cite: 118]
        // Mapped from: _PID
        int32_t pid;

        // 4. Source/Service Name 
        // Mapped from: SYSLOG_IDENTIFIER or _COMM
        // Fixed size for storage efficiency (approx 32 chars is usually safe)
        char source[32];

        // 5. Hostname 
        // Mapped from: _HOSTNAME
        char host[32];

        // 6. Message Payload 
        // Mapped from: MESSAGE
        // Truncated to fit in fixed-size pages for Phase 2. 
        // In a real DB, this would go to a separate overflow page/heap file.
        char message[200];

        // Helper to visualize (Debugging)
        std::string toString() const {
            return "TS:" + std::to_string(timestamp) +
                " | PRI:" + std::to_string(priority) +
                " | SRC:" + std::string(source) +
                " | MSG:" + std::string(message);
        }
    };

} // namespace cmse