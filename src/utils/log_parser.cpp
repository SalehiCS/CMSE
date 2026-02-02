#include "utils/log_parser.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace cmse::utils {

    // --- Constructor & Destructor ---
    LogParser::LogParser(const std::string& filename) : filename_(filename) {
        infile_.open(filename);
        if (infile_.is_open()) {
            infile_.seekg(0, std::ios::end);
            file_size_ = infile_.tellg();
            infile_.seekg(0, std::ios::beg);
        }
        resetRecord(current_record_);
    }

    LogParser::~LogParser() {
        if (infile_.is_open()) {
            infile_.close();
        }
    }

    size_t LogParser::GetTotalFileSize() const { return file_size_; }

    size_t LogParser::GetCurrentPosition() {
        if (infile_.is_open()) return (size_t)infile_.tellg();
        return 0;
    }

    void LogParser::resetRecord(LogRecord& r) {
        r.timestamp = 0;
        r.priority = 0;
        r.pid = 0;
        std::memset(r.source, 0, sizeof(r.source));
        std::memset(r.host, 0, sizeof(r.host));
        std::memset(r.message, 0, sizeof(r.message));
    }

    // --- Core Logic: Get Next Batch ---
    bool LogParser::GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records) {
        if (!infile_.is_open() || infile_.eof()) return false;

        std::string line;
        out_records.clear();
        out_records.reserve(max_records); // Optimization

        while (out_records.size() < max_records && std::getline(infile_, line)) {
            // Trim leading whitespace
            size_t first_char = line.find_first_not_of(" \t");
            if (first_char == std::string::npos) continue;

            bool is_indented = (first_char > 0);

            // Check for new record start (Day of week)
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 || line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 || line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 || line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            if (is_new_record_start) {
                // Save previous record if valid
                if (inside_record_ && current_record_.timestamp != 0) {
                    out_records.push_back(current_record_);
                }

                resetRecord(current_record_);
                inside_record_ = true;
            }
            else if (inside_record_) {
                parseMetadataLine(line, current_record_);
            }
        }

        // Check if we hit EOF but have one last record pending
        if (infile_.eof() && inside_record_ && current_record_.timestamp != 0) {
            out_records.push_back(current_record_);
            resetRecord(current_record_); // Clear it so we don't add it again
        }

        return !out_records.empty();
    }

    // --- Static Helper: Parse Metadata ---
    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return;

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            try { record.timestamp = std::stoll(value); }
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

    std::string LogParser::trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    // --- Compatibility Wrapper (Old static method) ---
    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        LogParser parser(filename);
        std::vector<LogRecord> all_records;
        std::vector<LogRecord> batch;

        // Read everything until done (Not recommended for huge files, but keeps compatibility)
        while (parser.GetNextBatch(batch, 500000)) { // Huge batch
            all_records.insert(all_records.end(), batch.begin(), batch.end());
        }
        return all_records;
    }

} // namespace cmse::utils