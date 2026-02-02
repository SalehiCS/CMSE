#pragma once
#include "../page/page.h" 
#include "../common/types.h"
#include <vector>
#include <cstring>
#include <algorithm>

namespace cmse::adapter {

    struct SplitResult {
        bool did_split = false;
        page_id_t left_page_id = INVALID_PAGE_ID;
        page_id_t right_page_id = INVALID_PAGE_ID;
        KeyType promoted_key;
    };

    struct BPlusNodeHeader {
        bool is_leaf;
        int16_t key_count;
        KeyType min_key;
        KeyType max_key;
        float density;
    };

    // --- CAPACITY CALCULATION ---
    // Page Size: 4096
    // Header: ~24 bytes
    // Available: 4072 bytes

    // Internal Entry: Key(8) + PageID(4) = 12 bytes
    // 4072 / 12 = ~339
    constexpr int MAX_KEYS_INTERNAL = 338;

    // Leaf Entry: Key(8) + LogRecord(sizeof(LogRecord) ~ 280) = ~288 bytes
    // 4072 / 288 = ~14
    constexpr int MAX_KEYS_LEAF = 14;

    struct BPlusInternalNode {
        BPlusNodeHeader header;
        KeyType keys[MAX_KEYS_INTERNAL];
        page_id_t children[MAX_KEYS_INTERNAL + 1];
    };

    struct BPlusLeafNode {
        BPlusNodeHeader header;
        KeyType keys[MAX_KEYS_LEAF];
        ValueType values[MAX_KEYS_LEAF];
        page_id_t next_leaf_id;
    };

    class BTreeAdapter {
    public:
        void initLeaf(Page* page);
        void initInternal(Page* page);
        bool isLeaf(Page* page);
        int getCount(Page* page);

        // Helper to get max keys based on node type
        int getMaxKeys(Page* page);

        page_id_t findChild(Page* internal_page, const KeyType& key);
        bool shouldSkip(Page* page, const KeyType& query_min, const KeyType& query_max);
        bool applyUpdateToLeaf(Page* leaf_page, const KeyType& key, const ValueType& val);
        void updateChildPointer(Page* parent_page, page_id_t old_child_id, page_id_t new_child_id);
        bool insertIntoInternal(Page* internal_page, const KeyType& key, page_id_t right_child_id);
        void splitNode(Page* node_to_split, Page* new_right_page, SplitResult* out_result);
        void createNewRoot(Page* new_root_page, page_id_t left_child, page_id_t right_child, const KeyType& key);
        void updateStatistics(Page* page);

    private:
        BPlusNodeHeader* getHeader(Page* page) {
            return reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        }

        BPlusInternalNode* getInternalNode(Page* page) {
            return reinterpret_cast<BPlusInternalNode*>(page->GetData());
        }

        BPlusLeafNode* getLeafNode(Page* page) {
            return reinterpret_cast<BPlusLeafNode*>(page->GetData());
        }
    };

} // namespace cmse::adapter