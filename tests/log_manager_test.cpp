#include "../src/utils/log_manager.h"
#include "../src/common/types.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <chrono>

using namespace cmse;
using namespace cmse::utils;

#define ASSERT_EQ(val1, val2, msg) \
    if ((val1) != (val2)) { \
        std::cerr << "[FAIL] " << msg << " | Expected: " << (val2) << ", Got: " << (val1) << std::endl; \
        std::exit(1); \
    }

int main() {
    std::cout << "Running LogManager Tests (New Schema)..." << std::endl;

    const int TEST_COUNT = 100;

    // 1. Generate Logs
    std::vector<LogRecord> logs = LogManager::generateSyntheticLogs(TEST_COUNT);

    ASSERT_EQ(logs.size(), TEST_COUNT, "Log count mismatch");

    // 2. Verify Data Structure
    const auto& r = logs[0];

    // Check Timestamp (Should be valid int64 > 0)
    if (r.timestamp <= 0) {
        std::cerr << "[FAIL] Generated timestamp is invalid: " << r.timestamp << std::endl;
        return 1;
    }

    // Check Priority (0-7)
    if (r.priority < 0 || r.priority > 7) {
        std::cerr << "[FAIL] Priority out of range: " << r.priority << std::endl;
        return 1;
    }

    // Check PID
    if (r.pid < 100) {
        std::cerr << "[FAIL] PID seems invalid: " << r.pid << std::endl;
        return 1;
    }

    // Check Strings
    std::string src(r.source);
    if (src.empty()) {
        std::cerr << "[FAIL] Source is empty" << std::endl;
        return 1;
    }

    std::cout << "[OK] Synthetic Log Generation matches new schema." << std::endl;
    std::cout << "Sample: " << r.toString() << std::endl;

    return 0;
}