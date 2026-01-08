/**
 * lru_replacer.h
 *
 * Implementation of the Least Recently Used (LRU) replacement policy.
 * It tracks the usage of frames (pages in memory) to decide which one to evict
 * when the buffer pool is full.
 */

#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../common/types.h"

namespace cmse {
    namespace bufferpool {

        /**
         * LRUReplacer manages the unpinned frames in the Buffer Pool.
         * It stores frames that are currently NOT being used by any thread (pin_count == 0).
         */
        class LRUReplacer {
        public:
            /**
             * Create a new LRUReplacer.
             * @param num_pages The maximum number of frames the replacer will need to track.
             */
            explicit LRUReplacer(size_t num_pages);

            /**
             * Destructor.
             */
            ~LRUReplacer();

            /**
             * Remove the object that was accessed the least recently compared to all the
             * elements being tracked by the Replacer, stores its contents in the output parameter
             * and deletes it from the Replacer.
             *
             * @param[out] frame_id The id of the frame that was evicted.
             * @return true if a victim was found, false if the Replacer is empty.
             */
            bool Victim(frame_id_t* frame_id);

            /**
             * Pins a frame, meaning it is currently in use by a thread.
             * A pinned frame should be removed from the LRUReplacer because it cannot be evicted.
             *
             * @param frame_id the id of the frame to pin.
             */
            void Pin(frame_id_t frame_id);

            /**
             * Unpins a frame, meaning the thread is done using it.
             * The frame is added to the LRUReplacer and becomes a candidate for eviction.
             *
             * @param frame_id the id of the frame to unpin.
             */
            void Unpin(frame_id_t frame_id);

            /** @return the number of elements in the replacer that can be evicted. */
            size_t Size();

        private:
            // Protects the replacer data structures for thread safety.
            std::mutex mutex_;

            // Doubly linked list to track usage order.
            // Front = Most Recently Used (MRU)
            // Back  = Least Recently Used (LRU) -> Victim
            std::list<frame_id_t> lru_list_;

            // Hash map to quickly find the iterator of a frame in the list.
            // Key: frame_id, Value: iterator to the node in lru_list_
            std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
        };

    } // namespace bufferpool
} // namespace cmse