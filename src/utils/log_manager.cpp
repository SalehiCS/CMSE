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
            // This is crucial for testing B+Tree updates and MVCC versions later.
            record.resource_id = start_resource_id + (i % 50);

            // 3. Generate Resource Name
            // Format: "vm-node-XX"
            // We align the name with the ID logic (i % 50) so ID and Name are consistent.
            std::string name = "vm-node-" + std::to_string(i % 50);

            // Safe copy to fixed-size char array
            std::strncpy(record.resource_name, name.c_str(), sizeof(record.resource_name) - 1);
            record.resource_name[sizeof(record.resource_name) - 1] = '\0'; // Ensure null-termination

            // 4. Generate Event Type
            // Cycle through common cloud event types
            const char* events[] = { "START", "STOP", "RESTART", "ERROR", "WARNING", "DEPLOY" };
            std::string event = events[i % 6];

            std::strncpy(record.event_type, event.c_str(), sizeof(record.event_type) - 1);
            record.event_type[sizeof(record.event_type) - 1] = '\0';

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
                    // Continue reading other lines even if one fails
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

        // 1. Parse Timestamp (stored as int64 ticks)
        if (std::getline(ss, segment, ',')) {
            try {
                int64_t ticks = std::stoll(segment);
                auto duration = std::chrono::milliseconds(ticks);
                record.timestamp = std::chrono::system_clock::time_point(duration);
            }
            catch (...) {
                // Handle parsing error if file is corrupted
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
            std::strncpy(record.resource_name, segment.c_str(), sizeof(record.resource_name) - 1);
            record.resource_name[sizeof(record.resource_name) - 1] = '\0';
        }

        // 4. Parse Event Type
        if (std::getline(ss, segment, ',')) {
            std::strncpy(record.event_type, segment.c_str(), sizeof(record.event_type) - 1);
            record.event_type[sizeof(record.event_type) - 1] = '\0';
        }

        return record;
    }

} // namespace cmse::utils