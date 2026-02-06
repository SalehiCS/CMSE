#pragma once
#include "../bufferpool/buffer_pool_manager.h"
#include "../bufferpool/page_guard.h"
#include "../adapter/btree_adapter.h"
#include "../common/types.h"

namespace cmse {

    /**
     * BTreeIterator
     * Provides a stateful, forward-only iterator for linear range scans across leaf nodes.
     * Uses RAII via PageGuard to maintain buffer pool pins during traversal.
     */
    class BTreeIterator {
    public:
        /**
         * Constructor
         * @param bpm Pointer to the global Buffer Pool Manager.
         * @param adapter Pointer to the BTree logic adapter for node interpretation.
         * @param start_guard A PageGuard holding the initial leaf page (takes ownership).
         * @param start_index The starting slot index within the initial leaf.
         */
        BTreeIterator(bufferpool::BufferPoolManager* bpm,
            adapter::BTreeAdapter* adapter,
            PageGuard&& start_guard,
            int start_index);

        /**
         * Move Semantics
         * Allows transferring ownership of the iterator and its internal PageGuard pin.
         */
        BTreeIterator(BTreeIterator&& other) noexcept;
        BTreeIterator& operator=(BTreeIterator&& other) noexcept;

        /**
         * Copying is disabled to prevent multiple iterators from attempting to
         * manage the same PageGuard pin simultaneously.
         */
        BTreeIterator(const BTreeIterator&) = delete;
        BTreeIterator& operator=(const BTreeIterator&) = delete;

        /**
         * Dereference Operator
         * Returns a reference to the LogRecord at the current iterator position.
         */
        const LogRecord& operator*() {
            return Current();
        }

        /**
         * Arrow Operator
         * Provides pointer-like access to the member fields of the current LogRecord.
         */
        const LogRecord* operator->() {
            return &Current();
        }

        /**
         * Current
         * Accesses the specific LogRecord within the current pinned leaf page.
         */
        const LogRecord& Current();

        /**
         * Destructor
         * Relies on PageGuard's destructor to automatically unpin the current page.
         */
        ~BTreeIterator();

        /**
         * IsEnd
         * Returns true if the iterator has exhausted all records in the tree.
         */
        bool IsEnd();

        /**
         * Prefix Increment Operator (++it)
         * Advances the iterator to the next record, potentially crossing leaf boundaries.
         */
        BTreeIterator& operator++();

        /**
         * Close
         * Manually terminates the iterator and releases any held buffer pool pins.
         */
        void Close();

    private:
        bufferpool::BufferPoolManager* bpm_;      // Reference to the pool for fetching next pages.
        adapter::BTreeAdapter* adapter_;          // Helper for parsing node structure.
        PageGuard curr_guard_;                    // RAII handle for the currently pinned leaf page.
        int curr_index_;                          // The current record slot index within the leaf.
        bool is_end_;                             // Flag indicating if traversal is complete.
    };

} // namespace cmse