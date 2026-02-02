#pragma once
#include "../common/types.h"
#include <vector>
#include <string>
#include <fstream>

namespace cmse::utils {

    /**
     * LogParser
     * Responsible for parsing raw system logs (systemd-journal style).
     * Follows the "Line-by-line" reading requirement[cite: 16].
     */
    class LogParser {
    public:
        /**
         * Parses a log file and returns a vector of structured LogRecords.
         * NOTE: In a production scenario (Phase 4), this should likely return
         * an iterator or batch to avoid loading all 100k records into RAM at once.
         * For Phase 2 testing, a vector is acceptable.
         */
        static std::vector<LogRecord> parseLogFile(const std::string& filename);

    private:
        // Helper to clean up strings (remove whitespace/quotes)
        static std::string trim(const std::string& str);

        // Helper to parse a single Key=Value line
        static void parseMetadataLine(const std::string& line, LogRecord& current_record);
    };

} // namespace cmse::utils