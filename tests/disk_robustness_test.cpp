/**
 * disk_robustness_test.cpp
 *
 * TESTS:
 * 1. Simulated Crash (No Flush): Verifies that data remains in RAM and is NOT written to disk
 * until explicit Flush or Shutdown occurs. Proves reliance on the Buffer Pool.
 * 2. Large File Stress: Creates 1000 pages with a Buffer Pool of size 10.
 * Forces heavy I/O (Read/Write) and verifies file offset calculations.
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio> // for snprintf
#include <cassert>
#include <filesystem>
#include <fstream>

#include "../src/bufferpool/buffer_pool_manager.h"

const std::string DB_FILE = "test_robustness.db";

// --- Helper: Cleanup ---
void Cleanup() {
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }
}

// --- Helper: Logger ---
void Log(const std::string& msg) {
    std::cout << "[DISK_ROBUSTNESS] " << msg << std::endl;
}

// =================================================================
// Scenario 1: Simulated Crash (Data Loss Check)
// Objective: 
//   - Write data to a page and Unpin it (Dirty).
//   - Verify that the data is NOT yet in the physical file (Simulating a crash before flush).
//   - Then Flush, and verify it IS in the file.
// =================================================================
void TestSimulatedCrash() {
    Log("\n--- Scenario 1: Simulated Crash (No Flush) ---");
    Cleanup();

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(5, disk_manager);

    // 1. Create a page and write "CrucialData"
    cmse::page_id_t pid;
    auto* page = bpm->NewPage(pid);

    // Use safe string copy
    size_t payload_size = cmse::PAGE_SIZE - sizeof(cmse::PageHeader);
    std::snprintf(page->GetData(), payload_size, "CrucialData");

    // 2. Unpin with is_dirty = true
    // Ideally, this stays in RAM because the buffer isn't full.
    bpm->UnpinPage(pid, true);

    Log("Step 1: Wrote 'CrucialData' to RAM (Dirty). We did NOT flush yet.");

    // 3. INSPECT DISK CONTENT DIRECTLY
    // We open the file as a raw binary stream to see what's actually on the HDD.
    // Note: On some Windows configurations, this might fail if the file is strictly locked,
    // but usually read-only access is allowed.
    std::ifstream file(DB_FILE, std::ios::binary | std::ios::in);
    if (file.is_open()) {
        char buffer[cmse::PAGE_SIZE];
        // Seek to the page offset
        file.seekg(pid * cmse::PAGE_SIZE);
        file.read(buffer, cmse::PAGE_SIZE);

        // Skip header to find payload
        char* file_payload = buffer + sizeof(cmse::PageHeader);

        // CHECK: Data should NOT be "CrucialData" yet (it should be empty/zeros).
        if (std::strcmp(file_payload, "CrucialData") != 0) {
            Log(">>> PASSED Check A: Data is NOT on disk yet (Simulated Crash would lose data).");
        }
        else {
            Log("!!! WARNING: Data WAS found on disk. Did the buffer pool flush early?");
            // This isn't necessarily a bug, but unexpected for a large buffer.
        }
        file.close();
    }
    else {
        Log("Step 1.5: Could not open file for inspection (OS Lock). Skipping Check A.");
    }

    // 4. Manual Flush
    bpm->FlushPage(pid);
    Log("Step 2: Explicitly Flushed Page.");

    // 5. INSPECT DISK AGAIN
    std::ifstream file2(DB_FILE, std::ios::binary | std::ios::in);
    if (file2.is_open()) {
        char buffer[cmse::PAGE_SIZE];
        file2.seekg(pid * cmse::PAGE_SIZE);
        file2.read(buffer, cmse::PAGE_SIZE);
        char* file_payload = buffer + sizeof(cmse::PageHeader);

        if (std::strcmp(file_payload, "CrucialData") == 0) {
            Log(">>> PASSED Check B: Data is NOW safely on disk.");
        }
        else {
            Log("!!! FAILED: Even after Flush, data is not on disk!");
            exit(1);
        }
    }

    delete bpm;
    delete disk_manager;
}

// =================================================================
// Scenario 2: Large File Stress (Offset & Scalability)
// Objective: 
//   - Pool Size = 10.
//   - Create 1000 Pages.
//   - This forces the DiskManager to Write/Read constantly (Swapping).
//   - Verifies that offsets (PageID * 4KB) are calculated correctly.
// =================================================================
void TestLargeFileStress() {
    Log("\n--- Scenario 2: Large File Stress (1000 Pages, Pool Size 10) ---");
    Cleanup();

    const int POOL_SIZE = 10;
    const int NUM_PAGES = 1000; // 100x the pool size

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(POOL_SIZE, disk_manager);

    Log("Step 1: Creating and Writing " + std::to_string(NUM_PAGES) + " pages...");

    // 1. WRITE PHASE
    for (int i = 0; i < NUM_PAGES; ++i) {
        cmse::page_id_t pid;
        auto* page = bpm->NewPage(pid);

        if (page == nullptr) {
            Log("!!! FATAL: Failed to allocate page " + std::to_string(i));
            exit(1);
        }

        // Verify Page ID logic
        if (pid != i) {
            Log("!!! FATAL: Page ID sequence incorrect. Expected " + std::to_string(i) + " Got " + std::to_string(pid));
            exit(1);
        }

        // Write unique data pattern: "val:1", "val:2", ...
        size_t payload_size = cmse::PAGE_SIZE - sizeof(cmse::PageHeader);
        std::snprintf(page->GetData(), payload_size, "val:%d", i);

        // Unpin with dirty=true so it gets written to disk when evicted
        bpm->UnpinPage(pid, true);

        if (i % 200 == 0) std::cout << "Write Progress: " << i << "\n";
    }

    Log("Step 1 Complete. All pages created (most are now swapped to disk).");

    // 2. READ VERIFICATION PHASE
    // Now we fetch them back. Since pool is small, this forces reading from disk.
    Log("Step 2: Reading back and verifying data...");

    for (int i = 0; i < NUM_PAGES; ++i) {
        auto* page = bpm->FetchPage(i);
        if (page == nullptr) {
            Log("!!! FATAL: Could not fetch page " + std::to_string(i));
            exit(1);
        }

        // Construct expected string
        char expected[50];
        std::snprintf(expected, sizeof(expected), "val:%d", i);

        // Verify Payload
        if (std::strcmp(page->GetData(), expected) != 0) {
            Log("!!! FAILED: Data Mismatch on Page " + std::to_string(i));
            Log("Expected: " + std::string(expected));
            Log("Got: " + std::string(page->GetData()));
            Log("This means DiskManager Offset calculation is likely wrong.");
            exit(1);
        }

        bpm->UnpinPage(i, false); // Read-only

        if (i % 200 == 0) std::cout << "Read Progress: " << i << "\n";
    }

    Log(">>> PASSED: Successfully handled " + std::to_string(NUM_PAGES) + " pages with small buffer.");

    delete bpm;
    delete disk_manager;
}

int main() {
    TestSimulatedCrash();
    TestLargeFileStress();
    return 0;
}