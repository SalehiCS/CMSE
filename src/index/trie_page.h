#pragma once

#include "common/types.h"
#include "page/page.h"
#include <cstring>

namespace cmse {

    // Fanout is 256 to support Extended ASCII directly (O(1) lookup per node)
    constexpr int TRIE_FANOUT = 256;

    /**
     * TriePage
     * Represents a single node in the Trie structure stored on a Disk Page.
     * Layout:
     * - is_terminal (4 bytes)
     * - value_page_id (4 bytes): Points to TrieValuePage if is_terminal is true
     * - children (1024 bytes): Array mapping char -> page_id_t
     */
    class TriePage {
    public:
        // --- Initialization ---
        void Init() {
            is_terminal_ = false;
            value_page_id_ = INVALID_PAGE_ID;

            // Initialize all children pointers to INVALID
            for (int i = 0; i < TRIE_FANOUT; i++) {
                children_[i] = INVALID_PAGE_ID;
            }
        }

        // --- Terminal & Value Page Management ---

        bool IsTerminal() const { return is_terminal_; }

        void SetTerminal(bool terminal) { is_terminal_ = terminal; }

        page_id_t GetValuePageId() const { return value_page_id_; }

        void SetValuePageId(page_id_t page_id) { value_page_id_ = page_id; }

        // --- Child Management ---

        /**
         * Set the child page ID for a specific character.
         */
        void SetChild(char ch, page_id_t child_id) {
            uint8_t index = static_cast<uint8_t>(ch);
            children_[index] = child_id;
        }

        /**
         * Get the child page ID for a specific character.
         * @return page_id_t or INVALID_PAGE_ID
         */
        page_id_t GetChild(char ch) const {
            uint8_t index = static_cast<uint8_t>(ch);
            return children_[index];
        }

        /**
         * Check if a child exists for the given character.
         */
        bool HasChild(char ch) const {
            return GetChild(ch) != INVALID_PAGE_ID;
        }

    private:
        // --- Physical Data Layout (Directly mapped to 4KB Page) ---

        // 1. Terminal Flag (Using int32 for alignment safety)
        int32_t is_terminal_;

        // 2. Pointer to the Bucket (ValuePage) containing actual logs
        page_id_t value_page_id_;

        // 3. Static array for children pointers (Direct Mapping)
        // 256 * 4 bytes = 1024 bytes
        page_id_t children_[TRIE_FANOUT];

        // The rest of the page (approx 3KB) is currently unused padding.
        // It could be used for compression or metadata in the future.
    };

} // namespace cmse