#pragma once

#include <vector>
#include <mutex>
#include <string>

#include "common/types.h"
#include "adapter/btree_adapter.h"
#include "btree_iterator.h"
#include "../bufferpool/page_guard.h"
#include "../version/transaction_context.h"

namespace cmse {
    class Page;
    namespace bufferpool {
        class BufferPoolManager;
    }
}

namespace cmse::index {

    class BTreeIndex {
    public:
        BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id = INVALID_PAGE_ID);



        // --- Core API ---
        bool Insert(const KeyType& key, const ValueType& value);
        bool GetValue(const KeyType& key, ValueType& result);

        // --- NEW: Range Scan API (Phase 2 Completion) ---
        // Returns all records where key is in [start_key, end_key] inclusive.
        std::vector<ValueType> Scan(const KeyType& start_key, const KeyType& end_key);

        page_id_t GetRootPageId() const { return root_page_id_; }

        // --- Visualizer API ---
        void PrintTree(int limit = 10);

        void SetRootPageId(page_id_t root_id) {
            std::lock_guard<std::mutex> lock(latch_);
            root_page_id_ = root_id;
        }
        
        BTreeIterator Begin(const KeyType& start_key);

        /**
         * Copy-on-Write Insert.
         * Inserts a key/value pair into the tree defined by txn.pending_root_id.
         * Does NOT modify existing pages on disk. Creates shadows as needed.
         */
        bool InsertCoW(const KeyType& key, const ValueType& value, TransactionContext& txn);
        

    private:
        cmse::bufferpool::BufferPoolManager* bpm_;
        cmse::adapter::BTreeAdapter adapter_;
        page_id_t root_page_id_;
        std::mutex latch_;

        // RAII Wrapper for the traversal stack
        struct TraversalContext {
            std::vector<PageGuard> path_pages; // <--- Changed from Page* to PageGuard

            // Mark all pages in the stack as dirty (Used on successful insert)
            void SetAllDirty(bool dirty) {
                for (auto& guard : path_pages) {
                    guard.SetDirty(dirty);
                }
            }

            // UnpinAll is no longer needed! The vector destructor handles it.
        };

        // Updated Signatures
        // FindLeaf now returns a Guard instead of a raw pointer
        PageGuard FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write);

        void HandleSplit(PageGuard node_guard, TraversalContext& ctx); // Updated
        void StartNewTree(const KeyType& key, const ValueType& value);

        // --- Helper for Visualization ---
        void PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix);

        void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key);

         /**
         * Helper: Ensures a page is writable for the current transaction.
         * 1. If page is already new/shadowed in txn, returns it.
         * 2. If page is old, creates a copy (Shadow), registers it in txn,
         * and returns the NEW shadow page.
         */
        PageGuard GetPageWritable(page_id_t page_id, TransactionContext& txn);

        /**
        * Helper: specific Split handling for CoW.
        * Since parents are already shadowed during traversal, we just need to
        * handle the split logic on the shadow pages.
        */
        // Update this line in btree_index.h
        void HandleSplitCoW(PageGuard node_guard, std::vector<PageGuard>& ancestors,
            TransactionContext& txn,
            const KeyType& key, const ValueType& value); // <--- Add Key/Value
    };

} // namespace cmse::index