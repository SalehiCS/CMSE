#pragma once

#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/trie_page.h"
#include "../src/index/trie_value_page.h"
#include <string>
#include <vector>
#include <mutex>

namespace cmse::index {

    class TrieIndex {
    public:
        /**
         * Constructor
         * @param bpm: The Buffer Pool Manager used to fetch/create pages.
         */
        explicit TrieIndex(cmse::bufferpool::BufferPoolManager* bpm);

        /**
         * Insert a key (e.g., "ssh") and its associated log data.
         * This handles creating new pages and chaining value pages.
         */
        void Insert(const std::string& key, int64_t timestamp, uint8_t log_level);

        /**
         * Exact Match Search.
         * Returns all log entries for the exact key.
         */
        std::vector<cmse::TrieLogEntry> Search(const std::string& key);

        /**
         * Prefix Search (e.g., "sys").
         * Returns all log entries for any key starting with the prefix.
         * (To be implemented later).
         */
        std::vector<cmse::TrieLogEntry> TrieIndex::SearchPrefix(const std::string& prefix);

        // Helper to get the root (useful for debugging/testing)
        page_id_t GetRootId() const { return root_page_id_; }

        // Helper: Recursive DFS to collect all entries from a subtree
        void CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results);
        

        void SetRootPageId(page_id_t root_id) { root_page_id_ = root_id; }

    private:
        cmse::bufferpool::BufferPoolManager* bpm_;
        page_id_t root_page_id_;
        std::mutex latch_; // For simple thread safety in Phase 4

        // Helper to cast a raw Page* to TriePage*
        cmse::TriePage* FetchTriePage(page_id_t page_id);

        // Helper to cast a raw Page* to TrieValuePage*
        cmse::TrieValuePage* FetchValuePage(page_id_t page_id);
    };

} // namespace cmse::index