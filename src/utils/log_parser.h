#pragma once
#include "../common/types.h"
#include <vector>
#include <string>
#include <fstream>

namespace cmse::utils {

    class LogParser {
    public:
        // Constructor opens the file
        explicit LogParser(const std::string& filename);
        ~LogParser();

        // New Method: Reads the next batch of records (e.g., up to 'limit' records or bytes)
        // Returns false if EOF is reached.
        bool GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records = 100000);

        // Helper to get total size (for progress bar)
        size_t GetTotalFileSize() const;
        size_t GetCurrentPosition();

        // Keep the old static method for compatibility (optional, but good for small tests)
        static std::vector<LogRecord> parseLogFile(const std::string& filename);

    private:
        std::ifstream infile_;
        std::string filename_;
        size_t file_size_ = 0;

        // Internal state for parsing
        LogRecord current_record_;
        bool inside_record_ = false;

        // Helpers
        static std::string trim(const std::string& str);
        static void parseMetadataLine(const std::string& line, LogRecord& current_record);
        void resetRecord(LogRecord& r);
    };

} // namespace cmse::utils