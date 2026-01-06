#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../disk/disk_manager.h"
#include "../page/page.h"
#include "../common/types.h"

namespace cmse {
    namespace bufferpool {

        class BufferPoolManager {
        public:
            BufferPoolManager(size_t pool_size, disk::DiskManager* disk_manager);
            ~BufferPoolManager();

            BufferPoolManager(const BufferPoolManager&) = delete;
            BufferPoolManager& operator=(const BufferPoolManager&) = delete;

            Page* FetchPage(page_id_t page_id);
            bool UnpinPage(page_id_t page_id, bool is_dirty);
            bool FlushPage(page_id_t page_id);
            Page* NewPage(page_id_t& page_id);
            void FlushAll();
            bool DeletePage(page_id_t page_id);

        private:
            bool FindVictim(frame_id_t* frame_id);

            size_t pool_size_;
            Page* pages_;
            disk::DiskManager* disk_manager_;
            std::unordered_map<page_id_t, frame_id_t> page_table_;
            std::list<frame_id_t>* free_list_; // Pointer to allow correct initialization
            std::mutex latch_;
        };

    } // namespace bufferpool
} // namespace cmse