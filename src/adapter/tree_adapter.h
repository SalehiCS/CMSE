#pragma once
#include "../page/page.h"
#include "../common/types.h"
#include <vector>

namespace cmse::adapter {

    /**
     * SplitResult
     * Output structure for the splitNode operation.
     */
    struct SplitResult {
        bool did_split = false;
        page_id_t left_page_id = INVALID_PAGE_ID;
        page_id_t right_page_id = INVALID_PAGE_ID; // The new page created during split
        KeyType promoted_key;                      // The key to be inserted into the parent
    };

    /**
     * TreeAdapter
     * Interface that Index implementations (B+Tree, Trie) must implement.
     * VersionManager uses this to perform logical operations on pinned CoW pages.
     */
    class TreeAdapter {
    public:
        virtual ~TreeAdapter() = default;

        // --- Basic Operations ---

        // Returns true if the page is a leaf node
        virtual bool isLeaf(Page* page) = 0;

        // Returns the root page ID for a committed version
        virtual page_id_t getRootForVersion(version_t v) = 0;

        // For an internal node, finds the child page ID that should contain the key
        virtual page_id_t findChild(Page* internal_page, const KeyType& key) = 0;

        // --- Modification Operations (Performed on CoW Copies) ---

        // Applies an insert/update/delete on a LEAF page.
        // Returns true if the page was modified.
        virtual bool applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val) = 0;

        // Updates a child pointer in a PARENT page.
        // Used when a child is copied to a new location (CoW) and parent must point to the new ID.
        virtual void updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id) = 0;

        // --- Structure Management (Split/Merge) ---

        // Splits a full node.
        // node_to_split: The full page (already a CoW copy).
        // new_right_page: An empty page allocated by VersionManager for the split.
        // out_promoted_key: The key that should move up to the parent.
        virtual void splitNode(Page* node_to_split, Page* new_right_page, KeyType& out_promoted_key) = 0;

        // Creates a new root when the old root splits.
        // new_root_page: Empty page allocated for the new root.
        virtual void createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key) = 0;
    };

} // namespace cmse::adapter