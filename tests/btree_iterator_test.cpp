#include "../src/index/btree_index.h"
#include "../src/index/btree_iterator.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>

using namespace cmse;

// --- Helper Macros ---
#define ASSERT_EQ(val1, val2, msg) \
    if ((val1) != (val2)) { \
        std::cerr << "[FAIL] " << msg << " | Expected: " << (val2) << ", Got: " << (val1) << std::endl; \
        std::exit(1); \
    }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << std::endl; \
        std::exit(1); \
    }

class BTreeIteratorTest {
    disk::DiskManager* disk;
    bufferpool::BufferPoolManager* bpm;
    BTreeIndex* btree;
    std::string db_file = "iterator_test.db";
    std::string meta_file = "iterator_test.meta";

    // Helper to create a dummy record
    LogRecord createRecord(int64_t ts) {
        LogRecord r;
        r.timestamp = ts;
        r.priority = 1;
        r.pid = 100;
        std::string msg = "Log entry " + std::to_string(ts);
        strncpy_s(r.message, msg.c_str(), _TRUNCATE);
        return r;
    }

public:
    BTreeIteratorTest() {
        // Clean up old files
        remove(db_file.c_str());
        remove(meta_file.c_str());

        disk = new disk::DiskManager(db_file);
        bpm = new bufferpool::BufferPoolManager(100, disk); // Small pool to force swapping
        btree = new BTreeIndex(bpm);
    }

    ~BTreeIteratorTest() {
        delete btree;
        delete bpm;
        delete disk;
    }

    void RunAllTests() {
        std::cout << "Starting BTreeIterator Tests..." << std::endl;

        SetupData();
        TestFullScan();
        TestRangeStart();
        TestEmptyIterator();

        std::cout << "All BTreeIterator tests passed successfully!" << std::endl;
    }

private:
    void SetupData() {
        std::cout << "   -> Populating B+Tree with 200 records..." << std::endl;
        // 200 records ensures multiple leaf pages (since max keys per leaf is ~14)
        for (int i = 0; i < 200; i++) {
            btree->Insert(i * 10, createRecord(i * 10));
        }
    }

    void TestFullScan() {
        std::cout << "   -> Testing Full Scan (0 to End)..." << std::endl;

        // Start iterator from the very beginning (Key 0)
        auto it = btree->Begin(0);
        int count = 0;
        int64_t expected_ts = 0;

        while (!it.IsEnd()) {
            LogRecord& rec = it.Current();

            // Verify Order and Integrity
            ASSERT_EQ(rec.timestamp, expected_ts, "Iterator returned wrong timestamp sequence");

            ++it; // Move next
            count++;
            expected_ts += 10;
        }

        ASSERT_EQ(count, 200, "Iterator did not visit all records");
        std::cout << "[OK] Full Scan Passed" << std::endl;
    }

    void TestRangeStart() {
        std::cout << "   -> Testing Range Start (Start from 1000)..." << std::endl;

        // Start from key 1000 (which is index 100 in our loop of i*10)
        int64_t start_key = 1000;
        auto it = btree->Begin(start_key);

        ASSERT_TRUE(!it.IsEnd(), "Iterator should not be empty for key 1000");
        ASSERT_EQ(it.Current().timestamp, 1000, "Iterator did not start at the correct key");

        int count = 0;
        while (!it.IsEnd()) {
            ++it;
            count++;
        }

        // We expect records from 1000 to 1990 (100 records total)
        ASSERT_EQ(count, 100, "Iterator Range count incorrect");

        std::cout << "[OK] Range Start Passed" << std::endl;
    }

    void TestEmptyIterator() {
        std::cout << "   -> Testing Out-of-Bounds Iterator..." << std::endl;

        // Try to start AFTER the last key (Max is 1990)
        auto it = btree->Begin(99999);

        ASSERT_TRUE(it.IsEnd(), "Iterator should be END for out-of-bounds key");

        std::cout << "[OK] Empty Iterator Passed" << std::endl;
    }
};

int main() {
    BTreeIteratorTest tester;
    tester.RunAllTests();
    return 0;
}