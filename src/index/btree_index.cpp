#include "index/btree_index.h"
#include "bufferpool/buffer_pool_manager.h"
#include "page/page.h"

#include <functional>
#include <iostream>
#include <algorithm> 

namespace cmse::index 
{



    // -------------------------------------------------------------------------
    // BTreeIndex Implementation
    // -------------------------------------------------------------------------

    BTreeIndex::BTreeIndex(cmse::bufferpool::BufferPoolManager* bpm, page_id_t root_id)
        : bpm_(bpm), root_page_id_(root_id) {

    }

    bool BTreeIndex::Insert(const KeyType& key, const ValueType& value) {
        // ... (Lazy Init and Lock code same as before) ...
        std::lock_guard<std::mutex> lock(latch_);

        int attempts = 0;
        const int MAX_ATTEMPTS = 5;

        while (attempts < MAX_ATTEMPTS) {
            if (root_page_id_ == INVALID_PAGE_ID) {
                StartNewTree(key, value);
                return true;
            }

            TraversalContext ctx;
            PageGuard leaf_guard = FindLeaf(key, ctx, true);

            if (!leaf_guard.IsValid()) return false;

            // Case 3: Try to insert into the leaf
            if (adapter_.applyUpdateToLeaf(leaf_guard.Get(), key, value)) {

                // --- CRITICAL FIX START ---
                // Update the Min/Max/Total stats of all ancestors (Parent -> Root).
                // If we don't do this, GetValue() will PRUNE valid keys!
                UpdateStatsUpwards(ctx, key);
                // --------------------------

                leaf_guard.SetDirty(true);
                return true; // Guards unpin automatically
            }

            // Case 4: Split
            HandleSplit(std::move(leaf_guard), ctx);
            attempts++;
        }
        return false;
    }

    // -------------------------------------------------------------------------
        // Phase 3: Point Lookup with Pruning
        // -------------------------------------------------------------------------
    bool BTreeIndex::GetValue(const KeyType& key, ValueType& result) {
        // --- DEBUG ENTRY ---
        if (key == 9999) {
            std::cout << "[GetValue 9999] Called. Current Root ID: " << root_page_id_ << std::endl;
        }
        // -------------------

        std::lock_guard<std::mutex> guard(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) {
            if (key == 9999) std::cout << "[GetValue 9999] FAIL: Root is Invalid!" << std::endl;
            return false;
        }

        TraversalContext ctx;

        // Explicitly call FindLeaf and check result
        PageGuard leaf_guard = FindLeaf(key, ctx, false);

        if (!leaf_guard.IsValid()) {
            if (key == 9999) std::cout << "[GetValue 9999] FAIL: FindLeaf returned Invalid Guard." << std::endl;
            return false;
        }

        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_guard.Get()->GetData());

        // Debug the Leaf Content
        if (key == 9999) {
            std::cout << "[GetValue 9999] Reached Leaf Page " << leaf_guard.Get()->GetPageId()
                << " with " << leaf->header.key_count << " keys." << std::endl;
            // Print first and last key to confirm range
            if (leaf->header.key_count > 0) {
                std::cout << "   -> Range: [" << leaf->keys[0] << " ... "
                    << leaf->keys[leaf->header.key_count - 1] << "]" << std::endl;
            }
        }

        // Search inside the leaf
        int index = -1;
        // Simple linear scan for safety/debug
        for (int i = 0; i < leaf->header.key_count; i++) {
            if (leaf->keys[i] == key) {
                index = i;
                break;
            }
        }

        if (index != -1) {
            result = leaf->values[index];
            if (key == 9999) std::cout << "[GetValue 9999] SUCCESS: Found at index " << index << std::endl;
            return true;
        }

        if (key == 9999) std::cout << "[GetValue 9999] FAIL: Key not found in this leaf." << std::endl;
        return false;
    }
    std::vector<ValueType> BTreeIndex::Scan(const KeyType& start_key, const KeyType& end_key) {
        std::lock_guard<std::mutex> lock(latch_);
        std::vector<ValueType> results;

        if (root_page_id_ == INVALID_PAGE_ID) return results;

        // --- OPTIMIZATION: Root Level Pruning (Safe Version) ---
        {
            PageGuard root_guard(bpm_, bpm_->FetchPage(root_page_id_));
            if (root_guard.IsValid()) {
                auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(root_guard.Get()->GetData());

                // Only trust stats if the node is clean
                if (header->total_keys > 0 && header->is_dirty == 0) {
                    if (end_key < header->min_key || start_key > header->max_key) {
                        return results; // AUTOMATIC UNPIN
                    }
                }
            }
        }
        // -------------------------------------------------------

        // 1. Find the starting leaf safe
        TraversalContext ctx;
        PageGuard curr_guard = FindLeaf(start_key, ctx, false);

        if (!curr_guard.IsValid()) return results;

        // --- SAFETY LOOP ---
        int scanned_pages = 0;
        const int MAX_SCAN_PAGES = 100000;

        // 2. Horizontal Linear Scan
        while (curr_guard.IsValid()) {

            // Safety Break
            scanned_pages++;
            if (scanned_pages > MAX_SCAN_PAGES) {
                std::cerr << "[FATAL] Infinite Loop detected in Scan!" << std::endl;
                break;
            }

            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(curr_guard.Get()->GetData());
            int count = leaf->header.key_count;
            page_id_t next_page_id = leaf->next_leaf_id;
            page_id_t current_id = curr_guard.Get()->GetPageId();

            // Collect keys
            for (int i = 0; i < count; ++i) {
                if (leaf->keys[i] > end_key) {
                    return results; // AUTOMATIC UNPIN of curr_guard
                }
                if (leaf->keys[i] >= start_key) {
                    results.push_back(leaf->values[i]);
                }
            }

            // End of chain?
            if (next_page_id == INVALID_PAGE_ID) break;

            // Loop detection
            if (next_page_id == current_id) {
                std::cerr << "[FATAL] Page " << next_page_id << " points to itself!" << std::endl;
                break;
            }

            // --- MOVE TO NEXT PAGE ---
            // 1. Fetch next page into a temporary guard
            // 2. Use Move Assignment to update curr_guard
            //    (This automatically Unpins the old page safely)
            curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_page_id));
        }

        return results; // AUTOMATIC UNPIN
    }

    void BTreeIndex::StartNewTree(const KeyType& key, const ValueType& value) {
        page_id_t root_id;
        // Wrap new page immediately
        PageGuard root_guard(bpm_, bpm_->NewPage(root_id));

        if (!root_guard.IsValid()) return;

        adapter_.initLeaf(root_guard.Get());

        adapter_.applyUpdateToLeaf(root_guard.Get(), key, value);

        root_page_id_ = root_id;

        // Mark dirty so it writes to disk
        root_guard.SetDirty(true);

        // Destructor runs here -> Unpins Root automatically
    }

    PageGuard BTreeIndex::FindLeaf(const KeyType& key, TraversalContext& ctx, bool for_write) {
        // 1. Fetch Root safely
        PageGuard curr_guard(bpm_, bpm_->FetchPage(root_page_id_));

        // --- DEBUG PRINT START ---
        if (key == 9999 || key == -1) {
            std::cout << "[FindLeaf " << key << "] Start at Root: " << root_page_id_ << std::endl;
        }
        // --- DEBUG PRINT END ---

        if (!curr_guard.IsValid()) {
            return {};
        }

        // 2. Traversal Loop
        while (!adapter_.isLeaf(curr_guard.Get())) {

            // --- DEBUG PRINT START ---
            if (key == 9999 || key == -1) {
                auto* h = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(curr_guard.Get()->GetData());
                auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(curr_guard.Get()->GetData());
                std::cout << "[FindLeaf " << key << "] At Page " << curr_guard.Get()->GetPageId()
                    << " (Count: " << h->key_count << ", MaxKey: " << h->max_key << ")" << std::endl;
            }
            // --- DEBUG PRINT END ---

            if (for_write) {
                // For write/stack tracing, keep parents pinned
                Page* raw_ptr = curr_guard.Get();
                ctx.path_pages.push_back(std::move(curr_guard));

                page_id_t next_id = adapter_.findChild(raw_ptr, key);

                // --- DEBUG PRINT ---
                if (key == 9999 || key == -1) {
                    std::cout << "   -> Chose Child: " << next_id << std::endl;
                }
                // -------------------

                curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_id));
            }
            else {
                // Read-only mode: Crab walking (Drop parent before fetching child)
                page_id_t next_id = adapter_.findChild(curr_guard.Get(), key);

                // --- DEBUG PRINT ---
                if (key == 9999 || key == -1) {
                    std::cout << "   -> Chose Child: " << next_id << std::endl;
                }
                // -------------------

                curr_guard.Drop();
                curr_guard = PageGuard(bpm_, bpm_->FetchPage(next_id));
            }

            if (!curr_guard.IsValid()) return {};
        }

        // --- DEBUG PRINT START ---
        if (key == 9999 || key == -1) {
            std::cout << "[FindLeaf " << key << "] Landed on Leaf: " << curr_guard.Get()->GetPageId() << std::endl;
        }
        // --- DEBUG PRINT END ---

        return curr_guard;
    }

    void BTreeIndex::HandleSplit(PageGuard node_guard, TraversalContext& ctx) {

        // We hold 'node_guard' (The child). It is pinned.
        cmse::Page* current_node = node_guard.Get();
        page_id_t current_id = current_node->GetPageId();

        // 1. Create Sibling Safe
        page_id_t sibling_id;
        PageGuard sibling_guard(bpm_, bpm_->NewPage(sibling_id));
        if (!sibling_guard.IsValid()) return;

        // 2. Perform Split (Leaf or Internal)
        cmse::adapter::SplitResult result;
        adapter_.splitNode(current_node, sibling_guard.Get(), &result);

        // Prepare propagation data
        KeyType key_to_insert = result.promoted_key;
        page_id_t child_val_to_insert = sibling_id;

        // Mark both dirty
        node_guard.SetDirty(true);
        sibling_guard.SetDirty(true);

        // We can drop the guards now. We only need the Page IDs for the parent.
        // (If we kept them pinned, we would deadlock when trying to fetch the parent in some designs)
        node_guard.Drop();
        sibling_guard.Drop();

        // ==========================================================
        // Iterative Upward Propagation
        // ==========================================================
        while (true) {

            // ---------------------------------------------------------
            // CASE 1: SPLITTING THE ROOT (Create New Root)
            // ---------------------------------------------------------
            if (ctx.path_pages.empty()) {
                page_id_t new_root_id;
                PageGuard new_root_guard(bpm_, bpm_->NewPage(new_root_id));
                if (!new_root_guard.IsValid()) return;

                // Initialize pointers: [OldRoot] [Key] [NewChild]
                adapter_.createNewRoot(new_root_guard.Get(), current_id, child_val_to_insert, key_to_insert);

                // --- RESTORED: Phase 3 Exact Statistics ---
                // We must re-fetch the children briefly to sum their stats safely
                {
                    PageGuard left_child(bpm_, bpm_->FetchPage(current_id));
                    PageGuard right_child(bpm_, bpm_->FetchPage(child_val_to_insert));

                    if (left_child.IsValid() && right_child.IsValid()) {
                        auto* root_h = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(new_root_guard.Get()->GetData());
                        auto* left_h = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(left_child.Get()->GetData());
                        auto* right_h = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(right_child.Get()->GetData());

                        // 1. Sum Total Keys (Exact)
                        root_h->total_keys = left_h->total_keys + right_h->total_keys;

                        // 2. Set Min/Max Boundaries
                        root_h->min_key = left_h->min_key;
                        root_h->max_key = right_h->max_key;

                        // 3. Recalculate Density
                        if (root_h->max_key >= root_h->min_key) {
                            double range = (double)(root_h->max_key - root_h->min_key) + 1.0;
                            if (range > 0) {
                                root_h->density = (float)((double)root_h->total_keys / range);
                            }
                            else {
                                root_h->density = 1.0f;
                            }
                        }
                        else {
                            root_h->density = 0.0f;
                        }
                    }
                } // Children unpin here automatically
                // ---------------------------------------------------------

                new_root_guard.SetDirty(true);
                this->root_page_id_ = new_root_id;
                return; // Done
            }

            // ---------------------------------------------------------
            // CASE 2: PARENT EXISTS
            // ---------------------------------------------------------
            // Get Parent from Stack (Move ownership)
            PageGuard parent_guard = std::move(ctx.path_pages.back());
            ctx.path_pages.pop_back();

            // Try Insert into Parent
            if (adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert)) {
                parent_guard.SetDirty(true);
                return; // Success, all guards unpin automatically
            }

            // ---------------------------------------------------------
            // CASE 3: PARENT IS FULL -> SPLIT PARENT
            // ---------------------------------------------------------
            page_id_t p_sibling_id;
            PageGuard p_sibling_guard(bpm_, bpm_->NewPage(p_sibling_id));
            if (!p_sibling_guard.IsValid()) return;

            cmse::adapter::SplitResult p_result;
            adapter_.splitNode(parent_guard.Get(), p_sibling_guard.Get(), &p_result);

            // "Dropped Key" Fix: Insert the pending key into the correct half
            if (key_to_insert >= p_result.promoted_key) {
                adapter_.insertIntoInternal(p_sibling_guard.Get(), key_to_insert, child_val_to_insert);
            }
            else {
                adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert);
            }

            // Prepare variables for the next iteration (Grandparent becomes Parent)
            current_id = parent_guard.Get()->GetPageId();
            child_val_to_insert = p_sibling_id;
            key_to_insert = p_result.promoted_key;

            // Mark Dirty
            parent_guard.SetDirty(true);
            p_sibling_guard.SetDirty(true);

            // Loop continues... Guards (parent & sibling) die here, unpinning the pages.
        }
    }

    // -------------------------------------------------------------------------
        // Phase 3: Visualization (Updated to show Stats)
        // -------------------------------------------------------------------------
    void BTreeIndex::PrintNode(page_id_t page_id, int depth, int limit_depth, const std::string& prefix) {
        if (depth > limit_depth) return;

        cmse::Page* page = bpm_->FetchPage(page_id);
        if (page == nullptr) {
            std::cout << prefix << "|- [ERROR: Cannot Fetch Page " << page_id << "]\n";
            return;
        }

        auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());
        int count = header->key_count;
        bool is_leaf = header->is_leaf;

        // Print Node Info + STATISTICS
        std::cout << prefix << "|- [" << (is_leaf ? "LEAF" : "INTERNAL") << "] "
            << "ID: " << page_id
            << " | Count: " << count
            // --- Stats Display ---
            << " | Stats { Min: " << header->min_key
            << ", Max: " << header->max_key
            << ", Total: " << header->total_keys
            << ", Density: " << header->density << " }";

        if (is_leaf) {
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(page->GetData());
            std::cout << "\n";
            // Optional: Print keys for debugging
            // if (count > 0) std::cout << prefix << "    Keys: [" << leaf->keys[0] << " ... " << leaf->keys[count-1] << "]\n";
        }
        else {
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(page->GetData());
            std::cout << "\n";

            // Print children (limited to avoid huge output)
            int branches_to_print = std::min((int)count + 1, 3);
            if (depth == limit_depth) branches_to_print = 0;

            for (int i = 0; i < branches_to_print; i++) {
                std::string new_prefix = prefix + (i == count ? "    " : "|   ");
                if (i < count) {
                    std::cout << prefix << "|   (Key >= " << internal->keys[i] << ")\n";
                }
                PrintNode(internal->children[i], depth + 1, limit_depth, new_prefix);
            }

            if (count + 1 > 3) {
                std::cout << prefix << "|   (... " << (count + 1 - 3) << " more children ...)\n";
            }
        }
        bpm_->UnpinPage(page_id, false);
    }

    void BTreeIndex::UpdateStatsUpwards(TraversalContext& ctx, const KeyType& key) {
        // Iterate backwards (Leaf -> Root)
        for (auto it = ctx.path_pages.rbegin(); it != ctx.path_pages.rend(); ++it) {

            // 'it' is now a PageGuard, not a Page*
            if (!it->IsValid()) continue;

            cmse::Page* page = it->Get();
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());

            if (header->is_leaf) {
                adapter_.updateStatistics(page);
            }
            else {
                // INTERNAL NODES logic
                if (header->total_keys == 0 || (header->min_key > header->max_key)) {
                    header->min_key = key;
                    header->max_key = key;
                }
                else {
                    if (key < header->min_key) header->min_key = key;
                    if (key > header->max_key) header->max_key = key;
                }

                header->total_keys++;

                // --- FORCE LOGICAL CONSISTENCY ---
                int32_t min_logical = header->key_count * 5;
                if (header->total_keys < min_logical) {
                    header->total_keys = min_logical;
                }
                // ---------------------------------

                if (header->max_key >= header->min_key) {
                    double range = (double)(header->max_key - header->min_key) + 1.0;
                    if (range > 0) {
                        header->density = (float)((double)header->total_keys / range);
                    }
                }
            }

            // IMPORTANT: Mark the page as dirty via the Guard
            it->SetDirty(true);
        }
    }

    BTreeIterator BTreeIndex::Begin(const KeyType& start_key) {
        std::lock_guard<std::mutex> lock(latch_);

        if (root_page_id_ == INVALID_PAGE_ID) {
            // Return empty iterator
            return BTreeIterator(bpm_, &adapter_, PageGuard(), 0);
        }

        // 1. Find the starting leaf safely
        TraversalContext ctx;

        // FindLeaf returns a PageGuard.
        // Note: Since we use for_write=false, ancestors are already unpinned/dropped 
        // by FindLeaf, so 'ctx' stack is empty. We only hold the leaf.
        PageGuard leaf_guard = FindLeaf(start_key, ctx, false);

        if (!leaf_guard.IsValid()) {
            return BTreeIterator(bpm_, &adapter_, PageGuard(), 0);
        }

        // 2. Find start index within the leaf
        auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(leaf_guard.Get()->GetData());
        int index = 0;
        while (index < leaf->header.key_count && leaf->keys[index] < start_key) {
            index++;
        }

        // 3. Return Iterator by MOVING the guard
        // We pass std::move(leaf_guard) so the Iterator takes the pin.
        return BTreeIterator(bpm_, &adapter_, std::move(leaf_guard), index);
    }

    // -------------------------------------------------------------------------
//  Debug / Visualization
// -------------------------------------------------------------------------
    void BTreeIndex::PrintTree(int limit) {
        std::lock_guard<std::mutex> lock(latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            std::cout << "[Empty Tree]" << std::endl;
            return;
        }

        // Helper Lambda for recursive printing
        std::function<void(page_id_t, int)> print_node =
            [&](page_id_t page_id, int depth) {

            if (depth > 10) return; // Safety depth limit

            PageGuard guard(bpm_, bpm_->FetchPage(page_id));
            if (!guard.IsValid()) return;

            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(guard.Get()->GetData());

            // Indentation
            std::string indent(depth * 4, ' ');

            if (header->is_leaf) {
                auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(guard.Get()->GetData());
                std::cout << indent << "[Leaf " << page_id << "] Keys: " << header->key_count
                    << " | Next: " << leaf->next_leaf_id << " -> ";

                // Print first few keys
                int print_k = std::min((int)header->key_count, limit);
                for (int i = 0; i < print_k; i++) {
                    std::cout << leaf->keys[i] << ",";
                }
                if (header->key_count > limit) std::cout << "...";
                std::cout << std::endl;
            }
            else {
                auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(guard.Get()->GetData());
                std::cout << indent << "[Internal " << page_id << "] Keys: " << header->key_count << std::endl;

                // Recursively print children
                // Internal node has (key_count + 1) children
                for (int i = 0; i <= header->key_count; i++) {
                    print_node(internal->children[i], depth + 1);
                }
            }
            };

        // Start printing from root
        print_node(root_page_id_, 0);
    }

    // ... includes ...

// =========================================================================
//  COPY-ON-WRITE IMPLEMENTATION
// =========================================================================

    PageGuard BTreeIndex::GetPageWritable(page_id_t page_id, TransactionContext& txn) {
        // Check Case 1
        if (std::find(txn.created_pages.begin(), txn.created_pages.end(), page_id) != txn.created_pages.end()) {
            // std::cout << "   [CoW] Page " << page_id << " is already NEW. Returning directly." << std::endl;
            return PageGuard(bpm_, bpm_->FetchPage(page_id));
        }

        // Check Case 2
        page_id_t shadow_id = txn.GetShadowPageId(page_id);
        if (shadow_id != INVALID_PAGE_ID) {
            // std::cout << "   [CoW] Page " << page_id << " already shadowed as " << shadow_id << ". Using Shadow." << std::endl;
            return PageGuard(bpm_, bpm_->FetchPage(shadow_id));
        }

        // Case 3: Copy
        PageGuard old_guard(bpm_, bpm_->FetchPage(page_id));
        if (!old_guard.IsValid()) return PageGuard();

        page_id_t new_id;
        PageGuard new_guard(bpm_, bpm_->NewPage(new_id));
        if (!new_guard.IsValid()) return PageGuard();

        // --- CRITICAL DEBUG PRINT ---
        std::cout << "   [CoW] COPYING Page " << page_id << " -> " << new_id << std::endl;
        // ----------------------------

        std::memcpy(new_guard.Get()->GetData(), old_guard.Get()->GetData(), PAGE_SIZE);


        new_guard.SetDirty(true);

        txn.RegisterShadow(page_id, new_id);

        return new_guard;
    }


    bool BTreeIndex::InsertCoW(const KeyType& key, const ValueType& value, TransactionContext& txn) {
        // std::lock_guard<std::mutex> lock(latch_); // CoW usually assumes single-writer or external lock

        // --- 1. Handle Empty Tree (Bootstrap) ---
        if (txn.pending_root_id == INVALID_PAGE_ID) {
            // Just like StartNewTree, but using txn tracking
            page_id_t root_id;
            PageGuard root_guard(bpm_, bpm_->NewPage(root_id));
            if (!root_guard.IsValid()) return false;

            adapter_.initLeaf(root_guard.Get());

            root_guard.SetDirty(true);

            // Fix loop bug (ensure -1)
            auto* leaf = reinterpret_cast<cmse::adapter::BPlusLeafNode*>(root_guard.Get()->GetData());
            leaf->next_leaf_id = INVALID_PAGE_ID;

            adapter_.applyUpdateToLeaf(root_guard.Get(), key, value);

            txn.created_pages.push_back(root_id);
            txn.pending_root_id = root_id; // Set the draft root
            return true;
        }

        // --- 2. Path Copying Traversal ---
        // We traverse from Root -> Leaf.
        // At EVERY step, we ensure the current node is "Writable" (Shadowed).
        // If we shadow a node, we MUST update its Parent to point to the new ID.

        std::vector<PageGuard> ancestors; // Keep shadow guards pinned for split propagation

        // Start with Root
        PageGuard curr_guard = GetPageWritable(txn.pending_root_id, txn);
        if (!curr_guard.IsValid()) return false;

        // Update Pending Root (in case Root was just copied)
        txn.pending_root_id = curr_guard.Get()->GetPageId();

        // Traverse down
        while (true) {
            auto* header = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(curr_guard.Get()->GetData());

            if (header->is_leaf) {
                // Found the leaf (Shadowed and ready)
                break;
            }

            // Internal Node: Find child to visit
            auto* internal = reinterpret_cast<cmse::adapter::BPlusInternalNode*>(curr_guard.Get()->GetData());
            page_id_t child_id = INVALID_PAGE_ID;

            // Simple linear search for child (could use binary search)
            // Internal keys: [K1, K2]  Children: [C0, C1, C2]
            // if key < K1 -> C0
            // if K1 <= key < K2 -> C1
            // if key >= K2 -> C2
            int i = 0;
            for (; i < header->key_count; i++) {
                if (key < internal->keys[i]) break;
            }
            child_id = internal->children[i];

            // --- CRITICAL: Shadow the Child ---
            PageGuard child_guard = GetPageWritable(child_id, txn);
            if (!child_guard.IsValid()) return false;

            // --- CRITICAL: Link Parent -> New Child ---
            // If the child was copied, its ID changed. We must update the parent's pointer.
            if (child_guard.Get()->GetPageId() != child_id) {
                internal->children[i] = child_guard.Get()->GetPageId();
                // Parent is already dirty/shadowed, so this write is safe.
            }

            // Push Parent to stack (we need it for splits)
            ancestors.push_back(std::move(curr_guard));

            // Move to Child
            curr_guard = std::move(child_guard);
        }

        // --- 3. Insert into Leaf (Shadow) ---
        // 'curr_guard' is now the Writable Shadow Leaf
        if (adapter_.applyUpdateToLeaf(curr_guard.Get(), key, value)) {
            // Success!
            // We do NOT need to traverse up to mark dirty, because
            // we already marked everything dirty/shadowed on the way down.
            // Just need to update stats if you want (optional for CoW drafts).
            return true;
        }

        // --- 4. Split Handling (Shadow) ---
        // If we are here, the Shadow Leaf is full.
        HandleSplitCoW(std::move(curr_guard), ancestors, txn, key, value);
        return true;
    }

    void BTreeIndex::HandleSplitCoW(PageGuard node_guard, std::vector<PageGuard>& ancestors,
        TransactionContext& txn,
        const KeyType& key, const ValueType& value) {

        // A. Create Sibling
        page_id_t sibling_id;
        PageGuard sibling_guard(bpm_, bpm_->NewPage(sibling_id));
        if (!sibling_guard.IsValid()) return;
        txn.created_pages.push_back(sibling_id);

        // B. Perform Split (Moves half existing keys to sibling)
        cmse::adapter::SplitResult result;
        adapter_.splitNode(node_guard.Get(), sibling_guard.Get(), &result);

        // --- C. CRITICAL FIX: INSERT THE PENDING KEY ---
        // We must decide if the new key goes to the Old Leaf (node_guard) or New Sibling (sibling_guard).
        // The 'promoted_key' tells us the split point.

        if (key >= result.promoted_key) {
            // Goes to New Sibling
            adapter_.applyUpdateToLeaf(sibling_guard.Get(), key, value);
        }
        else {
            // Goes to Old Leaf
            adapter_.applyUpdateToLeaf(node_guard.Get(), key, value);
        }
        // -----------------------------------------------

        KeyType key_to_insert = result.promoted_key;
        page_id_t child_val_to_insert = sibling_id;

        node_guard.Drop();
        sibling_guard.Drop();

        // D. Propagate Upwards (Existing Logic)
        while (true) {
            if (ancestors.empty()) {
                page_id_t new_root_id;
                PageGuard new_root_guard(bpm_, bpm_->NewPage(new_root_id));
                if (!new_root_guard.IsValid()) return;
                txn.created_pages.push_back(new_root_id);

                adapter_.createNewRoot(new_root_guard.Get(), txn.pending_root_id, child_val_to_insert, key_to_insert);
                txn.pending_root_id = new_root_id;
                return;
            }

            PageGuard parent_guard = std::move(ancestors.back());
            ancestors.pop_back();

            if (adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert)) {
                return;
            }

            // Parent Full -> Split Parent
            page_id_t p_sibling_id;
            PageGuard p_sibling_guard(bpm_, bpm_->NewPage(p_sibling_id));
            if (!p_sibling_guard.IsValid()) return;

            p_sibling_guard.SetDirty(true);

            txn.created_pages.push_back(p_sibling_id);

            cmse::adapter::SplitResult p_result;
            adapter_.splitNode(parent_guard.Get(), p_sibling_guard.Get(), &p_result);

            if (key_to_insert >= p_result.promoted_key) {
                adapter_.insertIntoInternal(p_sibling_guard.Get(), key_to_insert, child_val_to_insert);
            }
            else {
                adapter_.insertIntoInternal(parent_guard.Get(), key_to_insert, child_val_to_insert);
            }

            child_val_to_insert = p_sibling_id;
            key_to_insert = p_result.promoted_key;
        }
    }
}
