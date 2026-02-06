#include "../utils/log_parser.h"
#include "../common/logger.h" 
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace cmse::utils {

    // ---------------------------------------------------------------------------------------------------------
    // Constructor: LogParser
    // Initializes the file stream in Binary Mode to ensure byte-perfect seek operations.
    // ---------------------------------------------------------------------------------------------------------
    LogParser::LogParser(const std::string& filename) : filename_(filename) {
        // Open the file using the binary flag to prevent newline translation (CRLF -> LF).
        // This is critical for tellg() to return physical byte offsets matching the disk.
        infile_.open(filename, std::ios::in | std::ios::binary);

        // Check if the file successfully opened.
        if (infile_.is_open()) {
            // Move the file pointer to the very end of the file.
            infile_.seekg(0, std::ios::end);

            // Record the current position (which is now the file size) in bytes.
            file_size_ = infile_.tellg();

            // Move the file pointer back to the beginning to prepare for reading.
            infile_.seekg(0, std::ios::beg);
        }

        // Initialize the temporary record buffer to a clean, empty state.
        resetRecord(current_record_);

        // Initialize the tracking offset to 0 since we haven't processed anything yet.
        last_record_offset_ = 0;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Destructor: ~LogParser
    // Ensures system resources are released when the parser goes out of scope.
    // ---------------------------------------------------------------------------------------------------------
    LogParser::~LogParser() {
        // Check if the file stream is currently open.
        if (infile_.is_open()) {
            // Close the file stream explicitly.
            infile_.close();
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: GetTotalFileSize
    // Simple accessor to retrieve the total size of the log file.
    // ---------------------------------------------------------------------------------------------------------
    size_t LogParser::GetTotalFileSize() const {
        // Return the cached file size calculated during construction.
        return file_size_;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: GetCurrentPosition
    // Returns the safe "Resume Point" for persistence.
    // ---------------------------------------------------------------------------------------------------------
    size_t LogParser::GetCurrentPosition() {
        // If we are currently in the middle of parsing a record, return its start offset.
        // This ensures that if we save state now, we resume from the *header* of this record.
        if (inside_record_) {
            return last_record_offset_;
        }

        // If we are not inside a record (e.g., between records), return the raw stream pointer.
        if (infile_.is_open()) {
            return (size_t)infile_.tellg();
        }

        // If file is not open, return 0.
        return 0;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: resetRecord
    // Zeroes out the LogRecord struct to prevent data leakage between iterations.
    // ---------------------------------------------------------------------------------------------------------
    void LogParser::resetRecord(LogRecord& r) {
        // Set timestamp to -1 (Sentinel value) to indicate "Uninitialized".
        // We cannot use 0 because 0 is a valid Unix timestamp.
        r.timestamp = -1;

        // Reset priority to default (0).
        r.priority = 0;

        // Reset Process ID to default (0).
        r.pid = 0;

        // Zero out the source buffer memory.
        std::memset(r.source, 0, sizeof(r.source));

        // Zero out the host buffer memory.
        std::memset(r.host, 0, sizeof(r.host));

        // Zero out the message buffer memory.
        std::memset(r.message, 0, sizeof(r.message));
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: GetNextBatch
    // The core logic. Reads lines from the file until a batch limit is reached.
    // ---------------------------------------------------------------------------------------------------------
    bool LogParser::GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records) {
        // Guard Clause: If file is closed or EOF reached, return false immediately.
        if (!infile_.is_open() || infile_.eof()) return false;

        // String buffer to hold the current line being read.
        std::string line;

        // Clear the output vector to ensure we don't append to old data.
        out_records.clear();

        // Reserve memory upfront to avoid costly reallocations during the loop.
        out_records.reserve(max_records);

        // Loop until we fill the batch or run out of lines in the file.
        while (out_records.size() < max_records) {

            // Capture the exact file byte offset BEFORE reading the line.
            // This position marks the potential start of a new record header.
            size_t line_start_pos = (size_t)infile_.tellg();

            // Read the next line from the file. If reading fails (EOF), break the loop.
            if (!std::getline(infile_, line)) break;

            // Handle potential Windows-style CRLF line endings.
            // If the line ends with '\r', remove it to normalize to Unix style.
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Find the index of the first non-whitespace character.
            size_t first_char = line.find_first_not_of(" \t");

            // If the line is purely whitespace, skip it.
            if (first_char == std::string::npos) continue;

            // Determine if the line is indented (starts with whitespace).
            // In systemd-export format, indented lines belong to the previous key/value.
            bool is_indented = (first_char > 0);

            // Check if this line looks like a Record Header (e.g., "Mon 2026...").
            // It must NOT be indented, and it must start with a Day Name.
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 || line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 || line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 || line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            // If we detected a new record header...
            if (is_new_record_start) {
                // Check if we were already building a record in the buffer.
                // Also check if the timestamp is valid (!= -1) to avoid pushing empty/bad records.
                if (inside_record_ && current_record_.timestamp != -1) {
                    // Push the completed record into the output batch.
                    out_records.push_back(current_record_);
                }

                // Reset the temporary buffer to prepare for the NEW record.
                resetRecord(current_record_);

                // Set the flag indicating we are now actively parsing a record.
                inside_record_ = true;

                // CRITICAL: Update the state tracker to point to the start of THIS new record.
                last_record_offset_ = line_start_pos;
            }
            // If it's not a header, but we are inside a record context...
            else if (inside_record_) {
                // Parse the line as a Metadata Key=Value pair.
                parseMetadataLine(line, current_record_);
            }
        }

        // Post-Loop Check: Handle the very last record in the file.
        // If EOF was hit, the loop breaks, but the last record is still sitting in 'current_record_'.
        if (infile_.eof() && inside_record_ && current_record_.timestamp != -1) {
            // Push the final record to the batch.
            out_records.push_back(current_record_);

            // Reset the buffer to prevent double-pushing if called again.
            resetRecord(current_record_);

            // Mark that we are no longer inside a record.
            inside_record_ = false;
        }

        // Return true if we found at least one record; otherwise false.
        return !out_records.empty();
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: parseMetadataLine
    // Parses a single line of "KEY=VALUE" and updates the struct fields.
    // ---------------------------------------------------------------------------------------------------------
    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        // Find the position of the '=' separator.
        size_t eq_pos = line.find('=');

        // If no '=' is found, ignore the line.
        if (eq_pos == std::string::npos) return;

        // Extract the Key substring and trim whitespace.
        std::string key = trim(line.substr(0, eq_pos));

        // Extract the Value substring and trim whitespace.
        std::string value = trim(line.substr(eq_pos + 1));

        // Map specific keys to LogRecord fields.
        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            // Try converting value to long long; if fail, set to sentinel -1.
            try { record.timestamp = std::stoll(value); }
            catch (...) { record.timestamp = -1; }
        }
        else if (key == "PRIORITY") {
            // Try converting value to int; if fail, default to 0.
            try { record.priority = std::stoi(value); }
            catch (...) { record.priority = 0; }
        }
        else if (key == "_PID") {
            // Try converting value to int; if fail, default to 0.
            try { record.pid = std::stoi(value); }
            catch (...) { record.pid = 0; }
        }
        // Handle Source Identifier (fallback to _COMM if SYSLOG_IDENTIFIER is missing).
        else if (key == "SYSLOG_IDENTIFIER" || (key == "_COMM" && record.source[0] == '\0')) {
            // Safe copy to fixed-size char array with truncation protection.
            strncpy_s(record.source, sizeof(record.source), value.c_str(), _TRUNCATE);
        }
        else if (key == "_HOSTNAME") {
            // Safe copy to host buffer.
            strncpy_s(record.host, sizeof(record.host), value.c_str(), _TRUNCATE);
        }
        else if (key == "MESSAGE") {
            // Safe copy to message buffer.
            strncpy_s(record.message, sizeof(record.message), value.c_str(), _TRUNCATE);
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: trim
    // Utility to remove leading and trailing whitespace from strings.
    // ---------------------------------------------------------------------------------------------------------
    std::string LogParser::trim(const std::string& str) {
        // Find first non-whitespace character.
        size_t first = str.find_first_not_of(" \t\r\n");

        // If string is all whitespace, return original.
        if (std::string::npos == first) return str;

        // Find last non-whitespace character.
        size_t last = str.find_last_not_of(" \t\r\n");

        // Return the substring containing only the content.
        return str.substr(first, (last - first + 1));
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: parseLogFile (Legacy)
    // One-shot wrapper to parse an entire file into a single vector.
    // ---------------------------------------------------------------------------------------------------------
    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        // Instantiate a parser.
        LogParser parser(filename);

        // Vectors to hold results.
        std::vector<LogRecord> all_records;
        std::vector<LogRecord> batch;

        // repeatedly call GetNextBatch until it returns false (EOF).
        while (parser.GetNextBatch(batch, 500000)) {
            // Append the batch to the main vector.
            all_records.insert(all_records.end(), batch.begin(), batch.end());
        }

        // Return the complete list.
        return all_records;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Method: SeekToPosition
    // Jumps the file pointer to a specific byte. Used for Crash Recovery / Resume.
    // ---------------------------------------------------------------------------------------------------------
    void LogParser::SeekToPosition(size_t offset) {
        // Only proceed if file is open.
        if (infile_.is_open()) {
            // Clear any EOF or error flags before seeking.
            infile_.clear();

            // Perform the absolute seek from the beginning.
            infile_.seekg(offset, std::ios::beg);

            // Reset the internal record buffer since we jumped context.
            resetRecord(current_record_);

            // Mark that we are NOT inside a record currently.
            inside_record_ = false;

            // Update our logical tracker to match the physical position.
            last_record_offset_ = offset;
        }
    }

} // namespace cmse::utils