#include "btree_iterator.h"

namespace cmse {

    BTreeIterator::BTreeIterator(bufferpool::BufferPoolManager* bpm,
        adapter::BTreeAdapter* adapter,
        Page* start_page,
        int start_index)
        : bpm_(bpm), adapter_(adapter), curr_page_(start_page), curr_index_(start_index), is_end_(false) {

        if (curr_page_ == nullptr) {
            is_end_ = true;
        }
        else {
            // Validation: If starting index is out of bounds (e.g., empty tree), handle it
            auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_page_->GetData());
            if (curr_index_ >= leaf->header.key_count) {
                // Attempt to move to next page immediately
                this->operator++();
            }
        }
    }

    BTreeIterator::~BTreeIterator() {
        Close();
    }

    void BTreeIterator::Close() {
        if (curr_page_ != nullptr) {
            bpm_->UnpinPage(curr_page_->GetPageId(), false);
            curr_page_ = nullptr;
        }
        is_end_ = true;
    }

    bool BTreeIterator::IsEnd() {
        return is_end_;
    }

    LogRecord& BTreeIterator::Current() {
        // Dangerous if IsEnd() is true, but for performance we skip check
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_page_->GetData());
        return leaf->values[curr_index_];
    }

    BTreeIterator& BTreeIterator::operator++() {
        if (is_end_) return *this;

        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_page_->GetData());

        // 1. Advance index
        curr_index_++;

        // 2. Check if we went past the end of this page
        if (curr_index_ >= leaf->header.key_count) {
            page_id_t next_id = leaf->next_leaf_id;

            // Unpin current page (we are done with it)
            bpm_->UnpinPage(curr_page_->GetPageId(), false);
            curr_page_ = nullptr;

            // 3. Check if there is a next page
            if (next_id == INVALID_PAGE_ID) {
                is_end_ = true;
            }
            else {
                // Fetch next page
                curr_page_ = bpm_->FetchPage(next_id);
                if (curr_page_ == nullptr) {
                    is_end_ = true; // Error fetching next page
                }
                else {
                    curr_index_ = 0; // Reset index for new page
                }
            }
        }
        return *this;
    }

} // namespace cmse