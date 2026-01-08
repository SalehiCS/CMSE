#include "log_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <chrono>

namespace cmse::utils {

    std::vector<LogRecord> LogManager::generateSyntheticLogs(int count, int64_t start_resource_id, int time_step_ms) {
        std::vector<LogRecord> logs;
        logs.reserve(count);

        // Capture current time as the base start time
        auto current_time = std::chrono::system_clock::now();

        for (int i = 0; i < count; ++i) {
            LogRecord record;

            // 1. Generate Timestamp
            // Increases strictly by time_step_ms for each record
            record.timestamp = current_time + std::chrono::milliseconds(i * time_step_ms);

            // 2. Generate Resource ID
            // We use modulo to simulate updates on existing resources.
            // e.g., if i % 50, it means we have 50 unique resources being logged repeatedly.
            record.resource_id = start_resource_id + (i % 50);

            // 3. Generate Resource Name
            // Format: "vm-node-XX"
            std::string name = "vm-node-" + std::to_string(i % 50);

            // Safe copy using strncpy_s (MSVC secure version)
            // _TRUNCATE ensures that if the string is too long, it is truncated and null-terminated.
            strncpy_s(record.resource_name, sizeof(record.resource_name), name.c_str(), _TRUNCATE);

            // 4. Generate Event Type
            const char* events[] = { "START", "STOP", "RESTART", "ERROR", "WARNING", "DEPLOY" };
            std::string event = events[i % 6];

            strncpy_s(record.event_type, sizeof(record.event_type), event.c_str(), _TRUNCATE);

            logs.push_back(record);
        }

        return logs;
    }

    void LogManager::writeLogsToFile(const std::vector<LogRecord>& logs, const std::string& filename) {
        std::ofstream outfile(filename);
        if (!outfile.is_open()) {
            std::cerr << "[LogManager] Error: Could not open file " << filename << " for writing." << std::endl;
            return;
        }

        // Iterate and write each record using its toString helper (CSV format)
        for (const auto& log : logs) {
            outfile << log.toString() << "\n";
        }

        outfile.close();
        std::cout << "[LogManager] Successfully generated and wrote " << logs.size() << " records to " << filename << std::endl;
    }

    std::vector<LogRecord> LogManager::readLogsFromFile(const std::string& filename) {
        std::vector<LogRecord> logs;
        std::ifstream infile(filename);

        if (!infile.is_open()) {
            std::cerr << "[LogManager] Error: Could not open file " << filename << " for reading." << std::endl;
            return logs;
        }

        std::string line;
        int line_number = 0;
        while (std::getline(infile, line)) {
            line_number++;
            if (!line.empty()) {
                try {
                    logs.push_back(parseLine(line));
                }
                catch (const std::exception& e) {
                    std::cerr << "[LogManager] Warning: Failed to parse line " << line_number << ": " << e.what() << std::endl;
                }
            }
        }

        infile.close();
        std::cout << "[LogManager] Successfully loaded " << logs.size() << " records from " << filename << std::endl;
        return logs;
    }

    LogRecord LogManager::parseLine(const std::string& line) {
        LogRecord record;
        std::stringstream ss(line);
        std::string segment;

        // Expected Format: timestamp_ticks,resource_id,resource_name,event_type

        // 1. Parse Timestamp
        if (std::getline(ss, segment, ',')) {
            try {
                int64_t ticks = std::stoll(segment);
                auto duration = std::chrono::milliseconds(ticks);
                record.timestamp = std::chrono::system_clock::time_point(duration);
            }
            catch (...) {
                record.timestamp = std::chrono::system_clock::now();
            }
        }

        // 2. Parse Resource ID
        if (std::getline(ss, segment, ',')) {
            try {
                record.resource_id = std::stoll(segment);
            }
            catch (...) {
                record.resource_id = 0;
            }
        }

        // 3. Parse Resource Name
        if (std::getline(ss, segment, ',')) {
            strncpy_s(record.resource_name, sizeof(record.resource_name), segment.c_str(), _TRUNCATE);
        }

        // 4. Parse Event Type
        if (std::getline(ss, segment, ',')) {
            strncpy_s(record.event_type, sizeof(record.event_type), segment.c_str(), _TRUNCATE);
        }

        return record;
    }

} // namespace cmse::utils