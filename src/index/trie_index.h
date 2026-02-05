#pragma once

#include "../bufferpool/buffer_pool_manager.h"
#include "../bufferpool/page_guard.h" // <--- Include PageGuard
#include "../index/trie_page.h"
#include "../index/trie_value_page.h"
#include <string>
#include <vector>
#include <mutex>

namespace cmse::index {

    struct SearchResult {
        bool is_overflow = false;
        std::vector<int64_t> timestamps;
    };

    class TrieIndex {
    public:
        explicit TrieIndex(cmse::bufferpool::BufferPoolManager* bpm);

        // Standard Operations
        void Insert(const std::string& key, int64_t timestamp, uint8_t log_level);
        std::vector<cmse::TrieLogEntry> Search(const std::string& key);
        std::vector<cmse::TrieLogEntry> SearchPrefix(const std::string& prefix);

        // Debugging / Metadata
        page_id_t GetRootId() const { return root_page_id_; }
        void SetRootPageId(page_id_t root_id) { root_page_id_ = root_id; }

        SearchResult GetTimestampsWithCap(
            const std::string& key,
            bool is_prefix,
            int32_t priority_filter, // -1 for ANY
            int64_t min_ts,
            int64_t max_ts,
            size_t cap
        );
    private:
        cmse::bufferpool::BufferPoolManager* bpm_;
        page_id_t root_page_id_;
        std::mutex latch_;

        // --- Helpers ---

        // Helper: Recursive DFS to collect all entries from a subtree
        // Note: Modified to handle guards internally or via IDs
        void CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results);

        // Helper: Fetches a page and wraps it in a Guard immediately
        PageGuard FetchPageGuard(page_id_t page_id);

        // Recursive helper for the Cap logic
        void CollectAllWithCap(
            page_id_t page_id,
            SearchResult& result,
            int32_t priority_filter,
            int64_t min_ts,
            int64_t max_ts,
            size_t cap
        );

        // Helper to process a specific Value Page Chain
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