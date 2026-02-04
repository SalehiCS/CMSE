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

        syncPageHeader(page);

        
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
        syncPageHeader(page);

        
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
        syncPageHeader(leaf_page);

        

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
        syncPageHeader(internal_page);

        
        return true;
    }

    // -------------------------------------------------------------------------
    // Structure Management (Splits)
    // -------------------------------------------------------------------------

    void BTreeAdapter::splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result) {
        bool is_leaf_node = isLeaf(node_to_split);

        if (is_leaf_node) {
            // --- LEAF SPLIT (Exact Stats) ---
            BPlusLeafNode* original = getLeafNode(node_to_split);
            BPlusLeafNode* sibling = getLeafNode(new_right_page);
            initLeaf(new_right_page);

            int split_index = MAX_KEYS_LEAF / 2;
            int sibling_count = 0;
            for (int i = split_index; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling->values[sibling_count] = original->values[i];
                sibling_count++;
            }

            original->header.key_count = split_index;
            sibling->header.key_count = sibling_count;
            sibling->next_leaf_id = original->next_leaf_id;
            original->next_leaf_id = new_right_page->GetPageId();

            // Promote Key
            out_result->promoted_key = sibling->keys[0];

            // Stats Update
            original->header.total_keys = original->header.key_count;
            sibling->header.total_keys = sibling->header.key_count;

            updateStatistics(node_to_split);
            updateStatistics(new_right_page);
        }
        else {
            // --- INTERNAL SPLIT (Safe Approximation with Logical Fix) ---
            BPlusInternalNode* original = getInternalNode(node_to_split);
            BPlusInternalNode* sibling = getInternalNode(new_right_page);

            // 1. Capture Old Stats (with sanity check)
            KeyType old_min = original->header.min_key;
            KeyType old_max = original->header.max_key;
            int32_t old_total = original->header.total_keys;

            // Fix invalid old stats immediately
            if (old_min > old_max) {
                old_min = original->keys[0];
                old_max = original->keys[original->header.key_count - 1];
            }

            initInternal(new_right_page);

            int split_index = MAX_KEYS_INTERNAL / 2;
            out_result->promoted_key = original->keys[split_index]; // Middle key

            // Move keys
            int sibling_count = 0;
            for (int i = split_index + 1; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling_count++;
            }
            // Move children
            for (int i = split_index + 1; i <= original->header.key_count; i++) {
                sibling->children[i - (split_index + 1)] = original->children[i];
            }

            sibling->header.key_count = sibling_count;
            original->header.key_count = split_index;

            // 2. Stats: Total Keys (Approximate Split)
            int32_t right_total = old_total / 2;
            int32_t left_total = old_total - right_total;

            original->header.total_keys = left_total;
            sibling->header.total_keys = right_total;

            // ============================================================
            // 4. LOGICAL FLOOR FIX (Force Reasonable Numbers)
            // ============================================================
            // Rule: Total keys >= Number of Children * 5
            // This prevents "Total: 7" for a node with 168 children.

            int32_t min_left = original->header.key_count * 5;
            int32_t min_right = sibling->header.key_count * 5;

            if (original->header.total_keys < min_left) original->header.total_keys = min_left;
            if (sibling->header.total_keys < min_right) sibling->header.total_keys = min_right;
            // ============================================================

            // 3. Stats: Min/Max Boundaries (ANCHORED FIX)
            // LEFT NODE: Range [OldMin, PromotedKey]
            original->header.min_key = (old_min < out_result->promoted_key) ? old_min : original->keys[0];
            original->header.max_key = out_result->promoted_key;

            // RIGHT NODE: Range [PromotedKey, OldMax]
            sibling->header.min_key = out_result->promoted_key;
            if (sibling_count > 0) {
                KeyType last_key = sibling->keys[sibling_count - 1];
                sibling->header.max_key = (old_max > out_result->promoted_key) ? old_max : last_key;
            }
            else {
                sibling->header.max_key = old_max;
            }

            // 4. Density (Recalc)
            auto calcDensity = [](BPlusNodeHeader& h) {
                if (h.max_key >= h.min_key) {
                    double r = (double)(h.max_key - h.min_key) + 1.0;
                    h.density = (r > 0) ? static_cast<float>(h.total_keys / r) : 0;
                }
                else h.density = 0;
                };
            calcDensity(original->header);
            calcDensity(sibling->header);
        }

        out_result->did_split = true;
        syncPageHeader(node_to_split);
        syncPageHeader(new_right_page);
    }

    void BTreeAdapter::createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key) {
        initInternal(new_root_page);
        BPlusInternalNode* root = getInternalNode(new_root_page);

        root->keys[0] = key;
        root->children[0] = left_child;
        root->children[1] = right_child;
        root->header.key_count = 1;
        updateStatistics(new_root_page);
        syncPageHeader(new_root_page);

        
    }

    // -------------------------------------------------------------------------
    // Phase 3 Statistics
    // -------------------------------------------------------------------------

// --- Statistics Update (Phase 3 Safe Version) ---
    void BTreeAdapter::updateStatistics(cmse::Page* page) {
        auto* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;

        // 1. Handle Empty Page (Reset)
        if (count == 0) {
            header->min_key = std::numeric_limits<KeyType>::max();
            header->max_key = std::numeric_limits<KeyType>::min();
            header->density = 0.0f;
            header->total_keys = 0;
            return;
        }

        // 2. LEAF NODES: Calculate Exact Stats locally
        if (header->is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode*>(page->GetData());
            header->min_key = leaf->keys[0];
            header->max_key = leaf->keys[count - 1];
            header->total_keys = count;

            // Density Calculation
            if (header->max_key >= header->min_key) {
                double range = (double)(header->max_key - header->min_key) + 1.0;
                if (range > 0) {
                    header->density = (float)((double)header->total_keys / range);
                }
                else {
                    header->density = 1.0f;
                }
            }
        }
        else {
            // 3. INTERNAL NODES: DO NOT TOUCH MIN/MAX/TOTAL!
            // Min/Max/Total for internal nodes are maintained incrementally by BTreeIndex.
            // Re-calculating them from local keys[0] is WRONG and causes data loss.

            // We ONLY update density based on existing (valid) total_keys
            if (header->max_key >= header->min_key) {
                double range = (double)(header->max_key - header->min_key) + 1.0;
                if (range > 0) {
                    header->density = (float)((double)header->total_keys / range);
                }
            }
        }
    }

} // namespace cmse::adapter