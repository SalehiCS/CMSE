/**
 * persistence_test.cpp
 *
 * This suite tests the correctness of the Dirty Flag mechanism and
 * ensures data persistence across multiple updates.
 *
 * Scenarios:
 * 1. False Dirty: Marking a page as dirty without changing data must still trigger a disk write.
 * 2. Rapid Update: Modifying a cached page multiple times ensures the LATEST version persists.
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio> // Required for std::snprintf
#include <cassert>
#include <filesystem>
#include <thread>
#include <chrono>

#include "../src/bufferpool/buffer_pool_manager.h"

const std::string DB_FILE = "test_persistence.db";

// --- Helper: Cleanup DB File ---
void Cleanup() {
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }
}

// --- Helper: Logger ---
void Log(const std::string& msg) {
    std::cout << "[PERSISTENCE_TEST] " << msg << std::endl;
}

// =================================================================
// Scenario 1: False Dirty Flag Test
// Objective: Ensure that calling UnpinPage(..., true) triggers a write
//            to disk upon flushing, even if data wasn't actually changed.
// =================================================================
void TestFalseDirty() {
    Log("\n--- Scenario 1: False Dirty Flag Test ---");
    Cleanup();

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(5, disk_manager);

    // 1. Create a new page
    cmse::page_id_t page_id;
    auto* page = bpm->NewPage(page_id);

    // Calculate safe payload size (Page Size - Header Size)
    size_t payload_size = cmse::PAGE_SIZE - sizeof(cmse::PageHeader);

    // Write initial data using SAFE function (snprintf)
    std::snprintf(page->GetData(), payload_size, "Initial Data");
    Log("Step 1: Created Page " + std::to_string(page_id) + " with 'Initial Data'.");

    // Unpin with is_dirty = true (First write)
    bpm->UnpinPage(page_id, true);
    bpm->FlushPage(page_id);

    int initial_flushes = disk_manager->GetNumFlushes();
    Log("Disk Flushes after init: " + std::to_string(initial_flushes));

    // 2. Fetch the page again (It should be in RAM)
    page = bpm->FetchPage(page_id);
    Log("Step 2: Fetched Page " + std::to_string(page_id) + " from RAM.");

    // 3. DO NOT MODIFY DATA. But Unpin with is_dirty = true.
    // This simulates a "False Dirty" (e.g., system marked it dirty just in case).
    Log("Step 3: Unpinning with is_dirty=TRUE (No actual data change).");
    bpm->UnpinPage(page_id, true);

    // 4. Force Flush
    // If the flag works, BPM should call DiskManager::WritePage.
    bpm->FlushPage(page_id);

    int final_flushes = disk_manager->GetNumFlushes();
    Log("Disk Flushes after second flush: " + std::to_string(final_flushes));

    // 5. Verification
    if (final_flushes > initial_flushes) {
        Log(">>> PASSED: Disk write count increased. Dirty flag was respected.");
    }
    else {
        Log("!!! FAILED: Disk write count did NOT increase. Dirty flag was ignored.");
        exit(1);
    }

    delete bpm;
    delete disk_manager;
}

// =================================================================
// Scenario 2: Rapid Update Persistence
// Objective: Modify page -> Unpin -> Fetch (RAM hit) -> Modify again -> Unpin.
//            Ensure the LAST modification is the one on disk.
// =================================================================
void TestRapidUpdatePersistence() {
    Log("\n--- Scenario 2: Rapid Update Persistence Test ---");
    Cleanup();

    // Scope 1: Write updates
    {
        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(5, disk_manager);

        cmse::page_id_t pid;
        auto* page = bpm->NewPage(pid);

        size_t payload_size = cmse::PAGE_SIZE - sizeof(cmse::PageHeader);

        // Update 1: Write "Version_1" safely
        std::snprintf(page->GetData(), payload_size, "Version_1");
        Log("Step 1: Wrote 'Version_1' to Page " + std::to_string(pid));
        bpm->UnpinPage(pid, true); // Mark dirty

        // Update 2: Immediate Fetch (Should hit Buffer Pool, not Disk)
        page = bpm->FetchPage(pid);

        // Verify we are reading what we just wrote (in memory)
        if (std::strcmp(page->GetData(), "Version_1") != 0) {
            Log("!!! FAILED: Memory corruption! Expected Version_1.");
            exit(1);
        }

        // Overwrite with "Version_2"
        Log("Step 2: Overwriting with 'Version_2' (Still in RAM).");
        std::snprintf(page->GetData(), payload_size, "Version_2");

        // Unpin with dirty flag
        bpm->UnpinPage(pid, true);

        // Shutdown BPM (Should trigger FlushAll)
        Log("Step 3: Shutting down BPM (Force Flush).");
        delete bpm;
        delete disk_manager;
    }

    // Scope 2: Verify from Disk
    {
        Log("Step 4: Reopening DiskManager to verify persistence.");
        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        // We don't strictly need BPM here, we can use DiskManager directly to be sure,
        // but using BPM mimics real usage.
        auto* bpm = new cmse::bufferpool::BufferPoolManager(5, disk_manager);

        // We know the page_id is 0 because we started fresh
        auto* page = bpm->FetchPage(0);

        Log("Read from disk: " + std::string(page->GetData()));

        if (std::strcmp(page->GetData(), "Version_2") == 0) {
            Log(">>> PASSED: 'Version_2' persisted correctly.");
        }
        else {
            Log("!!! FAILED: Data Mismatch! Expected 'Version_2'.");
            Log("This implies the second update was lost or not flushed.");
            exit(1);
        }

        bpm->UnpinPage(0, false);
        delete bpm;
        delete disk_manager;
    }
}

int main() {
    TestFalseDirty();
    TestRapidUpdatePersistence();
    return 0;
}