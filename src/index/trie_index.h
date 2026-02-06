#pragma once

#include "../bufferpool/buffer_pool_manager.h"
#include "../bufferpool/page_guard.h" // RAII protection for buffer pool pins
#include "../index/trie_page.h"
#include "../index/trie_value_page.h"
#include <string>
#include <vector>
#include <mutex>

namespace cmse::index {

    /**
     * SearchResult
     * A specialized container for query results that prevents memory spikes.
     * It tracks whether a search exceeded a user-defined capacity (cap).
     */
    struct SearchResult {
        /** True if the search found more results than the 'cap' parameter allowed. */
        bool is_overflow = false;
        /** List of unique timestamps (log IDs) that matched the query and filters. */
        std::vector<int64_t> timestamps;
    };

    /**
     * TrieIndex
     * A persistent, disk-based Prefix Tree (Trie) for indexing log string data.
     * Unlike a memory-only Trie, this uses Page IDs to navigate between nodes
     * stored in the Buffer Pool.
     */
    class TrieIndex {
    public:
        /** Initializes the index with a reference to the global Buffer Pool Manager. */
        explicit TrieIndex(cmse::bufferpool::BufferPoolManager* bpm);

        // --- Standard Indexing Operations ---

        /** * Maps a string key to a specific log entry.
         * Navigates down the tree, creating nodes as needed.
         */
        void Insert(const std::string& key, int64_t timestamp, uint8_t log_level);

        /** Returns all log entries matching the key string exactly. */
        std::vector<cmse::TrieLogEntry> Search(const std::string& key);

        /** Returns all log entries for keys that start with the provided prefix string. */
        std::vector<cmse::TrieLogEntry> SearchPrefix(const std::string& prefix);

        // --- Persistence & Metadata ---

        /** Retrieves the Page ID of the tree root (used for persistent recovery). */
        page_id_t GetRootId() const { return root_page_id_; }

        /** Manually sets the root ID (used when loading an existing index from disk). */
        void SetRootPageId(page_id_t root_id) { root_page_id_ = root_id; }

        /**
         * Advanced Search: Optimized query with multiple server-side filters.
         * @param is_prefix If true, performs a wildcard search (*).
         * @param priority_filter Filter by log level (INFO/WARN/ERROR); use -1 for all.
         * @param min_ts/max_ts Timestamp range for time-based queries.
         * @param cap Maximum number of results to return (prevents OOM errors).
         */
        SearchResult GetTimestampsWithCap(
            const std::string& key,
            bool is_prefix,
            int32_t priority_filter,
            int64_t min_ts,
            int64_t max_ts,
            size_t cap
        );

    private:
        /** Global pointer to the Buffer Pool for page allocation/fetching. */
        cmse::bufferpool::BufferPoolManager* bpm_;
        /** The starting point of the Trie. Persistent across restarts. */
        page_id_t root_page_id_;
        /** Mutex protecting the tree from race conditions during concurrent Insert/Search. */
        std::mutex latch_;

        // --- Internal Helpers (Private logic) ---

        /** Performs a Depth-First Search (DFS) to harvest all entries in a subtree. */
        void CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results);

        /** Wraps BPM::FetchPage in a PageGuard to ensure automatic unpinning. */
        PageGuard FetchPageGuard(page_id_t page_id);

        /** Recursive logic for subtree collection that respects result capacity limits. */
        void CollectAllWithCap(
            page_id_t page_id,
            SearchResult& result,
            int32_t priority_filter,
            int64_t min_ts,
            int64_t max_ts,
            size_t cap
        );

        /** Iterates through the linked-list of data buckets (Value Pages) for a node. */
        void ScanValuePageChain(
            page_id_t vp_id,
            SearchResult& result,
            int32_t priority_filter,
            int64_t min_ts,
            int64_t max_ts,
            size_t cap
        );
    };

} // namespace cmse::index