#include "../src/index/btree_index.h"
#include "../src/version/version_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>

using namespace cmse;

// --- Macros ---
#define ASSERT_TRUE(cond, msg) if(!(cond)) { std::cerr << "[FAIL] " << msg << std::endl; std::exit(1); }
#define ASSERT_EQ(v1, v2, msg) if((v1)!=(v2)) { std::cerr << "[FAIL] " << msg << " " << v1 << "!=" << v2 << std::endl; std::exit(1); }

class PersistenceTest {
    std::string db_file = "persist_test.db";
    std::string meta_file = "persist_test.meta";

    LogRecord createRecord(int64_t ts) {
        LogRecord r; r.timestamp = ts; r.pid = 1;
        strncpy_s(r.message, "Test", _TRUNCATE);
        return r;
    }

public:
    void Run() {
        std::cout << "=== PERSISTENCE & RECOVERY TEST ===" << std::endl;

        // Cleanup old run
        remove(db_file.c_str());
        remove(meta_file.c_str());

        // --- STEP 1: INITIALIZE & INSERT ---
        {
            std::cout << "[Step 1] Initializing System..." << std::endl;
            disk::DiskManager disk(db_file);
            bufferpool::BufferPoolManager bpm(100, &disk);
            index::BTreeIndex btree(&bpm);
            VersionManager vm(meta_file, &bpm);

            // Start Transaction 1
            auto txn = vm.BeginTransaction();

            // Insert 100 records
            for (int i = 0; i < 100; i++) {
                btree.InsertCoW(i, createRecord(i), txn);
            }

            // Commit! (Max timestamp = 99)
            vm.Commit(txn, 99,0);

            // Note: txn.pending_root_id is the root we expect to find later
            std::cout << "   -> Committed Root: " << txn.pending_root_id << std::endl;
        }
        // Scope ends -> Managers destroyed -> Memory wiped.
        // Only Disk files remain.

        // --- STEP 2: RESTART (CRASH RECOVERY) ---
        {
            std::cout << "[Step 2] Restarting System..." << std::endl;
            disk::DiskManager disk(db_file);
            bufferpool::BufferPoolManager bpm(100, &disk);
            index::BTreeIndex btree(&bpm);

            // 1. Load Versions
            VersionManager vm(meta_file, &bpm);

            // Verify Metadata
            ASSERT_EQ(vm.GetLatestVersionId(), 1, "Should be Version 1");
            ASSERT_EQ(vm.GetLastCommittedLogTimestamp(), 99, "Should remember MaxTS 99");

            page_id_t recovered_root = vm.GetLatestRootPageId();
            ASSERT_TRUE(recovered_root != INVALID_PAGE_ID, "Root should be valid");

            // 2. Load BTree from Recovered Root
            btree.SetRootPageId(recovered_root);

            // 3. Verify Data Access
            LogRecord res;
            bool found_0 = btree.GetValue(0, res);
            bool found_99 = btree.GetValue(99, res);
            bool found_100 = btree.GetValue(100, res);

            ASSERT_TRUE(found_0, "Lost Key 0 after restart");
            ASSERT_TRUE(found_99, "Lost Key 99 after restart");
            ASSERT_TRUE(!found_100, "Found key 100 which never existed");

            std::cout << "[PASS] Recovery Successful!" << std::endl;
        }

        // --- STEP 3: RESUME IMPORT ---
        {
            std::cout << "[Step 3] Resuming Import..." << std::endl;
            disk::DiskManager disk(db_file);
            bufferpool::BufferPoolManager bpm(100, &disk);
            index::BTreeIndex btree(&bpm);
            VersionManager vm(meta_file, &bpm);

            // Start Txn 2 (Should automatically pick up V1 root)
            auto txn = vm.BeginTransaction();
            ASSERT_EQ(txn.version_id, 2, "Should start Version 2");
            ASSERT_EQ(txn.pending_root_id, vm.GetLatestRootPageId(), "Should inherit V1 Root");

            // Add more data
            btree.InsertCoW(100, createRecord(100), txn);
            vm.Commit(txn, 100,0);

            // Verify
            btree.SetRootPageId(vm.GetLatestRootPageId());
            LogRecord res;
            ASSERT_TRUE(btree.GetValue(0, res), "V2 missing old data");
            ASSERT_TRUE(btree.GetValue(100, res), "V2 missing new data");

            std::cout << "[PASS] Resume Successful!" << std::endl;
        }
    }
};

int main() {
    PersistenceTest t;
    t.Run();
    return 0;
}