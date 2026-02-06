#include "btree_iterator.h"
#include <iostream> 

namespace cmse {

    /**
     * Constructor
     * Initializes the iterator at a specific starting point within a leaf node.
     */
    BTreeIterator::BTreeIterator(bufferpool::BufferPoolManager* bpm,
        adapter::BTreeAdapter* adapter,
        PageGuard&& start_guard,
        int start_index)
        : bpm_(bpm),
        adapter_(adapter),
        curr_guard_(std::move(start_guard)), // <--- Explicitly transfer pin ownership from the caller
        curr_index_(start_index),
        is_end_(false) {

        // If the provided guard is invalid (e.g., empty tree), immediately mark as finished.
        if (!curr_guard_.IsValid()) {
            is_end_ = true;
        }
        else {
            // Check if the starting index is valid; if not (e.g., index at end of leaf), 
            // trigger an increment to potentially move to the next physical leaf.
            auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());
            if (curr_index_ >= leaf->header.key_count) {
                this->operator++();
            }
        }
    }

    /**
     * Destructor
     * Empty because curr_guard_ is an RAII object; its own destructor will handle unpinning.
     */
    BTreeIterator::~BTreeIterator() {

    }

    /**
     * Close
     * Forces the iterator to release its page pin and enter the 'End' state.
     */
    void BTreeIterator::Close() {
        curr_guard_.Drop(); // Manually decrement the pin count in the Buffer Pool
        is_end_ = true;
    }

    /**
     * IsEnd
     * Simple accessor to check if the iterator has exhausted the key range.
     */
    bool BTreeIterator::IsEnd() {
        return is_end_;
    }

    /**
     * operator++ (Prefix)
     * The core traversal engine. Moves the index forward and handles leaf-to-leaf jumping.
     */
    BTreeIterator& BTreeIterator::operator++() {
        // If we are already at the end, incrementing does nothing.
        if (is_end_) return *this;

        // Map raw page data to a Leaf Node structure.
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());

        // 1. Advance the internal slot index.
        curr_index_++;

        // 2. Boundary Check: Have we moved past the last key of the current page?
        if (curr_index_ >= leaf->header.key_count) {

            page_id_t current_id = curr_guard_.Get()->GetPageId(); // Current page ID (for tracing)
            page_id_t next_id = leaf->next_leaf_id;                // Extract sibling pointer

            // --- OPTIMIZATION: VALIDITY CHECK ---
            // If next_id is -1 (INVALID_PAGE_ID), we have reached the rightmost edge of the tree.
            if (next_id == INVALID_PAGE_ID) {
                is_end_ = true;
                // Note: The pin on the current page is still held until this guard is dropped or reset.
                return *this;
            }

            // 3. Leaf Jumping: Release the current page pin and fetch the sibling.
            // PageGuard move-assignment automatically unpins the old page before taking the new one.
            curr_guard_ = PageGuard(bpm_, bpm_->FetchPage(next_id));

            // If the next page cannot be fetched (e.g., IO error), terminate traversal.
            if (!curr_guard_.IsValid()) {
                is_end_ = true;
            }
            else {
                // Reset index to the first slot of the brand-new leaf.
                curr_index_ = 0;
            }
        }
        return *this;
    }

    /**
     * Move Constructor
     * Transfers ownership of a pinned page from one iterator instance to another.
     */
    BTreeIterator::BTreeIterator(BTreeIterator&& other) noexcept
        : bpm_(other.bpm_),
        adapter_(other.adapter_),
        curr_guard_(std::move(other.curr_guard_)), // <--- Transfer PageGuard ownership
        curr_index_(other.curr_index_),
        is_end_(other.is_end_) {

        // Invalidate the source iterator so it no longer points to valid data.
        other.is_end_ = true;
        other.curr_index_ = 0;
    }

    /**
     * Move Assignment Operator
     * Cleans up existing pins and steals resources from another iterator.
     */
    BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) noexcept {
        if (this != &other) {
            // 1. The existing 'curr_guard_' destructor is invoked by the move-assignment,
            //    safely unpinning the page this instance previously held.

            // 2. Steal resources from the rvalue 'other'.
            bpm_ = other.bpm_;
            adapter_ = other.adapter_;
            curr_guard_ = std::move(other.curr_guard_); // <--- Take the pin ownership
            curr_index_ = other.curr_index_;
            is_end_ = other.is_end_;

            // 3. Neutralize the source iterator.
            other.is_end_ = true;
        }
        return *this;
    }

    /**
     * Current
     * Returns a reference to the specific record at the current iterator position.
     */
    const LogRecord& BTreeIterator::Current() {
        // Access raw data via .Get() to ensure we are looking at the pinned memory block.
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_.Get()->GetData());

        // Return the LogRecord (ValueType) stored at the current index.
        return leaf->values[curr_index_];
    }

} // namespace cmse