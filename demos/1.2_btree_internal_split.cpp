/**
 * Test Case 1.2: B+Tree Internal Node Split
 * * OBJECTIVE:
 * Demonstrate the "Grandparent" Event (Height Growth).
 * * VISUALIZATION STRATEGY:
 * We use a custom "SmartPrint" function that fetches the Root Page manually.
 * It iterates the internal pointers array and applies a filter (Index < 3 or Index == Last)
 * to prevent screen flooding. It DOES NOT recurse.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>

 // Core Engine Components
#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/adapter/btree_adapter.h" // Required to cast raw page data
#include "../src/common/types.h"
#include "../src/utils/log_manager.h"

using namespace cmse;

// --- CUSTOM VISUALIZER (NON-RECURSIVE) ---
void SmartPrintRoot(index::BTreeIndex* btree, bufferpool::BufferPoolManager* bpm) {
    page_id_t root_id = btree->GetRootPageId();
    if (root_id == INVALID_PAGE_ID) return;

    // 1. Fetch the Raw Page directly from Buffer Pool
    auto guard = bpm->FetchPage(root_id);
    auto* node = reinterpret_cast<adapter::BPlusInternalNode*>(guard->GetData());

    std::cout << "[Visualizer] Root Page " << root_id << " (IsLeaf=" << (node->header.is_leaf ? "YES" : "NO") << ")" << std::endl;

    // 2. If it's a Leaf, just summarize
    if (node->header.is_leaf) {
        std::cout << "             Type: Leaf Node" << std::endl;
        std::cout << "             Keys: " << node->header.key_count << " records." << std::endl;
        return;
    }

    // 3. If it's Internal, print the children nicely
    int count = node->header.key_count + 1;
    std::cout << "             Type: Internal Node (The Router)" << std::endl;
    std::cout << "             Capacity Check: " << count << " children pointers." << std::endl;
    std::cout << "             Structure: ";

    // 4. The "Snip" Logic: Only iterate this single array. No recursion.
    for (int i = 0; i < count; i++) {
        // Condition: First 3 nodes OR the very last node
        if (i < 3 || i == count - 1) {
            std::cout << "[Key " << node->keys[i] << "->Pg" << node->children[i] << "] ";
        }
        // Condition: The 4th node (Trigger the "..." once)
        else if (i == 3) {
            std::cout << "... (" << (count - 4) << " hidden) ... ";
        }
    }
    std::cout << "\n" << std::endl;
}

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "======================================================\n";
}

int main() {
    // 1. SETUP
    std::string db_file = "test_internal_split.db";
    std::remove(db_file.c_str());

    std::cout << "[Setup] Initializing Engine..." << std::endl;
    auto disk = std::make_unique<disk::DiskManager>(db_file);
    // Large pool for deep tree operations
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
    auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

    // 2. MATH
    // We need to fill an Internal Node (max ~254 pointers) to force a split.
    const int TARGET_RECORDS = 2000;
    auto logs = utils::LogManager::generateSyntheticLogs(TARGET_RECORDS + 500);

    // 3. PHASE 1: FILL THE INTERNAL ROOT
    PrintBanner("PHASE 1: FILLING THE INTERNAL ROOT");
    std::cout << "Inserting " << TARGET_RECORDS << " records..." << std::endl;

    for (int i = 0; i < TARGET_RECORDS; ++i) {
        btree->Insert(i * 10, logs[i]);
        if (i > 0 && i % 500 == 0) std::cout << "  ... " << i << " records inserted." << std::endl;
    }

    // 4. VISUALIZE PRE-SPLIT
    PrintBanner("STATE: PRE-SPLIT (Height 2)");

    // Uses our custom non-recursive printer
    SmartPrintRoot(btree.get(), bpm.get());

    std::cout << "[Analysis] The Root holds ~250 pointers. It is full." << std::endl;
    std::cout << "           Inserting more will force it to split into two halves." << std::endl;
    std::cout << "           Press ENTER to trigger Grandparent Creation..." << std::endl;
    std::cin.get();


    // 5. TRIGGER SPLIT
    PrintBanner("ACTION: INSERTING UNTIL OVERFLOW");

    page_id_t old_root = btree->GetRootPageId();
    page_id_t new_root = old_root;
    int inserted_extra = 0;

    // Continue inserting until Root ID changes
    while (new_root == old_root && inserted_extra < 500) {
        int idx = TARGET_RECORDS + inserted_extra;
        btree->Insert(idx * 10, logs[idx % 100]);
        new_root = btree->GetRootPageId();
        inserted_extra++;
    }

    if (new_root == old_root) {
        std::cerr << "[Error] Failed to trigger split!" << std::endl;
        return 1;
    }

    // 6. VISUALIZE POST-SPLIT
    PrintBanner("STATE: POST-SPLIT (Height 3)");

    std::cout << "A NEW Grandparent Root has been created!" << std::endl;

    // Show the new root structure
    SmartPrintRoot(btree.get(), bpm.get());

    std::cout << "Explanation:" << std::endl;
    std::cout << "1. The Grandparent (Pg" << new_root << ") now points to:" << std::endl;
    std::cout << "   - Left Child:  Pg " << old_root << " (The old root)" << std::endl;
    std::cout << "   - Right Child: Pg " << (new_root + 1) << " (The new split sibling)" << std::endl;

    std::remove(db_file.c_str());
    return 0;
}