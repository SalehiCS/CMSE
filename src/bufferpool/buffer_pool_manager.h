/**
 * buffer_pool_manager.h
 *
 * BufferPoolManager reads/writes pages to/from disk via DiskManager and caches them in memory.
 * It uses LRUReplacer to keep track of unpinned pages and decides which page to evict.
 */

#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../common/types.h"
#include "../disk/disk_manager.h"
#include "../page/page.h"
#include "lru_replacer.h" // <--- Include the new LRU Replacer

namespace cmse {
    namespace bufferpool {

        class BufferPoolManager {
        public:
            /**
             * Creates a new BufferPoolManager.
             * @param pool_size The size of the buffer pool.
             * @param disk_manager The disk manager.
             */
            BufferPoolManager(size_t pool_size, cmse::disk::DiskManager* disk_manager);

            /**
             * Destroys the BufferPoolManager.
             */
            ~BufferPoolManager();

            /**
             * Fetches the requested page from the buffer pool.
             * @param page_id The id of the page to fetch.
             * @return nullptr if page_id cannot be fetched, otherwise pointer to the Page.
             */
            Page* FetchPage(page_id_t page_id);

            /**
             * Unpins the target page from the buffer pool.
             * @param page_id The id of the page to unpin.
             * @param is_dirty true if the page was modified.
             * @return false if the page_id is not in the page table or its pin count is <= 0.
             */
            bool UnpinPage(page_id_t page_id, bool is_dirty);

            /**
             * Flushes the target page to disk.
             * @param page_id The id of the page to flush.
             * @return false if the page could not be found in the page table, true otherwise.
             */
            bool FlushPage(page_id_t page_id);

            /**
             * Creates a new page in the buffer pool.
             * @param[out] page_id The id of the created page.
             * @return nullptr if no new page could be created, otherwise pointer to the new Page.
             */
            Page* NewPage(page_id_t& page_id);

            /**
             * Deletes a page from the buffer pool.
             * @param page_id The id of the page to delete.
             * @return false if the page exists but is pinned, true if deleted.
             */
            bool DeletePage(page_id_t page_id);

            /**
             * Flushes all the pages in the buffer pool to disk.
             */
            void FlushAllPages();

        private:
            /**
             * Helper to find a free frame.
             * It first checks the free_list_. If empty, it tries to find a victim in the replacer.
             * @param[out] frame_id The id of the found frame.
             * @return true if a frame was found, false otherwise.
             */
            bool FindFreeFrame(frame_id_t* frame_id);

            size_t pool_size_;
            cmse::disk::DiskManager* disk_manager_;
            Page* pages_; // Array of pages
            LRUReplacer* replacer_; // <--- The LRU Replacement Policy

            // List of free frames that do not hold any page data.
            std::list<frame_id_t> free_list_;

            // Map from PageId to FrameId
            std::unordered_map<page_id_t, frame_id_t> page_table_;

            std::mutex latch_; // Concurrency protection
        };

    } // namespace bufferpool
} // namespace cmse