#include "btree_iterator.h"
#include <iostream> 
#include <algorithm> // for std::find
#include "../common/logger.h" 

namespace cmse {

    // Helper to track visited pages for loop detection
    // static to persist across function calls (but reset per iterator instance ideally)
    // For debugging now, we will add a member to the class in the next step.
    // For now, let's just log loudly.

    BTreeIterator::BTreeIterator(bufferpool::BufferPoolManager* bpm,
        adapter::BTreeAdapter* adapter,
        PageGuard&& start_guard,
        int start_index)
        : bpm_(bpm),
        adapter_(adapter),
        curr_guard_(std::move(start_guard)),
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

    BTreeIterator::~BTreeIterator() {}

    void BTreeIterator::Close() {
        curr_guard_.Drop();
        is_end_ = true;
    }

    bool BTreeIterator::IsEnd() {
        return is_end_;
    }

    BTreeIterator& BTreeIterator::operator++() {
        if (is_end_) return *this;

        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_->GetData());
        curr_index_++;

        // Boundary Check
        if (curr_index_ >= leaf->header.key_count) {
            page_id_t current_id = curr_guard_.Get()->GetPageId();
            page_id_t next_id = leaf->next_leaf_id;

            // LOG THE JUMP
            LOG_DEBUG_QUERY("[Iterator] Finished Page " << current_id << ". Jumping to Next: " << next_id);

            // --- CYCLE DETECTION (Immediate Stop) ---
            if (next_id == current_id) {
                LOG_DEBUG_QUERY("[FATAL] Iterator Cycle Detected! Page " << current_id << " points to ITSELF.");
                is_end_ = true;
                return *this;
            }

            if (next_id == INVALID_PAGE_ID) {
                LOG_DEBUG_QUERY("[Iterator] End of Chain Reached.");
                is_end_ = true;
                return *this;
            }

            // Fetch Next Page
            curr_guard_ = PageGuard(bpm_, bpm_->FetchPage(next_id));

            if (!curr_guard_.IsValid()) {
                LOG_DEBUG_QUERY("[Iterator] Error: Failed to fetch Page " << next_id);
                is_end_ = true;
            }
            else {
                curr_index_ = 0;

                // Verify the new page is actually a leaf
                if (!reinterpret_cast<adapter::BPlusNodeHeader*>(curr_guard_.Get()->GetData())->is_leaf) {
                    LOG_DEBUG_QUERY("[FATAL] Iterator landed on INTERNAL Node " << next_id << "! Structure is broken.");
                    is_end_ = true;
                }
            }
        }
        return *this;
    }

    // ... (Keep Move Constructor/Assignment/Current as they were) ...

    BTreeIterator::BTreeIterator(BTreeIterator&& other) noexcept
        : bpm_(other.bpm_), adapter_(other.adapter_),
        curr_guard_(std::move(other.curr_guard_)),
        curr_index_(other.curr_index_), is_end_(other.is_end_) {
        other.is_end_ = true; other.curr_index_ = 0;
    }

    BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) noexcept {
        if (this != &other) {
            bpm_ = other.bpm_; adapter_ = other.adapter_;
            curr_guard_ = std::move(other.curr_guard_);
            curr_index_ = other.curr_index_; is_end_ = other.is_end_;
            other.is_end_ = true;
        }
        return *this;
    }

    const LogRecord& BTreeIterator::Current() {
        auto* leaf = reinterpret_cast<adapter::BPlusLeafNode*>(curr_guard_.Get()->GetData());
        return leaf->values[curr_index_];
    }

} // namespace cmse