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

        /**
         * BufferPoolManager Constructor
         * PURPOSE: Pre-allocates memory for the cache and prepares the frame tracking.
         */
        BufferPoolManager::BufferPoolManager(size_t pool_size, cmse::disk::DiskManager* disk_manager)
            : pool_size_(pool_size), disk_manager_(disk_manager) {

            // Allocate a contiguous array of Page objects. 
            // This represents the physical RAM reserved for the buffer pool.
            pages_ = new Page[pool_size_];

            // Initialize the Least Recently Used (LRU) policy tracker.
            replacer_ = new LRUReplacer(pool_size);

            // Initially, the pool is empty, so every frame index is added to the free_list_.
            for (size_t i = 0; i < pool_size_; ++i) {
                free_list_.push_back(static_cast<frame_id_t>(i));
            }
        }

        /**
         * BufferPoolManager Destructor
         * PURPOSE: Ensures all modified data is safe on disk before releasing memory.
         */
        BufferPoolManager::~BufferPoolManager() {
            FlushAllPages(); // Persistence safety: Write all dirty pages to disk.
            delete[] pages_; // Deallocate the page array.
            delete replacer_; // Deallocate the replacement policy tracker.
        }

        /**
         * FindFreeFrame
         * PURPOSE: Finds an available slot (frame) in the pages_ array.
         * LOGIC: 1. Check the Free List. 2. If empty, run LRU Victim selection.
         */
        bool BufferPoolManager::FindFreeFrame(frame_id_t* frame_id) {
            // 1. Try to get from free list first (cheapest / zero I/O cost).
            if (!free_list_.empty()) {
                *frame_id = free_list_.front();
                free_list_.pop_front();
                return true;
            }

            // 2. Try to get a victim from LRU Replacer (occurs when buffer is full).
            if (replacer_->Victim(frame_id)) {
                // A victim frame was selected based on the LRU policy.
                Page* victim_page = &pages_[*frame_id];

                // Check for "Dirty" status: If modified, we MUST write it to disk before evicting.
                if (victim_page->is_dirty_) {
                    // ARCHITECTURAL NOTE: We use GetHeader() to ensure the full 4KB block 
                    // (including metadata) is persisted, avoiding disk offset errors.
                    disk_manager_->WritePage(victim_page->GetPageId(), reinterpret_cast<char*>(victim_page->GetHeader()));
                    victim_page->is_dirty_ = false;
                }

                // Remove the old PageID -> FrameID mapping; that page is no longer in RAM.
                page_table_.erase(victim_page->GetPageId());

                // Prepare the frame memory for a new occupant.
                victim_page->ResetMemory();
                victim_page->pin_count_ = 0;
                victim_page->is_dirty_ = false;

                return true;
            }

            // Failure: No free frames and all frames currently in memory are "Pinned" (in use).
            return false;
        }

        /**
         * FetchPage
         * PURPOSE: The primary entry point for the rest of the engine to get data.
         * THREAD SAFETY: Uses a lock_guard to ensure only one thread modifies the table at a time.
         */
        Page* BufferPoolManager::FetchPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // 1. Check if the requested page is already cached in the buffer pool.
            if (page_table_.find(page_id) != page_table_.end()) {
                frame_id_t frame_id = page_table_[page_id];
                Page* page = &pages_[frame_id];

                // Increase the pin count so the replacer knows not to evict this page.
                replacer_->Pin(frame_id);
                page->pin_count_++;

                return page;
            }

            // 2. Page is not in memory; we must find a frame to load it into.
            frame_id_t free_frame_id;
            if (!FindFreeFrame(&free_frame_id)) {
                return nullptr; // Hard failure: Buffer is full and all pages are currently pinned.
            }

            // 3. Coordinate with DiskManager to load the actual data.
            Page* page = &pages_[free_frame_id];

            // Zero out memory to prevent data leakage from previous occupants.
            page->ResetMemory();

            // Perform the physical Disk I/O.
            // We cast GetHeader() to char* to treat the entire Page object as a raw buffer.
            disk_manager_->ReadPage(page_id, reinterpret_cast<char*>(page->GetHeader()));

            // 4. Synchronize Page metadata.
            page->GetHeader()->page_id = page_id; // Store the ID within the page itself.
            page->pin_count_ = 1;                 // The caller is now using this page.
            page->is_dirty_ = false;              // Freshly read from disk; not yet modified.

            // 5. Update the lookup table for future FetchPage calls.
            page_table_[page_id] = free_frame_id;

            // Inform the replacer that this frame is "active" and cannot be a victim.
            replacer_->Pin(free_frame_id);

            return page;
        }



        /**
                 * NewPage
                 * PURPOSE: Allocates a new physical page on disk and maps it to a memory frame.
                 * @param page_id: Output parameter that receives the newly generated ID.
                 */
        Page* BufferPoolManager::NewPage(page_id_t& page_id) {
            std::lock_guard<std::mutex> lock(latch_); // Thread safety for pool metadata.

            // 1. Find a frame for the new page (check free list or evict a victim).
            frame_id_t free_frame_id;
            if (!FindFreeFrame(&free_frame_id)) {
                return nullptr; // Failure: All frames are currently pinned.
            }

            // 2. Request a unique ID from the DiskManager's allocator.
            page_id = disk_manager_->AllocatePage();

            // 3. Setup the physical Page object in the pre-allocated array.
            Page* page = &pages_[free_frame_id];
            page->ResetMemory(); // Clear any stale data from previous occupants.

            // Initialize the PageHeader metadata for a fresh node.
            page->GetHeader()->page_id = page_id;
            page->GetHeader()->is_leaf = 0;           // Default to internal node type.
            page->GetHeader()->key_count = 0;        // New pages start empty.
            page->GetHeader()->creation_version = 0; // Placeholder for future MVCC/versioning.

            page->pin_count_ = 1;  // Set to 1 because the caller is immediately using it.
            page->is_dirty_ = true; // New pages are "dirty" because they exist in RAM but not yet on disk.

            // 4. Register the mapping between the Disk ID and the RAM Frame.
            page_table_[page_id] = free_frame_id;

            // Notify the replacer that this frame is "Pinned" and ineligible for eviction.
            replacer_->Pin(free_frame_id);

            return page;
        }

        /**
         * FlushPage
         * PURPOSE: Synchronizes a specific memory frame with its corresponding disk location.
         */
        bool BufferPoolManager::FlushPage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // Fail if the page is not currently resident in the buffer pool.
            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            // Perform physical Write using the start of the 4KB block (GetHeader).
            disk_manager_->WritePage(page_id, reinterpret_cast<char*>(page->GetHeader()));
            page->is_dirty_ = false; // Reset dirty flag as memory and disk are now in sync.

            return true;
        }

        /**
         * DeletePage
         * PURPOSE: Discards a page from the buffer pool and frees its memory frame.
         */
        bool BufferPoolManager::DeletePage(page_id_t page_id) {
            std::lock_guard<std::mutex> lock(latch_);

            // 1. If page isn't in RAM, there is nothing to remove from the pool.
            if (page_table_.find(page_id) == page_table_.end()) {
                return true;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            // 2. Safety: Cannot delete a page that is currently being accessed by another thread.
            if (page->pin_count_ > 0) {
                return false;
            }

            // 3. Prevent the Replacer from attempting to evict a frame we are about to manually free.
            replacer_->Pin(frame_id);

            // 4. Wipe the entry from the lookup table.
            page_table_.erase(page_id);

            // 5. Reset all metadata and memory to return the frame to a "pristine" state.
            page->ResetMemory();
            page->pin_count_ = 0;
            page->is_dirty_ = false;
            page->GetHeader()->page_id = INVALID_PAGE_ID;

            // 6. Push the frame index back to the free_list_ for immediate reuse by NewPage/FetchPage.
            free_list_.push_back(frame_id);

            return true;
        }

        /**
         * FlushAllPages
         * PURPOSE: Mass synchronization of the entire buffer pool to disk.
         */
        void BufferPoolManager::FlushAllPages() {
            std::lock_guard<std::mutex> lock(latch_);

            int flushed = 0;
            // Iterate through every active mapping in the page table.
            for (auto const& [pid, fid] : page_table_) {
                Page* page = &pages_[fid];
                // Only write pages that have actually been modified to save I/O cycles.
                if (page->is_dirty_) {
                    disk_manager_->WritePage(pid, reinterpret_cast<char*>(page->GetHeader()));
                    page->is_dirty_ = false;
                    flushed++;
                }
            }

            // [CRITICAL FIX]
            // Ensure the OS file buffers are physically committed to the hardware.
            // This prevents data corruption if the system crashes immediately after this call.
            disk_manager_->Sync();
        }

        /**
         * UnpinPage
         * PURPOSE: Decrements the usage count of a page.
         * @param is_dirty: If true, tells the manager the page was modified while pinned.
         */
        bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
            std::lock_guard<std::mutex> lock(latch_);

            // Verify the page exists in the pool.
            if (page_table_.find(page_id) == page_table_.end()) {
                return false;
            }

            frame_id_t frame_id = page_table_[page_id];
            Page* page = &pages_[frame_id];

            // Safety check to prevent negative pin counts.
            if (page->pin_count_ <= 0) {
                return false;
            }

            // Decrement the usage counter.
            page->pin_count_--;

            // If any thread modified the page, it remains dirty until flushed.
            if (is_dirty) {
                page->is_dirty_ = true;
            }

            // If no threads are using this page anymore, it becomes a candidate for the LRU Replacer.
            if (page->pin_count_ == 0) {
                replacer_->Unpin(frame_id);
            }

            return true;
        }

    } // namespace bufferpool
} // namespace cmse