#pragma once
#include <unordered_map>
#include <vector>
#include "../common/types.h"

namespace cmse {

    /**
     * TransactionContext
     * Encapsulates the isolation state for a single write operation or version update.
     * This structure implements the 'Shadow Paging' logic: instead of modifying pages
     * in place, we record redirected mappings here.
     */
    struct TransactionContext {
        // The unique identifier for the version currently being generated (e.g., Version 2).
        version_id_t version_id;

        // --- THE SHADOW CACHE ---
        /** * shadow_map
         * Redirects lookups from a persistent Page ID to a temporary 'Shadow' Page ID.
         * Key: Original Page ID (Stable)
         * Value: Shadow Page ID (Pending modified copy)
         * Example: { 5 -> 102 } indicates that Page 5 has been cloned to Page 102.
         */
        std::unordered_map<page_id_t, page_id_t> shadow_map;

        /**
         * created_pages
         * A transaction log of every new page ID allocated during this session.
         * Crucial for 'Atomic Abort': if the transaction fails, we use this list
         * to deallocate the orphaned shadow pages from the Buffer Pool.
         */
        std::vector<page_id_t> created_pages;

        // The temporary root of the B+Tree being constructed for this version.
        page_id_t pending_root_id = INVALID_PAGE_ID;

        // The temporary root of the Trie being constructed for this version.
        page_id_t pending_trie_root_id = INVALID_PAGE_ID;

        /**
         * GetShadowPageId
         * Checks if the current transaction has already created a private copy of a page.
         * @param original_id The permanent page ID in the tree.
         * @return The shadow page ID if it exists, otherwise INVALID_PAGE_ID.
         */
        page_id_t GetShadowPageId(page_id_t original_id) {
            // Perform a lookup in our local redirection table
            if (shadow_map.find(original_id) != shadow_map.end()) {
                return shadow_map[original_id];
            }
            // No shadow copy found; caller should use the original page
            return INVALID_PAGE_ID;
        }

        /**
         * RegisterShadow
         * Records a new redirection mapping and tracks the allocated page for cleanup safety.
         * @param original The original page ID that was cloned.
         * @param shadow The new, writable page ID provided by the Buffer Pool.
         */
        void RegisterShadow(page_id_t original, page_id_t shadow) {
            // Map the original ID to the new shadow ID
            shadow_map[original] = shadow;
            // Record this page for potential rollback/cleanup
            created_pages.push_back(shadow);
        }
    };

} // namespace cmse