#include "utils/log_parser.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace cmse::utils {

    // --- CONSTRUCTOR: Initializes stream and calculates file metrics ---
    LogParser::LogParser(const std::string& filename) : filename_(filename) {
        infile_.open(filename); // Open the raw log file for reading
        if (infile_.is_open()) {
            // Move pointer to end to calculate total bytes for progress tracking
            infile_.seekg(0, std::ios::end);
            file_size_ = infile_.tellg();
            // Reset pointer to the beginning for parsing
            infile_.seekg(0, std::ios::beg);
        }
        resetRecord(current_record_); // Initialize the internal temporary buffer
    }

    // --- DESTRUCTOR: Standard cleanup ---
    LogParser::~LogParser() {
        if (infile_.is_open()) {
            infile_.close(); // Release file handle
        }
    }

    // --- METRICS ACCESSORS ---
    size_t LogParser::GetTotalFileSize() const { return file_size_; }

    size_t LogParser::GetCurrentPosition() {
        if (infile_.is_open()) return (size_t)infile_.tellg(); // Return current byte offset
        return 0;
    }

    // --- DATA CLEANING: Zeroes out the LogRecord struct ---
    void LogParser::resetRecord(LogRecord& r) {
        r.timestamp = 0;
        r.priority = 0;
        r.pid = 0;
        // Efficiently clear character arrays to prevent cross-record data contamination
        std::memset(r.source, 0, sizeof(r.source));
        std::memset(r.host, 0, sizeof(r.host));
        std::memset(r.message, 0, sizeof(r.message));
    }

    // --- CORE LOGIC: BATCHED INGESTION ---
    /**
     * GetNextBatch
     * State-machine based parser that identifies record boundaries based on day-of-week
     * markers and accumulates metadata keys until a new record begins.
     */
    bool LogParser::GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records) {
        // Guard clause: ensure file is accessible and not already finished
        if (!infile_.is_open() || infile_.eof()) return false;

        std::string line;
        out_records.clear();
        out_records.reserve(max_records); // Pre-allocate memory to avoid re-allocations

        // Process line by line until batch limit is reached or file ends
        while (out_records.size() < max_records && std::getline(infile_, line)) {
            // Identify indentation level to distinguish between headers and body content
            size_t first_char = line.find_first_not_of(" \t");
            if (first_char == std::string::npos) continue; // Skip empty lines

            bool is_indented = (first_char > 0);

            // A new record starts if a line is NOT indented and starts with a Day (e.g., "Mon")
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 || line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 || line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 || line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            if (is_new_record_start) {
                // If we were already building a record, push it into the batch before starting new one
                if (inside_record_ && current_record_.timestamp != 0) {
                    out_records.push_back(current_record_);
                }

                resetRecord(current_record_); // Clear buffer for the new record
                inside_record_ = true;        // Set state to "accumulating metadata"
            }
            else if (inside_record_) {
                // If the line is part of a record body, extract KEY=VALUE pairs
                parseMetadataLine(line, current_record_);
            }
        }

        // POST-LOOP: If EOF hit, push the very last assembled record
        if (infile_.eof() && inside_record_ && current_record_.timestamp != 0) {
            out_records.push_back(current_record_);
            resetRecord(current_record_); // Prevent accidental double-add
        }

        // Return true if we successfully found any records in this batch
        return !out_records.empty();
    }

    // --- METADATA INTERPRETER: KEY=VALUE Extraction ---
    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return; // Ignore lines that don't follow the pair format

        // Split line into Key and Value components
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        // Mapping log keys to internal struct fields with safe conversion
        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            try { record.timestamp = std::stoll(value); } // Convert string to 64-bit microsecond int
            catch (...) { record.timestamp = 0; }
        }
        else if (key == "PRIORITY") {
            try { record.priority = std::stoi(value); }
            catch (...) { record.priority = 0; }
        }
        else if (key == "_PID") {
            try { record.pid = std::stoi(value); }
            catch (...) { record.pid = 0; }
        }
        // Identifier mapping (uses syslog identifier or command name)
        else if (key == "SYSLOG_IDENTIFIER" || (key == "_COMM" && record.source[0] == '\0')) {
            strncpy_s(record.source, sizeof(record.source), value.c_str(), _TRUNCATE);
        }
        else if (key == "_HOSTNAME") {
            strncpy_s(record.host, sizeof(record.host), value.c_str(), _TRUNCATE);
        }
        else if (key == "MESSAGE") {
            strncpy_s(record.message, sizeof(record.message), value.c_str(), _TRUNCATE);
        }
    }

    // --- STRING UTILITY: Trims whitespace/newlines from both ends ---
    std::string LogParser::trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    // --- COMPATIBILITY WRAPPER: Fully loads file (Legacy support) ---
    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        LogParser parser(filename);
        std::vector<LogRecord> all_records;
        std::vector<LogRecord> batch;

        // Iteratively pull batches until the parser signals completion
        while (parser.GetNextBatch(batch, 500000)) {
            all_records.insert(all_records.end(), batch.begin(), batch.end());
        }
        return all_records;
    }

} // namespace cmse::utils