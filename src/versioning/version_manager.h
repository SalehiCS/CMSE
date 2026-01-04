#pragma once
#include "../common/types.h"
#include "../adapter/bpm_adapter.h"
#include "../adapter/tree_adapter.h"
#include <vector>

namespace cmse::versioning {

    /**
     * VersionManager
     * Manages version lifecycles, Copy-on-Write (CoW) logic, and commit operations.
     * It acts as the coordinator between the Buffer Pool and the Index Adapters.
     */
    class VersionManager {
    public:
        VersionManager(adapter::BufferPoolAdapter* bpm, adapter::TreeAdapter* tree_adapter);

        // Starts a new version transaction and returns the version ID.
        version_t createVersion();

        // Applies a logical update (Insert/Update) within the context of a version.
        // This handles traversal, CoW page allocation, and split propagation.
        bool applyUpdate(version_t version, version_t base_version, const KeyType& key, const ValueType& val);

        // Commits the version, making it persistent and visible.
        bool commitVersion(version_t version);

        // Aborts a version, discarding all staged pages.
        void abortVersion(version_t version);

        // Helper to read a page (mostly for testing/debugging).
        Page* readPage(page_id_t page_id, version_t version);

    private:
        adapter::BufferPoolAdapter* bpm_;
        adapter::TreeAdapter* adapter_;

        // Internal helper to handle recursive updates and splits
        // Returns the new page ID of the current node (if it changed/copied)
        page_id_t recursiveUpdate(version_t v, page_id_t current_page_id, const KeyType& key, const ValueType& val, bool& needs_split, KeyType& out_promoted_key, page_id_t& out_new_sibling_id);
    };

} // namespace cmse::versioning