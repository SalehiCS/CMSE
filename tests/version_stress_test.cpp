#include "../src/index/btree_index.h"
#include "../src/version/version_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

using namespace cmse;

// --- Macros ---
#define ASSERT_TRUE(cond, msg) if(!(cond)) { std::cerr << "[FAIL] " << msg << std::endl; std::exit(1); }
#define ASSERT_EQ(v1, v2, msg) if((v1)!=(v2)) { std::cerr << "[FAIL] " << msg << " " << v1 << "!=" << v2 << std::endl; std::exit(1); }

class VersionStressTest {
    std::string db_file = "stress_ver.db";
    std::string meta_file = "stress_ver.meta";

    const int NUM_ITERATIONS = 10;
    const int KEYS_PER_BATCH = 500;

    LogRecord createRecord(int64_t ts) {
        LogRecord r; r.timestamp = ts; r.pid = 1;
        strncpy_s(r.message, "Stress", _TRUNCATE);
        return r;
    }

public:
    void Run() {
        std::cout << "=== VERSION & RECOVERY STRESS TEST ===" << std::endl;

        // Clean start
        remove(db_file.c_str());
        remove(meta_file.c_str());

        // --- PHASE 1: The Crash Loop ---
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            std::cout << "[Iter " << iter + 1 << "/" << NUM_ITERATIONS << "] Booting System... ";

            // 1. Simulate "Boot Up" (Constructors open files)
            disk::DiskManager disk(db_file);
            bufferpool::BufferPoolManager bpm(500, &disk);
            index::BTreeIndex btree(&bpm);
            VersionManager vm(meta_file, &bpm);

            // 2. Verify State from Previous Crash
            if (iter > 0) {
                // Should see previous version
                ASSERT_EQ(vm.GetLatestVersionId(), iter, "Lost version after crash!");

                // Verify random key from previous batch exists
                int prev_key = (iter - 1) * KEYS_PER_BATCH + 50;
                btree.SetRootPageId(vm.GetLatestRootPageId());
                LogRecord res;
                ASSERT_TRUE(btree.GetValue(prev_key, res), "Lost data from previous batch!");
            }

            // 3. Start New Work
            auto txn = vm.BeginTransaction();
            int start_key = iter * KEYS_PER_BATCH;
            int end_key = start_key + KEYS_PER_BATCH;

            for (int k = start_key; k < end_key; k++) {
                btree.InsertCoW(k, createRecord(k), txn);
            }

            // 4. Commit & "Crash" (Scope ends immediately after)
            vm.Commit(txn, end_key - 1);

            std::cout << "Committed v" << txn.version_id << " with " << KEYS_PER_BATCH << " keys." << std::endl;
        }

        // --- PHASE 2: Time Travel Audit ---
        std::cout << "\n[Audit] verifying Snapshot Isolation across history..." << std::endl;

        disk::DiskManager disk(db_file);
        bufferpool::BufferPoolManager bpm(500, &disk);
        index::BTreeIndex btree(&bpm);
        VersionManager vm(meta_file, &bpm);

        for (int v = 1; v <= NUM_ITERATIONS; v++) {
            page_id_t root = vm.GetRootForVersion(v);
            ASSERT_TRUE(root != INVALID_PAGE_ID, "Missing Root for Version");

            // Switch BTree view to this historical version
            btree.SetRootPageId(root);

            // Calculate Expected Range
            int expected_max_key = (v * KEYS_PER_BATCH) - 1;
            int future_key = expected_max_key + 1;

            LogRecord res;

            // 1. Verify we can see our own data
            bool see_start = btree.GetValue(0, res);
            bool see_end = btree.GetValue(expected_max_key, res);

            ASSERT_TRUE(see_start, "Version " + std::to_string(v) + " missing Key 0");
            ASSERT_TRUE(see_end, "Version " + std::to_string(v) + " missing MaxKey");

            // 2. Verify we CANNOT see the future
            if (v < NUM_ITERATIONS) {
                bool see_future = btree.GetValue(future_key, res);
                ASSERT_TRUE(!see_future, "Version " + std::to_string(v) + " leaked Future Key!");
            }

            std::cout << "   -> Version " << v << ": Verified (Keys 0-" << expected_max_key << ")" << std::endl;
        }

        std::cout << "\n[PASS] ALL PERSISTENCE TESTS PASSED." << std::endl;
    }
};

int main() {
    VersionStressTest t;
    t.Run();
    return 0;
}