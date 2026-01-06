#include "buffer_pool_manager.h"
#include <iostream>

namespace cmse {
    namespace bufferpool {

        BufferPoolManager::BufferPoolManager(size_t pool_size, disk::DiskManager* disk_manager)
            : pool_size_(pool_size), disk_manager_(disk_manager) {

            // Allocate contiguous memory for pages
            pages_ = new Page[pool_size_];

            // Initially, all frames are free
            for (size_t i = 0; i < pool_size_; ++i) {
                free_list_.push_back(static_cast<frame_id_t>(i));
            }
        }

        BufferPoolManager::~BufferPoolManager() {
            FlushAll(); // Flush dirty pages before destruction
            delete[] pages_;
        }

        bool BufferPoolManager::FindVictim(frame_id_t* out_frame_id) {
            // 1. Check free list first
            if (!free_list_.empty()) {
                *out_frame_id = free_list_.front();
                free_list_.pop_front();
                return true;
            }

            // 2. Check LRU list (replacer)
            if (!lru_list_.empty()) {
                frame_id_t victim_frame = lru_list_.front();
                lru_list_.pop_front();
                lru_map_.erase(victim_frame);
                *out_frame_id = victim_frame;
                return true;
            }

            // 3. No victim found (all pages are pinned)
            return false;
        }

        Page* BufferPoolManager::FetchPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // Case 1: Page is already in buffer
            if (page_table_.find(page_id) != page_table_.end()) {
                frame_id_t frame_id = page_table_[page_id];
                Page* page = &pages_[frame_id];

                page->pin_count_++;

                // If it was in LRU list (pin_count was 0), remove it because it's now pinned
                if (lru_map_.find(frame_id) != lru_map_.end()) {
                    lru_list_.erase(lru_map_[frame_id]);
                    lru_map_.erase(frame_id);
                }

                return page;
            }

            // Case 2: Page not in buffer, need to fetch from disk
            frame_id_t frame_id;
            if (!FindVictim(&frame_id)) {
                return nullptr; // Buffer pool is full and all pinned
            }

            Page* page = &pages_[frame_id];

            // If the victim page was dirty, write it back
            if (page->is_dirty_) {
                disk_manager_->WritePage(page->GetPageId(), page->GetData());
            }

            // Remove old entry from page table
            page_table_.erase(page->GetPageId());

            // Setup new page
            page_table_[page_id] = frame_id;
            page->pin_count_ = 1;
            page->is_dirty_ = false;

            // Read data from disk
            disk_manager_->ReadPage(page_id, page->data_);

            // Update header pointer inside page (implicit via GetData/GetHeader)
            // Since we read raw bytes, the header is already at the beginning of data_

            return page;
        }

        bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            if (is_dirty) {
                page->is_dirty_ = true;
            }

            if (page->pin_count_ > 0) {
                page->pin_count_--;
            }

            // If pin count reaches 0, add to LRU replacer
            if (page->pin_count_ == 0) {
                if (lru_map_.find(frame_id) == lru_map_.end()) {
                    lru_list_.push_back(frame_id);
                    lru_map_[frame_id] = --lru_list_.end(); // Store iterator for fast removal
                }
            }

            return true;
        }

        Page* BufferPoolManager::NewPage(page_id_t& out_page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            frame_id_t frame_id;
            if (!FindVictim(&frame_id)) {
                return nullptr; // No space available
            }

            Page* page = &pages_[frame_id];

            // Flush victim if dirty
            if (page->is_dirty_) {
                disk_manager_->WritePage(page->GetPageId(), page->GetData());
            }

            // Remove old mapping
            page_table_.erase(page->GetPageId());

            // Allocate new page ID from disk manager
            page_id_t new_page_id = disk_manager_->AllocatePage();

            // Setup new page metadata
            page_table_[new_page_id] = frame_id;
            page->pin_count_ = 1;
            page->is_dirty_ = true; // New pages are considered dirty until flushed

            // Reset memory (important for security and correctness)
            page->ResetMemory();
            page->GetHeader()->page_id = new_page_id; // Set ID in header

            out_page_id = new_page_id;
            return page;
        }

        bool BufferPoolManager::FlushPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            disk_manager_->WritePage(page_id, page->GetData());
            page->is_dirty_ = false;

            return true;
        }

        void BufferPoolManager::FlushAll() {
            std::lock_guard<std::mutex> lock(latch_);

            for (const auto& entry : page_table_) {
                frame_id_t frame_id = entry.second;
                Page* page = &pages_[frame_id];

                if (page->is_dirty_) {
                    disk_manager_->WritePage(page->GetPageId(), page->GetData());
                    page->is_dirty_ = false;
                }
            }
        }

    } // namespace bufferpool
} // namespace cmse