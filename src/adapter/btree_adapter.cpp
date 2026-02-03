#include "btree_adapter.h"
#include <iostream>
#include <cstring>
#include <algorithm> // For std::upper_bound, std::copy, std::distance

namespace cmse::adapter {

    // -------------------------------------------------------------------------
    // Helper: Accessors
    // -------------------------------------------------------------------------

    // Explicit helpers to ensure we cast to the correct struct based on node type logic.
    // Note: The Header part is identical in both, so checking header via either is safe initially.

    BPlusInternalNode* getInternalNode(Page* page) {
        return reinterpret_cast<BPlusInternalNode*>(page->GetData());
    }

    BPlusLeafNode* getLeafNode(Page* page) {
        return reinterpret_cast<BPlusLeafNode*>(page->GetData());
    }

    // -------------------------------------------------------------------------
    // Capacity Helper
    // -------------------------------------------------------------------------

    int BTreeAdapter::getMaxKeys(Page* page) {
        // Reads the header to determine type, then returns the appropriate constant.
        if (isLeaf(page)) {
            return MAX_KEYS_LEAF;     // ~14 records (Heavy nodes)
        }
        else {
            return MAX_KEYS_INTERNAL; // ~338 keys (Light nodes)
        }
    }

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    void BTreeAdapter::initLeaf(Page* page) {
        BPlusLeafNode* leaf = getLeafNode(page);
        leaf->header.is_leaf = true;
        leaf->header.key_count = 0;

        leaf->header.min_key = 0;
        leaf->header.max_key = 0;
        leaf->header.density = 0.0f;

        leaf->next_leaf_id = INVALID_PAGE_ID;

        leaf->header.total_keys = 0; // <--- Add this
        updateStatistics(page);      // <--- Call update to set defaults
    }

    void BTreeAdapter::initInternal(Page* page) {
        BPlusInternalNode* internal = getInternalNode(page);
        internal->header.is_leaf = false;
        internal->header.key_count = 0;

        internal->header.min_key = 0;
        internal->header.max_key = 0;
        internal->header.density = 0.0f;

        internal->header.total_keys = 0; // <--- Add this
        updateStatistics(page);      // <--- Call update to set defaults
    }

    // -------------------------------------------------------------------------
    // Inspection & Search
    // -------------------------------------------------------------------------

    bool BTreeAdapter::isLeaf(Page* page) {
        // It's safe to cast to Header directly
        return reinterpret_cast<BPlusNodeHeader*>(page->GetData())->is_leaf;
    }

    int BTreeAdapter::getCount(Page* page) {
        return reinterpret_cast<BPlusNodeHeader*>(page->GetData())->key_count;
    }

    page_id_t BTreeAdapter::findChild(Page* page, const KeyType& key) {
        BPlusInternalNode* internal = getInternalNode(page);
        int count = internal->header.key_count;

        // Binary Search
        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;

        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        // In internal node, children array has size count + 1.
        // Index returned by upper_bound corresponds to the child pointer index directly.
        // keys[i] separates children[i] and children[i+1].
        return internal->children[index];
    }

    bool BTreeAdapter::shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max) {
        BPlusNodeHeader* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());

        if (header->key_count == 0) return false;

        // Optimization: Skip if node range is strictly outside query range
        if (header->max_key < query_min) return true;
        if (header->min_key > query_max) return true;

        return false;
    }

    // -------------------------------------------------------------------------
    // Modification (Leaf) - Using MAX_KEYS_LEAF
    // -------------------------------------------------------------------------

    bool BTreeAdapter::applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val) {
        BPlusLeafNode* leaf = getLeafNode(leaf_page);

        // Check against LEAF capacity
        if (leaf->header.key_count >= MAX_KEYS_LEAF) {
            return false; // Page is full, require split
        }

        int count = leaf->header.key_count;

        // Binary Search to find insertion point
        auto* keys_begin = leaf->keys;
        auto* keys_end = leaf->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        // Shift existing elements right
        for (int i = count; i > index; i--) {
            leaf->keys[i] = leaf->keys[i - 1];
            leaf->values[i] = leaf->values[i - 1];
        }

        // Insert new entry
        leaf->keys[index] = key;
        leaf->values[index] = val; // Copy assignment of LogRecord
        leaf->header.key_count++;

        updateStatistics(leaf_page);

        return true;
    }

    // -------------------------------------------------------------------------
    // Modification (Internal) - Using MAX_KEYS_INTERNAL
    // -------------------------------------------------------------------------

    void BTreeAdapter::updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id) {
        BPlusInternalNode* internal = getInternalNode(parent_page);
        int count = internal->header.key_count;

        bool found = false;
        // Search in children array (size is count + 1)
        for (int i = 0; i <= count; i++) {
            if (internal->children[i] == old_child_id) {
                internal->children[i] = new_child_id;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "[BTreeAdapter] Error: Parent pointer update failed. Child "
                << old_child_id << " not found." << std::endl;
        }
    }

    bool BTreeAdapter::insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id) {
        BPlusInternalNode* internal = getInternalNode(internal_page);

        // Check against INTERNAL capacity
        if (internal->header.key_count >= MAX_KEYS_INTERNAL) {
            return false;
        }

        int count = internal->header.key_count;

        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        // Shift keys
        for (int i = count; i > index; i--) {
            internal->keys[i] = internal->keys[i - 1];
        }

        // Shift children (children array is larger by 1)
        for (int i = count + 1; i > index + 1; i--) {
            internal->children[i] = internal->children[i - 1];
        }

        internal->keys[index] = key;
        internal->children[index + 1] = right_child_id;
        internal->header.key_count++;

        updateStatistics(internal_page);
        return true;
    }

    // -------------------------------------------------------------------------
    // Structure Management (Splits)
    // -------------------------------------------------------------------------

    void BTreeAdapter::splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result) {
        bool is_leaf_node = isLeaf(node_to_split);

        if (is_leaf_node) {
            // --- LEAF SPLIT (Uses MAX_KEYS_LEAF) ---
            BPlusLeafNode* original = getLeafNode(node_to_split);
            BPlusLeafNode* sibling = getLeafNode(new_right_page);
            initLeaf(new_right_page);

            int split_index = MAX_KEYS_LEAF / 2;

            // Copy 2nd half to sibling
            int sibling_count = 0;
            for (int i = split_index; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling->values[sibling_count] = original->values[i];
                sibling_count++;
            }

            original->header.key_count = split_index;
            sibling->header.key_count = sibling_count;

            // Link Linked-List
            sibling->next_leaf_id = original->next_leaf_id;
            // Original's next pointer update happens in caller (VersionManager/BTreeIndex)

            // Promote Key (Copy Up for Leaf)
            out_result->promoted_key = sibling->keys[0];

            updateStatistics(node_to_split);
            updateStatistics(new_right_page);

        }
        else {
            // --- INTERNAL SPLIT (Uses MAX_KEYS_INTERNAL) ---
            BPlusInternalNode* original = getInternalNode(node_to_split);
            BPlusInternalNode* sibling = getInternalNode(new_right_page);
            initInternal(new_right_page);

            int split_index = MAX_KEYS_INTERNAL / 2;

            // Promote Key (Push Up for Internal)
            out_result->promoted_key = original->keys[split_index];

            // Move keys to sibling
            int sibling_count = 0;
            for (int i = split_index + 1; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling_count++;
            }

            // Move children to sibling
            for (int i = split_index + 1; i <= original->header.key_count; i++) {
                sibling->children[i - (split_index + 1)] = original->children[i];
            }

            sibling->header.key_count = sibling_count;
            original->header.key_count = split_index;

            updateStatistics(node_to_split);
            updateStatistics(new_right_page);
        }

        out_result->did_split = true;
    }

    void BTreeAdapter::createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key) {
        initInternal(new_root_page);
        BPlusInternalNode* root = getInternalNode(new_root_page);

        root->keys[0] = key;
        root->children[0] = left_child;
        root->children[1] = right_child;
        root->header.key_count = 1;

        updateStatistics(new_root_page);
    }

    // -------------------------------------------------------------------------
    // Phase 3 Statistics
    // -------------------------------------------------------------------------

// --- Statistics Update (Phase 3 Compliant) ---
    void BTreeAdapter::updateStatistics(Page* page) {
        auto* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;

        // 1. Handle Empty Page
        if (count == 0) {
            header->min_key = std::numeric_limits<KeyType>::max();
            header->max_key = std::numeric_limits<KeyType>::min();
            header->density = 0.0f;
            header->total_keys = 0;
            return;
        }

        // 2. Update Min/Max based on Node Type
        // Note: For Internal Nodes, this is a local approximation. 
        // True subtree stats are propagated via Insert/Delete.
        if (header->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode*>(page->GetData());
            header->min_key = leaf->keys[0];
            header->max_key = leaf->keys[count - 1];

            // For a leaf, total_keys is just the local count
            header->total_keys = count;
        }
        else {
            auto* internal = reinterpret_cast<BPlusInternalNode*>(page->GetData());
            // In Phase 3, Internal Node stats (min/max/total) are usually 
            // updated incrementally by the BTreeIndex class. 
            // However, as a fallback, we set local bounds:
            header->min_key = internal->keys[0];
            header->max_key = internal->keys[count - 1];

            // We do NOT reset total_keys here for internal nodes because 
            // it holds the aggregate sum of children, which we can't see here.
        }

        // 3. Calculate Density (Phase 3 Formula)
        // Formula: total_keys / (max_key - min_key + 1)
        if (header->max_key >= header->min_key) {
            double range = (double)(header->max_key - header->min_key) + 1.0;

            // Prevent division by zero or weird ranges
            if (range > 0) {
                header->density = (float)((double)header->total_keys / range);
            }
            else {
                header->density = 1.0f;
            }
        }
        else {
            header->density = 0.0f;
        }
    }

} // namespace cmse::adapter