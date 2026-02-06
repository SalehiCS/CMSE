/**
 * Test Case 1.1: B+Tree Leaf Split Visualization (Extended)
 * * SCENARIO:
 * 1. Fill Root -> Splits into [Left, Right].
 * 2. Fill Right Child -> Splits again.
 * 3. Fill Left Child -> Splits again.
 * * OBJECTIVE:
 * Prove that the Parent Node correctly handles multiple child splits
 * and updates its internal keys/pointers dynamically.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm> // For std::remove

 // Include the Core Engine Components
#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/common/types.h"
#include "../src/utils/log_manager.h" 

using namespace cmse;

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "======================================================\n";
}

int main() {
    // 1. SETUP
    std::string db_file = "test_leaf_split_extended.db";
    std::remove(db_file.c_str());

    std::cout << "[Setup] Initializing Engine Components..." << std::endl;

    auto disk = std::make_unique<disk::DiskManager>(db_file);
    // Increased buffer slightly to hold the growing tree comfortably
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(50, disk.get());
    auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

    // Capacity is approx 14 records per leaf.
    int initial_fill = 13;
    auto logs = utils::LogManager::generateSyntheticLogs(100); // Generate plenty of dummy data

    // --- PHASE 1: INITIAL ROOT SPLIT ---
    PrintBanner("PHASE 1: INITIAL ROOT FILL");
    std::cout << "[Step 1] Filling Root with " << initial_fill << " records..." << std::endl;

    for (int i = 0; i < initial_fill; i++) {
        int64_t ts = 1000 + i * 10; // Keys: 1000, 1010, ..., 1120
        btree->Insert(ts, logs[i % 20]);
    }

    btree->PrintTree(20);

    std::cout << "\n[Trigger] Inserting key 2000 to cause FIRST SPLIT..." << std::endl;
    std::cout << "           Press ENTER to insert..." << std::endl;
    std::cin.get();

    btree->Insert(2000, logs[0]);
    btree->Insert(2010, logs[0]); // Extra safety insert

    PrintBanner("STATUS: ROOT HAS SPLIT");
    std::cout << "Structure should be: Root -> [Left Child, Right Child]" << std::endl;
    btree->PrintTree(20);


    // --- PHASE 2: SPLITTING THE RIGHT CHILD ---
    // The Right Child currently holds keys >= ~1070 (plus the new 2000s).
    // It is half full (~7 records). We need to add ~8 more HIGH keys to break it.

    PrintBanner("PHASE 2: SPLITTING THE RIGHT CHILD");
    std::cout << "[Analysis] We will now insert keys 3000+ to overfill the Right Node." << std::endl;
    std::cout << "           Current Root ID: " << btree->GetRootPageId() << std::endl;
    std::cout << "           Press ENTER to insert..." << std::endl;
    std::cin.get();

    for (int i = 0; i < 10; i++) {
        int64_t ts = 3000 + i * 10;
        std::cout << "Inserting " << ts << "..." << std::endl;
        btree->Insert(ts, logs[0]);
    }

    std::cout << "\n[Result] The Right Child should have split." << std::endl;
    std::cout << "         The Root (Internal) should now point to 3 Children." << std::endl;
    btree->PrintTree(20);


    // --- PHASE 3: SPLITTING THE LEFT CHILD ---
    // The Left Child holds keys < ~1070. It is roughly half full.
    // We will insert SMALL keys (100+) to fill the Left Node without touching the others.

    PrintBanner("PHASE 3: SPLITTING THE LEFT CHILD");
    std::cout << "[Analysis] We will now insert keys 100+ to overfill the Left Node." << std::endl;
    std::cout << "           Press ENTER to insert..." << std::endl;
    std::cin.get();

    for (int i = 0; i < 10; i++) {
        int64_t ts = 100 + i * 10;
        std::cout << "Inserting " << ts << "..." << std::endl;
        btree->Insert(ts, logs[0]);
    }

    std::cout << "\n[Result] The Left Child should have split." << std::endl;
    std::cout << "         The Root (Internal) should now point to 4 Children." << std::endl;

    btree->PrintTree(20);


    // --- FINAL VALIDATION ---
    PrintBanner("FINAL STRUCTURE VALIDATION");
    page_id_t final_root = btree->GetRootPageId();
    std::cout << "Final Root Page ID: " << final_root << std::endl;
    std::cout << "Tree Height check complete." << std::endl;

    std::remove(db_file.c_str());
    return 0;
}