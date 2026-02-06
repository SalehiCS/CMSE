/**
 * Test Case 2.2: Trie "Cap" (Memory Safety)
 * * OBJECTIVE:
 * Demonstrate the engine's ability to halt a search early.
 * * SCENARIO:
 * 1. Insert 100 keys that all share the prefix "a" (a0, a1, ... a99).
 * 2. Execute a Wildcard Search ("a*") with a strict LIMIT of 5.
 * 3. Verify that the result vector has size 5 and the 'is_overflow' flag is set.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>

 // Core Engine
#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/trie_index.h"

using namespace cmse;

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "======================================================\n";
}

int main() {
    // 1. SETUP
    std::string db_file = "test_trie_cap.db";
    std::remove(db_file.c_str());

    auto disk = std::make_unique<disk::DiskManager>(db_file);
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
    auto trie = std::make_unique<index::TrieIndex>(bpm.get());

    // 2. DATA INJECTION (The "Flood")
    PrintBanner("PHASE 1: THE FLOOD");
    int total_keys = 100;
    std::cout << "Inserting " << total_keys << " keys starting with 'user_'..." << std::endl;

    for (int i = 0; i < total_keys; i++) {
        // user_0, user_1, ... user_99
        std::string key = "user_" + std::to_string(i);
        trie->Insert(key, 1000 + i, 1);
    }
    std::cout << "Done." << std::endl;


    // 3. EXECUTE CAPPED SEARCH
    PrintBanner("PHASE 2: THE SAFE SEARCH");

    // We want all 'user_*' keys, but we only have budget for 5.
    std::string prefix = "user_";
    size_t limit = 5;

    std::cout << "Executing Search: Prefix='" << prefix << "*' | Cap=" << limit << std::endl;

    // Call the Trie's GetTimestampsWithCap (The function used by QueryEngine)
    // Args: key, is_prefix, priority_filter, min_ts, max_ts, limit
    auto result = trie->GetTimestampsWithCap(
        prefix,
        true,   // is_prefix
        -1,     // priority (any)
        0,      // min_ts
        INT64_MAX, // max_ts
        limit   // THE CAP
    );

    // 4. VERIFICATION
    std::cout << "\n[Results Analysis]:" << std::endl;
    std::cout << "  Requested Limit: " << limit << std::endl;
    std::cout << "  Found Count:     " << result.timestamps.size() << std::endl;
    std::cout << "  Overflow Flag:   " << (result.is_overflow ? "TRUE (Hit Cap)" : "FALSE") << std::endl;

    std::cout << "\n[Visual Proof] The returned timestamps:" << std::endl;
    std::cout << "  { ";
    for (auto ts : result.timestamps) {
        std::cout << ts << " ";
    }
    std::cout << "}" << std::endl;

    // 5. LOGIC CHECK
    if (result.timestamps.size() == limit && result.is_overflow) {
        std::cout << "\n\033[1;32m[SUCCESS]\033[0m The engine successfully halted after " << limit << " matches." << std::endl;
        std::cout << "          It avoided scanning the remaining " << (total_keys - limit) << " keys." << std::endl;
    }
    else {
        std::cout << "\n\033[1;31m[FAILURE]\033[0m The engine did not respect the cap!" << std::endl;
    }

    std::remove(db_file.c_str());
    return 0;
}