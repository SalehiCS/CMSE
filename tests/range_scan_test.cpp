#include "../src/index/btree_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio> // for std::remove

using namespace cmse;

// Helper function to create a dummy log record
LogRecord create_dummy_record(int64_t timestamp) {
    LogRecord r;
    r.timestamp = timestamp;
    r.priority = 3;
    r.pid = 100 + (int)timestamp; // Fake PID
    std::string msg = "Log at time " + std::to_string(timestamp);
    strncpy_s(r.message, sizeof(r.message), msg.c_str(), _TRUNCATE);
    return r;
}

int main() {
    // 1. Clean up old test files to ensure a fresh start
    std::remove("scan_test.db");

    std::cout << "=== STARTING RANGE SCAN TEST ===\n" << std::endl;

    // 2. Initialize System Components
    // Using a small buffer pool (50 pages) to force disk I/O if necessary
    auto* disk_manager = new disk::DiskManager("scan_test.db");
    auto* bpm = new bufferpool::BufferPoolManager(50, disk_manager);
    auto* btree = new index::BTreeIndex(bpm);

    // 3. Insert Test Data
    // Inserting keys: 10, 20, 30 ... up to 1000.
    // This creates enough data to potentially split nodes and link leaves.
    std::cout << "[Step 1] Inserting 100 records (Keys: 10, 20, ..., 1000)..." << std::endl;
    for (int i = 1; i <= 100; i++) {
        int64_t key = i * 10;
        btree->Insert(key, create_dummy_record(key));
    }

    // 4. Test Scenarios

    // Scenario A: Middle Range Scan
    // Range [35, 75] should match keys: 40, 50, 60, 70
    std::cout << "\n[Step 2] Testing Range [35, 75]..." << std::endl;
    std::vector<LogRecord> res1 = btree->Scan(35, 75);

    std::cout << "   -> Found " << res1.size() << " records." << std::endl;
    for (const auto& rec : res1) {
        std::cout << "      Found Key: " << rec.timestamp << std::endl;
    }

    assert(res1.size() == 4);
    assert(res1[0].timestamp == 40);
    assert(res1[3].timestamp == 70);
    std::cout << "   -> [PASSED] Middle Range OK.\n";


    // Scenario B: Start Range Scan
    // Range [0, 25] should match keys: 10, 20
    std::cout << "\n[Step 3] Testing Start Range [0, 25]..." << std::endl;
    std::vector<LogRecord> res2 = btree->Scan(0, 25);

    std::cout << "   -> Found " << res2.size() << " records." << std::endl;
    assert(res2.size() == 2);
    assert(res2[0].timestamp == 10);
    assert(res2[1].timestamp == 20);
    std::cout << "   -> [PASSED] Start Range OK.\n";


    // Scenario C: Empty Range Scan
    // Range [5000, 6000] contains no keys.
    std::cout << "\n[Step 4] Testing Empty Range [5000, 6000]..." << std::endl;
    std::vector<LogRecord> res3 = btree->Scan(5000, 6000);

    std::cout << "   -> Found " << res3.size() << " records." << std::endl;
    assert(res3.empty());
    std::cout << "   -> [PASSED] Empty Range OK.\n";


    // Scenario D: Multi-Page Scan
    // Range [10, 250]. This covers 25 records (10 to 250 inclusive).
    // This ensures that 'next_leaf_id' traversal is working correctly.
    std::cout << "\n[Step 5] Testing Large Range [10, 250]..." << std::endl;
    std::vector<LogRecord> res4 = btree->Scan(10, 250);

    std::cout << "   -> Found " << res4.size() << " records." << std::endl;

    assert(res4.size() == 25);
    assert(res4.front().timestamp == 10);
    assert(res4.back().timestamp == 250);
    std::cout << "   -> [PASSED] Multi-Page Scan OK.\n";

    // 5. Cleanup
    delete btree;
    delete bpm;
    delete disk_manager;

    std::cout << "\n========================================" << std::endl;
    std::cout << "       ALL SCAN TESTS PASSED!           " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}