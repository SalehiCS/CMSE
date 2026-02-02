#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace cmse {

    // --- Type Definitions ---
    using page_id_t = int32_t;
    using frame_id_t = int32_t;
    using version_t = int32_t;

    // KeyType: Realtime timestamp in microseconds (int64)
    // Matches _SOURCE_REALTIME_TIMESTAMP from logs
    using KeyType = int64_t;

    // ValueType: For Phase 2, we store RecordID or Offset. 
    using ValueType = int64_t;

    // Constants
    constexpr page_id_t INVALID_PAGE_ID = -1;
    constexpr int PAGE_SIZE = 4096;
    constexpr version_t INVALID_VERSION = -1;

    // --- Log Record Structure ---
    // Mapped according to Project Guide (Section 3.7)
    struct LogRecord {
        // 1. Timestamp (Key)
        // Mapped from: _SOURCE_REALTIME_TIMESTAMP
        int64_t timestamp;

        // 2. Severity/Priority
        // Mapped from: PRIORITY
        int32_t priority;

        // 3. Process ID
        // Mapped from: _PID
        int32_t pid;

        // 4. Source/Service Name
        // Mapped from: SYSLOG_IDENTIFIER or _COMM
        // Fixed size: 32 chars is standard for service names
        char source[32];

        // 5. Hostname
        // Mapped from: _HOSTNAME or _MACHINE_ID
        char host[32];

        // 6. Message Payload
        // Mapped from: MESSAGE
        // Truncated to 200 chars to keep Page size manageable in B+Tree nodes
        char message[200];

        // Helper to visualize (Debugging)
        std::string toString() const {
            return "TS:" + std::to_string(timestamp) +
                " | PRI:" + std::to_string(priority) +
                " | PID:" + std::to_string(pid) +
                " | SRC:" + std::string(source) +
                " | HOST:" + std::string(host) +
                " | MSG:" + std::string(message);
        }
    };

} // namespace cmse