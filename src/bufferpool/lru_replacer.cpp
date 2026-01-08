/**
 * lru_replacer.cpp
 *
 * Implementation of LRU replacement logic.
 */

#include "lru_replacer.h"

namespace cmse {
    namespace bufferpool {

        LRUReplacer::LRUReplacer(size_t num_pages) {
            // Ideally, we could reserve space in the map if we wanted to optimize allocation,
            // but standard initialization is fine.
        }

        LRUReplacer::~LRUReplacer() = default;

        bool LRUReplacer::Victim(frame_id_t* frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);

            // 1. If the list is empty, we cannot evict anything.
            if (lru_list_.empty()) {
                return false;
            }

            // 2. The victim is always at the BACK of the list (Least Recently Used).
            frame_id_t victim_frame = lru_list_.back();
            lru_list_.pop_back();

            // 3. Remove from the map as well.
            lru_map_.erase(victim_frame);

            // 4. Output the victim frame id.
            *frame_id = victim_frame;
            return true;
        }

        void LRUReplacer::Pin(frame_id_t frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);

            // If the frame is in the replacer (map), it means it was a candidate for eviction.
            // Since it is being pinned now (used by a thread), we must remove it from the replacer.
            auto it = lru_map_.find(frame_id);
            if (it != lru_map_.end()) {
                lru_list_.erase(it->second);
                lru_map_.erase(it);
            }

            // If it wasn't in the map, it implies it was already pinned or invalid, 
            // so we do nothing.
        }

        void LRUReplacer::Unpin(frame_id_t frame_id) {
            std::lock_guard<std::mutex> lock(mutex_);

            // If the frame is already in the replacer, we typically don't add it again.
            // However, some implementations might move it to the front to update 'recency'.
            // Here, we check existence to avoid duplicates.
            if (lru_map_.find(frame_id) != lru_map_.end()) {
                return;
            }

            // When a page is unpinned (usage finished), it is considered "Recently Used".
            // So we add it to the FRONT of the list.
            lru_list_.push_front(frame_id);

            // Store the iterator in the map for O(1) access later.
            lru_map_[frame_id] = lru_list_.begin();
        }

        size_t LRUReplacer::Size() {
            std::lock_guard<std::mutex> lock(mutex_);
            return lru_list_.size();
        }

    } // namespace bufferpool
} // namespace cmse