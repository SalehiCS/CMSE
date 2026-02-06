/**
 * Test Case 1.3: Statistics & Pruning Visualizer
 * * OBJECTIVE:
 * 1. Demonstrate 'updateStatistics' by showing Density changes.
 * 2. Demonstrate 'shouldSkip' by running hypothetical range queries.
 * * FIX: Explicitly wraps raw Page* into PageGuard for RAII safety.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>

#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/adapter/btree_adapter.h" 
#include "../src/common/types.h"
#include "../src/utils/log_manager.h"
#include "../src/bufferpool/page_guard.h" // Ensure PageGuard is included

using namespace cmse;

// --- HELPER: PRINT METADATA ---
void PrintNodeMetadata(adapter::BTreeAdapter& adapter, page_id_t page_id, bufferpool::BufferPoolManager* bpm) {
    // 1. Fetch raw pointer
    Page* raw_page = bpm->FetchPage(page_id);

    // 2. Wrap in RAII Guard (Automatically Unpins on return)
    PageGuard guard(bpm, raw_page);

    // 3. Access Data (guard-> acts like a pointer to Page)
    auto* header = reinterpret_cast<adapter::BPlusNodeHeader*>(guard->GetData());

    std::cout << "\n[Metadata] Node " << page_id << ":" << std::endl;
    std::cout << "  Range:       [" << header->min_key << " - " << header->max_key << "]" << std::endl;
    std::cout << "  Total Keys:  " << header->total_keys << std::endl;
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Density:     " << header->density << std::endl;
}

// --- HELPER: VISUALIZE PRUNING ---
void TestPruning(adapter::BTreeAdapter& adapter, page_id_t page_id, int64_t q_min, int64_t q_max, bufferpool::BufferPoolManager* bpm) {
    // 1. Fetch raw pointer
    Page* raw_page = bpm->FetchPage(page_id);

    // 2. Wrap in RAII Guard
    PageGuard guard(bpm, raw_page);

    // 3. Use guard.Get() to pass the raw Page* to the adapter
    bool skip = adapter.shouldSkip(guard.Get(), q_min, q_max);

    std::cout << "  Query [" << std::setw(4) << q_min << " - " << std::setw(4) << q_max << "] ";
    if (skip) {
        std::cout << " -> \033[1;31m🔴 SKIP\033[0m  (Pruned by Metadata)" << std::endl;
    }
    else {
        std::cout << " -> \033[1;32m🟢 ENTER\033[0m (Range Overlap)" << std::endl;
    }
}

int main() {
    // 1. SETUP
    std::string db_file = "test_stats_pruning.db";
    std::remove(db_file.c_str());

    auto disk = std::make_unique<disk::DiskManager>(db_file);
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(50, disk.get());
    auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

    // Instantiate the Adapter
    adapter::BTreeAdapter adapter;

    auto logs = utils::LogManager::generateSyntheticLogs(20);

    // --- PHASE 1: updateStatistics ---
    std::cout << "======================================================" << std::endl;
    std::cout << " PHASE 1: DENSITY & METADATA UPDATE" << std::endl;
    std::cout << "======================================================" << std::endl;

    std::cout << "Inserting sequential keys [10, 11, 12, 13, 14]..." << std::endl;
    for (int i = 10; i <= 14; i++) btree->Insert(i, logs[0]);

    PrintNodeMetadata(adapter, btree->GetRootPageId(), bpm.get());

    std::cout << "\nInserting distant outlier: Key 500..." << std::endl;
    btree->Insert(500, logs[0]);

    PrintNodeMetadata(adapter, btree->GetRootPageId(), bpm.get());
    std::cout << "[Analysis] Notice how Density dropped as the range expanded." << std::endl;

    // --- PHASE 2: shouldSkip ---
    std::cout << "\n======================================================" << std::endl;
    std::cout << " PHASE 2: PRUNING LOGIC (shouldSkip)" << std::endl;
    std::cout << "======================================================" << std::endl;

    page_id_t root = btree->GetRootPageId();

    TestPruning(adapter, root, 0, 5, bpm.get());     // Should Skip
    TestPruning(adapter, root, 12, 100, bpm.get());  // Should Enter
    TestPruning(adapter, root, 600, 700, bpm.get()); // Should Skip

    std::remove(db_file.c_str());
    return 0;
}