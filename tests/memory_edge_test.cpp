/**
 * memory_edge_test.cpp
 *
 * Tests edge cases for memory management in BufferPoolManager.
 *
 * Scenarios:
 * 1. Delete Pinned: Attempting to delete a page that is currently pinned (in use) should FAIL.
 * 2. Delete & Refetch: Verifies that deleting a page removes it from cache.
 * Fetching it again should read from disk (which might be empty if not flushed), not return stale cache.
 * 3. All Pinned (Buffer Full): Filling the buffer with pinned pages and requesting more
 * should handle failure gracefully (return nullptr) without crashing.
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <filesystem>
#include <thread>

#include "../src/bufferpool/buffer_pool_manager.h"

const std::string DB_FILE = "test_memory_edge.db";

// --- Helper: Cleanup DB File ---
void Cleanup() {
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }
}

// --- Helper: Logger ---
void Log(const std::string& msg) {
    std::cout << "[MEMORY_EDGE_TEST] " << msg << std::endl;
}

// =================================================================
// Scenario 1: Delete Page Logic
// Objective: 
//   - Verify DeletePage fails if page is pinned.
//   - Verify DeletePage succeeds if page is unpinned.
//   - Verify Fetching a deleted page (that wasn't flushed) returns fresh/empty data from disk,
//     proving the old cached version is gone.
// =================================================================
void TestDeletePageLogic() {
    Log("\n--- Scenario 1: Delete Page Logic ---");
    Cleanup();

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(5, disk_manager);

    // 1. Create a page and write data
    cmse::page_id_t pid;
    auto* page = bpm->NewPage(pid);

    // Write "Secret Data" to memory
    // Note: We deliberately DO NOT Flush. Disk still contains zeros.
    size_t payload_size = cmse::PAGE_SIZE - sizeof(cmse::PageHeader);
    std::snprintf(page->GetData(), payload_size, "Secret Data");

    Log("Step 1: Created Page " + std::to_string(pid) + " and wrote 'Secret Data' (Memory Only).");

    // 2. Attempt to Delete while Pinned (PinCount = 1 because NewPage pins it)
    bool delete_result = bpm->DeletePage(pid);
    if (delete_result == false) {
        Log("Step 2: PASSED. DeletePage failed because page is pinned.");
    }
    else {
        Log("!!! FAILED: DeletePage succeeded on a pinned page!");
        exit(1);
    }

    // 3. Unpin the page so it can be deleted
    bpm->UnpinPage(pid, false); // false = not dirty (we don't want to flush "Secret Data")

    // 4. Delete Page again
    delete_result = bpm->DeletePage(pid);
    if (delete_result == true) {
        Log("Step 3: PASSED. DeletePage succeeded after unpinning.");
    }
    else {
        Log("!!! FAILED: DeletePage failed on an unpinned page.");
        exit(1);
    }

    // 5. Refetch the same Page ID
    // Since we deleted it from memory AND didn't flush "Secret Data" to disk,
    // fetching it now should trigger a read from disk (which is empty/zeros).
    // This proves we are NOT getting the stale "Secret Data" from cache.
    auto* page_refetched = bpm->FetchPage(pid);

    if (std::strcmp(page_refetched->GetData(), "Secret Data") != 0) {
        Log("Step 4: PASSED. Refetched data is NOT 'Secret Data' (Cache was cleared).");
    }
    else {
        Log("!!! FAILED. Refetched data is still 'Secret Data'. DeletePage didn't clear cache.");
        exit(1);
    }

    bpm->UnpinPage(pid, false);
    delete bpm;
    delete disk_manager;
}

// =================================================================
// Scenario 2: All Pinned (Resource Exhaustion)
// Objective: 
//   - Fill the buffer (size 5) completely with pinned pages.
//   - Requesting a 6th page should return nullptr (graceful failure).
//   - System should not crash or hang.
// =================================================================
void TestAllPinned() {
    Log("\n--- Scenario 2: All Pinned (Buffer Full) ---");
    Cleanup();

    const int POOL_SIZE = 5;
    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(POOL_SIZE, disk_manager);

    std::vector<cmse::page_id_t> pids;

    // 1. Fill the pool
    Log("Step 1: Filling the pool (Size 5) with pinned pages...");
    for (int i = 0; i < POOL_SIZE; ++i) {
        cmse::page_id_t pid;
        auto* page = bpm->NewPage(pid);
        if (page == nullptr) {
            Log("!!! FAILED: Could not allocate page " + std::to_string(i));
            exit(1);
        }
        // IMPORTANT: We DO NOT Unpin. We hold the pin.
        pids.push_back(pid);
    }

    // 2. Try to allocate the 6th page
    Log("Step 2: Attempting to allocate 6th page (Should Fail)...");
    cmse::page_id_t fail_pid;
    auto* fail_page = bpm->NewPage(fail_pid);

    if (fail_page == nullptr) {
        Log(">>> PASSED: NewPage returned nullptr as expected (No victim found).");
    }
    else {
        Log("!!! FAILED: NewPage managed to allocate a page! (Did it overwrite a pinned one?)");
        exit(1);
    }

    // 3. Try to Fetch a non-existent page (Requires a free frame to load)
    Log("Step 3: Attempting to Fetch a new page from disk (Should Fail)...");
    auto* fail_fetch = bpm->FetchPage(999);

    if (fail_fetch == nullptr) {
        Log(">>> PASSED: FetchPage returned nullptr as expected.");
    }
    else {
        Log("!!! FAILED: FetchPage managed to load a page!");
        exit(1);
    }

    // Cleanup: Unpin everything so destructor can flush nicely (optional but good practice)
    for (auto pid : pids) {
        bpm->UnpinPage(pid, false);
    }

    delete bpm;
    delete disk_manager;
}

int main() {
    TestDeletePageLogic();
    TestAllPinned();
    return 0;
}