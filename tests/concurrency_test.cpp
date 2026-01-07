/**
 * concurrency_test.cpp
 *
 * Comprehensive stress test for BufferPoolManager.
 * Focuses on Multi-threaded scenarios to catch race conditions,
 * deadlocks, and latching bugs.
 */

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <chrono>
#include <cassert>
#include <mutex>
#include <sstream>
#include <filesystem>

#include "../src/bufferpool/buffer_pool_manager.h"

 // --- Configuration ---
const std::string DB_FILE = "stress_test.db";
std::mutex log_mutex; // Mutex to prevent garbled log output

// --- Helper: Thread-Safe Logger ---
void Log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(log_mutex);
    // Get current thread ID for tracking
    std::cout << "[Thread-" << std::this_thread::get_id() << "] " << msg << std::endl;
}

// --- Helper: Cleanup DB File ---
void Cleanup() {
    if (std::filesystem::exists(DB_FILE)) {
        std::filesystem::remove(DB_FILE);
    }
}

// =================================================================
// Scenario 1: Single Page Contention
// Description: 
//   - Multiple threads constantly Fetch and Unpin the SAME page (Page 0).
//   - Tests the correctness of 'pin_count' and internal latches.
// =================================================================
void TestSinglePageContention() {
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout << "\n============================================\n";
        std::cout << "   SCENARIO 1: Single Page Contention Test    \n";
        std::cout << "============================================\n";
    }

    Cleanup();

    const int POOL_SIZE = 10;
    const int NUM_THREADS = 10;
    const int ITERATIONS = 500; // Each thread performs 500 fetch/unpin ops

    // 1. Setup Environment
    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(POOL_SIZE, disk_manager);

    // 2. Pre-allocate Page 0 to ensure it exists
    cmse::page_id_t page_id;
    auto* page0 = bpm->NewPage(page_id);
    if (page0 == nullptr || page_id != 0) {
        Log("CRITICAL ERROR: Failed to allocate initial Page 0.");
        exit(1);
    }
    // Unpin immediately so threads can fight for it
    bpm->UnpinPage(0, false);
    Log("Page 0 created and unpinned. Starting threads...");

    // 3. Launch Threads
    std::vector<std::thread> threads;
    std::atomic<int> success_ops{ 0 };

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&bpm, &success_ops, ITERATIONS, i]() {
            for (int j = 0; j < ITERATIONS; ++j) {
                // A) Fetch Page 0
                auto* page = bpm->FetchPage(0);
                if (page == nullptr) {
                    std::stringstream ss;
                    ss << "ERROR: FetchPage(0) returned nullptr at iter " << j;
                    Log(ss.str());
                    std::terminate(); // Crash hard on failure
                }

                // B) Integrity Check (Read ID from header)
                if (page->GetPageId() != 0) {
                    std::stringstream ss;
                    ss << "DATA CORRUPTION: Expected PageId 0, Got " << page->GetPageId();
                    Log(ss.str());
                    std::terminate();
                }

                // C) Unpin Page 0
                // We toggle 'is_dirty' randomly to stress the flush logic slightly
                bool is_dirty = (j % 2 == 0);
                bpm->UnpinPage(0, is_dirty);

                success_ops++;
            }
            });
    }

    // 4. Wait for completion
    for (auto& t : threads) {
        t.join();
    }

    Log("All threads finished.");

    // 5. Final Verification
    // We fetch Page 0 one last time. 
    // If latches worked correctly, pin_count should be exactly 1 (for this fetch).
    // If it's > 1, it means some thread failed to decrement pin_count correctly (race condition).
    auto* final_page = bpm->FetchPage(0);
    int final_pin_count = final_page->GetPinCount();

    std::stringstream ss;
    ss << "Final Check -> Pin Count: " << final_pin_count;
    Log(ss.str());

    if (final_pin_count != 1) {
        Log("!!! TEST FAILED: Pin Count Mismatch! Expected 1.");
        exit(1);
    }
    else {
        Log(">>> TEST PASSED: Pin counts matches expected value.");
    }

    bpm->UnpinPage(0, false);
    delete bpm;
    delete disk_manager;
}

// =================================================================
// Scenario 2: Buffer Full Race (Eviction Stress)
// Description:
//   - Buffer size is SMALL (5).
//   - Many threads try to create NEW pages simultaneously.
//   - Forces the system to constantly Evict pages to make room.
//   - Tests list management (free_list, page_table) under pressure.
// =================================================================
void TestBufferFullRace() {
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout << "\n============================================\n";
        std::cout << "   SCENARIO 2: Buffer Full / Eviction Race    \n";
        std::cout << "============================================\n";
    }
    Cleanup();

    const int POOL_SIZE = 5;       // Very small pool
    const int NUM_THREADS = 8;     // More threads than pool slots
    const int ITERATIONS = 50;     // Total pages to create: 8 * 50 = 400

    auto* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    auto* bpm = new cmse::bufferpool::BufferPoolManager(POOL_SIZE, disk_manager);

    Log("Pool Size: 5. Launching threads to create 400 pages total...");

    std::vector<std::thread> threads;
    std::atomic<int> created_count{ 0 };
    std::atomic<int> failed_count{ 0 }; // It's okay to fail if all pages are pinned

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&bpm, &created_count, &failed_count, ITERATIONS, i]() {
            // Small initial delay to stagger start slightly
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 5));

            for (int j = 0; j < ITERATIONS; ++j) {
                cmse::page_id_t new_pid;

                // Try to allocate new page
                auto* page = bpm->NewPage(new_pid);

                if (page == nullptr) {
                    // This happens if ALL 5 frames are currently PINNED by other threads.
                    // This is NOT a bug, but a valid state in a small buffer.
                    failed_count++;
                }
                else {
                    // Write something to prove access
                    // Note: We use GetData() which points after header
                    std::snprintf(page->GetData(), 20, "Thread%d_Iter%d", i, j);

                    // Artificial delay to hold the pin (increasing contention)
                    // std::this_thread::sleep_for(std::chrono::microseconds(10));

                    // Important: Must Unpin, otherwise buffer stays full forever!
                    bpm->UnpinPage(new_pid, true);

                    created_count++;

                    if (created_count % 50 == 0) {
                        std::stringstream ss;
                        ss << "Progress: " << created_count << " pages created so far.";
                        Log(ss.str());
                    }
                }
            }
            });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::stringstream ss;
    ss << "Stress Test Done.\n"
        << "   - Successfully Created: " << created_count << "\n"
        << "   - Failed (Buffer Busy): " << failed_count;
    Log(ss.str());

    // Final Liveness Check
    // Can we still allocate a page after all this chaos?
    cmse::page_id_t final_pid;
    auto* check_page = bpm->NewPage(final_pid);

    if (check_page != nullptr) {
        Log(">>> TEST PASSED: System survived and is still operational.");
        bpm->UnpinPage(final_pid, false);
    }
    else {
        Log("!!! TEST FAILED: System deadlocked or corrupted! Cannot allocate new page.");
        exit(1);
    }

    delete bpm;
    delete disk_manager;
}

int main() {
    // Run tests sequentially
    TestSinglePageContention();

    // Give OS a moment to close files/threads properly
    std::this_thread::sleep_for(std::chrono::seconds(1));

    TestBufferFullRace();

    return 0;
}