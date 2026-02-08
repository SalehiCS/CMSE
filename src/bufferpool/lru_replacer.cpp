/**
 * lru_replacer.cpp
 *
 * Implementation of LRU replacement logic.
 */

#include "lru_replacer.h"
#include "../common/logger.h"

namespace cmse {
    namespace bufferpool {

        /**
         * LRUReplacer Constructor
         * PURPOSE: Initializes the replacer to manage a fixed number of frames.
         * @param num_pages: The maximum capacity of the buffer pool.
         */
        LRUReplacer::LRUReplacer(size_t num_pages) {
            // Ideally, we could reserve space in the map if we wanted to optimize allocation,
            // but standard initialization is fine.
        }

        /**
         * LRUReplacer Destructor
         * PURPOSE: Standard cleanup of the replacer resources.
         */
        LRUReplacer::~LRUReplacer() = default;

        /**
         * Victim
         * PURPOSE: Identifies and removes the Least Recently Used frame.
         * @param[out] frame_id: The index of the frame to be evicted from RAM.
         * @return: true if a victim was successfully found; false if no frames are unpinned.
         */
        bool LRUReplacer::Victim(frame_id_t* frame_id) {
            // Ensure thread safety as multiple threads may call FetchPage/evict simultaneously.
            std::lock_guard<std::mutex> lock(mutex_);

            // 1. If the list is empty, all frames are currently pinned and nothing can be evicted.
            if (lru_list_.empty()) {
                return false;
            }

            // 2. The victim is always at the BACK of the list (the Least Recently Used position).
            frame_id_t victim_frame = lru_list_.back();
            lru_list_.pop_back(); // Remove the element from the tracking list.

            // 3. Remove the corresponding entry from the map to keep both structures synced.
            lru_map_.erase(victim_frame);

            // 4. Assign the resulting ID to the output pointer.
            *frame_id = victim_frame;
            return true;
        }

        /**
         * Pin
         * PURPOSE: Removes a frame from the replacer because it is now being used by a thread.
         * WHY: Pinned pages MUST NOT be evicted.
         */
        void LRUReplacer::Pin(frame_id_t frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);

            // [LOG] Pinning implies the page is now in active use.
            LOG_DEBUG_LRU("Pin Frame: frame_id" << frame_id);


            // If the frame is in the replacer (map), it was a potential candidate for eviction.
            // Since a thread has requested it, we remove it from the eviction "death row".
            auto it = lru_map_.find(frame_id);
            if (it != lru_map_.end()) {
                // Use the iterator stored in the map to perform an O(1) removal from the list.
                lru_list_.erase(it->second);
                lru_map_.erase(it);
            }

            // If it wasn't in the map, it was already pinned or newly created; do nothing.
        }

        /**
         * Unpin
         * PURPOSE: Adds a frame to the replacer once its pin_count reaches 0.
         * WHY: This frame is no longer actively used and can now be sacrificed for new pages.
         */
        void LRUReplacer::Unpin(frame_id_t frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);

            // Safety check: If already in the replacer, we don't want duplicate entries.
            if (lru_map_.find(frame_id) != lru_map_.end()) {
                return;
            }

            // [LOG] Unpinning implies the page is now a candidate for eviction.
            LOG_DEBUG_LRU("Unpin Frame " << frame_id << " (Candidate for eviction)");

            // New candidates are added to the FRONT (Most Recently Used position).
            // They will only move to the back (Victim position) as other pages are unpinned.
            lru_list_.push_front(frame_id);

            // Store the list iterator in the map to allow O(1) deletion if the page is Pinned again.
            lru_map_[frame_id] = lru_list_.begin();
        }

        /**
         * Size
         * @return: The number of unpinned frames currently eligible for eviction.
         */
        size_t LRUReplacer::Size() {
            std::lock_guard<std::mutex> lock(mutex_);
            return lru_list_.size();
        }

    } // namespace bufferpool
} // namespace cmse