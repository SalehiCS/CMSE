#include "buffer_pool_manager.h"

namespace cmse {
    namespace bufferpool {

        BufferPoolManager::BufferPoolManager(size_t pool_size, disk::DiskManager* disk_manager)
            : pool_size_(pool_size), disk_manager_(disk_manager) {

            pages_ = new Page[pool_size_];
            free_list_ = new std::list<frame_id_t>();

            for (size_t i = 0; i < pool_size_; ++i) {
                free_list_->push_back(static_cast<frame_id_t>(i));
            }
        }

        BufferPoolManager::~BufferPoolManager() {
            FlushAll();
            delete[] pages_;
            delete free_list_;
        }

        Page* BufferPoolManager::NewPage(page_id_t& page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            frame_id_t frame_id = -1;
            if (!free_list_->empty()) {
                frame_id = free_list_->front();
                free_list_->pop_front();
            }
            else {
                if (!FindVictim(&frame_id)) {
                    return nullptr;
                }
            }

            Page* page = &pages_[frame_id];

            // Write back dirty victim
            if (page->is_dirty_) {
                disk_manager_->WritePage(page->GetPageId(), page->GetData() - sizeof(PageHeader)); // Pointer arithmetic to get raw start
                // Simpler/Safer way: The Page structure manages data_. We can just cast data_ if page.h exposes it.
                // But since GetData() returns payload, we can use GetHeader() cast to char*.
                // Actually, in your new Page.h, GetData() skips header. 
                // We need to write the FULL page (Header + Data).
                // Let's rely on reinterpret_cast<char*>(page->GetHeader()) which points to start of data_.

                disk_manager_->WritePage(page->GetPageId(), reinterpret_cast<char*>(page->GetHeader()));
            }

            // Remove old mapping
            if (page->GetPageId() != INVALID_PAGE_ID) {
                page_table_.erase(page->GetPageId());
            }

            // Allocate new page
            page_id = disk_manager_->AllocatePage();

            // Reset memory (zeros out header and payload)
            page->ResetMemory();

            // SET PAGE ID IN HEADER (Correct way)
            page->GetHeader()->page_id = page_id;

            page->pin_count_ = 1;
            page->is_dirty_ = false; // New pages start clean (user will dirty them)

            page_table_[page_id] = frame_id;

            return page;
        }

        Page* BufferPoolManager::FetchPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) != page_table_.end()) {
                frame_id_t frame_id = page_table_[page_id];
                pages_[frame_id].pin_count_++;
                return &pages_[frame_id];
            }

            frame_id_t frame_id = -1;
            if (!free_list_->empty()) {
                frame_id = free_list_->front();
                free_list_->pop_front();
            }
            else {
                if (!FindVictim(&frame_id)) {
                    return nullptr;
                }
            }

            Page* page = &pages_[frame_id];

            if (page->is_dirty_) {
                disk_manager_->WritePage(page->GetPageId(), reinterpret_cast<char*>(page->GetHeader()));
            }

            if (page->GetPageId() != INVALID_PAGE_ID) {
                page_table_.erase(page->GetPageId());
            }

            // Read full page (Header + Data) from disk into the raw buffer
            disk_manager_->ReadPage(page_id, reinterpret_cast<char*>(page->GetHeader()));

            page->pin_count_ = 1;
            page->is_dirty_ = false;
            page_table_[page_id] = frame_id;

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

            return true;
        }

        bool BufferPoolManager::FlushPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            disk_manager_->WritePage(page_id, reinterpret_cast<char*>(page->GetHeader()));
            page->is_dirty_ = false;
            return true;
        }

        void BufferPoolManager::FlushAll() {
            std::lock_guard<std::mutex> lock(latch_);

            for (const auto& entry : page_table_) {
                frame_id_t frame_id = entry.second;
                Page* page = &pages_[frame_id];

                if (page->is_dirty_) {
                    disk_manager_->WritePage(page->GetPageId(), reinterpret_cast<char*>(page->GetHeader()));
                    page->is_dirty_ = false;
                }
            }
        }

        bool BufferPoolManager::DeletePage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            if (page_table_.find(page_id) == page_table_.end()) {
                return true;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            if (page->pin_count_ > 0) {
                return false;
            }

            page_table_.erase(page_id);
            page->ResetMemory();

            // FIX: Update ID in header, NOT in non-existent member variable
            page->GetHeader()->page_id = INVALID_PAGE_ID;

            page->is_dirty_ = false;
            page->pin_count_ = 0;

            free_list_->push_back(frame_id);
            return true;
        }

        bool BufferPoolManager::FindVictim(frame_id_t* frame_id) {
            for (const auto& entry : page_table_) {
                frame_id_t fid = entry.second;
                if (pages_[fid].pin_count_ == 0) {
                    *frame_id = fid;
                    return true;
                }
            }
            return false;
        }

    } // namespace bufferpool
} // namespace cmse