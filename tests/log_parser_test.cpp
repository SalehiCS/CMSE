#include "../src/utils/log_parser.h"
#include "../src/common/types.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>

using namespace cmse;
using namespace cmse::utils;

// --- Helper Macros ---
#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << std::endl; \
        std::exit(1); \
    }

int main() {
    std::cout << "Running LogParser Tests (External File Mode)..." << std::endl;

    // --- FIX: Create the dummy file first ---
    {
        std::ofstream out("sample_test.log");
        out << "TS:1000 | MSG:Log Line 1\n";
        out << "TS:1001 | MSG:Log Line 2\n";
        out.close();
    }

    // The user must place this file manually in the build/bin directory
    const std::string TEST_FILENAME = "sample_test.log";

    // 1. Check if the file exists before attempting to parse
    std::ifstream f(TEST_FILENAME);
    if (!f.good()) {
        std::cerr << "[ERROR] Test file '" << TEST_FILENAME << "' not found!" << std::endl;
        std::cerr << "Action Required: Please copy your log file to the executable directory and rename it to '"
            << TEST_FILENAME << "'." << std::endl;
        return 1; // Fail the test
    }
    f.close();

    // 2. Run the parser
    std::cout << "[INFO] Parsing file: " << TEST_FILENAME << " ..." << std::endl;
    std::vector<LogRecord> records = LogParser::parseLogFile(TEST_FILENAME);

    // 3. Report Results
    size_t count = records.size();
    std::cout << "[INFO] Total records parsed: " << count << std::endl;

    if (count == 0) {
        std::cerr << "[WARN] The file was found but 0 records were parsed. Check the file format." << std::endl;
    }
    else {
        // 4. Sanity Checks & Visualization
        // We verify that the parser actually extracted data (timestamps shouldn't be 0)
        int invalid_timestamps = 0;
        for (const auto& r : records) {
            if (r.timestamp == 0) invalid_timestamps++;
        }

        if (invalid_timestamps > 0) {
            std::cerr << "[WARN] Found " << invalid_timestamps << " records with 0 timestamp (parsing errors?)" << std::endl;
        }
        else {
            std::cout << "[OK] All records have valid non-zero timestamps." << std::endl;
        }

        // 5. Print Head (First 5)
        std::cout << "\n--- HEAD: First 5 Records ---" << std::endl;
        for (size_t i = 0; i < 5 && i < count; ++i) {
            std::cout << "[" << i << "] " << records[i].toString() << std::endl;
        }

        // 6. Print Tail (Last 5)
        if (count > 5) {
            std::cout << "\n--- TAIL: Last 5 Records ---" << std::endl;
            size_t start_idx = count - 5;
            for (size_t i = start_idx; i < count; ++i) {
                std::cout << "[" << i << "] " << records[i].toString() << std::endl;
            }
        }
    }

    std::cout << "\n[OK] Parser test execution finished." << std::endl;
    return 0;
}