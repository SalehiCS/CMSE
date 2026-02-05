#pragma once
#include <unordered_map>
#include <vector>
#include "../common/types.h"

namespace cmse {

    struct TransactionContext {
        // The Version ID we are currently building (e.g., Version 2)
        version_id_t version_id;

        // --- THE SHADOW CACHE ---
        // Maps Original Page ID -> Shadow Page ID (The "Pending" Copy)
        // Example: { 5 -> 102 } means "Page 5 was modified; use Page 102 instead."
        std::unordered_map<page_id_t, page_id_t> shadow_map;

        // Keep track of new pages created in this transaction 
        // (So we can clean them up if we Abort/Rollback)
        std::vector<page_id_t> created_pages;

        // The Pending Root for this version
        page_id_t pending_root_id = INVALID_PAGE_ID;

        page_id_t pending_trie_root_id = INVALID_PAGE_ID;


        // Helper: Check if a page has already been shadowed
        page_id_t GetShadowPageId(page_id_t original_id) {
            if (shadow_map.find(original_id) != shadow_map.end()) {
                return shadow_map[original_id];
            }
            return INVALID_PAGE_ID;
        }

        // Helper: Register a new shadow copy
        void RegisterShadow(page_id_t original, page_id_t shadow) {
            shadow_map[original] = shadow;
            created_pages.push_back(shadow);
        }
    };

} // namespace cmse