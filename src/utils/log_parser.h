#pragma once
#include "../common/types.h"
#include <vector>
#include <string>
#include <fstream>

namespace cmse::utils {

    /**
     * LogParser
     * Handles the ingestion of raw text logs into structured binary LogRecords.
     * Maintains precise byte offsets to support Crash Recovery and Resume.
     */
    class LogParser {
    public:
        // Initialize parser with a specific file path.
        explicit LogParser(const std::string& filename);

        // Clean up file handles.
        ~LogParser();

        // Fetches a specific number of records (batch) from the file.
        // Returns false if EOF is reached.
        bool GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records = 100000);

        // Returns the total size of the file (for progress bars).
        size_t GetTotalFileSize() const;

        /**
         * GetCurrentPosition
         * CRITICAL FIX: Returns the byte offset of the *next* record to be fully processed.
         * If we are mid-parse, this returns the start of the current pending record,
         * ensuring that a Resume operation re-reads the header correctly.
         */
        size_t GetCurrentPosition();

        // Seeks the file pointer to a specific byte (for Resume).
        void SeekToPosition(size_t offset);

        // Legacy helper for one-shot loading.
        static std::vector<LogRecord> parseLogFile(const std::string& filename);

    private:
        std::ifstream infile_;             // Handle to the source file.
        std::string filename_;             // Path to the file.
        size_t file_size_ = 0;             // Total bytes in file.

        // --- State Tracking ---
        LogRecord current_record_;         // Buffer for the record being built.
        bool inside_record_ = false;       // Flag: Are we currently parsing a multiline entry?

        // FIX: Tracks the byte offset where 'current_record_' started.
        size_t last_record_offset_ = 0;

        // --- Internal Helpers ---
        static std::string trim(const std::string& str);
        static void parseMetadataLine(const std::string& line, LogRecord& current_record);
        void resetRecord(LogRecord& r);
    };

} // namespace cmse::utils