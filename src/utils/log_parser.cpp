#include "../utils/log_parser.h"
#include "../common/logger.h" 
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace cmse::utils {

    LogParser::LogParser(const std::string& filename) : filename_(filename) {
        // KEEP BINARY MODE (Crucial for offsets)
        infile_.open(filename, std::ios::in | std::ios::binary);
        if (infile_.is_open()) {
            infile_.seekg(0, std::ios::end);
            file_size_ = infile_.tellg();
            infile_.seekg(0, std::ios::beg);
        }
        resetRecord(current_record_);
        last_record_offset_ = 0;
    }

    LogParser::~LogParser() {
        if (infile_.is_open()) infile_.close();
    }

    size_t LogParser::GetTotalFileSize() const { return file_size_; }

    size_t LogParser::GetCurrentPosition() {
        if (inside_record_) return last_record_offset_;
        if (infile_.is_open()) return (size_t)infile_.tellg();
        return 0;
    }

    void LogParser::resetRecord(LogRecord& r) {
        // FIX: Use -1 as sentinel. 0 is a valid timestamp!
        r.timestamp = -1;
        r.priority = 0;
        r.pid = 0;
        std::memset(r.source, 0, sizeof(r.source));
        std::memset(r.host, 0, sizeof(r.host));
        std::memset(r.message, 0, sizeof(r.message));
    }

    bool LogParser::GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records) {
        if (!infile_.is_open() || infile_.eof()) return false;

        std::string line;
        out_records.clear();
        out_records.reserve(max_records);

        while (out_records.size() < max_records) {
            size_t line_start_pos = (size_t)infile_.tellg();

            if (!std::getline(infile_, line)) break;

            // Handle Windows/Text mode line endings just in case
            if (!line.empty() && line.back() == '\r') line.pop_back();

            size_t first_char = line.find_first_not_of(" \t");
            if (first_char == std::string::npos) continue;

            bool is_indented = (first_char > 0);
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 || line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 || line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 || line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            if (is_new_record_start) {
                // FIX: Check against -1, not 0
                if (inside_record_ && current_record_.timestamp != -1) {
                    out_records.push_back(current_record_);
                }

                resetRecord(current_record_);
                inside_record_ = true;
                last_record_offset_ = line_start_pos;
            }
            else if (inside_record_) {
                parseMetadataLine(line, current_record_);
            }
        }

        // FIX: Check against -1 here too
        if (infile_.eof() && inside_record_ && current_record_.timestamp != -1) {
            out_records.push_back(current_record_);
            resetRecord(current_record_);
            inside_record_ = false;
        }

        return !out_records.empty();
    }

    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return;
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            try { record.timestamp = std::stoll(value); }
            catch (...) { record.timestamp = -1; } // FIX: Set error state to -1
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

    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        LogParser parser(filename);
        std::vector<LogRecord> all_records;
        std::vector<LogRecord> batch;
        while (parser.GetNextBatch(batch, 500000)) {
            all_records.insert(all_records.end(), batch.begin(), batch.end());
        }
        return all_records;
    }

    // --- Implement SeekToPosition if missing ---
    void LogParser::SeekToPosition(size_t offset) {
        if (infile_.is_open()) {
            infile_.clear();
            infile_.seekg(offset, std::ios::beg);
            resetRecord(current_record_);
            inside_record_ = false;
            last_record_offset_ = offset;
        }
    }

} // namespace cmse::utils