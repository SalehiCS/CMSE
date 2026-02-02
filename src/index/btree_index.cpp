#include "index/btree_index.h"

// Include full definitions of dependencies here
#include "bufferpool/buffer_pool_manager.h"
#include "page/page.h"

#include <iostream>
#include <algorithm> 

// We define methods inside the namespace block
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

        // Case 1: Tree is empty, create root
        if (root_page_id_ == INVALID_PAGE_ID) {
            StartNewTree(key, value);
            return true;
        }

        // Case 2: Traverse to find the correct leaf
        TraversalContext ctx;
        cmse::Page* leaf_page = FindLeaf(key, ctx, true); // for_write = true

        if (leaf_page == nullptr) return false;

        // Try inserting into the leaf using the adapter
        if (adapter_.applyUpdateToLeaf(leaf_page, key, value)) {
            // Success! Unpin the path (Leaf is dirty)
            ctx.UnpinAll(bpm_, true);
            return true;
        }

        // Case 3: Leaf is full, split is required
        HandleSplit(leaf_page, ctx);

        // HandleSplit manages unpinning internally
        return true;
    }

    bool BTreeIndex::GetValue(const KeyType& key, ValueType& result) {
        std::lock_guard<std::mutex> lock(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) return false;

        TraversalContext ctx;
        cmse::Page* leaf_page = FindLeaf(key, ctx, false); // for_write = false (Read Only)

        if (leaf_page == nullptr) return false;

        // Access leaf data directly to find the key.
        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_page->GetData());
        int count = leaf->header.key_count;
        bool found = false;

        // Linear scan to find the exact key match
        for (int i = 0; i < count; ++i) {
            if (leaf->keys[i] == key) {
                result = leaf->values[i];
                found = true;
                break;
            }
        }

        ctx.UnpinAll(bpm_, false); // Unpin path, not dirty
        return found;
    }

    void BTreeIndex::StartNewTree(const KeyType& key, const ValueType& value) {
        page_id_t new_root_id;

        // FIX: Pass variable directly (reference), not address (&)
        cmse::Page* root_page = bpm_->NewPage(new_root_id);

        if (root_page == nullptr) {
            std::cerr << "[BTreeIndex] Failed to allocate root page!" << std::endl;
            return;
        }

        // Initialize the new page as a Leaf
        adapter_.initLeaf(root_page);
        adapter_.applyUpdateToLeaf(root_page, key, value);

        // Update the root pointer
        root_page_id_ = new_root_id;

        bpm_->UnpinPage(root_page_id_, true);
        std::cout << "[Index] Created new B+Tree Root at PageID: " << root_page_id_ << std::endl;
    }

    cmse::Page* BTreeIndex::FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write) {
        page_id_t next_page_id = root_page_id_;
        cmse::Page* curr_page = nullptr;

        while (true) {
            curr_page = bpm_->FetchPage(next_page_id);
            if (curr_page == nullptr) {
                std::cerr << "[BTreeIndex] Failed to fetch page " << next_page_id << std::endl;
                ctx.UnpinAll(bpm_, false); // Cleanup
                return nullptr;
            }

            // Add page to context stack (locked/pinned)
            ctx.path_pages.push_back(curr_page);

            // If it's a leaf, we are done
            if (adapter_.isLeaf(curr_page)) {
                return curr_page;
            }

            // It's an internal node, find the correct child to descend
            next_page_id = adapter_.findChild(curr_page, key);
        }
    }

    void BTreeIndex::HandleSplit(cmse::Page* node, TraversalContext& ctx) {
        // 1. Allocate a new page for the sibling
        page_id_t sibling_id;

        // FIX: Pass variable directly (reference), not address (&)
        cmse::Page* sibling_page = bpm_->NewPage(sibling_id);

        if (sibling_page == nullptr) {
            std::cerr << "[BTreeIndex] OOM: Cannot split." << std::endl;
            ctx.UnpinAll(bpm_, false);
            return;
        }

        // 2. Perform the Split Logic (via Adapter)
        cmse::adapter::SplitResult result;
        adapter_.splitNode(node, sibling_page, &result);

        // 3. Remove the current node (child) from the stack to access the parent
        ctx.path_pages.pop_back();

        if (ctx.path_pages.empty()) {
            // CASE: Splitting the Root -> Tree height increases
            page_id_t new_root_id;

            // FIX: Pass variable directly (reference), not address (&)
            cmse::Page* new_root = bpm_->NewPage(new_root_id);

            // Create a new internal root pointing to [OldRoot] and [Sibling]
            adapter_.createNewRoot(new_root, node->GetPageId(), sibling_id, result.promoted_key);

            this->root_page_id_ = new_root_id;

            // Unpin all involved pages
            bpm_->UnpinPage(new_root_id, true);
            bpm_->UnpinPage(node->GetPageId(), true);
            bpm_->UnpinPage(sibling_id, true);
        }
        else {
            // CASE: Splitting an Internal/Leaf Node -> Propagate to Parent
            cmse::Page* parent = ctx.path_pages.back();

            // Try inserting the promoted key into the parent
            if (adapter_.insertIntoInternal(parent, result.promoted_key, sibling_id)) {
                // Parent had space, split propagation stops here
                bpm_->UnpinPage(node->GetPageId(), true);
                bpm_->UnpinPage(sibling_id, true);
                ctx.UnpinAll(bpm_, true); // Unpin parent and ancestors
            }
            else {
                // Parent is also full -> Recursive Split
                bpm_->UnpinPage(node->GetPageId(), true);
                bpm_->UnpinPage(sibling_id, true);

                // Recurse up the tree
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

        // Print Node Info
        std::cout << prefix << "|- [" << (is_leaf ? "LEAF" : "INTERNAL") << "] "
            << "PageID: " << page_id
            << " | Usage: " << count << "/" << max_keys;

        if (is_leaf) {
            // For leaves, print range of keys
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(page->GetData());
            if (count > 0) {
                std::cout << " | Keys: [" << leaf->keys[0] << " ... " << leaf->keys[count - 1] << "]";
            }
            std::cout << "\n";
        }
        else {
            // For internal nodes, just print info and recurse
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(page->GetData());
            std::cout << "\n";

            // Recursively print children
            // Note: Internal nodes have (Count+1) children. 
            // We print a few branches to give an idea.

            int branches_to_print = std::min(count + 1, 3); // Print first 3 branches only
            if (depth == limit_depth) branches_to_print = 0;

            for (int i = 0; i < branches_to_print; i++) {
                std::string new_prefix = prefix + (i == count ? "    " : "|   ");

                if (i < count) {
                    std::cout << prefix << "|   (Key >= " << internal->keys[i] << ")\n";
                }

                // Recurse to child
                PrintNode(internal->children[i], depth + 1, limit_depth, new_prefix);
            }

            if (count + 1 > 3) {
                std::cout << prefix << "|   (... " << (count + 1 - 3) << " more children ...)\n";
            }
        }

        bpm_->UnpinPage(page_id, false);
    }
} // namespace cmse::index