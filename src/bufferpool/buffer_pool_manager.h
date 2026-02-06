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

        /**
         * BufferPoolManager
         * PURPOSE: Manages the movement of pages between physical disk and main memory.
         * WHY: Database data is too large for RAM; this component ensures the most relevant
         * pages are cached while maintaining ACID durability.
         */
        class BufferPoolManager {
        public:
            /**
             * BufferPoolManager Constructor
             * @param pool_size: Total number of frames (slots) in memory.
             * @param disk_manager: The interface for physical I/O operations.
             * WHY: The manager must pre-allocate the physical 'Page' objects during creation.
             */
            BufferPoolManager(size_t pool_size, cmse::disk::DiskManager* disk_manager);

            /**
             * BufferPoolManager Destructor
             * PURPOSE: Cleans up the allocated page array and the replacement policy object.
             */
            ~BufferPoolManager();

            /**
             * FetchPage
             * PURPOSE: Retrieves a page by its logical ID.
             * LOGIC: Checks the Page Table (RAM) first. If missing, it finds a victim frame,
             * potentially flushes it to disk, and reads the new page from DiskManager.
             * @return: Pointer to the requested Page object, pinned in memory.
             */
            Page* FetchPage(page_id_t page_id);

            /**
             * UnpinPage
             * PURPOSE: Signals that the caller is done using a specific page.
             * @param is_dirty: If true, the page is marked modified and must be flushed eventually.
             * WHY: Once a page's pin_count hits zero, the LRUReplacer can choose to evict it.
             */
            bool UnpinPage(page_id_t page_id, bool is_dirty);

            /**
             * FlushPage
             * PURPOSE: Forces a specific page to be written to disk regardless of its dirty status.
             * WHY: Critical for checkpoints and ensuring data persistence.
             */
            bool FlushPage(page_id_t page_id);

            /**
             * NewPage
             * PURPOSE: Allocates a brand-new page on disk and brings it into a memory frame.
             * @param[out] page_id: Returns the ID assigned to the new page.
             * @return: Pointer to the newly allocated Page object.
             */
            Page* NewPage(page_id_t& page_id);

            /**
             * DeletePage
             * PURPOSE: Removes a page from both memory and the physical disk.
             * @return: False if the page is currently pinned (being used) and cannot be deleted.
             */
            bool DeletePage(page_id_t page_id);

            /**
             * FlushAllPages
             * PURPOSE: Iterates through the entire buffer pool and writes all pages to disk.
             * WHY: Used during system shutdown to prevent data loss.
             */
            void FlushAllPages();

        private:
            /**
             * FindFreeFrame
             * PURPOSE: Internal helper to identify a memory slot for a new page.
             * LOGIC: 1. Use empty slots from free_list_ first.
             * 2. If none, use LRUReplacer to evict an unpinned page.
             */
            bool FindFreeFrame(frame_id_t* frame_id);

            // Maximum number of pages the pool can hold in RAM.
            size_t pool_size_;
            // The physical storage driver.
            cmse::disk::DiskManager* disk_manager_;
            // Physical array of Page objects (the actual cache storage).
            Page* pages_;
            // The algorithm that decides which page to kick out when the pool is full.
            LRUReplacer* replacer_;

            // Tracks frame indices that are currently empty (not mapped to any page).
            std::list<frame_id_t> free_list_;

            // Maps logical Page IDs to physical Frame Indices in the 'pages_' array.
            std::unordered_map<page_id_t, frame_id_t> page_table_;

            // Mutex to ensure thread-safety during concurrent page access.
            std::mutex latch_;
        };

    } // namespace bufferpool
} // namespace cmse