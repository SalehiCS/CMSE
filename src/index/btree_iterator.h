#pragma once
#include "../bufferpool/buffer_pool_manager.h"
#include "../bufferpool/page_guard.h"
#include "../adapter/btree_adapter.h"
#include "../common/types.h"

namespace cmse {

    class BTreeIterator {
    public:
        // Constructor that accepts an existing PageGuard (Move Ownership)
        BTreeIterator(bufferpool::BufferPoolManager* bpm,
            adapter::BTreeAdapter* adapter,
            PageGuard&& start_guard,
            int start_index);

        // Move Semantics
        BTreeIterator(BTreeIterator&& other) noexcept;
        BTreeIterator& operator=(BTreeIterator&& other) noexcept;

        // Disable Copy
        BTreeIterator(const BTreeIterator&) = delete;
        BTreeIterator& operator=(const BTreeIterator&) = delete;

        // 1. Dereference Operator
        const LogRecord& operator*() {
            return Current();
        }

        // 2. Arrow Operator
        const LogRecord* operator->() {
            return &Current();
        }

        // --- FIX HERE: Removed 'BTreeIterator::' ---
        const LogRecord& Current();
        // ------------------------------------------

        ~BTreeIterator();

        bool IsEnd();

        // Move to next record
        BTreeIterator& operator++();

        void Close();

    private:
        bufferpool::BufferPoolManager* bpm_;
        adapter::BTreeAdapter* adapter_;
        PageGuard curr_guard_;
        int curr_index_;
        bool is_end_;
    };

} // namespace cmse