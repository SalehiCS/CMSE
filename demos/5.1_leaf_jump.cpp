/**
 * Test Case 5.1: The "Leaf Jump"
 * --------------------------------------------------------------------------------------
 * OBJECTIVE: Verify Iterator traversal across leaf boundaries.
 * FIX: Adjusted loop logic to correctly count the final record.
 * Updated to use safe string copy functions.
 * --------------------------------------------------------------------------------------
 */

#define _ALLOW_KEYWORD_MACROS
#define private public
#define protected public

#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/index/btree_iterator.h"
#include "../src/common/types.h"

#undef private
#undef protected

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <cstring>

using namespace cmse;

const std::string DB_FILE = "leaf_jump.db";
const int RECORD_COUNT = 60;

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n  " << msg << "\n======================================================\n";
}

int main() {
    std::remove(DB_FILE.c_str());

    auto disk = std::make_unique<disk::DiskManager>(DB_FILE);
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
    auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

    PrintBanner("PHASE 1: POPULATE TREE");
    for (int i = 0; i < RECORD_COUNT; i++) {
        LogRecord rec;
        rec.timestamp = i;
        rec.priority = 1;

        // SAFE COPY: Use strncpy_s with truncation to prevent buffer overflows
        strncpy_s(rec.source, sizeof(rec.source), "jump_test", _TRUNCATE);
        strncpy_s(rec.message, sizeof(rec.message), "payload", _TRUNCATE);

        btree->Insert(i, rec);
    }
    std::cout << "[Setup] Insertion Complete." << std::endl;

    PrintBanner("PHASE 2: ITERATOR SCAN");

    auto it = btree->Begin(0);
    // Access internal PageGuard via invasive macro trick
    page_id_t current_leaf_id = it.curr_guard_.Get()->GetPageId();

    int records_on_page = 0;
    int total_scanned = 0;

    std::cout << "[Iterator] Started at Leaf " << current_leaf_id << std::endl;

    while (!it.IsEnd()) {
        const LogRecord& rec = *it;

        // 1. Verify Data
        if (rec.timestamp != total_scanned) {
            std::cout << "\033[1;31m[FAIL]\033[0m Data Mismatch! Expected "
                << total_scanned << ", got " << rec.timestamp << std::endl;
            return 1;
        }

        // 2. Increment Counts (We are sitting on a valid record now)
        records_on_page++;
        total_scanned++;

        // 3. Move Forward
        ++it;

        // 4. Check status AFTER move
        if (it.IsEnd()) {
            std::cout << "    -> Scanned " << records_on_page << " records on Leaf " << current_leaf_id << "." << std::endl;
            break;
        }

        page_id_t new_leaf_id = it.curr_guard_.Get()->GetPageId();

        if (new_leaf_id != current_leaf_id) {
            std::cout << "    -> Scanned " << records_on_page << " records on Leaf " << current_leaf_id << "." << std::endl;

            std::cout << "\033[1;33m[Iterator] Jumping from Leaf " << current_leaf_id
                << " -> Leaf " << new_leaf_id << "\033[0m" << std::endl;

            current_leaf_id = new_leaf_id;
            records_on_page = 0;
        }
    }

    PrintBanner("PHASE 3: VERIFICATION");
    if (total_scanned == RECORD_COUNT) {
        std::cout << "[Check] Scanned " << total_scanned << " / " << RECORD_COUNT << " records. \033[1;32m[PASS]\033[0m" << std::endl;
    }
    else {
        std::cout << "[Check] Missing records! Scanned " << total_scanned << ". \033[1;31m[FAIL]\033[0m" << std::endl;
    }

    std::remove(DB_FILE.c_str());
    return 0;
}