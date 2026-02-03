#pragma once

#include <vector>
#include <mutex>
#include <string>

#include "common/types.h"
#include "adapter/btree_adapter.h"

namespace cmse {
    class Page;
    namespace bufferpool {
        class BufferPoolManager;
    }
}

namespace cmse::index {

    class BTreeIndex {
    public:
        BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id = INVALID_PAGE_ID);

        // --- Core API ---
        bool Insert(const KeyType& key, const ValueType& value);
        bool GetValue(const KeyType& key, ValueType& result);

        // --- NEW: Range Scan API (Phase 2 Completion) ---
        // Returns all records where key is in [start_key, end_key] inclusive.
        std::vector<ValueType> Scan(const KeyType& start_key, const KeyType& end_key);

        page_id_t GetRootPageId() const { return root_page_id_; }

        // --- Visualizer API ---
        void PrintTree(int limit_depth = 3);

    private:
        cmse::bufferpool::BufferPoolManager* bpm_;
        cmse::adapter::BTreeAdapter adapter_;
        page_id_t root_page_id_;
        std::mutex latch_;

        struct TraversalContext {
            std::vector<cmse::Page*> path_pages;
            void UnpinAll(cmse::bufferpool::BufferPoolManager* bpm, bool dirty);
        };

        cmse::Page* FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write);
        void HandleSplit(cmse::Page* node, TraversalContext& ctx);
        void StartNewTree(const KeyType& key, const ValueType& value);

        // --- Helper for Visualization ---
        void PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix);

        void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key);
    };

} // namespace cmse::index