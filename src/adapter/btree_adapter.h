#pragma once
// Core page definition for accessing the underlying byte buffer.
#include "../page/page.h" 
// Project-specific types: page_id_t, KeyType (int), and ValueType (LogRecord).
#include "../common/types.h"
// Standard library containers for internal adapter logic.
#include <vector>
// C-string library for low-level memory operations like memcpy or memset.
#include <cstring>
// Standard algorithms for binary searches and sorting within nodes.
#include <algorithm>
#include "../common/logger.h"

namespace cmse::adapter {

    /**
     * SplitResult
     * PURPOSE: A data packet used to return information from a node split operation.
     * WHY: When a node overflows, the parent needs to know the new sibling's ID
     * and the key that must be "promoted" to the upper level of the tree.
     */
    struct SplitResult {
        bool did_split = false;                      // Flag to verify if split occurred.
        page_id_t left_page_id = INVALID_PAGE_ID;    // The original page (shadowed).
        page_id_t right_page_id = INVALID_PAGE_ID;   // The newly created sibling page.
        KeyType promoted_key;                        // The key that acts as the new separator.
    };

    /**
     * BPlusNodeHeader
     * PURPOSE: Metadata stored at the very beginning of every physical Page.
     * WHY: This allows the engine to understand the node type and range
     * without scanning the entire 4KB block.
     */
    struct BPlusNodeHeader {
        bool is_leaf;           // True if node contains data; False if it contains index pointers.
        int16_t key_count;      // Number of active keys currently in the node.
        uint8_t is_dirty;       // State tracker: 0 = Stats are valid; 1 = Stats need recalculation.

        // --- Phase 3 Optimization Fields ---
        KeyType min_key;        // Lowest key in this node/subtree (for pruning).
        KeyType max_key;        // Highest key in this node/subtree (for pruning).
        float density;          // Sparsity metric: total_keys / (max - min).
        int32_t total_keys;     // Total count of records in the entire subtree below this node.
    };

    // Constant defining the size of the metadata block for calculation purposes.
    constexpr int HEADER_SIZE = sizeof(BPlusNodeHeader);
    // Standard hardware/OS page size used by the DiskManager.
    constexpr int PAGE_SIZE = 4096;

    /**
     * SAFETY_MARGIN
     * PURPOSE: Prevents buffer overflows.
     * WHY: Compilers often add "padding" bytes between struct members for alignment.
     * This 32-byte buffer ensures the struct never exceeds the 4096 physical page limit.
     */
    constexpr int SAFETY_MARGIN = 32;

    // Calculate how many Key-Value pairs fit in one leaf (Values are larger than page IDs).
    constexpr int MAX_KEYS_LEAF = (PAGE_SIZE - HEADER_SIZE - sizeof(page_id_t) - SAFETY_MARGIN) / (sizeof(KeyType) + sizeof(ValueType));
    // Calculate how many Key-Pointer pairs fit in one index node (High fan-out).
    constexpr int MAX_KEYS_INTERNAL = (PAGE_SIZE - HEADER_SIZE - SAFETY_MARGIN) / (sizeof(KeyType) + sizeof(page_id_t));

    /**
     * BPlusInternalNode
     * PURPOSE: An index node that directs the search down the tree.
     * LAYOUT: [Header][Key0, Key1...][Child0, Child1...]
     */
    struct BPlusInternalNode {
        BPlusNodeHeader header;                     // Fixed-size metadata.
        KeyType keys[MAX_KEYS_INTERNAL];            // Sorted array of keys.
        page_id_t children[MAX_KEYS_INTERNAL + 1];  // Pointers to the level below.
    };

    /**
     * BPlusLeafNode
     * PURPOSE: A data node at the bottom of the tree containing actual records.
     * LAYOUT: [Header][Key0, Key1...][Value0, Value1...][NextLeafID]
     */
    struct BPlusLeafNode {
        BPlusNodeHeader header;                     // Fixed-size metadata.
        KeyType keys[MAX_KEYS_LEAF];                // Sorted keys.
        ValueType values[MAX_KEYS_LEAF];            // LogRecord data.
        page_id_t next_leaf_id;                     // Pointer for leaf-level sequential scans.
    };

    /**
     * BTreeAdapter
     * PURPOSE: Logic layer that manipulates raw Page memory as structured B+ Tree nodes.
     * WHY: The BufferPool only gives us raw bytes; this class provides the "intelligence"
     * to perform insertions, searches, and splits on those bytes.
     */
    class BTreeAdapter {
    public:
        // Initialization functions for formatting blank pages.
        void initLeaf(Page* page);
        void initInternal(Page* page);

        // Node inspection methods.
        bool isLeaf(Page* page);
        int getCount(Page* page);
        int getMaxKeys(Page* page);

        // Search and optimization logic.
        // [UPDATED] Added 'for_write' to handle duplicate key routing
        page_id_t findChild(Page* internal_page, const KeyType& key, bool for_write);
        bool shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max);

        // Modification and structural logic.
        bool applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val);
        void updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id);
        bool insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id);

        // Tree growth and maintenance.
        void splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result);
        void createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key);

        // Statistics and Dirty State management.
        void updateStatistics(Page* page);
        void setDirty(Page* page);

    private:
        // Internal helper to access the header area of a raw page.
        BPlusNodeHeader* getHeader(Page* page) {
            return reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        }

        // Internal helper to treat a raw page as an internal index node.
        BPlusInternalNode* getInternalNode(Page* page) {
            return reinterpret_cast<BPlusInternalNode*>(page->GetData());
        }

        // Internal helper to treat a raw page as a data leaf node.
        BPlusLeafNode* getLeafNode(Page* page) {
            return reinterpret_cast<BPlusLeafNode*>(page->GetData());
        }

        /**
         * syncPageHeader
         * PURPOSE: Synchronizes the internal BTree metadata with the generic Page metadata.
         * WHY: This ensures the BufferPoolManager and DiskManager see the correct
         * type and count information for logging and debugging.
         */
        void syncPageHeader(Page* page) {
            auto* hdr = reinterpret_cast<BPlusNodeHeader*>(page->GetData());

            if (page->GetPageId() == 4036 && !hdr->is_leaf) {
                std::cout << "[CORRUPTION WRITER FOUND] syncPageHeader wrote INTERNAL into 4036";
                abort();
            }

            BPlusNodeHeader* internal_h = getHeader(page);
            PageHeader* external_h = page->GetHeader();

            external_h->is_leaf = internal_h->is_leaf ? 1 : 0;
            external_h->key_count = internal_h->key_count;
        }
    };

} // namespace cmse::adapter