#include "../src/adapter/btree_adapter.h"
#include "../src/page/page.h"
#include "../src/common/types.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <algorithm>

using namespace cmse;
using namespace cmse::adapter;

// --- Helper Macros ---
#define ASSERT_EQ(val1, val2, msg) \
    if ((val1) != (val2)) { \
        std::cerr << "[FAIL] " << msg << " | Expected: " << (val2) << ", Got: " << (val1) << std::endl; \
        std::exit(1); \
    }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << std::endl; \
        std::exit(1); \
    }

class BTreeAdapterTest {
    BTreeAdapter adapter;

    // Helper to create a dummy record easily
    LogRecord createDummyRecord(int64_t ts) {
        LogRecord r;
        r.timestamp = ts;
        r.priority = 1;
        r.pid = 100;
        strncpy_s(r.source, "test", _TRUNCATE);
        strncpy_s(r.host, "localhost", _TRUNCATE);
        strncpy_s(r.message, "dummy payload", _TRUNCATE);
        return r;
    }

    // Helper to setup a clean dummy page
    void SetupPage(Page* page, page_id_t id) {
        page->ResetMemory();
        page->GetHeader()->page_id = id;
    }

public:
    void RunAllTests() {
        std::cout << "Starting BTreeAdapter Tests..." << std::endl;

        TestLeafInit();
        TestLeafInsertSorted();
        TestLeafSplit();
        TestInternalInit();
        TestInternalInsertAndFind();
        TestInternalSplit();
        TestStatisticsUpdate();

        std::cout << "All BTreeAdapter tests passed successfully!" << std::endl;
    }

private:
    void TestLeafInit() {
        Page page;
        SetupPage(&page, 1);

        adapter.initLeaf(&page);
        ASSERT_TRUE(adapter.isLeaf(&page), "Page should be identified as leaf");
        ASSERT_EQ(adapter.getCount(&page), 0, "Initial leaf count should be 0");
        std::cout << "[OK] Leaf Initialization" << std::endl;
    }

    void TestLeafInsertSorted() {
        Page page;
        SetupPage(&page, 1);

        adapter.initLeaf(&page);

        // Insert using Dummy Records instead of integers
        adapter.applyUpdateToLeaf(&page, 300, createDummyRecord(300));
        adapter.applyUpdateToLeaf(&page, 100, createDummyRecord(100));
        adapter.applyUpdateToLeaf(&page, 200, createDummyRecord(200));

        ASSERT_EQ(adapter.getCount(&page), 3, "Leaf should have 3 keys");

        BPlusLeafNode* leaf = reinterpret_cast<BPlusLeafNode*>(page.GetData());
        ASSERT_EQ(leaf->keys[0], 100, "Key[0] order wrong");
        ASSERT_EQ(leaf->keys[1], 200, "Key[1] order wrong");
        ASSERT_EQ(leaf->keys[2], 300, "Key[2] order wrong");

        // Check Value correctness (via timestamp)
        ASSERT_EQ(leaf->values[0].timestamp, 100, "Value match failed");

        std::cout << "[OK] Leaf Insert Sorted" << std::endl;
    }

    void TestLeafSplit() {
        Page left_page;
        Page right_page;

        // FIX: Initialize memory and assign UNIQUE IDs
        SetupPage(&left_page, 10);
        SetupPage(&right_page, 11);

        adapter.initLeaf(&left_page);

        // Use dynamic max keys (returns MAX_KEYS_LEAF ~ 14)
        int max_keys = adapter.getMaxKeys(&left_page);
        std::cout << "    -> Filling Leaf Page up to MAX_KEYS_LEAF (" << max_keys << ")..." << std::endl;

        for (int i = 0; i < max_keys; i++) {
            bool res = adapter.applyUpdateToLeaf(&left_page, i * 10, createDummyRecord(i * 10));
            if (!res) {
                std::cerr << "[FAIL] Failed to insert key at index " << i << std::endl;
                std::exit(1);
            }
        }

        ASSERT_EQ(adapter.getCount(&left_page), max_keys, "Leaf page should be full");

        std::cout << "    -> Splitting Leaf Page..." << std::endl;

        SplitResult result;
        adapter.splitNode(&left_page, &right_page, &result);

        ASSERT_TRUE(result.did_split, "Split should report success");

        int expected_left = max_keys / 2;
        int expected_right = max_keys - expected_left;

        ASSERT_EQ(adapter.getCount(&left_page), expected_left, "Left page count incorrect");
        ASSERT_EQ(adapter.getCount(&right_page), expected_right, "Right page count incorrect");

        // Verify Promoted Key (Copy-up)
        BPlusLeafNode* right = reinterpret_cast<BPlusLeafNode*>(right_page.GetData());
        ASSERT_EQ(result.promoted_key, right->keys[0], "Promoted key mismatch");

        std::cout << "[OK] Leaf Split Logic" << std::endl;
    }

    void TestInternalInit() {
        Page page;
        SetupPage(&page, 2);

        adapter.initInternal(&page);
        ASSERT_TRUE(!adapter.isLeaf(&page), "Page should be identified as Internal");
        std::cout << "[OK] Internal Initialization" << std::endl;
    }

    void TestInternalInsertAndFind() {
        Page page;
        SetupPage(&page, 3);

        adapter.initInternal(&page);

        adapter.createNewRoot(&page, 2, 3, 100);
        ASSERT_EQ(adapter.getCount(&page), 1, "Root should have 1 key");

        adapter.insertIntoInternal(&page, 200, 4);
        adapter.insertIntoInternal(&page, 50, 5);

        ASSERT_EQ(adapter.findChild(&page, 10), 2, "Search for 10 failed");
        ASSERT_EQ(adapter.findChild(&page, 70), 5, "Search for 70 failed");
        ASSERT_EQ(adapter.findChild(&page, 150), 3, "Search for 150 failed");
        ASSERT_EQ(adapter.findChild(&page, 300), 4, "Search for 300 failed");

        std::cout << "[OK] Internal Node Insert & Find" << std::endl;
    }

    void TestInternalSplit() {
        Page left_page;
        Page right_page;

        // FIX: Initialize memory and assign UNIQUE IDs
        SetupPage(&left_page, 20);
        SetupPage(&right_page, 21);

        adapter.initInternal(&left_page);

        // Use MAX_KEYS_INTERNAL (which is large, around 338)
        int max_keys = MAX_KEYS_INTERNAL;

        BPlusInternalNode* internal = reinterpret_cast<BPlusInternalNode*>(left_page.GetData());

        // Manually fill internal node to avoid loop overhead
        for (int i = 0; i < max_keys; i++) {
            internal->keys[i] = i * 10;
            internal->children[i] = i + 1000;
        }
        internal->children[max_keys] = 9999;
        internal->header.key_count = max_keys;

        SplitResult result;
        adapter.splitNode(&left_page, &right_page, &result);

        // Internal Split: Middle key PUSHED UP.
        int split_idx = max_keys / 2;
        ASSERT_EQ(result.promoted_key, split_idx * 10, "Promoted key mismatch");

        ASSERT_EQ(adapter.getCount(&left_page), split_idx, "Left internal count mismatch");

        // Right side
        BPlusInternalNode* right_node = reinterpret_cast<BPlusInternalNode*>(right_page.GetData());
        // First key on right should be the one AFTER split index
        ASSERT_EQ(right_node->keys[0], (split_idx + 1) * 10, "Right first key mismatch");

        std::cout << "[OK] Internal Split Logic" << std::endl;
    }

    void TestStatisticsUpdate() {
        Page page;
        SetupPage(&page, 5);

        adapter.initLeaf(&page);

        adapter.applyUpdateToLeaf(&page, 10, createDummyRecord(10));
        adapter.applyUpdateToLeaf(&page, 100, createDummyRecord(100));

        BPlusNodeHeader* header = reinterpret_cast<BPlusNodeHeader*>(page.GetData());
        ASSERT_EQ(header->min_key, 10, "Min key stat incorrect");
        ASSERT_EQ(header->max_key, 100, "Max key stat incorrect");

        ASSERT_TRUE(adapter.shouldSkip(&page, 200, 300), "Should skip right");
        ASSERT_TRUE(adapter.shouldSkip(&page, 0, 5), "Should skip left");
        ASSERT_TRUE(!adapter.shouldSkip(&page, 50, 60), "Should NOT skip overlap");

        std::cout << "[OK] Statistics & Pruning" << std::endl;
    }
};

int main() {
    BTreeAdapterTest tester;
    tester.RunAllTests();
    return 0;
}