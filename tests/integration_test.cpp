#include "../src/index/btree_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/utils/log_parser.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

// We use specific using declarations to keep the test clean but safe
using cmse::index::BTreeIndex;
using cmse::bufferpool::BufferPoolManager;
using cmse::disk::DiskManager;
using cmse::utils::LogParser;
using cmse::LogRecord;

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "       CMSE SYSTEM INTEGRATION TEST         " << std::endl;
    std::cout << "============================================" << std::endl;

    // 0. Configuration
    const std::string DB_FILE = "test_integration.db";
    const std::string LOG_FILE = "sample_test.log";

    // Cleanup previous run
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }

    // Check if log file exists
    if (!std::filesystem::exists(LOG_FILE)) {
        std::cerr << "[ERROR] Log file '" << LOG_FILE << "' not found!" << std::endl;
        std::cerr << "Please copy 'sample_test.log' to the build directory." << std::endl;
        return 1;
    }

    try {
        // 1. Initialize System Components
        std::cout << "[1] Initializing Storage Engine..." << std::endl;

        // Create DiskManager (It will create the file if missing, based on your code)
        DiskManager* disk_manager = new DiskManager(DB_FILE);

        // Create BufferPoolManager (Size = 50 pages)
        BufferPoolManager* bpm = new BufferPoolManager(50, disk_manager);

        // Create BTreeIndex
        BTreeIndex* btree = new BTreeIndex(bpm);

        // 2. Parse Real Logs
        std::cout << "[2] Parsing Log File: " << LOG_FILE << std::endl;
        std::vector<LogRecord> records = LogParser::parseLogFile(LOG_FILE);
        std::cout << "    -> Parsed " << records.size() << " records." << std::endl;

        if (records.empty()) {
            std::cerr << "[ERROR] No records found in log file. Aborting." << std::endl;
            delete btree;
            delete bpm;
            delete disk_manager;
            return 1;
        }

        // 3. Insert Records
        std::cout << "[3] Inserting records into B+Tree..." << std::endl;
        int success_count = 0;

        for (const auto& rec : records) {
            // Key = Timestamp, Value = LogRecord
            bool res = btree->Insert(rec.timestamp, rec);
            if (res) {
                success_count++;
            }
            else {
                std::cerr << "    [WARN] Failed to insert TS: " << rec.timestamp << std::endl;
            }
        }
        std::cout << "    -> Successfully inserted " << success_count << " records." << std::endl;

        // 4. Verify Data (Point Query)
        std::cout << "[4] Verifying Data..." << std::endl;

        // Check the FIRST record
        LogRecord first_rec = records[0];
        LogRecord result_rec;

        bool found = btree->GetValue(first_rec.timestamp, result_rec);

        if (found) {
            std::cout << "    [PASS] Found FIRST record (TS: " << first_rec.timestamp << ")" << std::endl;
            if (std::string(first_rec.message) == std::string(result_rec.message)) {
                std::cout << "    [PASS] Content matches." << std::endl;
            }
            else {
                std::cout << "    [FAIL] Content mismatch!" << std::endl;
            }
        }
        else {
            std::cout << "    [FAIL] Could not find FIRST record." << std::endl;
        }

        // Check the LAST record
        LogRecord last_rec = records.back();
        found = btree->GetValue(last_rec.timestamp, result_rec);

        if (found) {
            std::cout << "    [PASS] Found LAST record (TS: " << last_rec.timestamp << ")" << std::endl;
        }
        else {
            std::cout << "    [FAIL] Could not find LAST record." << std::endl;
        }

        // 5. Cleanup
        std::cout << "[5] Cleaning up..." << std::endl;
        delete btree;
        delete bpm;
        delete disk_manager;

    }
    catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "           TEST FINISHED                    " << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}