#include "log_parser.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace cmse::utils {

    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        std::vector<LogRecord> records;
        std::ifstream infile(filename);

        if (!infile.is_open()) {
            std::cerr << "[LogParser] Error: Could not open file " << filename << std::endl;
            return records;
        }

        std::string line;
        LogRecord current_record;
        bool inside_record = false;

        // Default values for a fresh record
        auto resetRecord = [&](LogRecord& r) {
            r.timestamp = 0;
            r.priority = 0;
            r.pid = 0;
            std::memset(r.source, 0, sizeof(r.source));
            std::memset(r.host, 0, sizeof(r.host));
            std::memset(r.message, 0, sizeof(r.message));
            };

        resetRecord(current_record);

        while (std::getline(infile, line)) {
            // Trim leading whitespace to check for indentation
            size_t first_char = line.find_first_not_of(" \t");

            // Skip empty lines
            if (first_char == std::string::npos) continue;

            bool is_indented = (first_char > 0);

            // LOGIC:
            // 1. If line starts with NO indentation and looks like a Date (e.g. "Thu", "Wed"),
            //    it marks the START of a new record.
            // 2. If we were already building a record, push the PREVIOUS one to the vector.

            // Simple check: Days of week usually start the log lines in your sample
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 ||
                line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 ||
                line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 ||
                line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            if (is_new_record_start) {
                // If we have a pending record (and it has a valid timestamp), save it
                if (inside_record && current_record.timestamp != 0) {
                    records.push_back(current_record);
                }

                // Start a new record
                resetRecord(current_record);
                inside_record = true;

                // Note: The header line itself contains a human readable timestamp,
                // but we prefer _SOURCE_REALTIME_TIMESTAMP found in the body 
                // for precision and integer storage.
            }
            else if (inside_record) {
                // This is a metadata line (Key=Value)
                parseMetadataLine(line, current_record);
            }
        }

        // Push the final record after EOF
        if (inside_record && current_record.timestamp != 0) {
            records.push_back(current_record);
        }

        std::cout << "[LogParser] Parsed " << records.size() << " records from " << filename << std::endl;
        return records;
    }

    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        // Find position of '='
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return; // Not a key=value line

        // Extract Key and Value
        // Keys usually have leading spaces in the file, so we trim them.
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        // --- MAPPING (Based on Project Guide) ---

        // 1. Timestamp [_SOURCE_REALTIME_TIMESTAMP]
        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            try {
                record.timestamp = std::stoll(value);
            }
            catch (...) { record.timestamp = 0; }
        }
        // 2. Priority [PRIORITY]
        else if (key == "PRIORITY") {
            try {
                record.priority = std::stoi(value);
            }
            catch (...) { record.priority = 0; }
        }
        // 3. PID [_PID]
        else if (key == "_PID") {
            try {
                record.pid = std::stoi(value);
            }
            catch (...) { record.pid = 0; }
        }
        // 4. Source [SYSLOG_IDENTIFIER or _COMM]
        else if (key == "SYSLOG_IDENTIFIER" || (key == "_COMM" && record.source[0] == '\0')) {
            // Prefer SYSLOG_IDENTIFIER, fallback to _COMM
            strncpy_s(record.source, sizeof(record.source), value.c_str(), _TRUNCATE);
        }
        // 5. Host [_HOSTNAME]
        else if (key == "_HOSTNAME") {
            strncpy_s(record.host, sizeof(record.host), value.c_str(), _TRUNCATE);
        }
        // 6. Message [MESSAGE]
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

} // namespace cmse::utils