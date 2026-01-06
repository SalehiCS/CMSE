#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cassert>
#include <random>
#include <cstdio> // for remove()

#include "../src/bufferpool/buffer_pool_manager.h"

// Test Configuration
const std::string DB_FILE = "test_run.db";
const int BUFFER_SIZE = 5; // Small size to easily test LRU eviction

void Log(const std::string& msg) {
    std::cout << "[TEST] " << msg << std::endl;
}

// Helper function to assert conditions
void Assert(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}

int main() {
    // Clean up old DB file if exists
    std::remove(DB_FILE.c_str());

    Log("--- Starting Buffer Pool Manager Test ---");

    // =================================================================
    // Scenario 1: Basic Operations (New/Write/Unpin/Fetch)
    // =================================================================
    {
        Log("Scenario 1: Basic Write & Read");

        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(BUFFER_SIZE, disk_manager);

        cmse::page_id_t page_id_temp;
        auto* page0 = bpm->NewPage(page_id_temp);

        Assert(page0 != nullptr, "Should be able to allocate a new page");
        Assert(page_id_temp == 0, "First page ID should be 0");

        // Write data to the page
        char hello[] = "Hello CMSE!";
        std::memcpy(page0->GetData(), hello, sizeof(hello));

        // Unpin with Dirty=true (indicates content has changed and needs flush)
        Assert(bpm->UnpinPage(0, true), "Unpin page 0 should succeed");

        // Fetch the page again (should still be in memory)
        auto* page0_again = bpm->FetchPage(0);
        Assert(page0_again != nullptr, "Should be able to fetch page 0");
        Assert(std::strcmp(page0_again->GetData(), hello) == 0, "Content should match");

        bpm->UnpinPage(0, false);

        delete bpm;
        delete disk_manager;
        Log("Scenario 1 Passed.");
    }

    // =================================================================
    // Scenario 2: Persistence (Disk Check)
    // =================================================================
    {
        Log("Scenario 2: Persistence (Disk Check)");

        // Simulate a system crash or restart by creating new instances
        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(BUFFER_SIZE, disk_manager);

        // Try fetching page 0. Since buffer is fresh, it must read from disk.
        auto* page0 = bpm->FetchPage(0);
        Assert(page0 != nullptr, "Should be able to fetch page 0 from disk");

        char hello[] = "Hello CMSE!";
        Assert(std::strcmp(page0->GetData(), hello) == 0, "Data should persist on disk");

        bpm->UnpinPage(0, false);
        delete bpm;
        delete disk_manager;
        Log("Scenario 2 Passed.");
    }

    // =================================================================
    // Scenario 3: LRU Eviction Policy
    // =================================================================
    {
        Log("Scenario 3: LRU Eviction Policy");

        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(BUFFER_SIZE, disk_manager);

        // Fill the buffer (Size is 5)
        // Create pages 0 to 4 and unpin them so they become candidates for LRU
        for (int i = 0; i < BUFFER_SIZE; i++) {
            cmse::page_id_t pid;
            auto* p = bpm->NewPage(pid);
            // Write page ID into content for verification
            std::sprintf(p->GetData(), "Page-%d", i);
            bpm->UnpinPage(pid, true);
        }

        // Buffer is now full: [0, 1, 2, 3, 4]
        // Page 0 is the oldest (LRU victim)

        // Create a new page (5). This should trigger eviction of page 0.
        cmse::page_id_t pid5;
        auto* p5 = bpm->NewPage(pid5);
        Assert(p5 != nullptr, "Should be able to allocate when buffer is full (eviction)");
        Assert(pid5 == 5, "New page ID should be 5");
        bpm->UnpinPage(5, false);

        // Try fetching page 0. Since it was evicted, it must be read from disk.
        // Critical check: Did the dirty data get flushed before eviction?
        auto* p0 = bpm->FetchPage(0);
        Assert(p0 != nullptr, "Should fetch evicted page 0 back from disk");
        Assert(std::strcmp(p0->GetData(), "Page-0") == 0, "Evicted dirty page data should be preserved");
        bpm->UnpinPage(0, false);

        delete bpm;
        delete disk_manager;
        Log("Scenario 3 Passed.");
    }

    // Clean up test file
    std::remove(DB_FILE.c_str());

    Log("--- ALL TESTS PASSED SUCCESSFULLY ---");
    return 0;
}