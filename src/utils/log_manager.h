#pragma once
#include "../common/types.h"
#include <vector>
#include <string>

namespace cmse::utils {

    /**
     * LogManager
     * Handles generation of synthetic logs and parsing of log files.
     * This acts as the "Ingestion Layer" mentioned in the architecture docs.
     */
    class LogManager {
    public:
        // Generates 'count' synthetic log records.
        // start_resource_id: Starting ID for resources (increments sequentially).
        // time_step_ms: Time difference between logs in milliseconds.
        static std::vector<LogRecord> generateSyntheticLogs(int count, int64_t start_resource_id = 1000, int time_step_ms = 100);

        // Writes logs to a file (simulating the raw log file on disk).
        // Format: timestamp_ticks,resource_id,resource_name,event_type
        static void writeLogsToFile(const std::vector<LogRecord>& logs, const std::string& filename);

        // Reads logs from a file (to be fed into the Indexing Layer).
        static std::vector<LogRecord> readLogsFromFile(const std::string& filename);

    private:
        // Helper to parse a single CSV line into a LogRecord
        static LogRecord parseLine(const std::string& line);
    };

} // namespace cmse::utils