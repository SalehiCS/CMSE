#pragma once
#include "../storage/page/page.h"
#include "../common/types.h"
#include <vector>
#include <cstring>
#include <algorithm>

namespace cmse::adapter {

    /**
     * SplitResult
     * Output structure for the splitNode operation.
     * Captures the result of a page split to propagate up to the parent.
     */
    struct SplitResult {
        bool did_split = false;
        page_id_t left_page_id = INVALID_PAGE_ID;
        page_id_t right_page_id = INVALID_PAGE_ID; // The new page created during split
        KeyType promoted_key;                      // The key to be inserted into the parent
    };

    /**
     * BPlusNodeHeader
     * The standard header for every B+Tree page (Internal or Leaf).
     * * OPTIMIZATION NOTES:
     * 1. No 'parent_page_id': Parent pointers are removed to support efficient Copy-on-Write (CoW).
     * Parent tracking is handled via recursion/stack in VersionManager.
     * 2. Phase 3 Stats: 'min_key', 'max_key', and 'density' are reserved here to avoid
     * changing data layout later in Phase 3.
     */
    struct BPlusNodeHeader {
        bool is_leaf;
        int16_t key_count;

        // --- Phase 3: Statistical Indexing Metadata ---
        KeyType min_key;
        KeyType max_key;
        float density;    // (key_count / MAX_CAPACITY)
    };

    // Constants for array sizing (Simplified for project)
    // In a real system, these depend on KeyType size and PAGE_SIZE.
    constexpr int MAX_KEYS = 100;

    /**
     * BPlusInternalNode
     * Maps the raw Page data for Internal Nodes.
     * Memory Layout: [Header] [Keys Array] [Children PageIDs Array]
     */
    struct BPlusInternalNode {
        BPlusNodeHeader header;
        KeyType keys[MAX_KEYS];
        page_id_t children[MAX_KEYS + 1]; // N keys, N+1 children
    };

    /**
     * BPlusLeafNode
     * Maps the raw Page data for Leaf Nodes.
     * Memory Layout: [Header] [Keys Array] [Values Array] [Next Leaf ID]
     */
    struct BPlusLeafNode {
        BPlusNodeHeader header;
        KeyType keys[MAX_KEYS];
        ValueType values[MAX_KEYS];
        page_id_t next_leaf_id; // For Range Queries
    };


    /**
     * BTreeAdapter
     * Concrete implementation of the TreeAdapter interface for B+Tree logic.
     * Handles raw byte manipulation, splitting, and CoW pointer updates.
     */
    class BTreeAdapter {
    public:
        // --- Initialization Helpers ---
        void initLeaf(Page* page);
        void initInternal(Page* page);

        // --- Inspection (ReadOnly) ---
        bool isLeaf(Page* page);
        int getCount(Page* page);

        // Returns the child page ID that should contain the key (for Internal Nodes)
        page_id_t findChild(Page* internal_page, const KeyType& key);

        // --- Phase 3: Statistics (ReadOnly) ---
        // Checks if a subtree can be skipped during a range query based on min/max stats.
        bool shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max);


        // --- Modification Operations (Performed on CoW Copies) ---

        // Applies an insert/update on a LEAF page.
        // Returns true if successful, false if the page is full (needs split).
        bool applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val);

        // Updates a child pointer in a PARENT page.
        // Critical for CoW: When a child gets a new PageID, the parent must point to it.
        void updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id);

        // Inserts a promoted key and new child pointer into an INTERNAL node.
        // Returns true if successful, false if full (needs split).
        bool insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id);


        // --- Structure Management (Split/Merge) ---

        // Splits a full node (Leaf or Internal).
        // node_to_split: The full page (source).
        // new_right_page: An empty page allocated by VersionManager.
        // out_result: Filled with split details (promoted key, new IDs).
        void splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result);

        // Creates a new root when the old root splits (Tree height grows).
        // new_root_page: Empty page allocated for the new root.
        void createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key);

        // --- Phase 3: Stats Calculation ---
        // Recalculates min_key, max_key, and density. Called after modification.
        void updateStatistics(Page* page);

    private:
        // Helper to access raw headers
        BPlusNodeHeader* getHeader(Page* page) {
            return reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        }
    };

} // namespace cmse::adapter