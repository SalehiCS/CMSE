#include "index/btree_index.h"
#include "bufferpool/buffer_pool_manager.h"
#include "page/page.h"
#include "../common/logger.h"

#include <functional>
#include <iostream>
#include <algorithm> 

namespace cmse::index 
{
    // -------------------------------------------------------------------------
    // BTreeIndex Implementation: Core Operations & Range Scans
    // -------------------------------------------------------------------------

    /**
     * BTreeIndex Constructor
     * Initializes the indexing engine with a link to the Buffer Pool and an existing root.
     */
    BTreeIndex::BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id)
        : bpm_(bpm), root_page_id_(root_id) {
        // Initialization complete. Root remains INVALID_PAGE_ID if tree is new.
    }

    /**
     * Insert: Adds a key-value pair to the persistent index.
     * Strategy: Navigate to leaf -> Attempt Insert -> Split if Full -> Retry.
     */
    bool BTreeIndex::Insert(const KeyType& key, const ValueType& value) {
        // Guard the entire operation with a mutex to prevent structural corruption during splits.
        std::lock_guard<std::mutex> lock(latch_);

        int attempts = 0;
        const int MAX_ATTEMPTS = 5; // Prevent infinite loops in case of severe concurrent pressure

        

        while (attempts < MAX_ATTEMPTS) {
            // Case 1: The tree is completely empty.
            if (root_page_id_ == INVALID_PAGE_ID) {
                LOG_DEBUG_SPLIT("Empty tree, starting new root for Key=" << key);
                StartNewTree(key, value); // Allocates first root page
                return true;
            }

            // Case 2: Tree exists. Traverse from root down to the target leaf.
            TraversalContext ctx;
            PageGuard leaf_guard = FindLeaf(key, ctx, true); // ctx populates with the parent path

            if (!leaf_guard.IsValid()) {
                
                return false;
            }

            // Case 3: Try to insert the key into the found leaf page.
            // applyUpdateToLeaf handles binary search and insertion within the page bytes.
            if (adapter_.applyUpdateToLeaf(leaf_guard.Get(), key, value)) {

                // --- CRITICAL STATS PROPAGATION ---
                // We must update Min/Max/Total metadata on every node from the Leaf up to the Root.
                // This ensures the Pruning logic in Search/Scan remains accurate.
                UpdateStatsUpwards(ctx, key);

                

                // Mark page as modified so Buffer Pool flushes it to disk later.
                leaf_guard.SetDirty(true);
                return true; // leaf_guard and ctx.path_pages unpin automatically via RAII
            }

            // Case 4: The leaf is full. We must split it into two pages.
            LOG_DEBUG_SPLIT("Page Full, Handling Split on Leaf=" << leaf_guard.Get()->GetPageId());

            // Move leaf_guard into HandleSplit to pass ownership of the pin.
            HandleSplit(std::move(leaf_guard), ctx);
            attempts++; // Retry the insertion in the newly organized structure
        }

        LOG_DEBUG_SPLIT("Insert Failed: Max attempts reached for Key=" << key);
        return false;
    }

    // -------------------------------------------------------------------------
    // Point Lookup with Metadata Pruning
    // -------------------------------------------------------------------------
    bool BTreeIndex::GetValue(const KeyType& key, ValueType& result) {
        // Trace logic for specific high-value keys for debugging.
        if (key == 9999) {
            std::cout << "[GetValue 9999] Called. Current Root ID: " << root_page_id_ << std::endl;
        }

        std::lock_guard<std::mutex> guard(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) {
            if (key == 9999) std::cout << "[GetValue 9999] FAIL: Root is Invalid!" << std::endl;
            return false;
        }

        TraversalContext ctx;
        // Search path from root to leaf. Optimization: Read-only traversal (for_write=false).
        PageGuard leaf_guard = FindLeaf(key, ctx, false);

        if (!leaf_guard.IsValid()) {
            if (key == 9999) std::cout << "[GetValue 9999] FAIL: FindLeaf returned Invalid Guard." << std::endl;
            return false;
        }

        // Reinterpret the raw page bytes as a structured B+ Leaf node.
        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_guard.Get()->GetData());

        if (key == 9999) {
            std::cout << "[GetValue 9999] Reached Leaf Page " << leaf_guard.Get()->GetPageId()
                << " with " << leaf->header.key_count << " keys." << std::endl;
            if (leaf->header.key_count > 0) {
                std::cout << "    -> Range: [" << leaf->keys[0] << " ... "
                    << leaf->keys[leaf->header.key_count - 1] << "]" << std::endl;
            }
        }

        // Search inside the leaf via linear scan (or binary search if count is high).
        int index = -1;
        for (int i = 0; i < leaf->header.key_count; i++) {
            if (leaf->keys[i] == key) {
                index = i;
                break;
            }
        }

        if (index != -1) {
            result = leaf->values[index]; // Found match
            if (key == 9999) std::cout << "[GetValue 9999] SUCCESS: Found at index " << index << std::endl;
            return true;
        }

        if (key == 9999) std::cout << "[GetValue 9999] FAIL: Key not found in this leaf." << std::endl;
        return false;
    }

    /**
     * Scan: Range Query implementation [start_key, end_key].
     * Strategy: Prune at root -> Navigate to start -> Link-list scan horizontally.
     */
    std::vector<ValueType> BTreeIndex::Scan(const KeyType& start_key, const KeyType& end_key) {
        std::lock_guard<std::mutex> lock(latch_);
        std::vector<ValueType> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // --- OPTIMIZATION: Root Level Pruning ---
        // We check the root's metadata before even starting traversal.
        {
            PageGuard root_guard(bpm_, bpm_->FetchPage(root_page_id_));
            if (root_guard.IsValid()) {
                auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(root_guard.Get()->GetData());

                // Trust stats only if the page isn't currently undergoing a split (is_dirty == 0 check).
                if (header->total_keys > 0 && header->is_dirty == 0) {
                    // If the entire search range is outside the root's min/max bounds, abort instantly.
                    if (end_key < header->min_key || start_key > header->max_key) {
                        return results; // Root guard unpins here.
                    }
                }
            }
        }

        // 1. Locate the leaf containing the first possible key of the range.
        TraversalContext ctx;
        PageGuard curr_guard = FindLeaf(start_key, ctx, false);

        if (!curr_guard.IsValid()) {
            
            return results;
        }

        // Safety counters to protect against corrupt circular pointers in the linked list.
        int scanned_pages = 0;
        const int MAX_SCAN_PAGES = 100000;

        // 2. HORIZONTAL SCAN: Follow the leaf-to-leaf pointers.
        while (curr_guard.IsValid()) {
            scanned_pages++;
            if (scanned_pages > MAX_SCAN_PAGES) {
                std::cerr << "[FATAL] Infinite Loop detected in Scan!" << std::endl;
                break;
            }

            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(curr_guard.Get()->GetData());
            int count = leaf->header.key_count;
            page_id_t next_page_id = leaf->next_leaf_id;
            page_id_t current_id = curr_guard.Get()->GetPageId();

            // Collect all keys within the current leaf that match the range.
            for (int i = 0; i < count; ++i) {
                // Since keys are sorted, if we see a key > end_key, we can stop the entire scan.
                if (leaf->keys[i] > end_key) {
                    return results;
                }
                if (leaf->keys[i] >= start_key) {
                    results.push_back(leaf->values[i]);
                }
            }

            // Check if we reached the right-most edge of the tree.
            if (next_page_id == INVALID_PAGE_ID) break;

            // Integrity check: A page must never point to itself.
            if (next_page_id == current_id) {
                std::cerr << "[FATAL] Page " << next_page_id << " points to itself!" << std::endl;
                break;
            }

            // --- MOVE TO NEXT LEAF ---
            // Move Assignment is used here:
            // 1. A new guard is created for the next page.
            // 2. Assigning it to 'curr_guard' triggers the old guard's destructor.
            // 3. The old page is unpinned safely before the new one is processed.
            curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_page_id));
        }

        return results;
    }

    /**
     * StartNewTree
     * Initializes a brand new B+ Tree when the index is currently empty.
     * Strategy: Allocate page -> Init as Leaf -> Insert first record -> Set as Root.
     */
    void BTreeIndex::StartNewTree(const KeyType& key, const ValueType& value) {
        page_id_t root_id;
        // Allocate a new physical page from the Buffer Pool and wrap it in a Guard immediately.
        PageGuard root_guard(bpm_, bpm_->NewPage(root_id));

        // If the Buffer Pool is full or disk is out of space, abort.
        if (!root_guard.IsValid()) return;

        // Structure the raw page bytes as a Leaf Node.
        adapter_.initLeaf(root_guard.Get());

        // Perform the initial key-value insertion.
        adapter_.applyUpdateToLeaf(root_guard.Get(), key, value);

        // Update the index metadata to point to this new root.
        root_page_id_ = root_id;

        // Ensure the Buffer Pool knows this page must be written back to disk.
        root_guard.SetDirty(true);

        // RAII: As root_guard goes out of scope, the page is automatically unpinned.
    }

    /**
     * FindLeaf
     * Navigates the tree from the root down to the leaf page where 'key' should reside.
     * @param for_write If true, keeps parent pages pinned in the 'ctx' stack to allow for splits.
     * @param ctx The traversal context used to store the path for upward propagation.
     */
    PageGuard BTreeIndex::FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write) {
        // Step 1: Fetch the current root page.
        PageGuard curr_guard(bpm_, bpm_->FetchPage(root_page_id_));

        // Debug tracing for specific test keys.
        if (key == 9999 || key == -1) {
            std::cout << "[FindLeaf " << key << "] Start at Root: " << root_page_id_ << std::endl;
        }

        if (!curr_guard.IsValid()) {
            return {}; // Returns an invalid/null guard.
        }

        // Step 2: Iterate down through Internal Nodes until we land on a Leaf Node.
        while (!adapter_.isLeaf(curr_guard.Get())) {
            if (for_write) {
                Page* raw_ptr = curr_guard.Get();
                ctx.path_pages.push_back(std::move(curr_guard));

                // PASS 'true' for write mode
                page_id_t next_id = adapter_.findChild(raw_ptr, key, true);

                curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_id));
            }
            else {
                // PASS 'false' for read mode
                page_id_t next_id = adapter_.findChild(curr_guard.Get(), key, false);

                curr_guard.Drop();
                curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_id));
            }
            if (!curr_guard.IsValid()) return {};
        }
        return curr_guard;
    }

    /**
         * HandleSplit
         * Manages node overflow by creating siblings and propagating the split upwards.
         */
    void BTreeIndex::HandleSplit(PageGuard node_guard, TraversalContext& ctx) {

        cmse::Page* current_node = node_guard.Get();
        page_id_t current_id = current_node->GetPageId();

        LOG_DEBUG_SPLIT("--- HandleSplit Start: Page " << current_id << " ---");

        // 1. Allocate a new sibling page
        page_id_t sibling_id;
        PageGuard sibling_guard(bpm_, bpm_->NewPage(sibling_id));
        if (!sibling_guard.IsValid()) {
            LOG_DEBUG_SPLIT("[Fatal] Failed to allocate sibling for Page " << current_id);
            return;
        }

        // 2. Perform the physical split
        cmse::adapter::SplitResult result;
        adapter_.splitNode(current_node, sibling_guard.Get(), &result);

        // Data to propagate up
        KeyType key_to_insert = result.promoted_key;
        page_id_t child_val_to_insert = sibling_id;

        LOG_DEBUG_SPLIT("Split Page " << current_id << " -> Sibling " << sibling_id
            << " | Promoted Key: " << key_to_insert);

        // Commit changes to disk
        node_guard.SetDirty(true);
        sibling_guard.SetDirty(true);
        node_guard.Drop();
        sibling_guard.Drop();

        // ==========================================================
        // Iterative Upward Propagation
        // ==========================================================
        while (true) {

            // CASE 1: Root Split
            if (ctx.path_pages.empty()) {
                page_id_t new_root_id;
                PageGuard new_root_guard(bpm_, bpm_->NewPage(new_root_id));
                if (!new_root_guard.IsValid()) return;

                adapter_.createNewRoot(new_root_guard.Get(), current_id, child_val_to_insert, key_to_insert);

                new_root_guard.SetDirty(true);
                this->root_page_id_ = new_root_id;

                LOG_DEBUG_SPLIT("ROOT SPLIT! New Root: " << new_root_id
                    << " | Children: [" << current_id << ", " << child_val_to_insert << "]");
                return;
            }

            // CASE 2: Parent Exists
            PageGuard parent_guard = std::move(ctx.path_pages.back());
            ctx.path_pages.pop_back();
            page_id_t parent_id = parent_guard.Get()->GetPageId();

            LOG_DEBUG_SPLIT("Propagating to Parent " << parent_id
                << " | Inserting Key: " << key_to_insert << " Child: " << child_val_to_insert);

            // Attempt Insert into Parent
            if (adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert)) {
                parent_guard.SetDirty(true);
                LOG_DEBUG_SPLIT("Insert into Parent " << parent_id << " SUCCESS.");
                return;
            }

            // CASE 3: Parent Full (Recursive Split)
            LOG_DEBUG_SPLIT("Parent " << parent_id << " FULL. Recursive Split Required.");

            page_id_t p_sibling_id;
            PageGuard p_sibling_guard(bpm_, bpm_->NewPage(p_sibling_id));
            if (!p_sibling_guard.IsValid()) return;

            // Split the Parent
            cmse::adapter::SplitResult p_result;
            adapter_.splitNode(parent_guard.Get(), p_sibling_guard.Get(), &p_result);

            LOG_DEBUG_SPLIT("Parent Split: " << parent_id << " -> " << p_sibling_id
                << " | New Separator: " << p_result.promoted_key);

            // CRITICAL DECISION: Which parent half gets the pending key?
            bool insert_right = (key_to_insert >= p_result.promoted_key);

            if (insert_right) {
                LOG_DEBUG_SPLIT("Decision: Pending Key " << key_to_insert
                    << " >= Separator " << p_result.promoted_key
                    << " -> Insert into RIGHT Sibling " << p_sibling_id);

                adapter_.insertIntoInternal(p_sibling_guard.Get(), key_to_insert, child_val_to_insert);
            }
            else {
                LOG_DEBUG_SPLIT("Decision: Pending Key " << key_to_insert
                    << " < Separator " << p_result.promoted_key
                    << " -> Insert into LEFT Parent " << parent_id);

                adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert);
            }

            // Setup for Next Iteration (Grandparent)
            current_id = parent_id;
            child_val_to_insert = p_sibling_id;
            key_to_insert = p_result.promoted_key;

            parent_guard.SetDirty(true);
            p_sibling_guard.SetDirty(true);
        }
    }

    // -------------------------------------------------------------------------
    // Phase 3: Visualization (Updated to show Metadata Stats)
    // -------------------------------------------------------------------------

    /**
     * PrintNode
     * A diagnostic tool that recursively prints a node's structure and metadata.
     * @param page_id The physical page to display.
     * @param depth Current recursion depth (for indentation).
     * @param limit_depth Maximum depth to traverse.
     * @param prefix String prefix for visual tree alignment.
     */
    void BTreeIndex::PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix) {
        // Base case: stop recursion if we exceed the user-defined depth limit.
        if (depth > limit_depth) return;

        // Fetch the raw page from the buffer pool.
        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) {
            std::cout << prefix << "|- [ERROR: Cannot Fetch Page " << page_id << "]\n";
            return;
        }

        // Map the raw data to the generic node header to read common metadata.
        auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;
        bool is_leaf = header->is_leaf;

        // --- PRINT NODE TYPE, ID, AND CORE STATISTICS ---
        std::cout << prefix << "|- [" << (is_leaf ? "LEAF" : "INTERNAL") << "] "
            << "ID: " << page_id
            << " | Count: " << count
            << " | Stats { Min: " << header->min_key
            << ", Max: " << header->max_key
            << ", Total: " << header->total_keys
            << ", Density: " << header->density << " }";

        if (is_leaf) {
            // Reinterpret as Leaf to access specific leaf data if needed.
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(page->GetData());
            std::cout << "\n";
            // Optional: Print key range for granular debugging.
            // if (count > 0) std::cout << prefix << "    Keys: [" << leaf->keys[0] << " ... " << leaf->keys[count-1] << "]\n";
        }
        else {
            // Reinterpret as Internal to follow child pointers.
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(page->GetData());
            std::cout << "\n";

            // LIMITATION: Only print the first 3 branches to avoid terminal flooding on large trees.
            int branches_to_print = std::min((int)count + 1, 3);
            if (depth == limit_depth) branches_to_print = 0;

            for (int i = 0; i < branches_to_print; i++) {
                // Adjust visual prefix for the tree branches.
                std::string new_prefix = prefix + (i == count ? "    " : "|   ");
                if (i < count) {
                    std::cout << prefix << "|   (Key >= " << internal->keys[i] << ")\n";
                }
                // Recursively call PrintNode for each child.
                PrintNode(internal->children[i], depth + 1, limit_depth, new_prefix);
            }

            // Summary for hidden branches if node is very wide.
            if (count + 1 > 3) {
                std::cout << prefix << "|   (... " << (count + 1 - 3) << " more children ...)\n";
            }
        }
        // Manual unpin because this specific debug function does not use PageGuard.
        bpm_->UnpinPage(page_id, false);
    }

    /**
     * UpdateStatsUpwards
     * Propagates metadata (Min, Max, Total Keys) from a modified leaf up to the root.
     * This is vital for the 'Pruning' optimization during scans.
     */
    void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key) {
        // Iterate backwards through the path pages (from Leaf up to Root).
        for (auto it = ctx.path_pages.rbegin(); it != ctx.path_pages.rend(); ++it) {

            // Safety check: ensure the PageGuard is still holding a valid pin.
            if (!it->IsValid()) continue;

            cmse::Page* page = it->Get();
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());

            if (header->is_leaf) {
                // For leaves, the adapter re-scans the local keys to find exact Min/Max.
                adapter_.updateStatistics(page);
            }
            else {
                // --- INTERNAL NODES STATISTICS LOGIC ---
                // If the node was previously empty or has invalid range, initialize with current key.
                if (header->total_keys == 0 || (header->min_key > header->max_key)) {
                    header->min_key = key;
                    header->max_key = key;
                }
                else {
                    // Expand the node's boundaries to encompass the newly inserted key.
                    if (key < header->min_key) header->min_key = key;
                    if (key > header->max_key) header->max_key = key;
                }

                // Increment total key count represented by this subtree.
                header->total_keys++;

                // --- FORCE LOGICAL CONSISTENCY ---
                // Ensure total_keys is at least (branches * constant) to avoid 0% density 
                // calculations in sparse intermediate internal nodes.
                int32_t min_logical = header->key_count * 5;
                if (header->total_keys < min_logical) {
                    header->total_keys = min_logical;
                }

                // Recalculate key density (keys per integer unit of range).
                if (header->max_key >= header->min_key) {
                    double range = (double)(header->max_key - header->min_key) + 1.0;
                    if (range > 0) {
                        header->density = (float)((double)header->total_keys / range);
                    }
                }
            }

            // Crucial: Mark page as dirty so these metadata changes are persisted to disk.
            it->SetDirty(true);
        }
    }

    /**
     * Begin
     * Returns an iterator pointing to the first key >= start_key.
     * Used for linear range scanning.
     */
    BTreeIterator BTreeIndex::Begin(const KeyType& start_key) {
        // Lock to ensure the root ID doesn't change while we start traversal.
        std::lock_guard<std::mutex> lock(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) {
            // Return an empty/null iterator if the tree doesn't exist.
            return BTreeIterator(bpm_, &adapter_, PageGuard(), 0);
        }

        // 1. Traverse to the leaf that *should* contain the start_key.
        TraversalContext ctx;
        // Search mode (for_write=false) unpins parents automatically as it "crabs" down.
        PageGuard leaf_guard = FindLeaf(start_key, ctx, false);

        if (!leaf_guard.IsValid()) {
            return BTreeIterator(bpm_, &adapter_, PageGuard(), 0);
        }

        // 2. Binary search (or linear skip) inside the leaf to find the exact starting index.
        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_guard.Get()->GetData());
        int index = 0;
        while (index < leaf->header.key_count && leaf->keys[index] < start_key) {
            index++;
        }

        // 3. Construct and return the Iterator.
        // std::move(leaf_guard) transfers the page pin ownership to the BTreeIterator object.
        return BTreeIterator(bpm_, &adapter_, std::move(leaf_guard), index);
    }

    /**
     * PrintTree
     * Public entry point for visualization. Uses an internal lambda for recursion.
     */
    void BTreeIndex::PrintTree(int limit) {
        std::lock_guard<std::mutex> lock(latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            std::cout << "[Empty Tree]" << std::endl;
            return;
        }

        // Recursive Lambda for depth-first tree traversal.
        std::function<void(page_id_t, int)> print_node =
            [&](page_id_t page_id, int depth) {

            if (depth > 10) return; // Stop-gap to prevent stack overflow on corrupt trees.

            // Fetch and guard page for the duration of this print level.
            PageGuard guard(bpm_, bpm_->FetchPage(page_id));
            if (!guard.IsValid()) return;

            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(guard.Get()->GetData());

            // Build indentation based on depth level.
            std::string indent(depth * 4, ' ');

            if (header->is_leaf) {
                auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(guard.Get()->GetData());
                std::cout << indent << "[Leaf " << page_id << "] Keys: " << header->key_count
                    << " | Next: " << leaf->next_leaf_id << " -> ";

                // Print the first few keys up to the user-specified limit.
                int print_k = std::min((int)header->key_count, limit);
                for (int i = 0; i < print_k; i++) {
                    std::cout << leaf->keys[i] << (i == print_k - 1 ? "" : ", ");
                }
                if (header->key_count > limit) std::cout << "...";
                std::cout << std::endl;
            }
            else {
                auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(guard.Get()->GetData());
                std::cout << indent << "[Internal " << page_id << "] Keys: " << header->key_count << std::endl;

                // Internal nodes always have (key_count + 1) child pointers.
                for (int i = 0; i <= header->key_count; i++) {
                    print_node(internal->children[i], depth + 1);
                }
            }
            };

        // Initiation: Call the recursive printer starting from the root.
        print_node(root_page_id_, 0);
    }

    // =========================================================================
//  COPY-ON-WRITE (CoW) IMPLEMENTATION
// =========================================================================

    /**
     * GetPageWritable
     * Ensures that the requested page is safe to modify within the current transaction.
     * Logic:
     * 1. If page is brand new in this txn, return it.
     * 2. If page was already shadowed (copied) in this txn, return the shadow.
     * 3. Otherwise, create a new shadow copy, register it, and return the new page.
     */
    PageGuard BTreeIndex::GetPageWritable(page_id_t page_id, TransactionContext& txn) {
        // --- CASE 1: PAGE IS NEWLY CREATED ---
        // If the page was created during this transaction (e.g., via a split), it's already private.
        if (std::find(txn.created_pages.begin(), txn.created_pages.end(), page_id) != txn.created_pages.end()) {
            return PageGuard(bpm_, bpm_->FetchPage(page_id));
        }

        // --- CASE 2: PAGE IS ALREADY SHADOWED ---
        // Check if we have already made a copy of this specific original page earlier.
        page_id_t shadow_id = txn.GetShadowPageId(page_id);
        if (shadow_id != INVALID_PAGE_ID) {
            return PageGuard(bpm_, bpm_->FetchPage(shadow_id));
        }

        // --- CASE 3: CREATE NEW SHADOW COPY (CoW) ---
        // Step A: Fetch the original (read-only) page data.
        PageGuard old_guard(bpm_, bpm_->FetchPage(page_id));
        if (!old_guard.IsValid()) return PageGuard();

        // Step B: Allocate a brand new physical page to serve as the "Shadow".
        page_id_t new_id;
        PageGuard new_guard(bpm_, bpm_->NewPage(new_id));
        if (!new_guard.IsValid()) return PageGuard();

        // CRITICAL: Log the shadowing event for debugging version history.
        std::cout << "   [CoW] COPYING Page " << page_id << " -> " << new_id << std::endl;

        // Step C: Perform the physical copy of the 4KB block.
        std::memcpy(new_guard.Get()->GetData(), old_guard.Get()->GetData(), PAGE_SIZE);

        // Step D: Mark the new page as modified.
        new_guard.SetDirty(true);

        // Step E: Record the mapping so future calls for 'page_id' use 'new_id'.
        txn.RegisterShadow(page_id, new_id);

        return new_guard;
    }

    /**
     * InsertCoW
     * Performs a B+ Tree insertion using the Path-Copying technique.
     * Every node from the root down to the leaf is copied if it hasn't been already.
     */
    bool BTreeIndex::InsertCoW(const KeyType& key, const ValueType& value, TransactionContext& txn) {
        // NOTE: latch_ is omitted here as CoW allows multiple readers while a single 
        // writer works on its private 'pending_root_id' draft.

        // --- 1. HANDLE EMPTY TREE (BOOTSTRAP) ---
        if (txn.pending_root_id == INVALID_PAGE_ID) {
            page_id_t root_id;
            PageGuard root_guard(bpm_, bpm_->NewPage(root_id));
            if (!root_guard.IsValid()) return false;

            // Initialize as a fresh leaf.
            adapter_.initLeaf(root_guard.Get());
            root_guard.SetDirty(true);

            // Set the sibling link to -1 to represent the end of the list.
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(root_guard.Get()->GetData());
            leaf->next_leaf_id = INVALID_PAGE_ID;

            // Perform the first insertion.
            adapter_.applyUpdateToLeaf(root_guard.Get(), key, value);

            // Track this page as new so it doesn't get shadowed again.
            txn.created_pages.push_back(root_id);
            txn.pending_root_id = root_id; // Update the transaction's draft root.
            return true;
        }

        // --- 2. PATH COPYING TRAVERSAL ---
        // Strategy: Navigate Root -> Leaf. At each level, "shadow" the child before entering it.
        std::vector<PageGuard> ancestors; // Holds pins for internal nodes to allow splits.

        // Step A: Make a writable copy of the Root.
        PageGuard curr_guard = GetPageWritable(txn.pending_root_id, txn);
        if (!curr_guard.IsValid()) return false;

        // Update the draft root ID (in case the root was just shadowed).
        txn.pending_root_id = curr_guard.Get()->GetPageId();

        // Step B: Traverse downwards.
        while (true) {
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(curr_guard.Get()->GetData());

            if (header->is_leaf) {
                break; // Target leaf reached and it is already shadowed/writable.
            }

            // Step C: Determine which child to visit.
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(curr_guard.Get()->GetData());
            int i = 0;
            for (; i < header->key_count; i++) {
                if (key < internal->keys[i]) break;
            }
            page_id_t child_id = internal->children[i];

            // --- CRITICAL: SHADOW THE CHILD BEFORE VISITING ---
            PageGuard child_guard = GetPageWritable(child_id, txn);
            if (!child_guard.IsValid()) return false;

            // --- CRITICAL: UPDATE PARENT LINK ---
            // If GetPageWritable created a new copy, we must update the parent's pointer 
            // to point to the new Shadow ID instead of the original ID.
            if (child_guard.Get()->GetPageId() != child_id) {
                internal->children[i] = child_guard.Get()->GetPageId();
                // Parent is already dirty because it was shadowed earlier in the loop.
            }

            // Keep parent pinned in the stack for potential split propagation.
            ancestors.push_back(std::move(curr_guard));

            // Move the cursor to the shadowed child.
            curr_guard = std::move(child_guard);
        }

        // --- 3. INSERT INTO SHADOWED LEAF ---
        if (adapter_.applyUpdateToLeaf(curr_guard.Get(), key, value)) {
            // Insertion successful without a split.
            return true;
        }

        // --- 4. HANDLE SPLIT (SHADOW CONTEXT) ---
        // If leaf is full, perform split using the same CoW logic.
        HandleSplitCoW(std::move(curr_guard), ancestors, txn, key, value);
        return true;
    }

    /**
     * HandleSplitCoW
     * Manages splits by creating new siblings and propagating keys upward through shadowed parents.
     */
    void BTreeIndex::HandleSplitCoW(PageGuard node_guard, std::vector<PageGuard>& ancestors,
        TransactionContext& txn,
        const KeyType& key, const ValueType& value) {

        // A. Allocate the Sibling.
        page_id_t sibling_id;
        PageGuard sibling_guard(bpm_, bpm_->NewPage(sibling_id));
        if (!sibling_guard.IsValid()) return;

        // Since it's a new page, track it as private to this txn.
        txn.created_pages.push_back(sibling_id);

        // B. Distribute keys between the shadowed node and the new sibling.
        cmse::adapter::SplitResult result;
        adapter_.splitNode(node_guard.Get(), sibling_guard.Get(), &result);

        // --- C. INSERT PENDING KEY ---
        // Decide where the original target key belongs post-split.
        if (key >= result.promoted_key) {
            adapter_.applyUpdateToLeaf(sibling_guard.Get(), key, value);
        }
        else {
            adapter_.applyUpdateToLeaf(node_guard.Get(), key, value);
        }

        KeyType key_to_insert = result.promoted_key;
        page_id_t child_val_to_insert = sibling_id;

        // Release leaf pins; propagation now happens via the 'ancestors' stack.
        node_guard.Drop();
        sibling_guard.Drop();

        // D. Iterative Upward Propagation.
        while (true) {
            // Case: Split reached the Root.
            if (ancestors.empty()) {
                page_id_t new_root_id;
                PageGuard new_root_guard(bpm_, bpm_->NewPage(new_root_id));
                if (!new_root_guard.IsValid()) return;

                txn.created_pages.push_back(new_root_id);

                // Create a new root pointing to the current shadowed root and its new sibling.
                adapter_.createNewRoot(new_root_guard.Get(), txn.pending_root_id, child_val_to_insert, key_to_insert);

                // Update the transaction's draft root pointer.
                txn.pending_root_id = new_root_id;
                return;
            }

            // Fetch the already-shadowed parent from the stack.
            PageGuard parent_guard = std::move(ancestors.back());
            ancestors.pop_back();

            // Try to insert promoted key into shadowed parent.
            if (adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert)) {
                return; // Parent had space.
            }

            // Case: Parent is full, must split parent as well.
            page_id_t p_sibling_id;
            PageGuard p_sibling_guard(bpm_, bpm_->NewPage(p_sibling_id));
            if (!p_sibling_guard.IsValid()) return;

            p_sibling_guard.SetDirty(true);
            txn.created_pages.push_back(p_sibling_id);

            cmse::adapter::SplitResult p_result;
            adapter_.splitNode(parent_guard.Get(), p_sibling_guard.Get(), &p_result);

            // Re-insert the promoted key into the correct half of the split parent.
            if (key_to_insert >= p_result.promoted_key) {
                adapter_.insertIntoInternal(p_sibling_guard.Get(), key_to_insert, child_val_to_insert);
            }
            else {
                adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert);
            }

            // Update variables to propagate the parent split even higher.
            child_val_to_insert = p_sibling_id;
            key_to_insert = p_result.promoted_key;
        }
    }

}
