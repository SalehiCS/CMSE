#include "../src/index/trie_index.h"
#include "../common/logger.h"
#include <iostream>

namespace cmse::index {

    /**
     * TrieIndex Constructor.
     * @param bpm Pointer to the BufferPoolManager used for physical page allocation and retrieval.
     */
    TrieIndex::TrieIndex(cmse::bufferpool::BufferPoolManager* bpm)
        : bpm_(bpm), root_page_id_(INVALID_PAGE_ID) {
    }

    /**
     * Helper: Fetches a page and wraps it in a RAII PageGuard.
     * This ensures the page is automatically unpinned when the guard goes out of scope.
     */
    PageGuard TrieIndex::FetchPageGuard(page_id_t page_id) {
        return PageGuard(bpm_, bpm_->FetchPage(page_id));
    }

    /**
     * Inserts a key (string prefix) into the Trie and links it to a LogRecord.
     * @param key The string to index (e.g., "nginx", "error").
     * @param timestamp The primary key of the associated log.
     * @param log_level The importance level (INFO/WARN/ERROR).
     */
    void TrieIndex::Insert(const std::string& key, int64_t timestamp, uint8_t log_level) {
        // Thread-Safety: Protect the entire tree traversal and modification.
        std::lock_guard<std::mutex> guard(latch_);

        // 1. LAZY INITIALIZATION: Create the root node if the tree is empty.
        if (root_page_id_ == INVALID_PAGE_ID) {
            page_id_t new_root_id;
            PageGuard root_guard(bpm_, bpm_->NewPage(new_root_id));
            if (!root_guard.IsValid()) return; // Handle Out-of-Memory / Buffer failure.

            // Cast raw data to a structured TriePage and initialize the branching array.
            auto* node = reinterpret_cast<cmse::TriePage*>(root_guard.Get()->GetData());
            node->Init();

            root_page_id_ = new_root_id; // Set persistent root ID.
            root_guard.SetDirty(true);   // Mark for disk-write back.
            // root_guard destructor triggers here: automatically unpins the root page.
        }

        if (key.empty()) return;

        // 2. TRAVERSAL WITH GUARDS: Navigate through the tree character by character.
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return;

        for (char ch : key) {
            // Reinterpret the current page frame as a Trie Node.
            auto* current_node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

            if (!current_node->HasChild(ch)) {
                // --- CREATE NEW CHILD BRANCH ---
                page_id_t new_child_id;
                PageGuard child_guard(bpm_, bpm_->NewPage(new_child_id));
                if (!child_guard.IsValid()) return;

                auto* child_node = reinterpret_cast<cmse::TriePage*>(child_guard.Get()->GetData());
                child_node->Init();

                // Connect Parent to the new Child ID using the character as the index.
                current_node->SetChild(ch, new_child_id);
                curr_guard.SetDirty(true); // Parent is modified (child list updated).

                // Transfer ownership: curr_guard unpins the parent and starts guarding the child.
                curr_guard = std::move(child_guard);
            }
            else {
                // --- MOVE TO EXISTING BRANCH ---
                page_id_t next_id = current_node->GetChild(ch);

                // Fetch the child node BEFORE dropping the parent reference for safety.
                PageGuard next_guard = FetchPageGuard(next_id);

                // Move semantic: current unpins, and we proceed to the next depth in the tree.
                curr_guard = std::move(next_guard);

                if (!curr_guard.IsValid()) return;
            }
        }

        // 3. TERMINAL NODE FINALIZATION: Mark the end of the string.
        auto* current_node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
        if (!current_node->IsTerminal()) {
            current_node->SetTerminal(true);
            curr_guard.SetDirty(true);
        }

        // 4. HANDLE VALUE PAGE (The Data Bucket)
        page_id_t vp_id = current_node->GetValuePageId();

        if (vp_id == INVALID_PAGE_ID) {
            // CASE A: Initialize the very first data bucket for this prefix.
            page_id_t new_vp_id;
            PageGuard vp_guard(bpm_, bpm_->NewPage(new_vp_id));
            if (!vp_guard.IsValid()) return;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());
            vp->Init();
            vp->Insert(timestamp, log_level); // Store the first {Timestamp, Level} entry.

            // Back-link the Trie Node to its new data bucket.
            current_node->SetValuePageId(new_vp_id);

            vp_guard.SetDirty(true);
            curr_guard.SetDirty(true);
        }
        else {
            // CASE B: Add entry to an existing bucket chain.
            PageGuard vp_guard = FetchPageGuard(vp_id);
            if (!vp_guard.IsValid()) return;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());

            if (!vp->IsFull()) {
                // SUB-CASE B1: Simply append to the current bucket.
                vp->Insert(timestamp, log_level);
                vp_guard.SetDirty(true);
            }
            else {
                // SUB-CASE B2: BUCKET OVERFLOW (Chain/Prepend Logic).
                // To minimize traversal, we prepend new pages to the head of the list.
                page_id_t new_vp_id;
                PageGuard new_vp_guard(bpm_, bpm_->NewPage(new_vp_id));
                if (!new_vp_guard.IsValid()) return;

                auto* new_vp = reinterpret_cast<cmse::TrieValuePage*>(new_vp_guard.Get()->GetData());
                new_vp->Init();
                new_vp->Insert(timestamp, log_level);

                // LINK: New Head -> Old Bucket Chain.
                new_vp->SetNextPageId(vp_id);

                // LINK: Trie Node -> New Head.
                current_node->SetValuePageId(new_vp_id);

                new_vp_guard.SetDirty(true);
                curr_guard.SetDirty(true);
                // The old vp_guard (read-only in this context) unpins cleanly here.
            }
        }
    }

    /**
         * Exact Match Search: Locates all log entries for a specific string.
         * Logic: Traverses the Trie path character by character. If it hits the end
         * of the string and the node is "Terminal", it harvests the data bucket chain.
         */
    std::vector<cmse::TrieLogEntry> TrieIndex::Search(const std::string& key) {
        // Multi-threading safety: Ensure tree structure is stable during read.
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        // Boundary check: Empty index or empty query returns no results.
        if (root_page_id_ == INVALID_PAGE_ID || key.empty()) return results;

        // 1. PATH TRAVERSAL
        // Start guarding at the Root.
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return results;

        for (char ch : key) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

            // If the branching path doesn't exist, the key isn't in the index.
            if (!node->HasChild(ch)) return results;

            page_id_t next_id = node->GetChild(ch);

            // Move the guard down the tree (curr_guard automatically unpins the parent).
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return results;
        }

        // 2. DATA COLLECTION
        // We have reached the final character's node.
        auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
        if (node->IsTerminal()) {
            page_id_t vp_id = node->GetValuePageId();

            // Walk the linked-list of Value Pages (the "buckets") on disk.
            while (vp_id != INVALID_PAGE_ID) {
                PageGuard vp_guard = FetchPageGuard(vp_id);
                if (!vp_guard.IsValid()) break;

                auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());

                // Harvest entry data into the return vector.
                auto entries = vp->GetEntries();
                results.insert(results.end(), entries.begin(), entries.end());

                // Move to the next overflow bucket in the chain.
                vp_id = vp->GetNextPageId();
                // vp_guard goes out of scope and unpins the page here.
            }
        }
        return results;
    }

    /**
     * Recursive Subtree Harvest: Gathers all terminal records from a specific node downwards.
     * Logic: Implements a Depth-First Search (DFS) while strictly managing Buffer Pool pins.
     */
    void TrieIndex::CollectAll(page_id_t page_id, std::vector<cmse::TrieLogEntry>& results) {
        PageGuard guard = FetchPageGuard(page_id);
        if (!guard.IsValid()) return;

        auto* node = reinterpret_cast<cmse::TriePage*>(guard.Get()->GetData());

        // 1. HARVEST DATA (Terminal Check)
        // If this branch node is also a valid endpoint, collect its local buckets.
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

        // 2. RECURSIVE STEP (Anti-Deadlock Logic)
        // We cannot recurse while holding the 'guard' pin, as a deep tree could 
        // pin every page in the Buffer Pool simultaneously, causing a freeze.
        std::vector<page_id_t> children_to_visit;

        // Scan all 256 possible ASCII children.
        for (int i = 0; i < TRIE_FANOUT; i++) {
            char ch = static_cast<char>(i);
            if (node->HasChild(ch)) {
                children_to_visit.push_back(node->GetChild(ch));
            }
        }

        // CRITICAL: Release the memory pin for the current node BEFORE diving deeper!
        guard.Drop();

        // 3. VISIT CHILDREN
        for (page_id_t child_id : children_to_visit) {
            CollectAll(child_id, results);
        }
    }

    /**
     * Prefix Search: Locates all logs starting with the given string (e.g., "web*" -> web01, web02).
     * Logic: Navigates to the end of the prefix, then harvests the entire subtree beneath it.
     */
    std::vector<cmse::TrieLogEntry> TrieIndex::SearchPrefix(const std::string& prefix) {
        std::lock_guard<std::mutex> guard(latch_);
        std::vector<cmse::TrieLogEntry> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // 1. NAVIGATE TO PREFIX ROOT
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) return results;

        for (char ch : prefix) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());
            if (!node->HasChild(ch)) return results; // Prefix doesn't exist in tree.

            page_id_t next_id = node->GetChild(ch);
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return results;
        }

        // 2. SUBTREE HARVEST
        // We are now at the root of the subtree containing all matches.
        page_id_t subtree_root = curr_guard.Get()->GetPageId();

        // Release the pin so CollectAll can manage its own pins without interference.
        curr_guard.Drop();

        CollectAll(subtree_root, results);
        return results;
    }

    /**
     * GetTimestampsWithCap
     * The primary entry point for complex searches. Supports:
     * 1. Exact vs Prefix matching.
     * 2. Log Priority filtering.
     * 3. Time range filtering.
     * 4. Result capping (to prevent memory exhaustion).
     */
    SearchResult TrieIndex::GetTimestampsWithCap(
        const std::string& key,
        bool is_prefix,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        // Log query parameters for debugging system behavior
        LOG_DEBUG("[Trie] Search Start: Key='" << key << "' Prefix=" << is_prefix
            << " Prio=" << priority_filter << " Cap=" << cap);

        // Ensure thread-safe access to the tree structure
        std::lock_guard<std::mutex> guard(latch_);
        SearchResult result;

        // Diagnostic log: Verification that the SearchResult struct is correctly zero-initialized.
        LOG_DEBUG("[Trie] Init Result: Overflow=" << result.is_overflow << " Size=" << result.timestamps.size());

        // Basic validation: If the tree has no root, there is nothing to find.
        if (root_page_id_ == INVALID_PAGE_ID) {
            LOG_DEBUG("[Trie] Empty Trie (Root Invalid)");
            return result;
        }

        // Initialize traversal starting at the root page
        PageGuard curr_guard = FetchPageGuard(root_page_id_);
        if (!curr_guard.IsValid()) {
            LOG_DEBUG("[Trie] Failed to fetch Root Page " << root_page_id_);
            return result;
        }

        // 1. TRAVERSAL: Step through the tree based on the provided string key.
        for (char ch : key) {
            auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

            // If the current node doesn't have the next character as a child, the search fails.
            if (!node->HasChild(ch)) {
                LOG_DEBUG("[Trie] Traversal Stop: Node " << curr_guard.Get()->GetPageId()
                    << " has no child '" << ch << "'");
                return result;
            }

            page_id_t next_id = node->GetChild(ch);

            // Fetch the child page and move the guard (automatically unpins the parent).
            curr_guard = FetchPageGuard(next_id);
            if (!curr_guard.IsValid()) return result;
        }

        // Reinterpret the target node reached after full traversal of the key string.
        auto* node = reinterpret_cast<cmse::TriePage*>(curr_guard.Get()->GetData());

        LOG_DEBUG("[Trie] Reached Target Node: " << curr_guard.Get()->GetPageId()
            << " Terminal=" << node->IsTerminal());

        if (is_prefix) {
            // Case 1: Subtree Collection (Prefix Match)
            // Grab the ID of the node reached, then drop the pin to allow recursive collection.
            page_id_t start_node_id = curr_guard.Get()->GetPageId();
            curr_guard.Drop();
            CollectAllWithCap(start_node_id, result, priority_filter, min_ts, max_ts, cap);
        }
        else {
            // Case 2: Exact Match
            // Only collect values if this specific node marks the end of a valid word.
            if (node->IsTerminal()) {
                page_id_t vp_id = node->GetValuePageId();
                LOG_DEBUG("[Trie] Exact Match Found. ValuePage=" << vp_id);

                // Release the TrieNode pin before diving into the Value Page buckets.
                curr_guard.Drop();
                ScanValuePageChain(vp_id, result, priority_filter, min_ts, max_ts, cap);
            }
            else {
                LOG_DEBUG("[Trie] Key found but NOT Terminal (No values here)");
            }
        }

        LOG_DEBUG("[Trie] Search End. Found=" << result.timestamps.size()
            << " Overflow=" << result.is_overflow);
        return result;
    }

    /**
     * ScanValuePageChain
     * Iterates through a linked list of data buckets (Value Pages) on disk.
     * Applies priority and timestamp filters to every entry.
     */
    void TrieIndex::ScanValuePageChain(
        page_id_t vp_id,
        SearchResult& result,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        while (vp_id != INVALID_PAGE_ID) {
            // Abort if a previous page in the chain already exceeded the user's result cap.
            if (result.is_overflow) {
                LOG_DEBUG("[Trie] Scan Abort: Already Overflowed");
                return;
            }

            PageGuard vp_guard = FetchPageGuard(vp_id);
            if (!vp_guard.IsValid()) break;

            auto* vp = reinterpret_cast<cmse::TrieValuePage*>(vp_guard.Get()->GetData());
            int count = vp->GetCount();

            // Process every record stored inside this 4KB bucket.
            for (int i = 0; i < count; i++) {
                TrieLogEntry entry = vp->GetEntry(i);

                // --- FILTER CHECK ---
                // Priority Check: -1 means "ignore filter", otherwise must match level exactly.
                bool prio_ok = (priority_filter == -1 || entry.log_level == priority_filter);
                // Time Range Check: Timestamp must fall within [min_ts, max_ts].
                bool time_ok = (entry.timestamp >= min_ts && entry.timestamp <= max_ts);

                if (!prio_ok || !time_ok) {
                    continue; // Entry does not meet criteria, skip it.
                }

                // Add filtered candidate to the result set.
                result.timestamps.push_back(entry.timestamp);

                // --- CAP CHECK ---
                // If the vector grows larger than the cap, we stop immediately.
                if (result.timestamps.size() > cap) {
                    LOG_DEBUG("[Trie] CAP REACHED (" << cap << "). Mark Overflow.");

                    result.is_overflow = true;

                    // Removal: Pop the last element that broke the cap to maintain exact size limit.
                    result.timestamps.pop_back();

                    return;
                }
            }
            // Move to the next page in the disk-based linked list.
            vp_id = vp->GetNextPageId();
        }
    }

    /**
     * CollectAllWithCap
     * Recursively traverses a subtree to find all terminal nodes.
     * Used for wildcard/prefix searching (e.g., searching "sys*" finds "system", "syslog", etc).
     */
    void TrieIndex::CollectAllWithCap(
        page_id_t page_id,
        SearchResult& result,
        int32_t priority_filter,
        int64_t min_ts,
        int64_t max_ts,
        size_t cap)
    {
        // Safety: If the limit has already been reached in another branch, exit the recursion.
        if (result.is_overflow) return;

        PageGuard guard = FetchPageGuard(page_id);
        if (!guard.IsValid()) return;

        auto* node = reinterpret_cast<cmse::TriePage*>(guard.Get()->GetData());

        // 1. COLLECT CHILDREN IDs
        // We copy child IDs to a local vector so we can release the page pin quickly.
        std::vector<page_id_t> children;
        for (int i = 0; i < TRIE_FANOUT; i++) {
            char ch = static_cast<char>(i);
            if (node->HasChild(ch)) children.push_back(node->GetChild(ch));
        }

        // Check if this node itself is a terminal word node.
        page_id_t vp_id = node->IsTerminal() ? node->GetValuePageId() : INVALID_PAGE_ID;

        // CRITICAL: Drop the page lock/pin before performing value scanning or recursion.
        // This prevents the Buffer Pool from running out of frames during deep traversal.
        guard.Drop();

        // 2. PROCESS LOCAL VALUES
        // If this node has associated logs, scan their value page chain.
        if (vp_id != INVALID_PAGE_ID) {
            ScanValuePageChain(vp_id, result, priority_filter, min_ts, max_ts, cap);
        }

        // 3. RECURSE TO CHILDREN
        // Visit all branched paths beneath this node.
        for (page_id_t child : children) {
            CollectAllWithCap(child, result, priority_filter, min_ts, max_ts, cap);
            // Early exit recursion if a child branch fills the result cap.
            if (result.is_overflow) return;
        }
    }
} // namespace cmse::index