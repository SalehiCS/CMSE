#pragma once

#include "buffer_pool_manager.h"
#include "../page/page.h"

namespace cmse {

    /**
     * PageGuard: A RAII wrapper for Pages.
     * Automatically Unpins the page when this object goes out of scope.
     */
    class PageGuard {
    public:
        // 1. Default Constructor (Empty Guard)
        PageGuard() = default;

        // 2. Main Constructor (Takes ownership)
        PageGuard(bufferpool::BufferPoolManager* bpm, Page* page)
            : bpm_(bpm), page_(page) {
        }

        // 3. Destructor (The Magic)
        ~PageGuard() {
            Drop();
        }

        // 4. Move Constructor (Transfer ownership)
        PageGuard(PageGuard&& other) noexcept {
            bpm_ = other.bpm_;
            page_ = other.page_;
            is_dirty_ = other.is_dirty_;

            // Nullify other to prevent double-unpin
            other.bpm_ = nullptr;
            other.page_ = nullptr;
        }

        // 5. Move Assignment
        PageGuard& operator=(PageGuard&& other) noexcept {
            if (this != &other) {
                Drop(); // Clean up current page first

                bpm_ = other.bpm_;
                page_ = other.page_;
                is_dirty_ = other.is_dirty_;

                other.bpm_ = nullptr;
                other.page_ = nullptr;
            }
            return *this;
        }

        // DISABLE COPY (Unique Ownership)
        PageGuard(const PageGuard&) = delete;
        PageGuard& operator=(const PageGuard&) = delete;

        /**
         * Drop: Manually release the page (Unpin).
         * Called automatically by destructor.
         */
        void Drop() {
            if (bpm_ != nullptr && page_ != nullptr) {
                bpm_->UnpinPage(page_->GetPageId(), is_dirty_);
            }
            bpm_ = nullptr;
            page_ = nullptr;
            is_dirty_ = false;
        }

        /**
         * Mark the page as dirty.
         * When the guard is destroyed, it will tell BufferPool to write this to disk.
         */
        void SetDirty(bool dirty) {
            is_dirty_ = dirty;
        }

        // --- Accessors ---

        Page* Get() const { return page_; }

        Page* operator->() { return page_; }

        // Check if we hold a valid page
        bool IsValid() const { return page_ != nullptr; }

    private:
        bufferpool::BufferPoolManager* bpm_ = nullptr;
        Page* page_ = nullptr;
        bool is_dirty_ = false;
    };

} // namespace cmse