#include "btree_iterator.h"

namespace cmse {

    BTreeIterator::BTreeIterator(bufferpool::BufferPoolManager* bpm,
        adapter::BTreeAdapter* adapter,
        PageGuard&& start_guard,
        int start_index)
        : bpm_(bpm),
        adapter_(adapter),
        curr_guard_(std::move(start_guard)), // <--- Transfer ownership here
        curr_index_(start_index),
        is_end_(false) {

        if (!curr_guard_.IsValid()) {
            is_end_ = true;
        }
        else {
            auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());
            if (curr_index_ >= leaf->header.key_count) {
                this->operator++();
            }
        }
    }


    BTreeIterator::~BTreeIterator() {
        
    }

    void BTreeIterator::Close() {
        curr_guard_.Drop(); // Manually release if needed
        is_end_ = true;
    }

    bool BTreeIterator::IsEnd() {
        return is_end_;
    }

    LogRecord& BTreeIterator::Current() {
        // Use -> arrow syntax (thanks to operator->)
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());
        return leaf->values[curr_index_];
    }

    BTreeIterator& BTreeIterator::operator++() {
        if (is_end_) return *this;

        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());

        // 1. Advance index
        curr_index_++;

        // 2. Check if we went past the end of this page
        if (curr_index_ >= leaf->header.key_count) {
            page_id_t next_id = leaf->next_leaf_id;

            // 1. Release current page (Destroys old guard content)
            // 2. Fetch new page
            // 3. Wrap new page in guard
            curr_guard_ = PageGuard(bpm_, bpm_->FetchPage(next_id));

            // 3. Check if there is a next page
            if (!curr_guard_.IsValid()) {
                is_end_ = true;
            }
            else {
                curr_index_ = 0;
            }
        }
        return *this;
    }

    // --- Move Constructor ---
    BTreeIterator::BTreeIterator(BTreeIterator&& other) noexcept
        : bpm_(other.bpm_),
        adapter_(other.adapter_),
        curr_guard_(std::move(other.curr_guard_)), // <--- Transfer Guard Ownership
        curr_index_(other.curr_index_),
        is_end_(other.is_end_) {

        // Invalidate the other iterator
        other.is_end_ = true;
        other.curr_index_ = 0;
    }

    // --- Move Assignment Operator ---
    BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) noexcept {
        if (this != &other) {
            // 1. The 'curr_guard_' Destructor runs automatically here, 
            //    unpinning whatever page we were holding previously.

            // 2. Steal resources
            bpm_ = other.bpm_;
            adapter_ = other.adapter_;
            curr_guard_ = std::move(other.curr_guard_); // <--- Transfer Guard Ownership
            curr_index_ = other.curr_index_;
            is_end_ = other.is_end_;

            // 3. Invalidate other
            other.is_end_ = true;
        }
        return *this;
    }

} // namespace cmse