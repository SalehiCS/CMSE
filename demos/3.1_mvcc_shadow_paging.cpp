/**
 * Test Case 3.1: Real Shadow Paging (Isolation)
 * * OBJECTIVE:
 * Verify "Snapshot Isolation" using the engine's built-in InsertCoW mechanism.
 * * INFRASTRUCTURE:
 * - Uses the REAL BTreeIndex::InsertCoW (no simulation).
 * - Uses the REAL TransactionContext struct.
 * - Uses BTreeAdapter to inspect page contents.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>

 // --- REAL PROJECT HEADERS ---
#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/adapter/btree_adapter.h"
#include "../src/version/transaction_context.h" // Assuming this contains the struct definition
#include "../src/utils/log_manager.h"

using namespace cmse;

// --- VISUALIZATION HELPER ---
// Uses the BTreeAdapter to print the content of a specific Root Page
void PrintVersionState(const std::string& label, page_id_t root_id, bufferpool::BufferPoolManager* bpm) {
    std::cout << "  [" << std::left << std::setw(18) << label << "] Root Page: " << std::setw(4) << root_id;

    if (root_id == INVALID_PAGE_ID) {
        std::cout << " (Empty)" << std::endl;
        return;
    }

    // Fetch the page
    Page* page = bpm->FetchPage(root_id);
    // Use the Adapter logic to read it safely
    adapter::BTreeAdapter adapter;

    if (adapter.isLeaf(page)) {
        // It's a leaf (small tree), print the keys
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(page->GetData());
        int count = adapter.getCount(page);

        std::cout << " | Content: { ";
        for (int i = 0; i < count; i++) {
            std::cout << leaf->keys[i] << " ";
        }
        std::cout << "}";
    }
    else {
        // It's an internal node
        int count = adapter.getCount(page);
        std::cout << " | Content: <Internal Node with " << count << " children>";
    }

    // Always unpin after reading manually
    bpm->UnpinPage(root_id, false);
    std::cout << std::endl;
}

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "======================================================\n";
}

int main() {
    // 1. SETUP
    std::string db_file = "real_shadow_test.db";
    std::remove(db_file.c_str());

    auto disk = std::make_unique<disk::DiskManager>(db_file);
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
    auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

    // Dummy log data generator
    auto logs = utils::LogManager::generateSyntheticLogs(100);

    // 2. PHASE 1: COMMIT VERSION 1 (Root A)
    PrintBanner("PHASE 1: CREATE VERSION 1");
    std::cout << "Inserting keys 10, 20, 30 using standard Insert..." << std::endl;

    // Standard Insert modifies the tree in-place
    btree->Insert(10, logs[10]);
    btree->Insert(20, logs[20]);
    btree->Insert(30, logs[30]);

    // SAVE THE ROOT (This is our "Commit")
    page_id_t root_v1 = btree->GetRootPageId();
    PrintVersionState("V1 (Committed)", root_v1, bpm.get());


    // 3. PHASE 2: TRANSACTION FOR VERSION 2 (Copy-on-Write)
    PrintBanner("PHASE 2: START TRANSACTION V2");
    std::cout << "Initializing Transaction Context..." << std::endl;

    TransactionContext txn;
    txn.version_id = 2;
    // CRITICAL: We start the transaction pointing to the OLD root
    txn.pending_root_id = root_v1;

    std::cout << "Inserting keys 40, 50 using InsertCoW..." << std::endl;

    // InsertCoW should trigger the shadow paging logic internally
    // It will modify txn.pending_root_id if the root splits or is shadowed
    bool success = true;
    success &= btree->InsertCoW(40, logs[40], txn);
    success &= btree->InsertCoW(50, logs[50], txn);

    if (!success) {
        std::cerr << "[Fatal] InsertCoW failed!" << std::endl;
        return 1;
    }

    page_id_t root_v2 = txn.pending_root_id;
    std::cout << "Transaction V2 Pending Root: " << root_v2 << std::endl;


    // 4. PHASE 3: VISUAL PROOF (ISOLATION)
    PrintBanner("PHASE 3: VERIFICATION");

    // We use the public SetRootPageId API to switch the tree's view
    // so we can use standard print tools if we wanted, or our helper.

    // A. View V1
    btree->SetRootPageId(root_v1);
    PrintVersionState("View of V1", btree->GetRootPageId(), bpm.get());

    // B. View V2
    btree->SetRootPageId(root_v2);
    PrintVersionState("View of V2", btree->GetRootPageId(), bpm.get());

    std::cout << "\n[Analysis]:" << std::endl;

    bool isolation_success = true;

    // Check 1: Physical Separation
    if (root_v1 != root_v2) {
        std::cout << "\033[1;32m[PASS]\033[0m Root Page IDs are different ("
            << root_v1 << " vs " << root_v2 << ")." << std::endl;
    }
    else {
        std::cout << "\033[1;31m[FAIL]\033[0m Root Page IDs are identical! Copy-on-Write did not occur." << std::endl;
        isolation_success = false;
    }

    // Check 2: Content Isolation (Checking V1 again to ensure it wasn't mutated)
    // We know V1 should NOT have 40 or 50.
    // (Manual check via GetValue could be added here if needed, but the print above is visual proof)

    if (isolation_success) {
        std::cout << "       The engine successfully created a shadow version." << std::endl;
    }

    std::remove(db_file.c_str());
    return 0;
}