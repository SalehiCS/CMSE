/**
 * Test Case 4.1: The Rich Eviction Test
 * --------------------------------------------------------------------------------------
 * OBJECTIVE:
 * Verify complex memory management scenarios:
 * 1. "Pinning" logic: Pinned pages must NEVER be evicted.
 * 2. "LRU" logic: Unpinned pages should be evicted to make room.
 * 3. "Persistence" logic: Evicted pages must be readable from disk later.
 *
 * * CONSTRAINT:
 * Uses ONLY Public APIs. No "private" hacks.
 * We verify state by observing behavior and return values.
 * --------------------------------------------------------------------------------------
 */

#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/bufferpool/page_guard.h" 
#include "../src/common/logger.h"
#include "../src/common/types.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <thread>

using namespace cmse;

// --- CONFIGURATION ---
const std::string DB_FILE = "rich_eviction.db";
const int POOL_SIZE = 4; // Small pool to force contention (4 Frames)

// --- HELPER: PRINT PAGE STATUS ---
// helper to safely print public details of a page
void PrintPageInfo(const std::string& label, Page* page) {
    if (page == nullptr) {
        std::cout << "  " << std::left << std::setw(20) << label << ": [NULL]" << std::endl;
        return;
    }
    // Safe substring logic to avoid out-of-bounds read if data is not null-terminated
    char safe_buffer[16] = { 0 };
    strncpy_s(safe_buffer, sizeof(safe_buffer), page->GetData(), _TRUNCATE);

    std::cout << "  " << std::left << std::setw(20) << label
        << ": ID=" << std::setw(4) << page->GetPageId()
        << " | PinCount=" << std::setw(2) << page->GetPinCount()
        << " | Data='" << safe_buffer << "...'" << std::endl;
}

void PrintBanner(const std::string& msg) {
    std::cout << "\n========================================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "========================================================================\n";
}

int main() {
    // 1. SETUP
    std::remove(DB_FILE.c_str());
    Logger::GetInstance().SetEnabled(true); // Enable logs to see [Disk] activity

    auto disk = std::make_unique<disk::DiskManager>(DB_FILE);
    // Initialize BPM with exactly 4 frames
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(
        , disk.get());

    std::vector<page_id_t> page_ids; // Keep track of created IDs

    // =================================================================================
    // PHASE 1: FILL THE POOL
    // =================================================================================
    PrintBanner("PHASE 1: FILL POOL (Pages 0-3)");

    for (int i = 0; i < POOL_SIZE; i++) {
        page_id_t new_id;
        // 1. Allocate Page
        Page* p = bpm->NewPage(new_id); // Fixed: Pass address of new_id

        if (p == nullptr) {
            std::cerr << "[Fatal] Failed to allocate Page " << i << std::endl;
            return 1;
        }

        // 2. Write Data using SAFE copy
        std::string content = "Content_Page_" + std::to_string(new_id);
        // Using PAGE_SIZE (typically 4096) to ensure we don't overflow the page buffer
        strncpy_s(p->GetData(), PAGE_SIZE - sizeof(PageHeader), content.c_str(), _TRUNCATE);

        // 3. Log Status
        PrintPageInfo("Created Page", p);
        page_ids.push_back(new_id);

        // 4. UNPIN immediately so it becomes a candidate for eviction later
        // Mark dirty=true so it writes to disk on eviction
        bpm->UnpinPage(new_id, true);
    }
    std::cout << "[State] Pool Full. All pages Unpinned (PinCount=0)." << std::endl;


    // =================================================================================
    // PHASE 2: PIN A "VIP" PAGE
    // =================================================================================
    PrintBanner("PHASE 2: PIN PAGE 2 (The VIP)");

    page_id_t vip_id = page_ids[2]; // Usually Page 2

    // Fetch Page 2 twice to simulate heavy usage / multiple threads using it
    // This should raise its PinCount > 1
    std::cout << "[Action] Fetching Page " << vip_id << " (First Pin)..." << std::endl;
    Page* vip_page = bpm->FetchPage(vip_id); // PinCount 1

    std::cout << "[Action] Fetching Page " << vip_id << " (Second Pin)..." << std::endl;
    Page* vip_page_2 = bpm->FetchPage(vip_id); // PinCount 2

    PrintPageInfo("VIP Page State", vip_page);

    if (vip_page->GetPinCount() >= 2) {
        std::cout << "\033[1;32m[PASS]\033[0m Page " << vip_id << " is successfully pinned multiple times." << std::endl;
    }
    else {
        std::cout << "\033[1;33m[WARN]\033[0m Page " << vip_id << " PinCount low (Expected >=2)." << std::endl;
    }
    // IMPORTANT: We do NOT unpin here. This page must stick in RAM.


    // =================================================================================
    // PHASE 3: EVICTION STORM
    // =================================================================================
    PrintBanner("PHASE 3: CREATE NEW PAGES (Force Eviction)");

    std::cout << "Attempting to create 5 NEW pages. This requires evicting 5 old pages." << std::endl;
    std::cout << "Since Page " << vip_id << " is pinned, it MUST NOT be chosen as a victim." << std::endl;

    for (int i = 0; i < 5; i++) {
        page_id_t new_id;
        Page* p = bpm->NewPage(new_id); // Fixed: Pass address of new_id

        if (p != nullptr) {
            std::string content = "Storm_Page_" + std::to_string(new_id);
            // SAFE copy
            strncpy_s(p->GetData(), PAGE_SIZE - sizeof(PageHeader), content.c_str(), _TRUNCATE);

            // Print brief info
            std::cout << "  -> Created Page " << new_id << " (Evicted someone!)" << std::endl;

            page_ids.push_back(new_id);
            bpm->UnpinPage(new_id, true);
        }
        else {
            std::cout << "\033[1;31m[FAIL]\033[0m OOM! Could not create page. Did pinning lock the whole pool?" << std::endl;
        }
    }


    // =================================================================================
    // PHASE 4: VERIFY VIP SAFETY
    // =================================================================================
    PrintBanner("PHASE 4: VERIFY VIP INTEGRITY");

    // Check if the VIP page pointer is still valid and has data (It should be resident)
    // Note: Since we never unpinned it, the pointer 'vip_page' is still strictly valid in memory.
    PrintPageInfo("VIP Page Check", vip_page);

    // We manually ensure null-termination for the read check just in case, though std::string handles it.
    if (std::string(vip_page->GetData()) == "Content_Page_" + std::to_string(vip_id)) {
        std::cout << "\033[1;32m[PASS]\033[0m VIP Page data is intact in RAM." << std::endl;
    }
    else {
        std::cout << "\033[1;31m[FAIL]\033[0m VIP Page data corrupted or lost!" << std::endl;
    }

    // Now we clean up the pins so the system can shut down cleanly
    std::cout << "[Action] Unpinning VIP Page twice..." << std::endl;
    bpm->UnpinPage(vip_id, false);
    bpm->UnpinPage(vip_id, false);


    // =================================================================================
    // PHASE 5: VERIFY PERSISTENCE (Read back an Evicted Page)
    // =================================================================================
    PrintBanner("PHASE 5: RESURRECT EVICTED PAGE (Page 0)");

    page_id_t victim_id = page_ids[0]; // Page 0 was likely evicted first (LRU)

    std::cout << "[Action] Requesting Page " << victim_id << " (Should trigger [Disk] Read)..." << std::endl;

    // This Fetch should cause IO
    Page* resurrected = bpm->FetchPage(victim_id);

    if (resurrected != nullptr) {
        std::string actual_data = resurrected->GetData();
        std::string expected_data = "Content_Page_" + std::to_string(victim_id);

        PrintPageInfo("Resurrected", resurrected);

        if (actual_data == expected_data) {
            std::cout << "\033[1;32m[PASS]\033[0m Data matched! Page 0 survived the round-trip to disk." << std::endl;
        }
        else {
            std::cout << "\033[1;31m[FAIL]\033[0m Data Mismatch! Disk write/read failed." << std::endl;
        }
        bpm->UnpinPage(victim_id, false);
    }
    else {
        std::cout << "\033[1;31m[FAIL]\033[0m Could not fetch Page " << victim_id << " from disk." << std::endl;
    }

    // Cleanup
    std::remove(DB_FILE.c_str());

    return 0;
}