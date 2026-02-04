#include "../src/index/btree_index.h"
#include "../src/version/transaction_context.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <random>
#include <set>
#include <algorithm>

using namespace cmse;

// --- Macros ---
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

class BTreeCoWStressTest {
    disk::DiskManager* disk;
    bufferpool::BufferPoolManager* bpm;
    index::BTreeIndex* btree;
    std::string db_file;

    LogRecord createRecord(int64_t ts) {
        LogRecord r;
        r.timestamp = ts;
        r.priority = 1;
        r.pid = 100;
        std::string msg = "Data " + std::to_string(ts);
        strncpy_s(r.message, msg.c_str(), _TRUNCATE);
        return r;
    }

public:
    BTreeCoWStressTest() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        db_file = "cow_stress_" + std::to_string(now) + ".db";
        disk = new disk::DiskManager(db_file);
        bpm = new bufferpool::BufferPoolManager(500, disk); // Larger pool for stress
        btree = new index::BTreeIndex(bpm);
    }

    ~BTreeCoWStressTest() {
        delete btree; delete bpm; delete disk;
        remove(db_file.c_str());
    }

    void RunAllTests() {
        std::cout << "=== CoW STRESS TESTS ===" << std::endl;
        TestBranchingHistories();
        TestDeepSplits();
        TestRandomTorture();
        std::cout << "=== ALL STRESS TESTS PASSED ===" << std::endl;
    }

    // --- Scenario 1: Divergent Branches ---
    void TestBranchingHistories() {
        std::cout << "[Test 1] Divergent Branches (Git Style)..." << std::endl;

        // Base: V1 has {10, 20}
        btree->Insert(10, createRecord(10));
        btree->Insert(20, createRecord(20));
        page_id_t root_v1 = btree->GetRootPageId();

        // Branch A (From V1): Add {30}
        TransactionContext txnA;
        txnA.version_id = 2;
        txnA.pending_root_id = root_v1;
        btree->InsertCoW(30, createRecord(30), txnA);
        page_id_t root_branchA = txnA.pending_root_id;

        // Branch B (From V1): Add {40} -- CRITICAL: Start from root_v1 again!
        TransactionContext txnB;
        txnB.version_id = 3;
        txnB.pending_root_id = root_v1;
        btree->InsertCoW(40, createRecord(40), txnB);
        page_id_t root_branchB = txnB.pending_root_id;

        // Verify Branch A (Should have 10, 20, 30. NOT 40)
        btree->SetRootPageId(root_branchA);
        LogRecord r;
        ASSERT_TRUE(btree->GetValue(30, r), "Branch A missing 30");
        ASSERT_TRUE(!btree->GetValue(40, r), "Branch A corrupted by Branch B (Found 40)");

        // Verify Branch B (Should have 10, 20, 40. NOT 30)
        btree->SetRootPageId(root_branchB);
        ASSERT_TRUE(btree->GetValue(40, r), "Branch B missing 40");
        ASSERT_TRUE(!btree->GetValue(30, r), "Branch B corrupted by Branch A (Found 30)");

        std::cout << "[PASS] Branching Logic Perfect." << std::endl;
    }

    // --- Scenario 2: Deep Splits (Bubble Up) ---
    void TestDeepSplits() {
        std::cout << "[Test 2] Deep Split Propagation..." << std::endl;

        // Reset DB
        ResetDB();

        // 1. Build a Base Tree (V1) that is large enough to be 2 levels deep
        // Assuming Order ~100, insert 500 keys.
        int count = 500;
        for (int i = 0; i < count; i++) {
            btree->Insert(i, createRecord(i));
        }
        page_id_t root_v1 = btree->GetRootPageId();

        // 2. Start V2 transaction
        TransactionContext txn;
        txn.version_id = 2;
        txn.pending_root_id = root_v1;

        // 3. Insert KEY 9999 (Far end) to force right-side splits
        // 4. Insert KEY -1 (Far left) to force left-side splits
        // 5. Insert KEY 250 (Middle) to force internal splits
        btree->InsertCoW(9999, createRecord(9999), txn);
        btree->InsertCoW(-1, createRecord(-1), txn);
        btree->InsertCoW(250, createRecord(250), txn); // Duplicate key logic? usually overwrite or ignore.

        page_id_t root_v2 = txn.pending_root_id;

        // Verify V1 is untouched
        btree->SetRootPageId(root_v1);
        LogRecord r;
        ASSERT_TRUE(!btree->GetValue(9999, r), "V1 leaked new data");

        // Verify V2 has new data
        btree->SetRootPageId(root_v2);
        ASSERT_TRUE(btree->GetValue(9999, r), "V2 missing split data");
        ASSERT_TRUE(btree->GetValue(-1, r), "V2 missing left split data");

        // Verify V2 still has old data
        ASSERT_TRUE(btree->GetValue(100, r), "V2 lost old data");

        std::cout << "[PASS] Deep Splits handled correctly." << std::endl;
    }

    // --- Scenario 3: The Random Torture Test ---
    void TestRandomTorture() {
        std::cout << "[Test 3] Random Torture (1000 Keys)..." << std::endl;
        ResetDB();

        // 1. Base V1: 500 Even numbers
        std::vector<int64_t> v1_keys;
        for (int i = 0; i < 1000; i += 2) {
            btree->Insert(i, createRecord(i));
            v1_keys.push_back(i);
        }
        page_id_t root_v1 = btree->GetRootPageId();

        // 2. CoW V2: Insert 500 ODD numbers
        TransactionContext txn;
        txn.version_id = 2;
        txn.pending_root_id = root_v1;

        std::vector<int64_t> v2_keys;
        for (int i = 1; i < 1000; i += 2) {
            btree->InsertCoW(i, createRecord(i), txn);
            v2_keys.push_back(i);
        }
        page_id_t root_v2 = txn.pending_root_id;

        // 3. VERIFY V1 (Must ONLY have Evens)
        btree->SetRootPageId(root_v1);
        LogRecord r;
        for (int64_t k : v1_keys) ASSERT_TRUE(btree->GetValue(k, r), "V1 lost Key " + std::to_string(k));
        for (int64_t k : v2_keys) ASSERT_TRUE(!btree->GetValue(k, r), "V1 leaked Key " + std::to_string(k));

        // 4. VERIFY V2 (Must have Evens AND Odds)
        btree->SetRootPageId(root_v2);
        for (int64_t k : v1_keys) ASSERT_TRUE(btree->GetValue(k, r), "V2 lost inherited Key " + std::to_string(k));
        for (int64_t k : v2_keys) ASSERT_TRUE(btree->GetValue(k, r), "V2 missing new Key " + std::to_string(k));

        std::cout << "[PASS] Torture Test Survived." << std::endl;
    }

private:
    void ResetDB() {
        delete btree; delete bpm; delete disk;
        remove(db_file.c_str()); // Clean file

        // New unique file just to be safe from OS locking
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        db_file = "cow_stress_" + std::to_string(now) + ".db";

        disk = new disk::DiskManager(db_file);
        bpm = new bufferpool::BufferPoolManager(500, disk);
        btree = new index::BTreeIndex(bpm);
    }
};

int main() {
    BTreeCoWStressTest test;
    test.RunAllTests();
    return 0;
}