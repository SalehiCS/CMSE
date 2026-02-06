#pragma once

#include "buffer_pool_manager.h"
#include "../page/page.h"

namespace cmse {

    /**
     * PageGuard: A RAII (Resource Acquisition Is Initialization) wrapper for Pages.
     * PHILOSOPHY: Ensures "Leak-Proof" buffer pool management.
     * Even if your code throws an exception or returns early, this object's
     * destructor will automatically Unpin the page from RAM.
     */
    class PageGuard {
    public:
        // 1. Default Constructor (Empty Guard)
        // PURPOSE: Creates a placeholder that doesn't hold any page yet.
        PageGuard() = default;

        // 2. Main Constructor (Takes ownership)
        // PURPOSE: Binds a physical Page and the Manager together.
        // REQUIREMENT: The page should already be pinned by the BPM before passing it here.
        PageGuard(bufferpool::BufferPoolManager* bpm, Page* page)
            : bpm_(bpm), page_(page) {
        }

        // 3. Destructor (The Safety Net)
        // PURPOSE: Automatically triggers the Unpin logic when the guard's lifetime ends.
        ~PageGuard() {
            Drop();
        }

        // 4. Move Constructor (Transfer ownership)
        // PURPOSE: Allows moving a guard out of a function (returning a page).
        PageGuard(PageGuard&& other) noexcept {
            // Transfer all pointers and the dirty status to this new instance.
            bpm_ = other.bpm_;
            page_ = other.page_;
            is_dirty_ = other.is_dirty_;

            // NULLIFY the 'other' guard. Crucial: prevents the 'other' destructor 
            // from unpinning the page we just took over.
            other.bpm_ = nullptr;
            other.page_ = nullptr;
        }

        // 5. Move Assignment
        // PURPOSE: Allows re-assigning an existing guard to a new page.
        PageGuard& operator=(PageGuard&& other) noexcept {
            // Self-assignment check to prevent resource loss.
            if (this != &other) {
                // IMPORTANT: Unpin the page currently held by 'this' before taking a new one.
                Drop();

                // Adopt the state of the 'other' guard.
                bpm_ = other.bpm_;
                page_ = other.page_;
                is_dirty_ = other.is_dirty_;

                // Invalidate the 'other' guard to finalize the transfer.
                other.bpm_ = nullptr;
                other.page_ = nullptr;
            }
            return *this;
        }

        // DISABLE COPY (Unique Ownership)
        // WHY: If two guards owned the same page, the first one to be destroyed 
        // would unpin it, leaving the second guard pointing to a "Victim" candidate.
        PageGuard(const PageGuard&) = delete;
        PageGuard& operator=(const PageGuard&) = delete;

        /**
         * Drop
         * PURPOSE: Manually release the page back to the Buffer Pool.
         * LOGIC: If a valid page exists, it calls UnpinPage using the stored dirty flag.
         */
        void Drop() {
            if (bpm_ != nullptr && page_ != nullptr) {
                // Communicates with the BPM to decrement the pin_count.
                bpm_->UnpinPage(page_->GetPageId(), is_dirty_);
            }
            // Clear pointers to indicate the guard is now empty.
            bpm_ = nullptr;
            page_ = nullptr;
            is_dirty_ = false;
        }

        /**
         * SetDirty
         * PURPOSE: Flag the page as modified.
         * EFFECT: When Drop() is called, this flag ensures the BPM writes the page to disk.
         */
        void SetDirty(bool dirty) {
            is_dirty_ = dirty;
        }

        // --- Accessors ---

        // Returns the raw pointer to the Page object.
        Page* Get() const { return page_; }

        // Overload the -> operator so the Guard acts like a pointer to the Page.
        Page* operator->() { return page_; }

        // Logic check to see if the guard is actually protecting a page or is empty.
        bool IsValid() const { return page_ != nullptr; }

    private:
        // Pointer to the manager responsible for unpinning.
        bufferpool::BufferPoolManager* bpm_ = nullptr;
        // The actual page in the buffer pool frames.
        Page* page_ = nullptr;
        // Tracks if the page was changed during this guard's lifetime.
        bool is_dirty_ = false;
    };

} // namespace cmse