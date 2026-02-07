#include "../utils/log_parser.h"
#include "../common/logger.h" 
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iomanip> // Provides std::get_time for parsing date/time strings.
#include <ctime>   // Provides std::mktime for converting time structures to epoch.

namespace cmse::utils {

    /**
     * @brief Internal helper to extract a microsecond-precision timestamp from the log header line.
     * Format expected: "Day YYYY-MM-DD HH:MM:SS.uuuuuu"
     */
    static int64_t ParseHeaderTimestamp(const std::string& line) {
        try {
            // Locate the first space character to skip the day name (e.g., "Thu ").
            size_t first_space = line.find(' ');
            // Validate that the space exists and there is enough remaining string for a date.
            if (first_space == std::string::npos || first_space + 20 > line.size()) return -1;

            // Extract the core date and time component: "YYYY-MM-DD HH:MM:SS".
            std::string datetime_str = line.substr(first_space + 1, 19);

            std::tm tm = {};              // Initialize time structure to zero.
            std::istringstream ss(datetime_str);
            // Parse the string into the tm struct using the standard format.
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

            // Return error sentinel if parsing the date string failed.
            if (ss.fail()) return -1;

            // Convert the parsed calendar time into seconds since the Unix epoch.
            time_t seconds = std::mktime(&tm);
            if (seconds == -1) return -1;

            // Convert seconds to microseconds as the base for the final timestamp.
            int64_t total_micros = static_cast<int64_t>(seconds) * 1000000;

            // Locate the decimal point to find the microsecond fractional part.
            size_t dot_pos = line.find('.', first_space + 19);
            if (dot_pos != std::string::npos) {
                // Find the space following the microsecond digits.
                size_t space_after = line.find(' ', dot_pos);
                if (space_after != std::string::npos) {
                    // Isolate the fractional string (e.g., "575408").
                    std::string us_str = line.substr(dot_pos + 1, space_after - dot_pos - 1);

                    // Normalize the string to exactly 6 digits to ensure correct microsecond value.
                    if (us_str.length() > 6) us_str = us_str.substr(0, 6);
                    while (us_str.length() < 6) us_str += '0';

                    // Add the fractional microseconds to the total epoch value.
                    total_micros += std::stoi(us_str);
                }
            }
            return total_micros;
        }
        catch (...) {
            // Return sentinel on any parsing exception.
            return -1;
        }
    }

    /**
     * @brief Constructor: Opens the file in binary mode and calculates total file size.
     */
    LogParser::LogParser(const std::string& filename) : filename_(filename) {
        // Open in binary mode to ensure seek/tell operations match physical byte offsets.
        infile_.open(filename, std::ios::in | std::ios::binary);

        if (infile_.is_open()) {
            // Seek to the end to determine the total size of the file.
            infile_.seekg(0, std::ios::end);
            file_size_ = infile_.tellg();
            // Reset to the beginning for subsequent reading.
            infile_.seekg(0, std::ios::beg);
        }

        // Initialize the internal state and buffers.
        resetRecord(current_record_);
        last_record_offset_ = 0;
    }

    /**
     * @brief Destructor: Closes the file handle if it remains open.
     */
    LogParser::~LogParser() {
        if (infile_.is_open()) {
            infile_.close();
        }
    }

    /**
     * @brief Returns the total size of the opened log file in bytes.
     */
    size_t LogParser::GetTotalFileSize() const {
        return file_size_;
    }

    /**
     * @brief Returns the current byte offset in the file for resuming later.
     */
    size_t LogParser::GetCurrentPosition() {
        // If we are currently parsing fields for a record, return the start of that record.
        if (inside_record_) {
            return last_record_offset_;
        }
        // Otherwise, return the current read pointer of the file stream.
        if (infile_.is_open()) {
            return (size_t)infile_.tellg();
        }
        return 0;
    }

    /**
     * @brief Clears a LogRecord structure to its default state.
     */
    void LogParser::resetRecord(LogRecord& r) {
        r.timestamp = -1; // -1 indicates the timestamp has not been set yet.
        r.priority = 0;   // Default priority.
        r.pid = 0;        // Default process ID.
        // Wipe all fixed-size character arrays.
        std::memset(r.source, 0, sizeof(r.source));
        std::memset(r.host, 0, sizeof(r.host));
        std::memset(r.message, 0, sizeof(r.message));
    }

    /**
     * @brief Parses the next batch of log records from the file up to max_records.
     */
    bool LogParser::GetNextBatch(std::vector<LogRecord>& out_records, size_t max_records) {
        // Validate stream status.
        if (!infile_.is_open() || infile_.eof()) return false;

        std::string line;
        out_records.clear();
        out_records.reserve(max_records); // Pre-allocate memory for performance.

        while (out_records.size() < max_records) {
            // Track the starting position of the current line before reading it.
            size_t line_start_pos = (size_t)infile_.tellg();

            // Attempt to read a line; exit loop if EOF is encountered.
            if (!std::getline(infile_, line)) break;

            // Strip trailing carriage returns for Windows/DOS file compatibility.
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Skip lines that contain only whitespace.
            size_t first_char = line.find_first_not_of(" \t");
            if (first_char == std::string::npos) continue;

            // Indented lines (leading whitespace) signify continuation of the previous record.
            bool is_indented = (first_char > 0);

            // Determine if the line is the start of a new record by checking for day prefixes.
            bool is_new_record_start = !is_indented && (
                line.rfind("Thu", 0) == 0 || line.rfind("Wed", 0) == 0 ||
                line.rfind("Mon", 0) == 0 || line.rfind("Tue", 0) == 0 ||
                line.rfind("Fri", 0) == 0 || line.rfind("Sat", 0) == 0 ||
                line.rfind("Sun", 0) == 0
                );

            if (is_new_record_start) {
                // If we were already building a valid record, commit it to the batch now.
                if (inside_record_ && current_record_.timestamp != -1) {
                    out_records.push_back(current_record_);
                }

                // Prepare the buffer for the new record.
                resetRecord(current_record_);
                inside_record_ = true;
                last_record_offset_ = line_start_pos;

                // Extract a fallback timestamp from the header. This prevents record loss if
                // the explicit _SOURCE_REALTIME_TIMESTAMP field is missing in the metadata.
                current_record_.timestamp = ParseHeaderTimestamp(line);
            }
            else if (inside_record_) {
                // If we are within a record context, parse the line as a key=value pair.
                parseMetadataLine(line, current_record_);
            }
        }

        // Final check: if EOF was reached, commit the final record currently in the buffer.
        if (infile_.eof() && inside_record_ && current_record_.timestamp != -1) {
            out_records.push_back(current_record_);
            resetRecord(current_record_);
            inside_record_ = false;
        }

        // Return true if any records were added to this batch.
        return !out_records.empty();
    }

    /**
     * @brief Parses a metadata line in "KEY=VALUE" format and assigns to LogRecord fields.
     */
    void LogParser::parseMetadataLine(const std::string& line, LogRecord& record) {
        // Locate the assignment operator.
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) return;

        // Extract key and value strings, removing surrounding whitespace.
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (key == "_SOURCE_REALTIME_TIMESTAMP") {
            try {
                // If the explicit high-precision timestamp exists, overwrite the header fallback.
                record.timestamp = std::stoll(value);
            }
            catch (...) {
                // Ignore conversion errors; the fallback timestamp remains intact.
            }
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
            // Copy identifier to the source field with truncation safety.
            strncpy_s(record.source, sizeof(record.source), value.c_str(), _TRUNCATE);
        }
        else if (key == "_HOSTNAME") {
            // Copy hostname to the record.
            strncpy_s(record.host, sizeof(record.host), value.c_str(), _TRUNCATE);
        }
        else if (key == "MESSAGE") {
            // Copy the main log message content.
            strncpy_s(record.message, sizeof(record.message), value.c_str(), _TRUNCATE);
        }
    }

    /**
     * @brief Utility function to remove leading and trailing whitespace from a string.
     */
    std::string LogParser::trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    /**
     * @brief Static wrapper to parse an entire log file into a single vector.
     */
    std::vector<LogRecord> LogParser::parseLogFile(const std::string& filename) {
        LogParser parser(filename);
        std::vector<LogRecord> all_records;
        std::vector<LogRecord> batch;
        // Continuously fetch batches until the file is exhausted.
        while (parser.GetNextBatch(batch, 500000)) {
            all_records.insert(all_records.end(), batch.begin(), batch.end());
        }
        return all_records;
    }

    /**
     * @brief Jumps the file pointer to a specific offset, clearing buffers and status flags.
     */
    void LogParser::SeekToPosition(size_t offset) {
        if (infile_.is_open()) {
            infile_.clear(); // Clear EOF and error flags.
            infile_.seekg(offset, std::ios::beg);
            resetRecord(current_record_);
            inside_record_ = false;
            last_record_offset_ = offset;
        }
    }

} // namespace cmse::utils

