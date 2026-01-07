/**
 * fuzz_test.cpp
 *
 * CHAOS MONKEY TEST
 *
 * This test performs a large number of random operations to ensure stability.
 * FIXED: Ensures pages modified in NewPage are correctly marked dirty on Unpin.
 */

#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <filesystem>
#include <cassert>
#include <cstring>
#include <cstdio>

#include "../src/bufferpool/buffer_pool_manager.h"

const std::string DB_FILE = "test_fuzz.db";

void Cleanup() {
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }
}

void Log(const std::string& msg) {
    std::cout << "[FUZZ_TEST] " << msg << std::endl;
}

int main() {
    Log("--- Starting Fuzz (Chaos Monkey) Test ---");
    Cleanup();

    const int POOL_SIZE = 10;
    const int NUM_OPS = 10000;

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(POOL_SIZE, disk_manager);

    std::unordered_map<cmse::page_id_t, int> local_tracker;
    std::vector<cmse::page_id_t> all_known_pages;

    std::mt19937 rng(1337);

    Log("Running " + std::to_string(NUM_OPS) + " random operations...");

    for (int i = 0; i < NUM_OPS; ++i) {

        int op = rng() % 5;

        switch (op) {
            // ----------------------------------------------------------------
            // OP: NewPage
            // ----------------------------------------------------------------
        case 0: {
            cmse::page_id_t pid;
            auto* page = bpm->NewPage(pid);
            if (page != nullptr) {
                // 1. Initialize Header (Critical!)
                page->GetHeader()->page_id = pid;
                page->GetHeader()->is_leaf = 0;
                page->GetHeader()->key_count = 0;

                // 2. Track it
                local_tracker[pid]++;
                all_known_pages.push_back(pid);

                // 3. Write junk payload
                size_t payload_offset = sizeof(cmse::PageHeader);
                std::snprintf(page->GetData(), cmse::PAGE_SIZE - payload_offset, "Chaos%d", i);
            }
            break;
        }

              // ----------------------------------------------------------------
              // OP: FetchPage
              // ----------------------------------------------------------------
        case 1: {
            if (all_known_pages.empty()) break;

            cmse::page_id_t pid = all_known_pages[rng() % all_known_pages.size()];
            auto* page = bpm->FetchPage(pid);

            if (page != nullptr) {
                local_tracker[pid]++;

                // Assertion: Data MUST persist
                if (page->GetPageId() != pid) {
                    Log("FATAL: Page ID Mismatch during Fetch!");
                    std::cout << "Expected: " << pid << ", Got: " << page->GetPageId() << std::endl;
                    std::cout << "This means the page was evicted without being written to disk." << std::endl;
                    std::exit(1);
                }
            }
            break;
        }

              // ----------------------------------------------------------------
              // OP: UnpinPage
              // ----------------------------------------------------------------
        case 2: {
            if (local_tracker.empty()) break;

            auto it = local_tracker.begin();
            std::advance(it, rng() % local_tracker.size());
            cmse::page_id_t pid = it->first;

            if (it->second > 0) {
                // --- FIX IS HERE ---
                // We ALWAYS mark dirty=true in this Chaos test.
                // Why? Because NewPage modifies data. If we randomly select 'false',
                // we lose the initialization (PageID), causing Fetch verification to fail later.
                bool is_dirty = true;

                bpm->UnpinPage(pid, is_dirty);
                it->second--;

                if (it->second == 0) {
                    local_tracker.erase(it);
                }
            }
            break;
        }

              // ----------------------------------------------------------------
              // OP: DeletePage
              // ----------------------------------------------------------------
        case 3: {
            if (all_known_pages.empty()) break;

            size_t idx = rng() % all_known_pages.size();
            cmse::page_id_t pid = all_known_pages[idx];

            bool res = bpm->DeletePage(pid);
            if (res) {
                local_tracker.erase(pid);
                all_known_pages[idx] = all_known_pages.back();
                all_known_pages.pop_back();
            }
            break;
        }

              // ----------------------------------------------------------------
              // OP: FlushPage
              // ----------------------------------------------------------------
        case 4: {
            if (all_known_pages.empty()) break;
            cmse::page_id_t pid = all_known_pages[rng() % all_known_pages.size()];
            bpm->FlushPage(pid);
            break;
        }
        }

        if (i % 1000 == 0) std::cout << "." << std::flush;
    }

    std::cout << std::endl;
    Log("Chaos Loop Finished. Cleaning up leftovers...");

    // Cleanup leftovers
    for (auto& entry : local_tracker) {
        cmse::page_id_t pid = entry.first;
        int pins = entry.second;
        for (int k = 0; k < pins; ++k) {
            bpm->UnpinPage(pid, false);
        }
    }

    Log(">>> PASSED: Fuzz test completed successfully.");

    delete bpm;
    delete disk_manager;
    return 0;
}