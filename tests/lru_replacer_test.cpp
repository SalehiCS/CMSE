/**
 * lru_replacer_test.cpp
 *
 * Unit test specifically for the LRUReplacer class.
 * Verifies eviction policy, pinning logic, and size tracking.
 */

#include <iostream>
#include <vector>
#include <cassert>
#include "../src/bufferpool/lru_replacer.h"

void Log(const std::string& msg) {
    std::cout << "[LRU_TEST] " << msg << std::endl;
}

// 1. Basic Victim Logic (FIFO behavior without re-access)
void TestVictimOrder() {
    Log("--- Test 1: Basic Victim Order ---");

    // Create LRU with capacity 7
    cmse::bufferpool::LRUReplacer lru(7);

    // Unpin 1, 2, 3 (Add them to LRU)
    lru.Unpin(1);
    lru.Unpin(2);
    lru.Unpin(3);

    // Expect Size = 3
    if (lru.Size() != 3) {
        Log("!!! FAILED: Size mismatch. Expected 3, Got " + std::to_string(lru.Size()));
        exit(1);
    }

    // Since 1 was unpinned first (Least Recently Used), it should be evicted first.
    cmse::frame_id_t victim;

    // Evict 1
    if (!lru.Victim(&victim) || victim != 1) {
        Log("!!! FAILED: Expected victim 1.");
        exit(1);
    }

    // Evict 2
    if (!lru.Victim(&victim) || victim != 2) {
        Log("!!! FAILED: Expected victim 2.");
        exit(1);
    }

    // Evict 3
    if (!lru.Victim(&victim) || victim != 3) {
        Log("!!! FAILED: Expected victim 3.");
        exit(1);
    }

    // Now empty
    if (lru.Victim(&victim)) {
        Log("!!! FAILED: Expected empty LRU, but found a victim.");
        exit(1);
    }

    Log(">>> PASSED: Basic Victim Order.");
}

// 2. Pinning Logic (Removing from eviction list)
void TestPinning() {
    Log("--- Test 2: Pinning Logic ---");

    cmse::bufferpool::LRUReplacer lru(7);

    // Unpin 1, 2, 3, 4, 5
    lru.Unpin(1);
    lru.Unpin(2);
    lru.Unpin(3);
    lru.Unpin(4);
    lru.Unpin(5);

    // Now someone uses page 3 and 4 again. We Pin them.
    // They should be removed from the LRU list.
    lru.Pin(3);
    lru.Pin(4);

    // Expected order of eviction: 1, 2, 5 (3 and 4 are safe)
    cmse::frame_id_t victim;

    lru.Victim(&victim);
    if (victim != 1) { Log("FAILED: Expected 1"); exit(1); }

    lru.Victim(&victim);
    if (victim != 2) { Log("FAILED: Expected 2"); exit(1); }

    lru.Victim(&victim);
    if (victim != 5) { Log("FAILED: Expected 5"); exit(1); } // 3 and 4 skipped

    if (lru.Victim(&victim)) {
        Log("FAILED: Expected empty.");
        exit(1);
    }

    Log(">>> PASSED: Pinning Logic.");
}

// 3. Resilience / Re-access Logic
// Verify that if we use a page again, it moves to the MRU (Most Recently Used) position.
void TestReAccess() {
    Log("--- Test 3: Re-Access (LRU Update) ---");

    cmse::bufferpool::LRUReplacer lru(7);

    // Initial: 1, 2, 3 (1 is LRU)
    lru.Unpin(1);
    lru.Unpin(2);
    lru.Unpin(3);

    // Scenario: We access 1 again.
    // To simulate access in BPM, we Pin(1) then Unpin(1).
    lru.Pin(1);
    lru.Unpin(1);

    // Now order should be: 2, 3, 1 (1 became MRU because it was just unpinned)
    // 2 is now the LRU.

    cmse::frame_id_t victim;

    lru.Victim(&victim);
    if (victim != 2) {
        Log("FAILED: Expected 2 (1 should be saved). Got " + std::to_string(victim));
        exit(1);
    }

    lru.Victim(&victim);
    if (victim != 3) { Log("FAILED: Expected 3"); exit(1); }

    lru.Victim(&victim);
    if (victim != 1) { Log("FAILED: Expected 1 (It was moved to back)."); exit(1); }

    Log(">>> PASSED: Re-Access Logic.");
}

int main() {
    TestVictimOrder();
    TestPinning();
    TestReAccess();
    return 0;
}