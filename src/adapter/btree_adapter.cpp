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


     /**
    * splitNode
    * PURPOSE: Distributes keys/children from an overflowing node into a new sibling node.
    * BEHAVIOR: Handles the structural growth of the B+ Tree.
    */
    void BTreeAdapter::splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result) {

        // Add this DEBUG block at the very beginning
        LOG_DEBUG_SPLIT("[Adapter] Raw Split Request on Page " << node_to_split->GetPageId());

        // --- SAFETY GUARD: Ensure distinct physical pages ---
        // Critical: If the IDs match, the split would overwrite the same memory, causing data loss.
        if (node_to_split->GetPageId() == new_right_page->GetPageId()) {
            std::cerr << "[FATAL] splitNode called with SAME page! PageID: "
                << node_to_split->GetPageId() << std::endl;
            out_result->did_split = false; // Signal failure to the index manager.
            return;
        }

        // Determine node type to apply correct split protocol (Leaf vs Internal).
        bool is_leaf_node = isLeaf(node_to_split);

        if (is_leaf_node) {
            // ==========================================================
            // LEAF NODE SPLIT LOGIC (Data stays in the leaf)
            // ==========================================================
            BPlusLeafNode* original = getLeafNode(node_to_split); // Cast original raw page.
            BPlusLeafNode* sibling = getLeafNode(new_right_page); // Cast new sibling page.

            // Initialize the new page with leaf headers and default values.
            initLeaf(new_right_page);

            // Calculate the midpoint based on the leaf-specific capacity.
            int split_index = MAX_KEYS_LEAF / 2;
            int sibling_count = 0;

            // Transfer the upper half of [Key, Value] pairs to the new sibling.
            for (int i = split_index; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling->values[sibling_count] = original->values[i];
                sibling_count++;
            }

            // Adjust the key counts for both nodes post-split.
            original->header.key_count = split_index;
            sibling->header.key_count = sibling_count;

            // Link sibling into the Leaf Linked List (maintains sequential scan capability).
            // New sibling inherits the "next" pointer of the original node.
            sibling->next_leaf_id = original->next_leaf_id;
            // Original node now points to its new immediate right neighbor.
            original->next_leaf_id = new_right_page->GetPageId();

            // In a Leaf Split, the promoted key is a COPY of the sibling's first key.
            out_result->promoted_key = sibling->keys[0];

            // After calculating promoted key:
            LOG_DEBUG_SPLIT("[Adapter] Leaf Split Promoted Key: " << out_result->promoted_key);
              
             
            // Recalculate Min/Max/Density exactly for these modified leaves.
            updateStatistics(node_to_split);
            updateStatistics(new_right_page);
        }
        else {
            // ==========================================================
            // INTERNAL NODE SPLIT LOGIC (Index keys move UP)
            // ==========================================================
            BPlusInternalNode* original = getInternalNode(node_to_split);
            BPlusInternalNode* sibling = getInternalNode(new_right_page);

            // Buffer existing stats before redistribution to ensure consistency.
            KeyType old_min = original->header.min_key;
            KeyType old_max = original->header.max_key;
            int32_t old_total = original->header.total_keys;

            // Safety check: if stats were uninitialized, fallback to physical key boundaries.
            if (old_min > old_max) {
                old_min = original->keys[0];
                old_max = original->keys[original->header.key_count - 1];
            }

            // Prepare the new sibling as an internal node.
            initInternal(new_right_page);

            // Calculate midpoint for internal routing keys.
            int split_index = MAX_KEYS_INTERNAL / 2;

            // In an internal split, the middle key is PROMOTED out of the node completely.
            out_result->promoted_key = original->keys[split_index];

            // After calculating promoted key:
            LOG_DEBUG_SPLIT("[Adapter] Internal Split Promoted Key: " << out_result->promoted_key);

            int sibling_count = 0;
            // Move keys strictly AFTER the split_index to the sibling.
            for (int i = split_index + 1; i < original->header.key_count; i++) {
                sibling->keys[sibling_count] = original->keys[i];
                sibling_count++;
            }

            // Move the associated children pointers to the sibling (Internal nodes have N+1 children).
            for (int i = split_index + 1; i <= original->header.key_count; i++) {
                sibling->children[i - (split_index + 1)] = original->children[i];
            }

            // Set the new physical counts (Original is reduced to split_index).
            sibling->header.key_count = sibling_count;
            original->header.key_count = split_index;

            // Approx. distribution of 'total_keys' (aggregate count of leaf records in subtree).
            int32_t right_total = old_total / 2;
            int32_t left_total = old_total - right_total;

            // Assign approximated aggregate counts to the new nodes.
            original->header.total_keys = left_total;
            sibling->header.total_keys = right_total;

            // Safety: Ensure aggregate total_keys is at least the physical count * a heuristic factor (5).
            int32_t min_left = original->header.key_count * 5;
            int32_t min_right = sibling->header.key_count * 5;

            // Correct aggregate totals if they fell below the logical minimum.
            if (original->header.total_keys < min_left)  original->header.total_keys = min_left;
            if (sibling->header.total_keys < min_right)  sibling->header.total_keys = min_right;

            // Redefine logical boundaries: Left max is now the promoted separator key.
            original->header.min_key = (old_min < out_result->promoted_key) ? old_min : original->keys[0];
            original->header.max_key = out_result->promoted_key;
            // Right min starts at the promoted separator key.
            sibling->header.min_key = out_result->promoted_key;

            // If sibling has keys, set its max based on the original range.
            if (sibling_count > 0) {
                KeyType last_key = sibling->keys[sibling_count - 1];
                sibling->header.max_key = (old_max > out_result->promoted_key) ? old_max : last_key;
            }
            else {
                sibling->header.max_key = old_max;
            }

            // Define logic for recalculating density (records per key-range).
            auto calcDensity = [](BPlusNodeHeader& h) {
                if (h.max_key >= h.min_key) {
                    double r = (double)(h.max_key - h.min_key) + 1.0;
                    // Density = aggregate records / key range.
                    h.density = (r > 0) ? static_cast<float>(h.total_keys / r) : 0.0f;
                }
                else {
                    h.density = 0.0f;
                }
                };

            // Apply density recalculation to finalized nodes.
            calcDensity(original->header);
            calcDensity(sibling->header);
        }

        // Mark operation as successful.
        out_result->did_split = true;
        // Commit the internal header changes to the persistent PageHeader structure.
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