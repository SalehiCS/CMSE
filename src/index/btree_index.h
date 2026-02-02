#pragma once

#include <vector>
#include <mutex>
#include <string>

// Include necessary common types and the adapter definition
// We assume "src" is in the include path, so we use absolute paths from src root.
#include "common/types.h"
#include "adapter/btree_adapter.h"

namespace cmse {
    // Forward declaration of Page class
    class Page;

    // Forward declaration of BufferPoolManager inside its specific namespace
    namespace bufferpool {
        class BufferPoolManager;
    }
}

namespace cmse::index {

    /**
     * BTreeIndex
     * The main entry point for the Storage Engine.
     * Manages the B+Tree structure on disk via BufferPoolManager.
     */
    class BTreeIndex {
    public:
        // Constructor
        // We use the fully qualified name for BufferPoolManager to avoid ambiguity
        BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id = INVALID_PAGE_ID);

        // --- Core API ---

        // Inserts a log record into the index
        bool Insert(const KeyType& key, const ValueType& value);

        // Point Query: Finds a record by exact timestamp
        bool GetValue(const KeyType& key, ValueType& result);

        // Returns the current root page id
        page_id_t GetRootPageId() const { return root_page_id_; }

    private:
        // Member variables
        cmse::bufferpool::BufferPoolManager* bpm_;
        cmse::adapter::BTreeAdapter adapter_;
        page_id_t root_page_id_;
        std::mutex latch_; // Thread safety

        // --- Helper Context ---
        // Keeps track of the path taken from Root to Leaf during traversal
        struct TraversalContext {
            std::vector<cmse::Page*> path_pages;

            // Declaration only. Defined in .cpp to avoid incomplete type errors
            void UnpinAll(cmse::bufferpool::BufferPoolManager* bpm, bool dirty);
        };

        // --- Internal Helpers ---

        // Descend tree to find the correct leaf node
        cmse::Page* FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write);

        // Handle splitting of a full node and propagating changes up
        void HandleSplit(cmse::Page* node, TraversalContext& ctx);

        // Initialize a brand new tree if root is invalid
        void StartNewTree(const KeyType& key, const ValueType& value);
    };

} // namespace cmse::index