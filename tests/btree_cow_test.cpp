#include "../src/index/btree_index.h"
#include "../src/version/transaction_context.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <unordered_set>

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

class BTreeCoWTest {
    disk::DiskManager* disk;
    bufferpool::BufferPoolManager* bpm;
    index::BTreeIndex* btree;
    std::string db_file;

    LogRecord createRecord(int64_t ts) {
        LogRecord r;
        r.timestamp = ts;
        r.priority = 1;
        r.pid = 100;
        std::string msg = "Rec " + std::to_string(ts);
        strncpy_s(r.message, msg.c_str(), _TRUNCATE);
        return r;
    }

public:
    BTreeCoWTest() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        db_file = "cow_test_" + std::to_string(now) + ".db";
        disk = new disk::DiskManager(db_file);
        bpm = new bufferpool::BufferPoolManager(100, disk);
        btree = new index::BTreeIndex(bpm);
    }

    ~BTreeCoWTest() {
        delete btree;
        delete bpm;
        delete disk;
        remove(db_file.c_str());
    }

    void RunAllTests() {
        std::cout << "=== COPY-ON-WRITE TESTS ===" << std::endl;

        Test1_BasicShadowing();
        Test2_BatchReuse();
        Test3_SnapshotIsolation();

        std::cout << "=== ALL CoW TESTS PASSED ===" << std::endl;
    }

    // Test 1: Verify that inserting into V2 creates a NEW Root and leaves V1 alone.
    void Test1_BasicShadowing() {
        std::cout << "[Test 1] Basic Shadowing..." << std::endl;

        // 1. Setup Version 1 (Standard Insert)
        for (int i = 0; i < 5; i++) {
            btree->Insert(i * 10, createRecord(i * 10));
        }
        page_id_t root_v1 = btree->GetRootPageId();
        std::cout << "   -> Version 1 Root: " << root_v1 << std::endl;

        // 2. Start Transaction for Version 2
        TransactionContext txn;
        txn.version_id = 2;
        txn.pending_root_id = root_v1; // Start from V1

        // 3. InsertCoW (Should create shadows)
        bool success = btree->InsertCoW(999, createRecord(999), txn);
        ASSERT_TRUE(success, "InsertCoW failed");

        page_id_t root_v2 = txn.pending_root_id;
        std::cout << "   -> Version 2 Root: " << root_v2 << std::endl;

        // 4. Assert Roots are DIFFERENT
        ASSERT_TRUE(root_v1 != root_v2, "CoW failed! Root ID did not change.");
        ASSERT_TRUE(root_v2 > root_v1, "New Root ID should be higher (allocated later).");

        std::cout << "[PASS] New Root Created." << std::endl;
    }

    // Test 2: Verify that multiple inserts in ONE transaction reuse the same shadow pages.
    void Test2_BatchReuse() {
        std::cout << "[Test 2] Batch Shadow Reuse..." << std::endl;

        // Setup V1
        delete btree; delete bpm; delete disk; // Reset
        disk = new disk::DiskManager(db_file);
        bpm = new bufferpool::BufferPoolManager(100, disk);
        btree = new index::BTreeIndex(bpm);

        for (int i = 0; i < 100; i++) btree->Insert(i, createRecord(i));
        page_id_t root_v1 = btree->GetRootPageId();

        // Start Txn
        TransactionContext txn;
        txn.version_id = 2;
        txn.pending_root_id = root_v1;

        // Insert 10 records. 
        // Logic: First insert copies Root -> Root'.
        // Next 9 inserts should USE Root' and NOT create Root'', Root''', etc.

        size_t initial_created_count = 0;

        for (int i = 200; i < 210; i++) {
            btree->InsertCoW(i, createRecord(i), txn);
            if (i == 200) initial_created_count = txn.created_pages.size();
        }

        std::cout << "   -> Pages created after 1st insert: " << initial_created_count << std::endl;
        std::cout << "   -> Pages created after 10th insert: " << txn.created_pages.size() << std::endl;

        // We expect the count to stay roughly the same (maybe +1 if a leaf split happened),
        // but definitely NOT +30 (which would happen if we copied the path every time).
        ASSERT_TRUE(txn.created_pages.size() < (initial_created_count + 5),
            "Shadow Reuse Failed! Too many pages created.");

        std::cout << "[PASS] Batch Reuse working." << std::endl;
    }

    // Test 3: The "Time Travel" Test. 
    // Can we read V1 data from Root V1 and V2 data from Root V2?
    void Test3_SnapshotIsolation() {
        std::cout << "[Test 3] Snapshot Isolation..." << std::endl;

        // Setup: V1 has Keys [10, 20, 30]
        btree->Insert(10, createRecord(10));
        btree->Insert(20, createRecord(20));
        btree->Insert(30, createRecord(30));
        page_id_t root_v1 = btree->GetRootPageId();

        // Transaction: V2 adds Key [40] and [50]
        TransactionContext txn;
        txn.version_id = 2;
        txn.pending_root_id = root_v1;

        btree->InsertCoW(40, createRecord(40), txn);
        btree->InsertCoW(50, createRecord(50), txn);
        page_id_t root_v2 = txn.pending_root_id;

        // --- VERIFY V1 (Old World) ---
        // We must manually swap the root in the BTree to "Time Travel"
        // (In a real system, we'd pass the root_id to FindLeaf, but here we hack it)
        btree->SetRootPageId(root_v1);

        LogRecord res;
        bool found_10 = btree->GetValue(10, res);
        bool found_40 = btree->GetValue(40, res);

        ASSERT_TRUE(found_10, "V1 should contain 10");
        ASSERT_TRUE(!found_40, "V1 should NOT contain 40 (Future Data)");

        // --- VERIFY V2 (New World) ---
        btree->SetRootPageId(root_v2);

        found_10 = btree->GetValue(10, res); // Should still exist (Shared page)
        found_40 = btree->GetValue(40, res); // Should exist now

        ASSERT_TRUE(found_10, "V2 should inherit 10 from V1");
        ASSERT_TRUE(found_40, "V2 should contain 40");

        std::cout << "[PASS] Time Travel Successful." << std::endl;
    }
};

int main() {
    BTreeCoWTest test;
    test.RunAllTests();
    return 0;
}