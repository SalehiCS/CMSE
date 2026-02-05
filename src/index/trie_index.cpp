#include "../src/index/trie_index.h"
#include <iostream>

namespace cmse::index {

    TrieIndex::TrieIndex(cmse::bufferpool::BufferPoolManager* bpm)
        : bpm_(bpm), root_page_id_(INVALID_PAGE_ID) {
    }

    // --- Helper to fetch Guard ---
    PageGuard TrieIndex::FetchPageGuard(page_id_t page_id) {
        return PageGuard(bpm_, bpm_->FetchPage(page_id));
    }

    void TrieIndex::Insert(const std::string& key, int64_t timestamp, uint8_t log_level) {
        std::lock_guard<std::mutex> guard(latch_);

        // 1. Lazy Initialization (Safe)
        if (root_page_id_ == INVALID_PAGE_ID) {
            page_id_t new_root_id;
            PageGuard root_guard(bpm_, bpm_->NewPage(new_root_id));
            if (!root_guard.IsValid()) return; // OOM

            auto* node = reinterpret_cast<cmse::TriePage*>(root_guard.Get()->GetData());
            node->Init();

            root_page_id_ = new_root_id;
            root_guard.SetDirty(true);
            // root_guard dies here -> Unpins automatically
        }

        if (key.empty()) return;

        // 2. Traversal with Guards
        // Start at Root
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return;

        for (char ch : key) {
            auto* current_node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

            if (!current_node->HasChild(ch)) {
                // --- Create New Child ---
                page_id_t new_child_id;
                PageGuard child_guard(bpm_, bpm_->NewPage(new_child_id));
                if (!child_guard.IsValid()) return;

                auto* child_node = reinterpret_cast<cmse::TriePage*>(child_guard.Get()->GetData());
                child_node->Init();

                // Link Parent -> Child
                current_node->SetChild(ch, new_child_id);
                curr_guard.SetDirty(true); // Parent changed

                // MOVE to Child (Parent unpins automatically)
                curr_guard = std::move(child_guard);
            }
            else {
                // --- Move to Existing Child ---
                page_id_t next_id = current_node->GetChild(ch);

                // Fetch next BEFORE dropping current (strictly safer, though guards handle it)
                PageGuard next_guard = FetchPageGuard(next_id);

                // Replace current with next (Old current unpins here)
                curr_guard = std::move(next_guard);

                if (!curr_guard.IsValid()) return;
            }
        }

        // 3. We are at Terminal Node
        auto* current_node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
        if (!current_node->IsTerminal()) {
            current_node->SetTerminal(true);
            curr_guard.SetDirty(true);
        }

        // 4. Handle Value Page (Bucket)
        page_id_t vp_id = current_node->GetValuePageId();

        if (vp_id == INVALID_PAGE_ID) {
            // Case A: Create First Value Page
            page_id_t new_vp_id;
            PageGuard vp_guard(bpm_, bpm_->NewPage(new_vp_id));
            if (!vp_guard.IsValid()) return;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());
            vp->Init();
            vp->Insert(timestamp, log_level);

            // Link Trie -> VP
            current_node->SetValuePageId(new_vp_id);

            vp_guard.SetDirty(true);
            curr_guard.SetDirty(true);
        }
        else {
            // Case B: Existing Value Page
            PageGuard vp_guard = FetchPageGuard(vp_id);
            if (!vp_guard.IsValid()) return;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());

            if (!vp->IsFull()) {
                // Sub-case B1: Insert into existing
                vp->Insert(timestamp, log_level);
                vp_guard.SetDirty(true);
            }
            else {
                // Sub-case B2: Chain (Prepend new page)
                page_id_t new_vp_id;
                PageGuard new_vp_guard(bpm_, bpm_->NewPage(new_vp_id));
                if (!new_vp_guard.IsValid()) return;

                auto* new_vp = reinterpret_cast<cmse::TrieValuePage*>(new_vp_guard.Get()->GetData());
                new_vp->Init();
                new_vp->Insert(timestamp, log_level);

                // Link New -> Old
                new_vp->SetNextPageId(vp_id);

                // Link Trie -> New
                current_node->SetValuePageId(new_vp_id);

                new_vp_guard.SetDirty(true);
                curr_guard.SetDirty(true);
                // Old vp_guard unpins cleanly (read-only in this op)
            }
        }
    }

    std::vector<cmse::TrieLogEntry> TrieIndex::Search(const std::string& key) {
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        if (root_page_id_ == INVALID_PAGE_ID || key.empty()) return results;

        // 1. Traverse
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return results;

        for (char ch : key) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

            if (!node->HasChild(ch)) return results; // Not found (Guard unpins)

            page_id_t next_id = node->GetChild(ch);

            // Move down
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return results;
        }

        // 2. Collect
        auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
        if (node->IsTerminal()) {
            page_id_t vp_id = node->GetValuePageId();

            // Walk the value page chain
            while (vp_id != INVALID_PAGE_ID) {
                PageGuard vp_guard = FetchPageGuard(vp_id);
                if (!vp_guard.IsValid()) break;

                auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());

                // Copy data
                auto entries = vp->GetEntries();
                results.insert(results.end(), entries.begin(), entries.end());

                // Move next
                vp_id = vp->GetNextPageId();
                // vp_guard dies here -> unpin
            }
        }
        return results;
    }

    void TrieIndex::CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results) {
        PageGuard guard = FetchPageGuard(page_id);
        if (!guard.IsValid()) return;

        auto* node = reinterpret_cast<cmse::TriePage*>(guard.Get()->GetData());

        // 1. Harvest Data (if terminal)
        if (node->IsTerminal()) {
            page_id_t vp_id = node->GetValuePageId();
            while (vp_id != INVALID_PAGE_ID) {
                PageGuard vp_guard = FetchPageGuard(vp_id);
                if (!vp_guard.IsValid()) break;

                auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());
                auto entries = vp->GetEntries();
                results.insert(results.end(), entries.begin(), entries.end());

                vp_id = vp->GetNextPageId();
            }
        }

        // 2. Recursive Step
        // To avoid pinning the entire depth of the tree (which could exhaust the BufferPool),
        // we must collect child IDs, UNPIN the current node, and then recurse.

        std::vector<page_id_t> children_to_visit;

        // Optimize: Check 0..255
        for (int i = 0; i < TRIE_FANOUT; i++) {
            char ch = static_cast<char>(i);
            if (node->HasChild(ch)) {
                children_to_visit.push_back(node->GetChild(ch));
            }
        }

        // CRITICAL: Drop the current page pin BEFORE recursing!
        guard.Drop();

        // 3. Visit Children
        for (page_id_t child_id : children_to_visit) {
            CollectAll(child_id, results);
        }
    }

    std::vector<cmse::TrieLogEntry> TrieIndex::SearchPrefix(const std::string& prefix) {
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // 1. Navigate to prefix end
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return results;

        for (char ch : prefix) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
            if (!node->HasChild(ch)) return results;

            page_id_t next_id = node->GetChild(ch);
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return results;
        }

        // 2. We are at the subtree root. 
        // We need to pass the PageID to CollectAll.
        page_id_t subtree_root = curr_guard.Get()->GetPageId();

        // Drop guard so CollectAll can re-fetch it fresh (consistent logic)
        curr_guard.Drop();

        CollectAll(subtree_root, results);
        return results;
    }

    // Add these implementations to trie_index.cpp

    SearchResult TrieIndex::GetTimestampsWithCap(
        const std::string& key,
        bool is_prefix,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        std::lock_guard<std::mutex> guard(latch_);
        SearchResult result;

        if (root_page_id_ == INVALID_PAGE_ID) return result;

        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return result;

        // 1. Traverse to node
        for (char ch : key) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
            if (!node->HasChild(ch)) return result;

            page_id_t next_id = node->GetChild(ch);
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return result;
        }

        auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

        if (is_prefix) {
            // Collect subtree
            page_id_t start_node_id = curr_guard.Get()->GetPageId();
            curr_guard.Drop();
            CollectAllWithCap(start_node_id, result, priority_filter, min_ts, max_ts, cap);
        }
        else {
            // Collect exact match
            if (node->IsTerminal()) {
                page_id_t vp_id = node->GetValuePageId();
                curr_guard.Drop();
                ScanValuePageChain(vp_id, result, priority_filter, min_ts, max_ts, cap);
            }
        }
        return result;
    }

    void TrieIndex::ScanValuePageChain(
        page_id_t vp_id,
        SearchResult& result,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        while (vp_id != INVALID_PAGE_ID) {
            if (result.is_overflow) return;

            PageGuard vp_guard = FetchPageGuard(vp_id);
            if (!vp_guard.IsValid()) break;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());
            int count = vp->GetCount();

            for (int i = 0; i < count; i++) {
                TrieLogEntry entry = vp->GetEntry(i);

                // Filter Check
                if (priority_filter != -1 && entry.log_level != priority_filter) continue;
                if (entry.timestamp < min_ts || entry.timestamp > max_ts) continue;

                // Add Candidate
                result.timestamps.push_back(entry.timestamp);

                // Cap Check
                if (result.timestamps.size() > cap) {
                    result.is_overflow = true;
                    result.timestamps.clear();
                    return;
                }
            }
            vp_id = vp->GetNextPageId();
        }
    }

    void TrieIndex::CollectAllWithCap(
        page_id_t page_id,
        SearchResult& result,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        if (result.is_overflow) return;

        PageGuard guard = FetchPageGuard(page_id);
        if (!guard.IsValid()) return;

        auto* node = reinterpret_cast<cmse::TriePage*>(guard.Get()->GetData());

        // 1. Collect Children IDs (Read from page then drop lock)
        std::vector<page_id_t> children;
        for (int i = 0; i < TRIE_FANOUT; i++) {
            char ch = static_cast<char>(i);
            if (node->HasChild(ch)) children.push_back(node->GetChild(ch));
        }

        page_id_t vp_id = node->IsTerminal() ? node->GetValuePageId() : INVALID_PAGE_ID;
        guard.Drop(); // Drop lock before heavy work

        // 2. Process Values
        if (vp_id != INVALID_PAGE_ID) {
            ScanValuePageChain(vp_id, result, priority_filter, min_ts, max_ts, cap);
        }

        // 3. Recurse
        for (page_id_t child : children) {
            CollectAllWithCap(child, result, priority_filter, min_ts, max_ts, cap);
            if (result.is_overflow) return;
        }
    }
} // namespace cmse::index