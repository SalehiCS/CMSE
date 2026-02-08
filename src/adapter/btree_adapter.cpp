#include "btree_adapter.h"
#include <iostream>
#include <cstring>
#include <algorithm> // For std::upper_bound, std::copy, std::distance
#include "../common/logger.h"


namespace cmse::adapter {

    // -------------------------------------------------------------------------
    // Helper: Accessors
    // -------------------------------------------------------------------------

    /**
     * getInternalNode
     * PURPOSE: Interprets the raw byte buffer of a Page as an Internal Node structure.
     * WHY: Pages are raw memory; we must cast them to access the keys and child pointers.
     */
    BPlusInternalNode* getInternalNode(Page* page) {
        return reinterpret_cast<BPlusInternalNode*>(page->GetData());
    }

    /**
     * getLeafNode
     * PURPOSE: Interprets the raw byte buffer as a Leaf Node structure.
     * WHY: Leaves contain values (LogRecords) instead of page pointers.
     */
    BPlusLeafNode* getLeafNode(Page* page) {
        return reinterpret_cast<BPlusLeafNode*>(page->GetData());
    }

    // -------------------------------------------------------------------------
    // Capacity Helper
    // -------------------------------------------------------------------------

    /**
     * getMaxKeys
     * PURPOSE: Returns the branching factor/limit for a specific node type.
     * WHY: Leaf nodes carry large data payloads (Values), so they hold fewer keys.
     * Internal nodes only store keys and IDs, allowing for a much higher fan-out.
     */
    int BTreeAdapter::getMaxKeys(Page* page) {
        if (isLeaf(page)) {
            return MAX_KEYS_LEAF;     // Limit for data-heavy nodes
        }
        else {
            return MAX_KEYS_INTERNAL; // Limit for pointer-heavy nodes
        }
    }

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    /**
     * initLeaf
     * PURPOSE: Formats a fresh Page into a B+ Tree Leaf.
     * LOGIC: Sets the leaf flag, clears the dirty status, and initializes stats.
     * WHY: next_leaf_id is set to INVALID_PAGE_ID to signify the end of the linked list.
     */
    void BTreeAdapter::initLeaf(Page* page) {
        BPlusLeafNode* leaf = getLeafNode(page);
        leaf->header.is_leaf = true;
        leaf->header.is_dirty = 0; // Freshly initialized nodes start "Clean"
        leaf->header.key_count = 0;

        leaf->header.min_key = 0;
        leaf->header.max_key = 0;
        leaf->header.density = 0.0f;

        leaf->next_leaf_id = INVALID_PAGE_ID; // Terminate linked list

        leaf->header.total_keys = 0;

        updateStatistics(page);      // Establish default stats immediately
        syncPageHeader(page);        // Commit header changes to the buffer
    }

    /**
     * initInternal
     * PURPOSE: Formats a fresh Page into an Internal Index Node.
     * WHY: Internal nodes must have is_leaf = false so traversals know to follow pointers.
     */
    void BTreeAdapter::initInternal(Page* page) {
        BPlusInternalNode* internal = getInternalNode(page);
        internal->header.is_leaf = false;
        internal->header.is_dirty = 0;
        internal->header.key_count = 0;

        internal->header.min_key = 0;
        internal->header.max_key = 0;
        internal->header.density = 0.0f;

        internal->header.total_keys = 0;
        updateStatistics(page);
        syncPageHeader(page);
    }

    // -------------------------------------------------------------------------
    // Inspection & Search
    // -------------------------------------------------------------------------

    /**
     * isLeaf
     * PURPOSE: Identifies node type via the common header.
     * WHY: Since Header is the first member of both structs, this cast is safe for any B+ Node.
     */
    bool BTreeAdapter::isLeaf(Page* page) {
        return reinterpret_cast<BPlusNodeHeader*>(page->GetData())->is_leaf;
    }

    /**
     * getCount
     * PURPOSE: Returns the number of active keys currently stored in the node.
     */
    int BTreeAdapter::getCount(Page* page) {
        return reinterpret_cast<BPlusNodeHeader*>(page->GetData())->key_count;
    }

    /**
     * findChild
     * PURPOSE: Determines which child page to descend into during a search.
     * LOGIC: Uses binary search (std::upper_bound) to find the first key > search key.
     * WHY: In a B+ Tree, keys[i] separates children[i] and children[i+1].
     */
    page_id_t BTreeAdapter::findChild(Page* page, const KeyType& key) {
        BPlusInternalNode* internal = getInternalNode(page);
        int count = internal->header.key_count;

        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;

        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        return internal->children[index]; // Return pointer to the correct branch
    }

    /**
     * shouldSkip
     * PURPOSE: Pruning optimization for range queries.
     * WHY: If the entire range of this node [min, max] is outside the query range,
     * we save disk I/O by not descending into this subtree.
     */
    bool BTreeAdapter::shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max) {
        BPlusNodeHeader* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());

        // It is safer to visit an empty page and find nothing than
        // to skip a page that might have been the starting point for new insertions.
        // Guarding Against Uninitialized Metadata
        if (header->key_count == 0) return false;

        // Logic: Return true if Node Max < Query Min OR Node Min > Query Max
        if (header->max_key < query_min) return true;
        if (header->min_key > query_max) return true;

        return false;
    }

    // -------------------------------------------------------------------------
    // Modification (Leaf)
    // -------------------------------------------------------------------------

    /**
     * applyUpdateToLeaf
     * PURPOSE: Inserts a key-value pair into a leaf, maintaining sorted order.
     * LOGIC: Shifts existing entries to the right to create space for the new entry.
     * RETURNS: False if the node is full, signaling the caller to trigger a split.
     */
    bool BTreeAdapter::applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val) {
        BPlusLeafNode* leaf = getLeafNode(leaf_page);

        if (leaf->header.key_count >= MAX_KEYS_LEAF) {
            return false; // Buffer full; overflow imminent
        }

        int count = leaf->header.key_count;
        auto* keys_begin = leaf->keys;
        auto* keys_end = leaf->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        // Create a gap at 'index' by shifting elements forward
        for (int i = count; i > index; i--) {
            leaf->keys[i] = leaf->keys[i - 1];
            leaf->values[i] = leaf->values[i - 1];
        }

        leaf->keys[index] = key;
        leaf->values[index] = val;
        leaf->header.key_count++;
        updateStatistics(leaf_page); // Re-calculate Min/Max immediately
        syncPageHeader(leaf_page);

        return true;
    }

    // -------------------------------------------------------------------------
    // Modification (Internal)
    // -------------------------------------------------------------------------

    /**
     * updateChildPointer
     * PURPOSE: Updates a parent's reference to a child (used during Copy-on-Write splits).
     * WHY: When a child node is shadowed (moved to a new Page ID), the parent
     * must point to the new location to maintain tree connectivity.
     */
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

        // Parent may be not found because of:
        // Concurrency issues (race conditions in multi-threaded environment)
        // Cascading CoW updates (parent itself was shadowed)
        if (!found) {
            std::cerr << "[BTreeAdapter] Error: Parent pointer update failed. Child "
                << old_child_id << " not found." << std::endl;
        }
    }

    /**
     * insertIntoInternal
     * PURPOSE: Adds a promoted key and its right-side child into an internal node.
     * WHY: This is the second half of a split. When a child splits, a key is
     * promoted to the parent to separate the old child and the new sibling.
     */
    bool BTreeAdapter::insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id) {
        BPlusInternalNode* internal = getInternalNode(internal_page);

        if (internal->header.key_count >= MAX_KEYS_INTERNAL) {
            return false; // Parent itself must split
        }

        int count = internal->header.key_count;
        auto* keys_begin = internal->keys;
        auto* keys_end = internal->keys + count;
        auto it = std::upper_bound(keys_begin, keys_end, key);
        int index = static_cast<int>(std::distance(keys_begin, it));

        for (int i = count; i > index; i--) {
            internal->keys[i] = internal->keys[i - 1];
        }

        // Shift children array (N keys have N+1 children)
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


// =========================================================================
    // PARANOID SPLIT NODE (With Corruption Detection)
    // =========================================================================
    void BTreeAdapter::splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result) {

        page_id_t orig_id = node_to_split->GetPageId();
        page_id_t sib_id = new_right_page->GetPageId();

        LOG_DEBUG_SPLIT("[Adapter] Request Split: Page " << orig_id << " into New Sibling " << sib_id);

        if (orig_id == sib_id) {
            std::cerr << "[FATAL] splitNode on SAME PAGE! ID=" << orig_id << std::endl;
            out_result->did_split = false;
            return;
        }

        bool is_leaf_node = isLeaf(node_to_split);

        if (is_leaf_node) {
            BPlusLeafNode* original = getLeafNode(node_to_split);
            BPlusLeafNode* sibling = getLeafNode(new_right_page);

            // ---------------------------------------------------------
            // 1. PRE-SPLIT INTEGRITY CHECK
            // ---------------------------------------------------------
            if (original->next_leaf_id == orig_id) {
                LOG_DEBUG_SPLIT("[FATAL] CORRUPTION DETECTED BEFORE SPLIT!");
                LOG_DEBUG_SPLIT("Page " << orig_id << " points to ITSELF (Self-Loop).");
            }

            // ---------------------------------------------------------
            // 2. PERFORM SPLIT
            // ---------------------------------------------------------
            initLeaf(new_right_page);

            int split_index = MAX_KEYS_LEAF / 2;
            int sibling_count = 0;

            // Move Upper Half
            for (int i = split_index; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling->values[sibling_count] = original->values[i];
                sibling_count++;
            }

            // Update Counts
            original->header.key_count = split_index;
            sibling->header.key_count = sibling_count;

            // ---------------------------------------------------------
            // 3. LINKING (The Danger Zone)
            // ---------------------------------------------------------
            page_id_t old_next = original->next_leaf_id;

            // New Chain: Original -> Sibling -> OldNext
            sibling->next_leaf_id = old_next;
            original->next_leaf_id = sib_id;

            out_result->promoted_key = sibling->keys[0];

            // Update Stats
            updateStatistics(node_to_split);
            updateStatistics(new_right_page);

            // ---------------------------------------------------------
            // 4. POST-SPLIT CORRUPTION CHECK (The Trap)
            // ---------------------------------------------------------

            // CHECK A: Did we create a generic loop?
            if (sibling->next_leaf_id == orig_id) {
                LOG_DEBUG_SPLIT("[FATAL] CORRUPTION: Sibling " << sib_id << " points back to Original " << orig_id);
            }
            if (sibling->next_leaf_id == sib_id) {
                LOG_DEBUG_SPLIT("[FATAL] CORRUPTION: Sibling " << sib_id << " points to ITSELF.");
            }

            // CHECK B: Are keys strictly increasing across the split?
            if (original->header.key_count > 0 && sibling->header.key_count > 0) {
                KeyType max_left = original->keys[original->header.key_count - 1];
                KeyType min_right = sibling->keys[0];

                if (max_left >= min_right) {
                    LOG_DEBUG_SPLIT("[FATAL] SORT ORDER VIOLATION!");
                    LOG_DEBUG_SPLIT("LeftPage Max (" << max_left << ") >= RightPage Min (" << min_right << ")");
                    LOG_DEBUG_SPLIT("This means the page was NOT sorted before splitting!");
                }
            }

            LOG_DEBUG_SPLIT("[Adapter] Leaf Split Done. Chain: " << orig_id << " -> " << sib_id << " -> " << old_next);
        }
        else {
            // ... (Internal Node Split Logic - Assuming this is standard) ...
            // [Keep your existing Internal Node logic here]

            BPlusInternalNode* original = getInternalNode(node_to_split);
            BPlusInternalNode* sibling = getInternalNode(new_right_page);
            initInternal(new_right_page);

            int split_index = MAX_KEYS_INTERNAL / 2;
            out_result->promoted_key = original->keys[split_index]; // Middle key goes UP

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
        syncPageHeader(node_to_split);
        syncPageHeader(new_right_page);
    }
    /**
     * createNewRoot
     * PURPOSE: Creates a new level at the top of the tree after a root split.
     * WHY: This is how the B+ Tree grows in height. The new root will have
     * exactly 1 key and 2 children (the split halves of the old root).
     */
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

    /**
     * updateStatistics
     * PURPOSE: Updates Min/Max/Density/Total metadata used for query optimization.
     * LOGIC: Leaf nodes are updated exactly; Internal nodes are updated incrementally.
     * WHY: We avoid deep tree traversals for every insert. Internal nodes trust
     * the total_keys provided by their children during recursive updates.
     */
    void BTreeAdapter::updateStatistics(cmse::Page* page) {
        auto* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;

        if (count == 0) {
            header->min_key = std::numeric_limits<KeyType>::max();
            header->max_key = std::numeric_limits<KeyType>::min();
            header->density = 0.0f;
            header->total_keys = 0;
            return;
        }

        if (header->is_leaf) {
            // Leaf Nodes have the raw data, so we can set exact boundaries.
            auto* leaf = reinterpret_cast<BPlusLeafNode*>(page->GetData());
            header->min_key = leaf->keys[0];
            header->max_key = leaf->keys[count - 1];
            header->total_keys = count;

            if (header->max_key >= header->min_key) {
                double range = (double)(header->max_key - header->min_key) + 1.0;
                header->density = (range > 0) ? (float)((double)header->total_keys / range) : 1.0f;
            }
        }
        else {
            // Internal nodes trust their min/max/total range.
            // We only recalculate density here to reflect the latest total_keys.
            if (header->max_key >= header->min_key) {
                double range = (double)(header->max_key - header->min_key) + 1.0;
                if (range > 0) {
                    header->density = (float)((double)header->total_keys / range);
                }
            }
        }

        // After stats update, we treat the node as "Validated" and clear the dirty-bit logic marker.
        // Physically Dirty : The keys or pointers changed(handled by the Buffer Pool).
        // Logically Dirty : The statistics(min_key, max_key, total_keys) are no longer accurate because of an insertion or split.
        header->is_dirty = 0;
    }

    /**
     * setDirty
     * PURPOSE: Explicitly marks a page as modified.
     * WHY: This ensures the BufferPoolManager knows this page MUST be written to disk.
     */
    void BTreeAdapter::setDirty(Page* page) {
        if (page == nullptr) return;
        BPlusNodeHeader* h = getHeader(page);
        h->is_dirty = 1;
        syncPageHeader(page);
    }
} // namespace cmse::adapter