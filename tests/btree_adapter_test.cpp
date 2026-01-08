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

// --- Improved Helper Macros ---
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

public:
    void RunAllTests() {
        std::cout << "Starting BTreeAdapter Tests..." << std::endl;

        TestLeafInit();
        TestLeafInsertSorted();
        TestLeafSplit();       // <--- The failing test
        TestInternalInit();
        TestInternalInsertAndFind();
        TestInternalSplit();
        TestStatisticsUpdate();

        std::cout << "All BTreeAdapter tests passed successfully!" << std::endl;
    }

private:
    void TestLeafInit() {
        Page page;
        adapter.initLeaf(&page);
        ASSERT_TRUE(adapter.isLeaf(&page), "Page should be identified as leaf");
        ASSERT_EQ(adapter.getCount(&page), 0, "Initial leaf count should be 0");
        std::cout << "[OK] Leaf Initialization" << std::endl;
    }

    void TestLeafInsertSorted() {
        Page page;
        adapter.initLeaf(&page);

        adapter.applyUpdateToLeaf(&page, 300, 30);
        adapter.applyUpdateToLeaf(&page, 100, 10);
        adapter.applyUpdateToLeaf(&page, 200, 20);

        ASSERT_EQ(adapter.getCount(&page), 3, "Leaf should have 3 keys");

        BPlusLeafNode* leaf = reinterpret_cast<BPlusLeafNode*>(page.GetData());
        ASSERT_EQ(leaf->keys[0], 100, "Key[0] order wrong");
        ASSERT_EQ(leaf->keys[1], 200, "Key[1] order wrong");
        ASSERT_EQ(leaf->keys[2], 300, "Key[2] order wrong");

        std::cout << "[OK] Leaf Insert Sorted" << std::endl;
    }

    void TestLeafSplit() {
        Page left_page;
        Page right_page;
        adapter.initLeaf(&left_page);

        std::cout << "   -> Filling Leaf Page up to MAX_KEYS (" << MAX_KEYS << ")..." << std::endl;

        // Fill the leaf to MAX_KEYS
        for (int i = 0; i < MAX_KEYS; i++) {
            // Keys: 0, 10, 20 ... 
            bool res = adapter.applyUpdateToLeaf(&left_page, i * 10, i);

            if (!res) {
                std::cerr << "[FAIL] Failed to insert key at index " << i
                    << ". Current Count: " << adapter.getCount(&left_page)
                    << ", MAX_KEYS: " << MAX_KEYS << std::endl;
                std::exit(1);
            }
        }

        // Validate Full State
        ASSERT_EQ(adapter.getCount(&left_page), MAX_KEYS, "Leaf page should be completely full before split");

        std::cout << "   -> Splitting Leaf Page..." << std::endl;

        // Split
        SplitResult result;
        adapter.splitNode(&left_page, &right_page, &result);

        ASSERT_TRUE(result.did_split, "Split should report success");

        // Check Counts
        // Split logic: MAX_KEYS=101. mid=50.
        // Left: 0..49 (50 items)
        // Right: 50..100 (51 items)
        int expected_left = MAX_KEYS / 2;
        int expected_right = MAX_KEYS - expected_left;

        ASSERT_EQ(adapter.getCount(&left_page), expected_left, "Left page count after split incorrect");
        ASSERT_EQ(adapter.getCount(&right_page), expected_right, "Right page count after split incorrect");

        // Check Promoted Key
        // Key at index 50 is 50 * 10 = 500.
        ASSERT_EQ(result.promoted_key, 500, "Promoted key mismatch");

        // Verify Data Integrity
        BPlusLeafNode* left = reinterpret_cast<BPlusLeafNode*>(left_page.GetData());
        BPlusLeafNode* right = reinterpret_cast<BPlusLeafNode*>(right_page.GetData());

        // Left ends at 490
        ASSERT_EQ(left->keys[expected_left - 1], 490, "Left page last key mismatch");
        // Right starts at 500
        ASSERT_EQ(right->keys[0], 500, "Right page first key mismatch");

        std::cout << "[OK] Leaf Split Logic" << std::endl;
    }

    void TestInternalInit() {
        Page page;
        adapter.initInternal(&page);
        ASSERT_TRUE(!adapter.isLeaf(&page), "Page should be identified as Internal");
        std::cout << "[OK] Internal Initialization" << std::endl;
    }

    void TestInternalInsertAndFind() {
        Page page;
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
        adapter.initInternal(&left_page);

        BPlusInternalNode* internal = reinterpret_cast<BPlusInternalNode*>(left_page.GetData());

        for (int i = 0; i < MAX_KEYS; i++) {
            internal->keys[i] = i * 10;
            internal->children[i] = i + 1000;
        }
        internal->children[MAX_KEYS] = 9999;
        internal->header.key_count = MAX_KEYS;

        SplitResult result;
        adapter.splitNode(&left_page, &right_page, &result);

        // Internal Split: Middle key PUSHED UP.
        // mid = 50. Key = 500.
        ASSERT_EQ(result.promoted_key, 500, "Promoted key should be 500");

        // Left: 0..49 (50 items)
        ASSERT_EQ(adapter.getCount(&left_page), 50, "Left internal count mismatch");

        // Right: 51..100 (50 items) -> Key 50 is GONE.
        ASSERT_EQ(adapter.getCount(&right_page), 50, "Right internal count mismatch");

        BPlusInternalNode* right_node = reinterpret_cast<BPlusInternalNode*>(right_page.GetData());
        ASSERT_EQ(right_node->keys[0], 510, "Right first key should be 510");

        std::cout << "[OK] Internal Split Logic" << std::endl;
    }

    void TestStatisticsUpdate() {
        Page page;
        adapter.initLeaf(&page);

        adapter.applyUpdateToLeaf(&page, 10, 1);
        adapter.applyUpdateToLeaf(&page, 100, 3);

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