#pragma once
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>

#include "../adapter/bpm_adapter.h"
#include "../disk/disk_manager.h"
#include "../page/page.h"

namespace cmse {
    namespace bufferpool {

        /**
         * BufferPoolManager
         * Manages a pool of pages in memory. Handles fetching from disk,
         * writing back dirty pages, and LRU replacement policy.
         */
        class BufferPoolManager : public adapter::BufferPoolAdapter {
        public:
            /**
             * @param pool_size Number of pages in the buffer pool
             * @param disk_manager Pointer to the disk manager (takes ownership if needed, but here we assume shared)
             */
            BufferPoolManager(size_t pool_size, disk::DiskManager* disk_manager);

            ~BufferPoolManager();

            // --- Implementation of Adapter Interface ---

            Page* FetchPage(page_id_t page_id) override;
            bool UnpinPage(page_id_t page_id, bool is_dirty) override;
            Page* NewPage(page_id_t& out_page_id) override;
            bool FlushPage(page_id_t page_id) override;
            void FlushAll() override;

            // --- Debug / Helper Methods ---
            size_t GetPoolSize() const { return pool_size_; }

        private:
            /**
             * Finds a frame to use for a new page.
             * 1. If there is a page in the free list, use it.
             * 2. If not, evict a page using LRU policy.
             * Returns nullptr if all pages are pinned (buffer full).
             */
            bool FindVictim(frame_id_t* out_frame_id);

            size_t pool_size_;
            disk::DiskManager* disk_manager_;

            // Array of pages in memory
            Page* pages_;

            // Map page_id -> frame_id (index in pages_ array)
            std::unordered_map<page_id_t, frame_id_t> page_table_;

            // List of free frames (indices)
            std::list<frame_id_t> free_list_;

            // LRU Replacement Data Structures
            // List of frame_ids that are candidates for eviction (pin_count == 0)
            std::list<frame_id_t> lru_list_;
            // Map for fast removal from LRU list: frame_id -> iterator
            std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;

            std::mutex latch_; // Concurrency protection
        };

    } // namespace bufferpool
} // namespace cmse