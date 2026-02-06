#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../common/types.h"

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

    /**
     * QueryEngine
     * The decision-making heart of the system. It implements Hybrid Search:
     * 1. Cost Estimation: Asks the Trie how many records match the filters.
     * 2. Plan Selection: If results are sparse (< 100), use point lookups.
     * 3. Plan Selection: If results are dense, use a sequential B+Tree scan.
     */
    class QueryEngine {
    public:
        /**
         * Constructor
         * @param btree Pointer to the primary B+Tree (Temporal ordering)
         * @param trie Pointer to the secondary Trie (Textual/Categorical indexing)
         */
        QueryEngine(index::BTreeIndex* btree, index::TrieIndex* trie)
            : btree_(btree), trie_(trie) {
        }

        /**
         * Execute
         * Performs the search using the most efficient physical plan available.
         * @param q The user-defined Query criteria.
         * @return A vector of fully materialized LogRecords.
         */
        std::vector<LogRecord> Execute(const Query& q) {
            std::vector<int64_t> candidates; // Stores potential matching timestamps
            bool use_sparse_scan = false;    // Toggle for Plan A vs Plan B

            // --- STEP 1: COST ESTIMATION (Index Racing) ---

            /** * THRESHOLD
             * The "tipping point" for optimization. If we find more than 100 matches,
             * random I/O for each record becomes slower than one sequential scan.
             */
            const size_t THRESHOLD = 100;

            // EVALUATE SOURCE FILTER: Check the Trie for selectivity
            if (!q.source.empty()) {
                bool is_prefix = (q.source.back() == '*');
                std::string key = is_prefix ? q.source.substr(0, q.source.size() - 1) : q.source;

                // Query the Trie with a "Cap" (limit) to avoid massive memory allocations
                auto result = trie_->GetTimestampsWithCap(
                    key, is_prefix, q.priority,
                    q.min_timestamp, q.max_timestamp,
                    THRESHOLD
                );

                // If the number of results is below our threshold (not overflowed)
                if (!result.is_overflow) {
                    candidates = result.timestamps;
                    use_sparse_scan = true;
                }
            }

            // EVALUATE HOST FILTER: Only run if Source didn't already produce a sparse plan
            if (!use_sparse_scan && !q.host.empty()) {
                bool is_prefix = (q.host.back() == '*');
                std::string key = is_prefix ? q.host.substr(0, q.host.size() - 1) : q.host;

                auto result = trie_->GetTimestampsWithCap(
                    key, is_prefix, q.priority,
                    q.min_timestamp, q.max_timestamp,
                    THRESHOLD
                );

                if (!result.is_overflow) {
                    candidates = result.timestamps;
                    use_sparse_scan = true;
                }
            }

            // --- STEP 2: EXECUTION ---
            std::vector<LogRecord> results;

            if (use_sparse_scan) {
                // [PLAN A] SPARSE LOOKUP: Point queries via the B+Tree
                // Optimal for surgical searches (e.g., "Finding specific error on 1 machine")
                for (int64_t ts : candidates) {
                    LogRecord rec;
                    if (btree_->GetValue(ts, rec)) {
                        // Verify all criteria (including message keywords) before adding
                        if (Matches(rec, q)) {
                            results.push_back(rec);
                        }
                    }
                }
            }
            else {
                // [PLAN B] SEQUENTIAL SCAN: Range scan via the B+Tree Leaf nodes
                // Optimal for broad queries or when indexes are missing (Full Table Scan)
                auto it = btree_->Begin(q.min_timestamp);

                while (!it.IsEnd()) {
                    const LogRecord& rec = *it;

                    // EXIT OPTIMIZATION: B+Tree is sorted by time; stop if we exceed range
                    if (rec.timestamp > q.max_timestamp) break;

                    if (Matches(rec, q)) {
                        results.push_back(rec);
                    }

                    ++it; // Move to next contiguous log entry
                }
            }

            return results;
        }

    private:
        index::BTreeIndex* btree_;
        index::TrieIndex* trie_;

        /**
         * Matches
         * Final verification function to ensure a record meets every Query constraint.
         */
        bool Matches(const LogRecord& rec, const Query& q) {
            // Check Priority mismatch
            if (q.priority != -1 && rec.priority != q.priority) return false;

            // Check Source (supports wildcard)
            if (!q.source.empty()) {
                if (!StringMatch(rec.source, q.source)) return false;
            }

            // Check Host (supports wildcard)
            if (!q.host.empty()) {
                if (!StringMatch(rec.host, q.host)) return false;
            }

            // Check Message Content (Substring/Greedy match)
            if (!q.message_contains.empty()) {
                if (strstr(rec.message, q.message_contains.c_str()) == nullptr) return false;
            }

            return true;
        }

        /**
         * StringMatch
         * Efficiently handles both exact matches and prefix-based wildcard matches.
         */
        bool StringMatch(const char* text, const std::string& pattern) {
            bool is_prefix = (pattern.back() == '*');
            size_t len = is_prefix ? pattern.size() - 1 : pattern.size();

            // Compare prefix first
            if (strncmp(text, pattern.c_str(), len) == 0) {
                if (is_prefix) return true;
                // If not a prefix, ensure the string isn't longer than the pattern
                return strlen(text) == len;
            }
            return false;
        }
    };

} // namespace cmse