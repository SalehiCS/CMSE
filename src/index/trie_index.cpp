#include "../src/index/trie_index.h"
#include <iostream>

namespace cmse::index {

    TrieIndex::TrieIndex(cmse::bufferpool::BufferPoolManager* bpm)
        : bpm_(bpm), root_page_id_(INVALID_PAGE_ID) {

        // Create the Root Page immediately upon initialization
        cmse::Page* root_page = bpm_->NewPage(root_page_id_);

        if (root_page == nullptr) {
            throw std::runtime_error("Failed to allocate Root Page for TrieIndex. Buffer Pool might be full.");
        }

        // Initialize the root as a generic TriePage
        auto* trie_node = reinterpret_cast<cmse::TriePage*>(root_page->GetData());
        trie_node->Init();

        // Always unpin pages when done
        bpm_->UnpinPage(root_page_id_, true);
    }

    void TrieIndex::Insert(const std::string& key, int64_t timestamp, uint8_t log_level) {
        std::lock_guard<std::mutex> guard(latch_);

        if (key.empty()) return;

        // 1. Start traversal from the Root
        page_id_t current_page_id = root_page_id_;
        cmse::TriePage* current_node = FetchTriePage(current_page_id);

        if (current_node == nullptr) return;

        // 2. Traverse or Create the Path
        for (char ch : key) {
            // Check if the child exists for character 'ch'
            if (!current_node->HasChild(ch)) {

                // --- Create New Trie Node Page ---
                page_id_t new_child_id;
                cmse::Page* new_page = bpm_->NewPage(new_child_id);

                if (new_page == nullptr) {
                    bpm_->UnpinPage(current_page_id, false);
                    return; // Out of memory
                }

                auto* child_node = reinterpret_cast<cmse::TriePage*>(new_page->GetData());
                child_node->Init();

                // Link Parent -> Child
                current_node->SetChild(ch, new_child_id);

                // Unpin Parent (dirty=true because we updated the child link)
                bpm_->UnpinPage(current_page_id, true);

                // Move to Child
                current_page_id = new_child_id;
                current_node = child_node;
            }
            else {
                // --- Navigate to Existing Child ---
                page_id_t next_page_id = current_node->GetChild(ch);

                // Unpin Parent (dirty=false, we didn't change it)
                bpm_->UnpinPage(current_page_id, false);

                // Fetch Child
                current_page_id = next_page_id;
                current_node = FetchTriePage(current_page_id);
            }
        }

        // 3. We are now at the Terminal Node (End of Key)
        // Mark it as terminal if it wasn't already
        if (!current_node->IsTerminal()) {
            current_node->SetTerminal(true);
        }

        // 4. Handle Value Page (The Bucket)
        page_id_t vp_id = current_node->GetValuePageId();

        if (vp_id == INVALID_PAGE_ID) {
            // --- Case A: No Value Page exists yet. Create one. ---
            page_id_t new_vp_id;
            cmse::Page* vp_raw = bpm_->NewPage(new_vp_id);

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_raw->GetData());
            vp->Init();
            vp->Insert(timestamp, log_level);

            // Link Trie Node -> Value Page
            current_node->SetValuePageId(new_vp_id);

            bpm_->UnpinPage(new_vp_id, true);
        }
        else {
            // --- Case B: Value Page exists. Try to insert. ---
            cmse::TrieValuePage* vp = FetchValuePage(vp_id);

            if (!vp->IsFull()) {
                // Sub-case B1: Space available
                vp->Insert(timestamp, log_level);
                bpm_->UnpinPage(vp_id, true);
            }
            else {
                // Sub-case B2: Page is FULL -> Chain Strategy (Prepend)
                // We create a NEW Value Page, insert the data there, 
                // and link it to the OLD Value Page.
                // This ensures O(1) insertion time.

                page_id_t new_vp_id;
                cmse::Page* new_vp_raw = bpm_->NewPage(new_vp_id);
                auto* new_vp = reinterpret_cast<cmse::TrieValuePage*>(new_vp_raw->GetData());

                new_vp->Init();
                new_vp->Insert(timestamp, log_level);

                // Link New -> Old (Chaining)
                new_vp->SetNextPageId(vp_id);

                // Update Trie Node -> New (New Head of the chain)
                current_node->SetValuePageId(new_vp_id);

                bpm_->UnpinPage(new_vp_id, true);
                bpm_->UnpinPage(vp_id, false); // Old page wasn't modified, just linked to
            }
        }

        // Final unpin of the terminal Trie Node
        bpm_->UnpinPage(current_page_id, true);
    }

    std::vector<cmse::TrieLogEntry> TrieIndex::Search(const std::string& key) {
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        if (key.empty()) return results;

        page_id_t current_page_id = root_page_id_;
        cmse::TriePage* current_node = FetchTriePage(current_page_id);

        // 1. Traverse
        for (char ch : key) {
            if (!current_node->HasChild(ch)) {
                bpm_->UnpinPage(current_page_id, false);
                return results; // Not found
            }

            page_id_t next_id = current_node->GetChild(ch);
            bpm_->UnpinPage(current_page_id, false);

            current_page_id = next_id;
            current_node = FetchTriePage(current_page_id);
        }

        // 2. Collect Data
        if (current_node->IsTerminal()) {
            page_id_t vp_id = current_node->GetValuePageId();

            // Traverse the Value Page Chain
            while (vp_id != INVALID_PAGE_ID) {
                cmse::TrieValuePage* vp = FetchValuePage(vp_id);

                // Copy entries from this page
                std::vector<cmse::TrieLogEntry> page_entries = vp->GetEntries();
                results.insert(results.end(), page_entries.begin(), page_entries.end());

                // Move to next page in chain
                page_id_t next_vp_id = vp->GetNextPageId();
                bpm_->UnpinPage(vp_id, false);
                vp_id = next_vp_id;
            }
        }

        bpm_->UnpinPage(current_page_id, false);
        return results;
    }

    // --- Helpers ---

    cmse::TriePage* TrieIndex::FetchTriePage(page_id_t page_id) {
        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) return nullptr;
        return reinterpret_cast<cmse::TriePage*>(page->GetData());
    }

    cmse::TrieValuePage* TrieIndex::FetchValuePage(page_id_t page_id) {
        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) return nullptr;
        return reinterpret_cast<cmse::TrieValuePage*>(page->GetData());
    }

    // Helper: Recursive DFS to collect all entries from a subtree
    void TrieIndex::CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results) {
        cmse::TriePage* node = FetchTriePage(page_id);
        if (node == nullptr) return;

        // 1. If this node is a terminal (end of a word), collect its data
        if (node->IsTerminal()) {
            page_id_t vp_id = node->GetValuePageId();
            while (vp_id != INVALID_PAGE_ID) {
                cmse::TrieValuePage* vp = FetchValuePage(vp_id);
                auto page_entries = vp->GetEntries();
                results.insert(results.end(), page_entries.begin(), page_entries.end());

                page_id_t next = vp->GetNextPageId();
                bpm_->UnpinPage(vp_id, false);
                vp_id = next;
            }
        }

        // 2. Recursively visit all children
        // Note: For a production system, an iterative stack is safer, 
        // but recursion is fine here since keys are short (max 32 chars).
        for (int i = 0; i < TRIE_FANOUT; i++) {
            // We iterate 0..255. In a real optimization, we would store a list of active children 
            // to avoid checking 256 null pointers.
            char ch = static_cast<char>(i);
            if (node->HasChild(ch)) {
                page_id_t child_id = node->GetChild(ch);

                // Important: We must unpin the current node before recursing 
                // to avoid holding too many pages in the BufferPool (Deadlock risk).
                bpm_->UnpinPage(page_id, false);

                CollectAll(child_id, results);

                // Re-fetch current node to continue the loop (Crabbing)
                node = FetchTriePage(page_id);
            }
        }

        bpm_->UnpinPage(page_id, false);
    }

    std::vector<cmse::TrieLogEntry> TrieIndex::SearchPrefix(const std::string& prefix) {
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        if (prefix.empty()) return results;

        page_id_t current_page_id = root_page_id_;
        cmse::TriePage* current_node = FetchTriePage(current_page_id);

        // 1. Navigate to the end of the prefix
        for (char ch : prefix) {
            if (!current_node->HasChild(ch)) {
                bpm_->UnpinPage(current_page_id, false);
                return results; // Prefix not found
            }

            page_id_t next_id = current_node->GetChild(ch);
            bpm_->UnpinPage(current_page_id, false);

            current_page_id = next_id;
            current_node = FetchTriePage(current_page_id);
        }

        // 2. Perform DFS from this point to find ALL descendants
        // We are currently holding 'current_node'. We pass its ID to CollectAll.
        // We must unpin it first because CollectAll fetches it again.
        bpm_->UnpinPage(current_page_id, false);

        CollectAll(current_page_id, results);

        return results;
    }
} // namespace cmse::index