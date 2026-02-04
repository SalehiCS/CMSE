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
            PageGuard&& start_guard, // <--- Accepts Guard
            int start_index);

        // --- NEW: Enable Move Semantics ---
        BTreeIterator(BTreeIterator&& other) noexcept;            // Move Constructor
        BTreeIterator& operator=(BTreeIterator&& other) noexcept; // Move Assignment

        // DISABLE COPY (Because PageGuard cannot be copied)
        BTreeIterator(const BTreeIterator&) = delete;
        BTreeIterator& operator=(const BTreeIterator&) = delete;

        // Destructor: Automatically unpins the held page
        ~BTreeIterator();

        // Check if we reached the end of the query or database
        bool IsEnd();

        // Access the current record
        LogRecord& Current();

        // Move to next record (Prefix ++it)
        BTreeIterator& operator++();

        // Helper to close iterator early if needed
        void Close();

    private:
        bufferpool::BufferPoolManager* bpm_;
        adapter::BTreeAdapter* adapter_;
        PageGuard curr_guard_;
        int curr_index_;
        bool is_end_;
    };

} // namespace cmse