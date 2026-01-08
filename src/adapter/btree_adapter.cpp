#include "btree_adapter.h"
#include <iostream>
#include <cstring>
#include <algorithm> // For std::upper_bound, std::copy

namespace cmse::adapter {

    // -------------------------------------------------------------------------
    // Helper: Accessors for Raw Page Data
    // -------------------------------------------------------------------------

    // Helper to cast raw page data to Internal Node structure
    BPlusInternalNode* getInternalNode(Page* page) {
        return reinterpret_cast<BPlusInternalNode*>(page->GetData());
    }

    // Helper to cast raw page data to Leaf Node structure
    BPlusLeafNode* getLeafNode(Page* page) {
        return reinterpret_cast<BPlusLeafNode*>(page->GetData());
    }

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    void BTreeAdapter::initLeaf(Page* page) {
        BPlusLeafNode* leaf = getLeafNode(page);
        leaf->header.is_leaf = true;
        leaf->header.key_count = 0;

        // Initialize stats (Phase 3)
        leaf->header.min_key = 0;
        leaf->header.max_key = 0;
        leaf->header.density = 0.0f;

        leaf->next_leaf_id = INVALID_PAGE_ID;
    }

    void BTreeAdapter::initInternal(Page* page) {
        BPlusInternalNode* internal = getInternalNode(page);
        internal->header.is_leaf = false;
        internal->header.key_count = 0;

        // Initialize stats (Phase 3)
        internal->header.min_key = 0;
        internal->header.max_key = 0;
        internal->header.density = 0.0f;
    }

    // -------------------------------------------------------------------------
    // Inspection & Search
    // -------------------------------------------------------------------------

    bool BTreeAdapter::isLeaf(Page* page) {
        return getLeafNode(page)->header.is_leaf;
    }

    int BTreeAdapter::getCount(Page* page) {
        return getLeafNode(page)->header.key_count;
    }

    page_id_t BTreeAdapter::findChild(Page* page, const KeyType& key) {
        BPlusInternalNode* internal = getInternalNode(page);
        int count = internal->header.key_count;

        // Binary Search using std::upper_bound
        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;

        auto it = std::upper_bound(keys_begin, keys_end, key);

        // FIX: Explicit cast to int to suppress warning C4244
        int index = static_cast<int>(std::distance(keys_begin, it));

        return internal->children[index];
    }

    bool BTreeAdapter::shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max) {
        BPlusNodeHeader* header = getHeader(page);

        if (header->key_count == 0) return false;

        if (header->max_key < query_min) return true;
        if (header->min_key > query_max) return true;

        return false;
    }

    // -------------------------------------------------------------------------
    // Modification (Leaf)
    // -------------------------------------------------------------------------

    bool BTreeAdapter::applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val) {
        BPlusLeafNode* leaf = getLeafNode(leaf_page);

        if (leaf->header.key_count >= MAX_KEYS) {
            return false; // Page is full
        }

        int count = leaf->header.key_count;

        // Find position to insert using Binary Search
        auto* keys_begin = leaf->keys;
        auto* keys_end = leaf->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);

        // FIX: Explicit cast to int
        int index = static_cast<int>(std::distance(keys_begin, it));

        // Shift existing elements to the right
        for (int i = count; i > index; i--) {
            leaf->keys[i] = leaf->keys[i - 1];
            leaf->values[i] = leaf->values[i - 1];
        }

        // Insert new entry
        leaf->keys[index] = key;
        leaf->values[index] = val;
        leaf->header.key_count++;

        updateStatistics(leaf_page);

        return true;
    }

    // -------------------------------------------------------------------------
    // Modification (Internal)
    // -------------------------------------------------------------------------

    void BTreeAdapter::updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id) {
        BPlusInternalNode* internal = getInternalNode(parent_page);
        int count = internal->header.key_count;

        bool found = false;
        for (int i = 0; i <= count; i++) {
            if (internal->children[i] == old_child_id) {
                internal->children[i] = new_child_id;
                found = true;
                break;
            }
        }

        if (!found) {
            std::cerr << "[BTreeAdapter] CRITICAL: Could not find child pointer "
                << old_child_id << " in parent page." << std::endl;
        }
    }

    bool BTreeAdapter::insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id) {
        BPlusInternalNode* internal = getInternalNode(internal_page);

        if (internal->header.key_count >= MAX_KEYS) {
            return false; // Full
        }

        int count = internal->header.key_count;

        // Find position using Binary Search
        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);

        // FIX: Explicit cast to int
        int index = static_cast<int>(std::distance(keys_begin, it));

        // Shift keys
        for (int i = count; i > index; i--) {
            internal->keys[i] = internal->keys[i - 1];
        }

        // Shift children
        for (int i = count + 1; i > index + 1; i--) {
            internal->children[i] = internal->children[i - 1];
        }

        // Insert data
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
            // --- LEAF SPLIT ---
            BPlusLeafNode* original = getLeafNode(node_to_split);
            BPlusLeafNode* sibling = getLeafNode(new_right_page);
            initLeaf(new_right_page);

            int split_index = MAX_KEYS / 2;

            // Copy data to new sibling
            int sibling_count = 0;
            for (int i = split_index; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling->values[sibling_count] = original->values[i];
                sibling_count++;
            }

            original->header.key_count = split_index;
            sibling->header.key_count = sibling_count;

            sibling->next_leaf_id = original->next_leaf_id;

            // Promote key (Copy up)
            out_result->promoted_key = sibling->keys[0];

            updateStatistics(node_to_split);
            updateStatistics(new_right_page);

        }
        else {
            // --- INTERNAL SPLIT ---
            BPlusInternalNode* original = getInternalNode(node_to_split);
            BPlusInternalNode* sibling = getInternalNode(new_right_page);
            initInternal(new_right_page);

            int split_index = MAX_KEYS / 2;

            // Promote key (Push up)
            out_result->promoted_key = original->keys[split_index];

            int sibling_count = 0;
            for (int i = split_index + 1; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling_count++;
            }

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

    void BTreeAdapter::updateStatistics(Page* page) {
        BPlusNodeHeader* header = getHeader(page);
        bool is_leaf = header->is_leaf;
        int count = header->key_count;

        if (count == 0) {
            header->min_key = 0;
            header->max_key = 0;
            header->density = 0.0f;
            return;
        }

        if (is_leaf) {
            BPlusLeafNode* leaf = getLeafNode(page);
            header->min_key = leaf->keys[0];
            header->max_key = leaf->keys[count - 1];
        }
        else {
            BPlusInternalNode* internal = getInternalNode(page);
            header->min_key = internal->keys[0];
            header->max_key = internal->keys[count - 1];
        }

        header->density = static_cast<float>(count) / static_cast<float>(MAX_KEYS);
    }

} // namespace cmse::adapter