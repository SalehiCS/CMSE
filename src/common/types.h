#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <limits> 
#include <cstring> // Required for strncpy_s

/**
 * CMSE (Centralized Management System Engine) Namespace
 * Contains core primitive definitions and the fundamental data record structure.
 */
namespace cmse {

    // --- ALIASES FOR SYSTEM IDENTIFIERS ---

    /** Identifier for physical pages on disk/buffer pool; signed 32-bit allows for negative invalid sentinels. */
    using page_id_t = int32_t;

    /** Identifier for memory slots within the Buffer Pool Manager. */
    using frame_id_t = int32_t;

    /** Identifier for Multi-Version Concurrency Control (MVCC) visibility tracking. */
    using version_id_t = int32_t;

    /** * KeyType: The primary search key for the B+Tree.
     * Represented as an 8-byte integer (Unix Timestamp in Microseconds).
     */
    using KeyType = int64_t;

    // --- SYSTEM-WIDE CONSTANTS ---

    /** Sentinel value used to indicate a null or uninitialized page reference. */
    constexpr page_id_t INVALID_PAGE_ID = -1;

    /** Physical size of a single database page in bytes; matches standard OS page sizes. */
    constexpr int PAGE_SIZE = 4096;

    /** Sentinel value indicating that no specific version is associated with a record. */
    constexpr version_id_t INVALID_VERSION = -1;

    // --- LOG RECORD STRUCTURE (Physical Layout) ---

    /**
     * LogRecord: The fundamental unit of data storage.
     * Designed for a Clustered Index where the value is the record itself.
     */
    struct LogRecord {
        // [Offset 0] Primary Key: Time of event in microseconds
        int64_t timestamp;

        // [Offset 8] Metadata: Importance level of the log
        int32_t priority;

        // [Offset 12] Metadata: Process ID originating the log
        int32_t pid;

        // [Offset 16] Fixed-size buffer for log source (e.g., Service Name)
        char source[32];

        // [Offset 48] Fixed-size buffer for origin host (e.g., IP or Hostname)
        char host[32];

        // [Offset 80] Fixed-size buffer for the actual log payload
        char message[200];

        /**
         * Safely populates the struct members.
         * Uses strncpy_s to prevent buffer overflows and ensure null-termination.
         */
        void Set(int64_t ts, int32_t prio, const char* src, const char* hst, const char* msg) {
            timestamp = ts;                   // Assign 8-byte key
            priority = prio;                  // Assign 4-byte priority
            pid = 0;                          // Default initialization for PID
            strncpy_s(source, src, _TRUNCATE); // Safe copy to 32-byte source buffer
            strncpy_s(host, hst, _TRUNCATE);   // Safe copy to 32-byte host buffer
            strncpy_s(message, msg, _TRUNCATE);// Safe copy to 200-byte message buffer
        }

        /**
         * Generates a human-readable representation for CLI/Debugging purposes.
         */
        std::string toString() const {
            // Concatenates timestamp and message for quick log identification
            return "TS:" + std::to_string(timestamp) + " | MSG:" + std::string(message);
        }
    };

    /** * ValueType: Mapping for the Index.
     * Since this is a clustered index, the ValueType is equivalent to the full LogRecord.
     */
    using ValueType = LogRecord;


    // --- QUERY STRUCTURE (Logical Filter) ---

    /**
     * Query: Represents a set of filters applied to the log database.
     * Used to pass search criteria from the CLI to the Execution Engine.
     */
    struct Query {
        // Range Filter: Start time (Default: 0 / Beginning of time)
        int64_t min_timestamp = 0;

        // Range Filter: End time (Default: Max Int / End of time)
        int64_t max_timestamp = std::numeric_limits<int64_t>::max();

        // Equality Filter: Log Priority (-1 indicates "Any Priority")
        int32_t priority = -1;

        // String Filter: Source/Service Name (Supports '*' suffix for prefix matching)
        std::string source;

        // String Filter: Hostname (Supports '*' suffix for prefix matching)
        std::string host;

        // Substring Filter: Message body must contain this string
        std::string message_contains;
    };

} // namespace cmse