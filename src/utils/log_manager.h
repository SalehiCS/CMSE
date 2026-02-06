#pragma once
#include "../common/types.h"
#include <vector>
#include <string>

namespace cmse::utils {

    /**
     * LogManager
     * A utility class for high-level log lifecycle management.
     * Primary roles:
     * 1. Synthetic Data Generation: Creating massive datasets for stress-testing.
     * 2. Basic Persistence: Fast serialization of records to disk.
     */
    class LogManager {
    public:
        /**
         * generateSyntheticLogs
         * Procedurally generates a specified number of LogRecords with randomized but realistic data.
         * @param count Number of records to create.
         * @param start_id Baseline ID for sequencing (if applicable).
         * @param time_range_ms The temporal spread of the logs in milliseconds.
         */
        static std::vector<LogRecord> generateSyntheticLogs(int count = 10000, int64_t start_id = 1000, int time_range_ms = 3600000);

        /**
         * writeLogsToFile
         * Serializes a vector of LogRecords into a text file using the LogRecord::toString() format.
         */
        static void writeLogsToFile(const std::vector<LogRecord>& logs, const std::string& filename);

        /**
         * readLogsFromFile
         * A simplified reader for compatibility. Note: Use LogParser for complex/real-world log files.
         */
        static std::vector<LogRecord> readLogsFromFile(const std::string& filename);

    private:
        /**
         * parseLine
         * Internal helper to reconstruct a LogRecord from the specific string format used by writeLogsToFile.
         */
        static LogRecord parseLine(const std::string& line);
    };

} // namespace cmse::utils