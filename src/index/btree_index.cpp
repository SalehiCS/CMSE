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

        if (root_page_id_ == INVALID_PAGE_ID) {
            StartNewTree(key, value);
            return true;
        }

        TraversalContext ctx;
        cmse::Page* leaf_page = FindLeaf(key, ctx, true);

        if (leaf_page == nullptr) return false;

        if (adapter_.applyUpdateToLeaf(leaf_page, key, value)) {
            ctx.UnpinAll(bpm_, true);
            return true;
        }

        HandleSplit(leaf_page, ctx);
        return true;
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
        page_id_t sibling_id;
        cmse::Page* sibling_page = bpm_->NewPage(sibling_id);

        if (sibling_page == nullptr) {
            std::cerr << "[BTreeIndex] OOM: Cannot split." << std::endl;
            ctx.UnpinAll(bpm_, false);
            return;
        }

        cmse::adapter::SplitResult result;
        adapter_.splitNode(node, sibling_page, &result);

        ctx.path_pages.pop_back();

        if (ctx.path_pages.empty()) {
            page_id_t new_root_id;
            cmse::Page* new_root = bpm_->NewPage(new_root_id);

            adapter_.createNewRoot(new_root, node->GetPageId(), sibling_id, result.promoted_key);
            this->root_page_id_ = new_root_id;

            bpm_->UnpinPage(new_root_id, true);
            bpm_->UnpinPage(node->GetPageId(), true);
            bpm_->UnpinPage(sibling_id, true);
        }
        else {
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
} // namespace cmse::index