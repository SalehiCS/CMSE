#pragma once
#include "../common/types.h"
#include <vector>
#include <string>
#include <fstream>

namespace cmse::utils {

    /**
     * LogParser
     * Responsible for the heavy lifting of ingestion. It transforms human-readable
     * log files into the engine's internal LogRecord binary format.
     * Features: Batch processing, file seeking, and progress tracking.
     */
    class LogParser {
    public:
        /**
         * Constructor: Initializes the ingestion stream.
         * @param filename Path to the raw .log or .txt file.
         */
        explicit LogParser(const std::string& filename);

        /**
         * Destructor: Ensures the input file stream is safely closed.
         */
        ~LogParser();

        /**
         * GetNextBatch
         * Performance-oriented method to read logs in chunks rather than loading
         * the entire file into memory at once.
         * @param out_records Vector to be populated with parsed LogRecords.
         * @param max_records Maximum number of records to ingest in this specific call.
         * @return false if the end of the file (EOF) is reached; true if more data remains.
         */
        bool GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records = 100000);

        /**
         * GetTotalFileSize
         * Calculates the total size of the source file in bytes. Useful for calculating
         * percentage-based progress during large ingestions.
         */
        size_t GetTotalFileSize() const;

        /**
         * GetCurrentPosition
         * Returns the current byte offset within the file stream.
         */
        size_t GetCurrentPosition();

        /**
         * parseLogFile
         * Legacy static utility for small-scale testing where batching is not required.
         * @param filename Path to the file to be parsed.
         * @return A vector containing all records from the file.
         */
        static std::vector<LogRecord> parseLogFile(const std::string& filename);

        /**
         * SeekToPosition
         * Instantly jumps the file pointer to a specific byte offset. Essential for
         * resuming interrupted ingestions or parallelizing file reading.
         * @param offset The byte position from the beginning of the file.
         */
        void SeekToPosition(size_t offset) {
            if (infile_.is_open()) {
                // Reset stream state (essential if previous operations hit EOF or errors)
                infile_.clear();
                // Perform the absolute seek from the beginning of the file
                infile_.seekg(offset, std::ios::beg);
            }
        }

    private:
        std::ifstream infile_;             // The active file input stream handle
        std::string filename_;            // Storage for the source file path
        size_t file_size_ = 0;            // Cached total file size in bytes

        // Internal temporary state to handle multi-line log parsing logic
        LogRecord current_record_;        // Current record being assembled
        bool inside_record_ = false;      // State flag for multi-line parsing transitions

        // --- INTERNAL UTILITIES ---

        /**
         * trim
         * Removes whitespace from the beginning and end of extracted log strings.
         */
        static std::string trim(const std::string& str);

        /**
         * parseMetadataLine
         * Extracts specific fields (Timestamp, Host, Level) from a single line of text.
         */
        static void parseMetadataLine(const std::string& line, LogRecord& current_record);

        /**
         * resetRecord
         * Zeroes out a LogRecord struct to ensure no data leaks between different entries.
         */
        void resetRecord(LogRecord& r);
    };

} // namespace cmse::utils