/**
 * buffer_pool_manager.cpp
 *
 * Implementation of BufferPoolManager using LRUReplacer.
 */

#include "buffer_pool_manager.h"
#include <cstring>
#include <iostream>

namespace cmse {
    namespace bufferpool {

        BufferPoolManager::BufferPoolManager(size_t pool_size, cmse::disk::DiskManager* disk_manager)
            : pool_size_(pool_size), disk_manager_(disk_manager) {

            // Allocate contiguous memory for pages
            pages_ = new Page[pool_size_];

            // Initialize LRU Replacer
            replacer_ = new LRUReplacer(pool_size);

            // Initially, all frames are in the free list
            for (size_t i = 0; i < pool_size_; ++i) {
                free_list_.push_back(static_cast<frame_id_t>(i));
            }
        }

        BufferPoolManager::~BufferPoolManager() {
            FlushAllPages();
            delete[] pages_;
            delete replacer_;
        }

        bool BufferPoolManager::FindFreeFrame(frame_id_t* frame_id) {
            // 1. Try to get from free list first (cheapest)
            if (!free_list_.empty()) {
                *frame_id = free_list_.front();
                free_list_.pop_front();
                return true;
            }

            // 2. Try to get a victim from LRU Replacer
            if (replacer_->Victim(frame_id)) {
                // We found a victim frame.
                Page* victim_page = &pages_[*frame_id];

                // If the page in this victim frame is dirty, we MUST write it to disk.
                if (victim_page->is_dirty_) {
                    // BUG FIX: Use GetHeader() to get the start of the raw 4KB block.
                    // Previously used GetData(), which skipped the header and caused offset errors on disk.
                    disk_manager_->WritePage(victim_page->GetPageId(), reinterpret_cast<char*>(victim_page->GetHeader()));
                    victim_page->is_dirty_ = false;
                }

                // Remove the old mapping from the page table
                page_table_.erase(victim_page->GetPageId());

                // Reset memory for the new user
                victim_page->ResetMemory();
                victim_page->pin_count_ = 0;
                victim_page->is_dirty_ = false;

                return true;
            }

            // No free frame available (All pages are pinned)
            return false;
        }

        Page* BufferPoolManager::FetchPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // 1. Check if page is already in buffer pool
            if (page_table_.find(page_id) != page_table_.end()) {
                frame_id_t frame_id = page_table_[page_id];
                Page* page = &pages_[frame_id];

                // Mark usage in replacer (Pin it so it won't be evicted)
                replacer_->Pin(frame_id);
                page->pin_count_++;

                return page;
            }

            // 2. Page not in memory, find a frame for it
            frame_id_t free_frame_id;
            if (!FindFreeFrame(&free_frame_id)) {
                return nullptr; // Buffer full and all pages pinned
            }

            // 3. Read page from disk
            Page* page = &pages_[free_frame_id];

            // Safety: Reset memory before reading
            page->ResetMemory();

            // Read directly into the raw data buffer (including header space)
            // Note: DiskManager::ReadPage expects a pointer to the start of 4KB block.
            // Since Page wraps the buffer, we assume GetData() returns payload, 
            // but here we need the raw buffer pointer. 
            // Wait! Page class has 'char data_[PAGE_SIZE]'.
            // We should cast Page* to char* or add a friend/getter for raw data.
            // For now, let's assume Page class exposes 'GetHeader()' which is the start of data.
            disk_manager_->ReadPage(page_id, reinterpret_cast<char*>(page->GetHeader()));

            // 4. Setup metadata
            page->GetHeader()->page_id = page_id; // Ensure ID matches
            page->pin_count_ = 1;
            page->is_dirty_ = false;

            // 5. Update mappings
            page_table_[page_id] = free_frame_id;

            // Tell replacer this frame is now in use (Pinned)
            replacer_->Pin(free_frame_id);

            return page;
        }

        Page* BufferPoolManager::NewPage(page_id_t& page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // 1. Find a frame for the new page
            frame_id_t free_frame_id;
            if (!FindFreeFrame(&free_frame_id)) {
                return nullptr;
            }

            // 2. Allocate a new page ID from disk manager
            page_id = disk_manager_->AllocatePage();

            // 3. Setup the page object
            Page* page = &pages_[free_frame_id];
            page->ResetMemory();

            // Initialize Header
            page->GetHeader()->page_id = page_id;
            page->GetHeader()->is_leaf = 0;
            page->GetHeader()->key_count = 0;
            page->GetHeader()->creation_version = 0; // TODO: Implement Versioning Logic later

            page->pin_count_ = 1;
            page->is_dirty_ = true; // New pages are implicitly dirty until saved? usually yes.

            // 4. Update mappings
            page_table_[page_id] = free_frame_id;

            // Tell replacer this frame is in use
            replacer_->Pin(free_frame_id);

            return page;
        }

        bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            if (page->pin_count_ <= 0) {
                return false;
            }

            // Decrement pin count
            page->pin_count_--;

            // Update dirty flag
            if (is_dirty) {
                page->is_dirty_ = true;
            }

            // If pin count reaches 0, the page is candidate for eviction
            if (page->pin_count_ == 0) {
                replacer_->Unpin(frame_id);
            }

            return true;
        }

        bool BufferPoolManager::FlushPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            // Use GetHeader() to get the raw buffer start pointer
            disk_manager_->WritePage(page_id, reinterpret_cast<char*>(page->GetHeader()));
            page->is_dirty_ = false;

            return true;
        }

        bool BufferPoolManager::DeletePage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // 1. If page is not in memory, consider it "done" (or handle deallocation on disk if needed)
            if (page_table_.find(page_id) == page_table_.end()) {
                return true;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            // 2. If pinned, cannot delete
            if (page->pin_count_ > 0) {
                return false;
            }

            // 3. Remove from Replacer (It shouldn't be evicted anymore, it's becoming free)
            // We "Pin" it to remove it from the LRU list.
            replacer_->Pin(frame_id);

            // 4. Remove from Page Table
            page_table_.erase(page_id);

            // 5. Reset Metadata
            page->ResetMemory();
            page->pin_count_ = 0;
            page->is_dirty_ = false;
            page->GetHeader()->page_id = INVALID_PAGE_ID;

            // 6. Return frame to Free List
            free_list_.push_back(frame_id);

            // Note: We might want to call disk_manager_->DeallocatePage(page_id) if supported.
            return true;
        }

        void BufferPoolManager::FlushAllPages() {
            // We iterate over the page table to flush only valid pages
            std::lock_guard<std::mutex> lock(latch_);

            for (auto const& [pid, fid] : page_table_) {
                Page* page = &pages_[fid];
                if (page->is_dirty_) {
                    disk_manager_->WritePage(pid, reinterpret_cast<char*>(page->GetHeader()));
                    page->is_dirty_ = false;
                }
            }
        }

    } // namespace bufferpool
} // namespace cmse