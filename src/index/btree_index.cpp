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
    bool BTreeIndex::GetValue(const KeyType& key, ValueType& result) {
        std::lock_guard<std::mutex> lock(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) return false;

        TraversalContext ctx;
        cmse::Page* leaf_page = FindLeaf(key, ctx, false);

        if (leaf_page == nullptr) return false;

        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_page->GetData());
        int count = leaf->header.key_count;
        bool found = false;

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

    // --- NEW: Scan Implementation ---
    std::vector<ValueType> BTreeIndex::Scan(const KeyType& start_key, const KeyType& end_key) {
        std::lock_guard<std::mutex> lock(latch_);
        std::vector<ValueType> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // 1. Find the starting leaf page
        TraversalContext ctx;
        cmse::Page* curr_page = FindLeaf(start_key, ctx, false); // Read mode

        if (curr_page == nullptr) return results;

        // CRITICAL: We need to traverse horizontally (Leaf -> Leaf).
        // FindLeaf puts all ancestors + leaf into 'ctx'. 
        // We remove the LEAF from 'ctx' so we can manage it manually, 
        // and let 'ctx' unpin the ancestors (we don't need parents for scanning).
        ctx.path_pages.pop_back();
        ctx.UnpinAll(bpm_, false); // Unpins parents/root

        // 2. Linear Scan Loop
        while (curr_page != nullptr) {
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(curr_page->GetData());
            int count = leaf->header.key_count;
            page_id_t next_leaf_id = leaf->next_leaf_id; // Horizontal Link

            // Iterate keys in this page
            for (int i = 0; i < count; ++i) {
                if (leaf->keys[i] > end_key) {
                    // Optimization: Since keys are sorted, if we pass end_key, we are done.
                    bpm_->UnpinPage(curr_page->GetPageId(), false);
                    return results;
                }

                if (leaf->keys[i] >= start_key) {
                    results.push_back(leaf->values[i]);
                }
            }

            // Done with this page, move to next
            bpm_->UnpinPage(curr_page->GetPageId(), false);

            if (next_leaf_id == INVALID_PAGE_ID) {
                break; // End of chain
            }

            // Fetch next leaf
            curr_page = bpm_->FetchPage(next_leaf_id);
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

        while (true) {
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
        // 1. Allocate a new page for the sibling
        page_id_t sibling_id;
        cmse::Page* sibling_page = bpm_->NewPage(sibling_id);

        if (sibling_page == nullptr) {
            std::cerr << "[BTreeIndex] OOM: Cannot split." << std::endl;
            ctx.UnpinAll(bpm_, false);
            return;
        }

        // 2. Perform the Split Logic
        cmse::adapter::SplitResult result;
        adapter_.splitNode(node, sibling_page, &result);

        // ============================================================
        // [FIX] CRITICAL: Update Leaf Linked-List Pointers
        // ============================================================
        if (adapter_.isLeaf(node)) {
            // Cast to Leaf Node structure
            auto* left_leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(node->GetData());
            auto* right_leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(sibling_page->GetData());

            // Maintain the chain: Left -> Right -> (Old Next)
            right_leaf->next_leaf_id = left_leaf->next_leaf_id;
            left_leaf->next_leaf_id = sibling_id;

            // Debug print to confirm linking happens
            // std::cout << "[Split] Linked Leaf " << node->GetPageId() << " -> " << sibling_id << std::endl;
        }
        // ============================================================

        // 3. Remove child from stack
        ctx.path_pages.pop_back();

        if (ctx.path_pages.empty()) {
            // CASE: Splitting Root
            page_id_t new_root_id;
            cmse::Page* new_root = bpm_->NewPage(new_root_id);

            adapter_.createNewRoot(new_root, node->GetPageId(), sibling_id, result.promoted_key);
            this->root_page_id_ = new_root_id;

            bpm_->UnpinPage(new_root_id, true);
            bpm_->UnpinPage(node->GetPageId(), true);
            bpm_->UnpinPage(sibling_id, true);
        }
        else {
            // CASE: Propagate to Parent
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

    void BTreeIndex::PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix) {
        if (depth > limit_depth) return;

        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) {
            std::cout << prefix << "|- [ERROR: Cannot Fetch Page " << page_id << "]\n";
            return;
        }

        bool is_leaf = adapter_.isLeaf(page);
        int count = adapter_.getCount(page);
        int max_keys = adapter_.getMaxKeys(page);

        std::cout << prefix << "|- [" << (is_leaf ? "LEAF" : "INTERNAL") << "] "
            << "PageID: " << page_id
            << " | Usage: " << count << "/" << max_keys;

        if (is_leaf) {
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(page->GetData());
            if (count > 0) {
                std::cout << " | Keys: [" << leaf->keys[0] << " ... " << leaf->keys[count - 1] << "]";
            }
            std::cout << "\n";
        }
        else {
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(page->GetData());
            std::cout << "\n";

            int branches_to_print = std::min(count + 1, 3);
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
        // Iterate from Leaf to Root
        for (auto it = ctx.path_pages.rbegin(); it != ctx.path_pages.rend(); ++it) {
            cmse::Page* page = *it;
            if (page == nullptr) continue;

            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());

            if (header->is_leaf) {
                // For Leaves: Recalculate everything from scratch (Safe & Easy)
                adapter_.updateStatistics(page);
            }
            else {
                // For Internal Nodes: Incremental Update (as per Project Doc)
                // 1. Update Min/Max
                if (header->total_keys == 0) {
                    header->min_key = key;
                    header->max_key = key;
                }
                else {
                    if (key < header->min_key) header->min_key = key;
                    if (key > header->max_key) header->max_key = key;
                }

                // 2. Increment Total Keys (Aggregate count)
                header->total_keys++;

                // 3. Recalculate Density
                // We can reuse the logic in adapter, or do it here manually.
                // Let's call adapter's logic but preserve the total_keys we just incremented.
                // Actually, calling adapter_.updateStatistics(page) might mess up total_keys 
                // if we are not careful (see comment in adapter).
                // Let's just update density manually here:

                if (header->max_key >= header->min_key) {
                    double range = (double)(header->max_key - header->min_key) + 1.0;
                    header->density = (float)((double)header->total_keys / range);
                }
            }
        }
    }
} // namespace cmse::index