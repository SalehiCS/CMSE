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

    /**
     * BTreeIndex Class
     * High-level wrapper for the B+ Tree structure. It manages tree traversal,
     * splits, and transactional isolation through Copy-on-Write logic.
     */
    class BTreeIndex {
    public:
        /** Constructor: Associates the index with a BufferPoolManager and an optional root. */
        BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id = INVALID_PAGE_ID);

        // --- Core API (Standard Operations) ---

        /** Inserts a key-value pair into the persistent tree. Returns true on success. */
        bool Insert(const KeyType& key, const ValueType& value);

        /** Retrieves the value associated with a specific key. */
        bool GetValue(const KeyType& key, ValueType& result);

        // --- Range Scan API (Phase 2 Completion) ---

        /** * Performs a range query.
         * @return All records where the key is in [start_key, end_key] inclusive.
         */
        std::vector<ValueType> Scan(const KeyType& start_key, const KeyType& end_key);

        /** Returns the current persistent root Page ID. */
        page_id_t GetRootPageId() const { return root_page_id_; }

        // --- Visualizer & Management API ---

        /** Debug utility: Prints a text representation of the tree to console. */
        void PrintTree(int limit = 10);

        /** Manually updates the root page ID, protected by a mutex for thread safety. */
        void SetRootPageId(page_id_t root_id) {
            std::lock_guard<std::mutex> lock(latch_);
            root_page_id_ = root_id;
        }

        /** Returns an iterator starting at the first occurrence of start_key (or the next closest). */
        BTreeIterator Begin(const KeyType& start_key);

        /**
         * Copy-on-Write (CoW) Insert.
         * Used for multi-version concurrency control (MVCC).
         * @param txn The transaction context tracking modified "shadow" pages.
         * @note Does NOT modify existing disk pages; creates path-shadows to the new root.
         */
        bool InsertCoW(const KeyType& key, const ValueType& value, TransactionContext& txn);

    private:
        /** Resource Manager: Handles physical page fetching/flushing. */
        cmse::bufferpool::BufferPoolManager* bpm_;
        /** Logical Logic: Translates B-Tree concepts (keys/offsets) into physical page offsets. */
        cmse::adapter::BTreeAdapter adapter_;
        /** The starting point of the current tree version. */
        page_id_t root_page_id_;
        /** Mutex to prevent race conditions during root updates or tree structural changes. */
        std::mutex latch_;

        /**
         * TraversalContext (RAII Wrapper)
         * Maintains the stack of pages visited during a top-down traversal.
         * The use of PageGuard ensures that all pages are unpinned automatically
         * when the context is destroyed, even if an exception occurs.
         */
        struct TraversalContext {
            /** Stack of guarded pages representing the path from root to leaf. */
            std::vector<PageGuard> path_pages;

            /** Marks every page in the current path as dirty (ready for disk sync). */
            void SetAllDirty(bool dirty) {
                for (auto& guard : path_pages) {
                    guard.SetDirty(dirty);
                }
            }

            // Note: Manual 'UnpinAll' is removed. PageGuard destructors handle this.
        };

        // --- Internal Helper Methods ---

        /** * Navigates from root to leaf.
         * @return A PageGuard for the leaf page and populates ctx with the path.
         */
        PageGuard FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write);

        /** Handles overflow in a node by splitting it and updating the parent in the context. */
        void HandleSplit(PageGuard node_guard, TraversalContext& ctx);

        /** Initializes a brand new B+ Tree with a single root page. */
        void StartNewTree(const KeyType& key, const ValueType& value);

        /** Recursive helper used by PrintTree to navigate and display tree levels. */
        void PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix);

        /** Propagates statistical or structural metadata updates back up the path to the root. */
        void UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key);

        /**
         * CoW Helper: Ensures a page is safe to write to within a transaction.
         * If the page is "old" (already on disk), it creates a "Shadow" copy.
         */
        PageGuard GetPageWritable(page_id_t page_id, TransactionContext& txn);

        /**
         * CoW Split Handler: Specifically manages node splitting for shadow pages.
         * Because parents are shadowed during descent, this avoids modifying any original data.
         */
        void HandleSplitCoW(PageGuard node_guard, std::vector<PageGuard>& ancestors,
            TransactionContext& txn,
            const KeyType& key, const ValueType& value);
    };

} // namespace cmse::index