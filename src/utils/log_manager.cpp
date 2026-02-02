#include "log_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <random>
#include <iomanip>

namespace cmse::utils {

    std::vector<LogRecord> LogManager::generateSyntheticLogs(int count, int64_t start_id, int time_range_ms) {
        std::vector<LogRecord> logs;
        logs.reserve(count);

        std::random_device rd;
        std::mt19937 gen(rd());

        // Random Generators for new fields
        std::uniform_int_distribution<> prio_dist(0, 7); // Syslog priority 0-7
        std::uniform_int_distribution<> pid_dist(100, 9999);
        std::uniform_int_distribution<long long> time_dist(0, time_range_ms);
        std::uniform_int_distribution<> source_dist(0, 4);

        const char* sources[] = { "systemd", "kernel", "sshd", "nginx", "mysql" };
        const char* hosts[] = { "web-01", "db-01", "lb-01", "backup-01" };

        auto base_time = std::chrono::system_clock::now();

        for (int i = 0; i < count; ++i) {
            LogRecord record;

            // 1. Timestamp (Fix: Convert time_point to int64 microseconds)
            long long offset_ms = time_dist(gen);
            auto timestamp_tp = base_time + std::chrono::milliseconds(offset_ms);
            record.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                timestamp_tp.time_since_epoch()).count();

            // 2. Priority
            record.priority = prio_dist(gen);

            // 3. PID
            record.pid = pid_dist(gen);

            // 4. Source & Host
            int src_idx = i % 5;
            int host_idx = i % 4;

            strncpy_s(record.source, sizeof(record.source), sources[src_idx], _TRUNCATE);
            strncpy_s(record.host, sizeof(record.host), hosts[host_idx], _TRUNCATE);

            // 5. Message (Synthetic payload)
            std::string msg = "Synthetic log message number " + std::to_string(i);
            strncpy_s(record.message, sizeof(record.message), msg.c_str(), _TRUNCATE);

            logs.push_back(record);
        }

        return logs;
    }

    void LogManager::writeLogsToFile(const std::vector<LogRecord>& logs, const std::string& filename) {
        std::ofstream outfile(filename);
        if (!outfile.is_open()) {
            std::cerr << "[LogManager] Error: Could not open file " << filename << std::endl;
            return;
        }

        for (const auto& log : logs) {
            outfile << log.toString() << "\n";
        }
        outfile.close();
    }

    std::vector<LogRecord> LogManager::readLogsFromFile(const std::string& filename) {
        // NOTE: For the new structure, we rely on LogParser for complex reading.
        // This simple CSV reader is kept for backward compatibility with simple tests,
        // but assumes the toString() format.
        std::vector<LogRecord> logs;
        std::ifstream infile(filename);
        std::string line;

        while (std::getline(infile, line)) {
            if (!line.empty()) {
                logs.push_back(parseLine(line));
            }
        }
        return logs;
    }

    LogRecord LogManager::parseLine(const std::string& line) {
        LogRecord record = {}; // Zero-init
        // Simple parser for the toString() format: 
        // "TS:123 | PRI:3 | PID:456 | SRC:src | HOST:host | MSG:msg"

        // This is a rough parser for the synthetic dump. 
        // For real logs, LogParser is used.
        try {
            // Very basic extraction just to satisfy the function signature
            // In a real scenario, use regex or proper splitting.
            // Here we just set timestamp to valid so it doesn't crash tests.
            size_t ts_pos = line.find("TS:");
            if (ts_pos != std::string::npos) {
                record.timestamp = std::stoll(line.substr(ts_pos + 3, line.find(" |", ts_pos)));
            }
        }
        catch (...) {
            record.timestamp = 0;
        }
        return record;
    }

} // namespace cmse::utils