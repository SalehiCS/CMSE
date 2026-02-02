#pragma once
#include "../common/types.h"
#include <vector>
#include <string>

namespace cmse::utils {

    class LogManager {
    public:
        // Updated to use default args compatible with new logic
        static std::vector<LogRecord> generateSyntheticLogs(int count = 10000, int64_t start_id = 1000, int time_range_ms = 3600000);

        static void writeLogsToFile(const std::vector<LogRecord>& logs, const std::string& filename);
        static std::vector<LogRecord> readLogsFromFile(const std::string& filename);

    private:
        static LogRecord parseLine(const std::string& line);
    };

} // namespace cmse::utils