#pragma once
#include "../bufferpool/buffer_pool_manager.h"
#include "../adapter/btree_adapter.h"
#include "../common/types.h"

namespace cmse {

    class BTreeIterator {
    public:
        // Constructor: Takes ownership of the starting page (it must be PINNED)
        BTreeIterator(bufferpool::BufferPoolManager* bpm,
            adapter::BTreeAdapter* adapter,
            Page* start_page,
            int start_index);

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
        Page* curr_page_;
        int curr_index_;
        bool is_end_;
    };

} // namespace cmse