#pragma once

#include "../src/common/types.h"
#include "../src/page/page.h"
#include <cstring>

namespace cmse {

    /** * Fanout is 256 to support the full range of Extended ASCII.
     * This allows us to use a character's byte value directly as an array index (Direct Addressing).
     */
    constexpr int TRIE_FANOUT = 256;

    /**
     * TriePage
     * Represents a single node in the Prefix-Tree (Trie) stored within a physical 4KB Disk Page.
     * * PHYSICAL MEMORY LAYOUT (Internal):
     * - [0-3]   is_terminal_   : Indicates if this node completes a stored string.
     * - [4-7]   value_page_id_ : Reference to the Page containing actual log data for this prefix.
     * - [8-1031] children_[]   : 256 pointers (4-bytes each) to child Trie nodes.
     * - [1032-4096] Padding    : Unused space ensuring alignment and future extensibility.
     */
    class TriePage {
    public:
        // --- Initialization ---

        /**
         * Resets the node state.
         * Important: Must be called when the Buffer Pool first allocates a page for a Trie node.
         */
        void Init() {
            is_terminal_ = false;               // Node defaults to a non-word/branching node
            value_page_id_ = INVALID_PAGE_ID;   // No data bucket associated yet

            // Initialize all child slots to INVALID to prevent junk pointer traversal
            for (int i = 0; i < TRIE_FANOUT; i++) {
                children_[i] = INVALID_PAGE_ID;
            }
        }

        // --- Terminal & Value Page Management ---

        /** Checks if this node marks the end of a valid indexed string prefix. */
        bool IsTerminal() const { return is_terminal_; }

        /** Marks this node as a terminal point in the trie. */
        void SetTerminal(bool terminal) { is_terminal_ = terminal; }

        /** Returns the Page ID where the actual LogRecords for this prefix are stored. */
        page_id_t GetValuePageId() const { return value_page_id_; }

        /** Links this trie node to a specific data bucket (Value Page). */
        void SetValuePageId(page_id_t page_id) { value_page_id_ = page_id; }

        // --- Child Management ---

        /**
         * Sets the child page ID for a specific character.
         * Maps char [0-255] -> PageID in constant time.
         */
        void SetChild(char ch, page_id_t child_id) {
            // Treat char as unsigned to ensure it falls within the 0-255 range
            uint8_t index = static_cast<uint8_t>(ch);
            children_[index] = child_id;
        }

        /**
         * Retrieves the child page ID for a specific character.
         * @return page_id_t if child exists, otherwise INVALID_PAGE_ID.
         */
        page_id_t GetChild(char ch) const {
            uint8_t index = static_cast<uint8_t>(ch);
            return children_[index];
        }

        /**
         * Utility to check existence of a child without retrieving the full ID.
         */
        bool HasChild(char ch) const {
            return GetChild(ch) != INVALID_PAGE_ID;
        }

    private:
        // --- Physical Data Layout (Directly mapped to 4KB Page) ---

        /** * Boolean flag stored as a 32-bit integer.
         * Logic: Using int32 ensures the next member (page_id_t) starts on a 4-byte boundary.
         */
        int32_t is_terminal_;

        /** * Pointer to the Bucket (TrieValuePage) containing the actual log entries.
         * Only meaningful if is_terminal_ is true.
         */
        page_id_t value_page_id_;

        /** * The branching array.
         * Total size: 256 * 4 bytes = 1024 bytes (Exactly 1/4 of a standard page).
         */
        page_id_t children_[TRIE_FANOUT];

        /** * The remaining ~3064 bytes are unused in the current version.
         * Architecture Note: This padding ensures that even if we add more metadata,
         * we do not exceed the PAGE_SIZE limit or break alignment.
         */
    };

} // namespace cmse