#pragma once
#include "../storage/page/page.h"
#include "../common/types.h"
#include <vector>
#include <cstring>

namespace cmse::adapter {

    /**
     * TrieNodeEntry
     * Represents a single edge in the Trie node.
     * Maps a character to a child Page ID.
     */
    struct TrieNodeEntry {
        char key_char;          // The character for this edge
        page_id_t child_page_id; // The pointer to the next node (Page ID)
    };

    /**
     * TrieNodeHeader
     * Header for Trie nodes stored on disk.
     * * STATISTICAL OPTIMIZATION (Phase 3/4 specific):
     * We include 'subtree_terminals'. This count helps the query optimizer
     * estimate the selectivity of a prefix without traversing the whole subtree.
     */
    struct TrieNodeHeader {
        bool is_terminal;           // True if this node marks the end of a valid word
        int16_t child_count;        // Number of active children entries
        ValueType value;            // The payload (e.g., RecordID) if is_terminal is true

        // --- Statistical Metadata ---
        int32_t subtree_terminals;  // Total number of terminal nodes in the subtree rooted here.
        // Used for quick COUNT(*) queries on prefixes.
    };

    // Constants
    // Max children per node depends on PAGE_SIZE. 
    // For 4KB page: (4096 - sizeof(Header)) / sizeof(Entry) ~ 800.
    // We can safely assume it handles full ASCII (256) without overflow/splitting logic.
    constexpr int MAX_TRIE_CHILDREN = 256;

    /**
     * TrieAdapter
     * Manages Page-based Trie operations for Text Indexing (Phase 4).
     * Unlike B+Tree, Trie nodes do not split horizontally; they grow vertically.
     * * This class handles raw byte manipulation on the Page object.
     */
    class TrieAdapter {
    public:
        // --- Initialization ---
        void initNode(Page* page);

        // --- Read Operations (ReadOnly) ---

        // Returns the PageID of the child corresponding to the character 'c'.
        // Returns INVALID_PAGE_ID if no such child exists.
        // Implements Binary Search on the entries for O(log child_count) access.
        page_id_t findChild(Page* page, char c);

        // Returns true if the node represents a complete word.
        bool isTerminal(Page* page);

        // Returns the stored value (valid only if isTerminal is true).
        ValueType getValue(Page* page);

        // --- Statistical Operations ---
        // Returns the pre-calculated count of words in this subtree.
        int32_t getSubtreeCount(Page* page);

        // --- Modification Operations (Performed on CoW Copies) ---

        // Sets the terminal status and value of the node.
        void setTerminal(Page* page, bool terminal, ValueType val = 0);

        // Inserts a link from character 'c' to 'child_page_id'.
        // Returns true if successful, false if page is physically full (rare).
        // Maintains sorted order of entries for Binary Search.
        bool insertChild(Page* page, char c, page_id_t child_page_id);

        // Updates a child pointer. Crucial for Copy-on-Write (CoW).
        // When a child node is copied to a new version, the parent must point to the new ID.
        void updateChildPointer(Page* page, char c, page_id_t new_child_id);

        // Removes a child connection (used during deletion or pruning).
        void removeChild(Page* page, char c);

        // --- Helper for Stats Updates ---
        // Increments or decrements the subtree count.
        // This change must propagate up to the root during the recursive update.
        void adjustSubtreeCount(Page* page, int delta);

    private:
        // Helper to access header
        TrieNodeHeader* getHeader(Page* page) {
            return reinterpret_cast<TrieNodeHeader*>(page->GetData());
        }

        // Helper to access entry array (starts after the header)
        TrieNodeEntry* getEntries(Page* page) {
            return reinterpret_cast<TrieNodeEntry*>(page->GetData() + sizeof(TrieNodeHeader));
        }
    };

} // namespace cmse::adapter