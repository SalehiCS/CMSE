#pragma once

#include <vector>
#include <mutex>
#include <string>

#include "common/types.h"
#include "adapter/btree_adapter.h"
#include "btree_iterator.h"
#include "../bufferpool/page_guard.h"

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
        void PrintTree(int limit = 10);

        void SetRootPageId(page_id_t root_id) { root_page_id_ = root_id; }
        
        BTreeIterator Begin(const KeyType& start_key);

        

    private:
        cmse::bufferpool::BufferPoolManager* bpm_;
        cmse::adapter::BTreeAdapter adapter_;
        page_id_t root_page_id_;
        std::mutex latch_;

        // RAII Wrapper for the traversal stack
        struct TraversalContext {
            std::vector<PageGuard> path_pages; // <--- Changed from Page* to PageGuard

            // Mark all pages in the stack as dirty (Used on successful insert)
            void SetAllDirty(bool dirty) {
                for (auto& guard : path_pages) {
                    guard.SetDirty(dirty);
                }
            }

            // UnpinAll is no longer needed! The vector destructor handles it.
        };

        // Updated Signatures
        // FindLeaf now returns a Guard instead of a raw pointer
        PageGuard FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write);

        void HandleSplit(PageGuard node_guard, TraversalContext& ctx); // Updated
        void StartNewTree(const KeyType& key, const ValueType& value);

        // --- Helper for Visualization ---
        void PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix);

        void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key);
    };

} // namespace cmse::index