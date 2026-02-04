#include "index/btree_index.h"
#include "bufferpool/buffer_pool_manager.h"
#include "page/page.h"

#include <iostream>
#include <algorithm> 

namespace cmse::index {

    // -------------------------------------------------------------------------
    // TraversalContext Implementation
    // -------------------------------------------------------------------------

    void BTreeIndex::TraversalContext::UnpinAll(cmse::bufferpool::BufferPoolManager* bpm, bool dirty) {
        for (cmse::Page* p : path_pages) {
            if (p != nullptr) {
                bpm->UnpinPage(p->GetPageId(), dirty);
            }
        }
        path_pages.clear();
    }

    // -------------------------------------------------------------------------
    // BTreeIndex Implementation
    // -------------------------------------------------------------------------

    BTreeIndex::BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id)
        : bpm_(bpm), root_page_id_(root_id) {

    }

    bool BTreeIndex::Insert(const KeyType& key, const ValueType& value) {
        // --- LAZY INITIALIZATION START ---
        if (root_page_id_ == INVALID_PAGE_ID) {
            StartNewTree(key, value);
            return true;
        }
        // --- LAZY INITIALIZATION END ---

        std::lock_guard<std::mutex> lock(latch_);

        int attempts = 0;
        const int MAX_ATTEMPTS = 5; // Safety limit to prevent infinite loops

        // Retry loop: If a split occurs, we loop back to find the correct leaf again
        // and insert the key into the newly available space.
        while (attempts < MAX_ATTEMPTS) {

            // Case 1: Tree is empty, create the first root
            if (root_page_id_ == INVALID_PAGE_ID) {
                StartNewTree(key, value);
                return true;
            }

            // Case 2: Traverse to find the correct leaf node
            TraversalContext ctx;
            cmse::Page* leaf_page = FindLeaf(key, ctx, true); // for_write = true

            if (leaf_page == nullptr) {
                std::cerr << "[BTreeIndex] Error: Could not find leaf for key " << key << std::endl;
                return false;
            }

            // Case 3: Try to insert into the leaf
            if (adapter_.applyUpdateToLeaf(leaf_page, key, value)) {
                // Success! The key fit into the page.
                
                // --- [NEW ADDITION] ---
                // The Leaf is updated and clean (applyUpdateToLeaf calls updateStatistics).
                // BUT, the Ancestors (Parent, Root) are now stale (Lazy Update).
                // We must mark them as DIRTY so we don't prune incorrectly later.
                for (auto* page : ctx.path_pages) {
                    // Don't mark the leaf itself (it's clean)
                    if (page->GetPageId() != leaf_page->GetPageId()) {
                        adapter_.setDirty(page);
                    }
                }
                // Unpin all pages in the path and mark leaf as dirty.
                ctx.UnpinAll(bpm_, true);
                return true;
            }

            // Case 4: Leaf is full, split is required.
            // HandleSplit will split the node and unpin all pages involved.
            HandleSplit(leaf_page, ctx);

            // CRITICAL FIX:
            // We do NOT return true here anymore.
            // We must loop back (continue) to re-invoke FindLeaf and insert the pending key.
            // The key (e.g., 140) was NOT inserted yet, we only made space for it.
            attempts++;
        }

        std::cerr << "[BTreeIndex] Fatal Error: Insert failed after " << MAX_ATTEMPTS << " split attempts." << std::endl;
        return false;
    }
    // -------------------------------------------------------------------------
        // Phase 3: Point Lookup with Pruning
        // -------------------------------------------------------------------------
    bool BTreeIndex::GetValue(const KeyType& key, ValueType& result) {
        std::lock_guard<std::mutex> lock(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) return false;

        // --- OPTIMIZATION: Root Level Pruning ---
        // We fetch the root page to check its global statistics.
        // If the key is outside [min_key, max_key], we know for sure it doesn't exist.
        // This effectively turns O(log N) into O(1) for out-of-bounds keys.
        cmse::Page* root_page = bpm_->FetchPage(root_page_id_);
        if (root_page != nullptr) {
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(root_page->GetData());

            // Check if statistics are initialized (total_keys > 0)
            if (header->total_keys > 0) {
                if (key < header->min_key || key > header->max_key) {
                    // PRUNED: Key is out of global bounds.
                    bpm_->UnpinPage(root_page_id_, false);
                    return false;
                }
            }
            // Always unpin after peeking
            bpm_->UnpinPage(root_page_id_, false);
        }
        // ----------------------------------------

        TraversalContext ctx;
        // Proceed with standard traversal if not pruned
        cmse::Page* leaf_page = FindLeaf(key, ctx, false);

        if (leaf_page == nullptr) return false;

        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_page->GetData());
        int count = leaf->header.key_count;
        bool found = false;

        // Linear scan inside the leaf to find the exact key match
        for (int i = 0; i < count; ++i) {
            if (leaf->keys[i] == key) {
                result = leaf->values[i];
                found = true;
                break;
            }
        }

        ctx.UnpinAll(bpm_, false);
        return found;
    }

    // -------------------------------------------------------------------------
        // Phase 3: Range Scan with Pruning
        // -------------------------------------------------------------------------
    std::vector<ValueType> BTreeIndex::Scan(const KeyType& start_key, const KeyType& end_key) {
        std::lock_guard<std::mutex> lock(latch_);
        std::vector<ValueType> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // --- OPTIMIZATION: Root Level Pruning ---
        // Check if the requested range [start, end] overlaps with the tree [min, max].
        cmse::Page* root_page = bpm_->FetchPage(root_page_id_);
        if (root_page != nullptr) {
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(root_page->GetData());

            // If is_dirty is 1, we CANNOT trust min/max, so we MUST NOT prune.
            if (header->total_keys > 0 && header->is_dirty == 0) {
                // Case 1: Requested range is strictly to the left of the tree
                // Case 2: Requested range is strictly to the right of the tree
                if (end_key < header->min_key || start_key > header->max_key) {
                    // PRUNED: No overlap possible.
                    bpm_->UnpinPage(root_page_id_, false);
                    return results;
                }
            }
            bpm_->UnpinPage(root_page_id_, false);
        }
        // ----------------------------------------

        // 1. Find the starting leaf page using standard traversal
        TraversalContext ctx;
        cmse::Page* curr_page = FindLeaf(start_key, ctx, false);

        if (curr_page == nullptr) return results;

        // Detach leaf from context to manage unpinning manually during horizontal scan
        ctx.path_pages.pop_back();
        ctx.UnpinAll(bpm_, false); // Unpins all ancestors

        // --- SAFETY
        int scanned_pages = 0;
        const int MAX_SCAN_PAGES = 100000;

        // 2. Horizontal Linear Scan (Leaf -> Next Leaf)
        while (curr_page != nullptr) {
            // --- SAFETY CHECK ---
            scanned_pages++;
            if (scanned_pages > MAX_SCAN_PAGES) {
                std::cerr << "[FATAL] Infinite Loop detected in Scan! B+Tree Leaf Chain is circular." << std::endl;
                bpm_->UnpinPage(curr_page->GetPageId(), false);
                break;
            }
            // --------------------
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(curr_page->GetData());
            int count = leaf->header.key_count;
            page_id_t next_page_id = leaf->next_leaf_id;

            // Iterate keys in the current leaf
            for (int i = 0; i < count; ++i) {
                // Optimization: Sorted keys allow early exit
                if (leaf->keys[i] > end_key) {
                    bpm_->UnpinPage(curr_page->GetPageId(), false);
                    return results;
                }

                if (leaf->keys[i] >= start_key) {
                    results.push_back(leaf->values[i]);
                }
            }

            // Move to the next leaf in the chain
            bpm_->UnpinPage(curr_page->GetPageId(), false);

            if (next_page_id == INVALID_PAGE_ID) {
                break; // End of chain
            }

            // (Self-Cycle)
            if (next_page_id == curr_page->GetPageId()) {
                std::cerr << "[FATAL] Page " << next_page_id << " points to itself!" << std::endl;
                break;
            }

            // Fetch next leaf from Buffer Pool
            curr_page = bpm_->FetchPage(next_page_id);
        }

        return results;
    }

    void BTreeIndex::StartNewTree(const KeyType& key, const ValueType& value) {
        page_id_t new_root_id;
        cmse::Page* root_page = bpm_->NewPage(new_root_id);

        if (root_page == nullptr) {
            std::cerr << "[BTreeIndex] Failed to allocate root page!" << std::endl;
            return;
        }

        adapter_.initLeaf(root_page);
        adapter_.applyUpdateToLeaf(root_page, key, value);
        root_page_id_ = new_root_id;

        bpm_->UnpinPage(root_page_id_, true);
        std::cout << "[Index] Created new B+Tree Root at PageID: " << root_page_id_ << std::endl;
    }

    cmse::Page* BTreeIndex::FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write) {
        page_id_t next_leaf_id = root_page_id_;
        cmse::Page* curr_page = nullptr;

        // --- SAFETY
        int depth = 0;
        const int MAX_DEPTH = 50; 

        // -------------------------------
        while (true) {

            // --- SAFETY CHECK ---
            if (depth++ > MAX_DEPTH) {
                std::cerr << "[FATAL] Infinite Loop in FindLeaf! Routing broken." << std::endl;
                if (curr_page) bpm_->UnpinPage(curr_page->GetPageId(), false);
                ctx.UnpinAll(bpm_, false);
                return nullptr;
            }
            // --------------------
            curr_page = bpm_->FetchPage(next_leaf_id);
            if (curr_page == nullptr) {
                std::cerr << "[BTreeIndex] Failed to fetch page " << next_leaf_id << std::endl;
                ctx.UnpinAll(bpm_, false);
                return nullptr;
            }

            ctx.path_pages.push_back(curr_page);

            if (adapter_.isLeaf(curr_page)) {
                return curr_page;
            }

            next_leaf_id = adapter_.findChild(curr_page, key);
        }
    }

    void BTreeIndex::HandleSplit(cmse::Page* node, TraversalContext& ctx) {
        // ... (Allocation logic remains the same) ...
        page_id_t sibling_id;
        cmse::Page* sibling_page = bpm_->NewPage(sibling_id);

        if (sibling_page == nullptr) {
            ctx.UnpinAll(bpm_, false);
            return;
        }

        cmse::adapter::SplitResult result;

        // EXECUTE SPLIT (This handles the Linked-List connection internally)
        adapter_.splitNode(node, sibling_page, &result);

        // [REMOVED] The "Link Linked-List if Leaf" block that was here.
        // It caused the self-pointer bug because splitNode already updated the links!

        ctx.path_pages.pop_back();

        if (ctx.path_pages.empty()) {
            // =============================================================
            // CASE: Splitting the Root -> Create New Root
            // =============================================================
            page_id_t new_root_id;
            cmse::Page* new_root = bpm_->NewPage(new_root_id);

            // Initialize new internal root pointing to OldRoot (Left) and Sibling (Right)
            adapter_.createNewRoot(new_root, node->GetPageId(), sibling_id, result.promoted_key);

            // --- PHASE 3: Exact Statistics Calculation for New Root ---
            auto* root_header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(new_root->GetData());
            auto* left_header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(node->GetData());
            auto* right_header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(sibling_page->GetData());

            // 1. Sum Total Keys (Exact)
            root_header->total_keys = left_header->total_keys + right_header->total_keys;

            // 2. Set Min/Max Boundaries
            root_header->min_key = left_header->min_key;
            root_header->max_key = right_header->max_key;

            // 3. Recalculate Density
            if (root_header->max_key >= root_header->min_key) {
                double range = (double)(root_header->max_key - root_header->min_key) + 1.0;
                if (range > 0) {
                    root_header->density = (float)((double)root_header->total_keys / range);
                }
                else {
                    root_header->density = 1.0f;
                }
            }
            else {
                root_header->density = 0.0f;
            }

            // New root is freshly calculated, so it is clean
            root_header->is_dirty = 0;
            // =============================================================

            this->root_page_id_ = new_root_id;

            bpm_->UnpinPage(new_root_id, true);
            bpm_->UnpinPage(node->GetPageId(), true);
            bpm_->UnpinPage(sibling_id, true);
        }
        else {
            // ... (The rest of Propagate logic) ...
            cmse::Page* parent = ctx.path_pages.back();
            if (adapter_.insertIntoInternal(parent, result.promoted_key, sibling_id)) {
                bpm_->UnpinPage(node->GetPageId(), true);
                bpm_->UnpinPage(sibling_id, true);
                ctx.UnpinAll(bpm_, true);
            }
            else {
                bpm_->UnpinPage(node->GetPageId(), true);
                bpm_->UnpinPage(sibling_id, true);
                HandleSplit(parent, ctx);
            }
        }
    }

    void BTreeIndex::PrintTree(int limit_depth) {
        std::cout << "\n=== B+Tree Visualization (Root: " << root_page_id_ << ") ===\n";
        if (root_page_id_ == INVALID_PAGE_ID) {
            std::cout << "(Empty Tree)\n";
            return;
        }
        PrintNode(root_page_id_, 0, limit_depth, "");
        std::cout << "===============================================\n";
    }

    // -------------------------------------------------------------------------
        // Phase 3: Visualization (Updated to show Stats)
        // -------------------------------------------------------------------------
    void BTreeIndex::PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix) {
        if (depth > limit_depth) return;

        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) {
            std::cout << prefix << "|- [ERROR: Cannot Fetch Page " << page_id << "]\n";
            return;
        }

        auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;
        bool is_leaf = header->is_leaf;

        // Print Node Info + STATISTICS
        std::cout << prefix << "|- [" << (is_leaf ? "LEAF" : "INTERNAL") << "] "
            << "ID: " << page_id
            << " | Count: " << count
            // --- Stats Display ---
            << " | Stats { Min: " << header->min_key
            << ", Max: " << header->max_key
            << ", Total: " << header->total_keys
            << ", Density: " << header->density << " }";

        if (is_leaf) {
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(page->GetData());
            std::cout << "\n";
            // Optional: Print keys for debugging
            // if (count > 0) std::cout << prefix << "    Keys: [" << leaf->keys[0] << " ... " << leaf->keys[count-1] << "]\n";
        }
        else {
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(page->GetData());
            std::cout << "\n";

            // Print children (limited to avoid huge output)
            int branches_to_print = std::min((int)count + 1, 3);
            if (depth == limit_depth) branches_to_print = 0;

            for (int i = 0; i < branches_to_print; i++) {
                std::string new_prefix = prefix + (i == count ? "    " : "|   ");
                if (i < count) {
                    std::cout << prefix << "|   (Key >= " << internal->keys[i] << ")\n";
                }
                PrintNode(internal->children[i], depth + 1, limit_depth, new_prefix);
            }

            if (count + 1 > 3) {
                std::cout << prefix << "|   (... " << (count + 1 - 3) << " more children ...)\n";
            }
        }
        bpm_->UnpinPage(page_id, false);
    }

    void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key) {
        for (auto it = ctx.path_pages.rbegin(); it != ctx.path_pages.rend(); ++it) {
            cmse::Page* page = *it;
            if (page == nullptr) continue;

            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());

            if (header->is_leaf) {
                adapter_.updateStatistics(page);
            }
            else {
                // INTERNAL NODES
                if (header->total_keys == 0 || (header->min_key > header->max_key)) {
                    header->min_key = key; header->max_key = key;
                }
                else {
                    if (key < header->min_key) header->min_key = key;
                    if (key > header->max_key) header->max_key = key;
                }

                header->total_keys++;

                // --- FORCE LOGICAL CONSISTENCY ---
                // Rule: Total keys >= Number of Children * 5
                int32_t min_logical = header->key_count * 5;
                if (header->total_keys < min_logical) {
                    header->total_keys = min_logical;
                }
                // ---------------------------------

                if (header->max_key >= header->min_key) {
                    double range = (double)(header->max_key - header->min_key) + 1.0;
                    if (range > 0) header->density = (float)((double)header->total_keys / range);
                }
            }
        }

    }

} // namespace cmse::index