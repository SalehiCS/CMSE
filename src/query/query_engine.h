#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../common/types.h"
#include "query_cursor.h"

namespace cmse {

    /**
     * Query
     * A structured representation of the user's search intent.
     * Supports range-based temporal filters, categorical filters, and prefix wildcards.
     */
    struct Query {
        int64_t min_timestamp = 0;              // Start of time range
        int64_t max_timestamp = INT64_MAX;      // End of time range
        int32_t priority = -1;                  // Syslog priority filter (-1 for wildcard)

        std::string source;           // Originating service (supports '*' prefix)
        std::string host;             // Originating machine (supports '*' prefix)
        std::string message_contains; // Keyword search within the log payload
    };

    class QueryEngine {
    public:
        QueryEngine(index::TrieIndex* trie, index::BTreeIndex* btree)
            : trie_(trie), btree_(btree) {
        }

        /**
         * Execute
         * ---------------------------------------------------------
         * Selects the best physical plan (Plan A or B) and returns a Cursor.
         * DOES NOT iterate records. Returns immediately.
         */
        std::unique_ptr<QueryCursor> Execute(const Query& q) {

            std::vector<int64_t> candidates;
            bool use_sparse_scan = false;

            // --- STEP 1: COST ESTIMATION (Index Racing) ---
            const size_t THRESHOLD = 100;

            // Check Source Index
            if (!q.source.empty()) {
                bool is_prefix = (q.source.back() == '*');
                std::string key = is_prefix ? q.source.substr(0, q.source.size() - 1) : q.source;

                auto result = trie_->GetTimestampsWithCap(
                    key, is_prefix, q.priority,
                    q.min_timestamp, q.max_timestamp, THRESHOLD
                );

                if (!result.is_overflow) {
                    candidates = result.timestamps;
                    use_sparse_scan = true;
                }
            }

            // Check Host Index (if Source didn't already win)
            if (!use_sparse_scan && !q.host.empty()) {
                bool is_prefix = (q.host.back() == '*');
                std::string key = is_prefix ? q.host.substr(0, q.host.size() - 1) : q.host;

                auto result = trie_->GetTimestampsWithCap(
                    key, is_prefix, q.priority,
                    q.min_timestamp, q.max_timestamp, THRESHOLD
                );

                if (!result.is_overflow) {
                    candidates = result.timestamps;
                    use_sparse_scan = true;
                }
            }

            // --- STEP 2: CONSTRUCT CURSOR ---
            if (use_sparse_scan) {
                // [PLAN A] Return Sparse Cursor
                return std::make_unique<QueryCursor>(btree_, std::move(candidates), q);
            }
            else {
                // [PLAN B] Return Sequential Cursor
                // Initialize BTreeIterator at start of range
                auto it = btree_->Begin(q.min_timestamp);

                // IMPORTANT: std::move(it) to transfer ownership to the Cursor
                return std::make_unique<QueryCursor>(std::move(it), q);
            }
        }

    private:
        index::TrieIndex* trie_;
        index::BTreeIndex* btree_;
    };

} // namespace cmse