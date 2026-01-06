#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cassert>
#include <cstdio>
#include <fstream> // Used only for debug reading
#include <iomanip>

#include "../src/bufferpool/buffer_pool_manager.h"

// Configuration
const std::string DB_FILE = "test_run.db";
const int BUFFER_SIZE = 5;

void Log(const std::string& msg) {
    std::cout << "[TEST_LOG] " << msg << std::endl;
}

void Assert(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "!!! FAILED: " << message << std::endl;
        std::exit(1);
    }
}

// DEBUG HELPER: Reads the file directly from OS to see what's actually there
void DebugFileContent(const std::string& filename, int expected_bytes_check) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "[DEBUG_FILE] File '" << filename << "' DOES NOT EXIST!" << std::endl;
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "[DEBUG_FILE] File Size: " << size << " bytes." << std::endl;

    if (size == 0) {
        std::cout << "[DEBUG_FILE] Content: [EMPTY]" << std::endl;
        return;
    }

    // Read first few bytes
    std::vector<char> buffer(50); // read first 50 bytes
    file.read(buffer.data(), buffer.size());
    size_t read_cnt = file.gcount();

    std::cout << "[DEBUG_FILE] First " << read_cnt << " bytes (Hex/Char): ";
    for (size_t i = 0; i < read_cnt; i++) {
        char c = buffer[i];
        if (std::isprint(c)) std::cout << c;
        else std::cout << ".";
    }
    std::cout << std::endl;

    file.close();
}

int main() {
    // 0. Cleanup
    std::remove(DB_FILE.c_str());
    Log("Cleaned up old DB file.");

    // =================================================================
    // Scenario 1: Write and Flush
    // =================================================================
    {
        Log("\n--- Scenario 1: Write & Flush ---");

        // 1. Init
        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(BUFFER_SIZE, disk_manager);

        Log("BPM and DiskManager created.");
        DebugFileContent(DB_FILE, 0); // Should be created now

        // 2. New Page
        cmse::page_id_t page_id_temp;
        auto* page0 = bpm->NewPage(page_id_temp);
        Assert(page0 != nullptr, "NewPage failed");
        Assert(page_id_temp == 0, "Page ID is not 0");
        Log("Page 0 allocated in memory.");

        // 3. Write Data
        char hello[] = "Hello_Persistence";
        std::memcpy(page0->GetData(), hello, sizeof(hello));
        Log("Data written to memory: 'Hello_Persistence'");

        // 4. Unpin (Mark Dirty)
        // NOTE: This does NOT write to disk yet, just marks it.
        bpm->UnpinPage(0, true);
        Log("Page 0 unpinned and marked DIRTY.");

        // Check disk - should be empty or just zero-filled depending on allocation
        // But our data 'Hello...' should NOT be there yet (unless policy is immediate write, which is not)
        std::cout << "-> Check Disk (Before Flush/Destructor): ";
        DebugFileContent(DB_FILE, 0);

        // 5. Explicit Flush (Test explicit flush first)
        // bpm->FlushPage(0);
        // Log("Explicit FlushPage(0) called.");

        // 6. Delete BPM (Should trigger FlushAll in destructor)
        Log("Deleting BPM (Expect FlushAll)...");
        delete bpm;

        Log("Deleting DiskManager (Expect fclose)...");
        delete disk_manager;

        // 7. FINAL CHECK FOR SCENARIO 1
        std::cout << "-> Check Disk (After Shutdown): ";
        DebugFileContent(DB_FILE, 0);
    }

    // =================================================================
    // Scenario 2: Recovery
    // =================================================================
    {
        Log("\n--- Scenario 2: Recovery (Re-open) ---");

        std::cout << "-> Check Disk (Before Open New DiskManager): ";
        DebugFileContent(DB_FILE, 0);

        // 1. Re-Init
        auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
        auto* bpm = new cmse::bufferpool::BufferPoolManager(BUFFER_SIZE, disk_manager);
        Log("New BPM/DM created.");

        std::cout << "-> Check Disk (After Open New DiskManager): ";
        DebugFileContent(DB_FILE, 0);

        // 2. Fetch Page 0
        auto* page0 = bpm->FetchPage(0);
        Assert(page0 != nullptr, "FetchPage(0) failed");

        // 3. Verify Data
        std::cout << "[DEBUG_MEM] Content in RAM after fetch: " << page0->GetData() << std::endl;

        char hello[] = "Hello_Persistence";
        if (std::strcmp(page0->GetData(), hello) != 0) {
            Log("!!! DATA MISMATCH !!!");
            Log("Expected: " + std::string(hello));
            Log("Actual:   " + std::string(page0->GetData()));
            Assert(false, "Data persistence check failed");
        }
        else {
            Log("Data Verified Successfully!");
        }

        bpm->UnpinPage(0, false);
        delete bpm;
        delete disk_manager;
    }

    std::remove(DB_FILE.c_str());
    Log("\n--- ALL TESTS PASSED ---");
    return 0;
}