#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../common/types.h"

namespace cmse {

    struct Query {
        int64_t min_timestamp = 0;
        int64_t max_timestamp = INT64_MAX;
        int32_t priority = -1; // -1 = Any

        std::string source; // Empty = Any. Ends with '*' = Prefix.
        std::string host;   // Empty = Any. Ends with '*' = Prefix.
        std::string message_contains;
    };

    class QueryEngine {
    public:
        QueryEngine(index::BTreeIndex* btree, index::TrieIndex* trie)
            : btree_(btree), trie_(trie) {
        }

        std::vector<LogRecord> Execute(const Query& q) {
            std::vector<int64_t> candidates;
            bool use_sparse_scan = false;

            // --- STEP 1: COST ESTIMATION (Race the Indexes) ---

            // We use a threshold of 100. If we find fewer than 100 matching IDs in the Trie,
            // we skip the B+Tree Scan.
            const size_t THRESHOLD = 100;

            // Try Source Index
            if (!q.source.empty()) {
                bool is_prefix = (q.source.back() == '*');
                std::string key = is_prefix ? q.source.substr(0, q.source.size() - 1) : q.source;

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

            // Try Host Index (Only if Source didn't already give us a good plan)
            if (!use_sparse_scan && !q.host.empty()) {
                bool is_prefix = (q.host.back() == '*');
                std::string key = is_prefix ? q.host.substr(0, q.host.size() - 1) : q.host;

                auto result = trie_->GetTimestampsWithCap(
                    key, is_prefix, q.priority,
                    q.min_timestamp, q.max_timestamp,
                    THRESHOLD
                );

                if (!result.is_overflow) {
                    // If Source failed but Host succeeded, use Host candidates
                    candidates = result.timestamps;
                    use_sparse_scan = true;
                }
            }

            // --- STEP 2: EXECUTION ---
            std::vector<LogRecord> results;

            if (use_sparse_scan) {
                // [PLAN A] Sparse Lookup (Random I/O but few records)
                for (int64_t ts : candidates) {
                    LogRecord rec;
                    if (btree_->GetValue(ts, rec)) {
                        if (Matches(rec, q)) {
                            results.push_back(rec);
                        }
                    }
                }
            }
            else {
                // [PLAN B] Sequential Scan (Sequential I/O)
                // Used if filters are "Dense" (Overflow) or no filters exist.
                auto it = btree_->Begin(q.min_timestamp);

                while (!it.IsEnd()) {
                    const LogRecord& rec = *it;

                    // Stop Optimization
                    if (rec.timestamp > q.max_timestamp) break;

                    if (Matches(rec, q)) {
                        results.push_back(rec);
                    }

                    ++it;
                }
            }

            return results;
        }

    private:
        index::BTreeIndex* btree_;
        index::TrieIndex* trie_;

        bool Matches(const LogRecord& rec, const Query& q) {
            // Priority
            if (q.priority != -1 && rec.priority != q.priority) return false;

            // Source
            if (!q.source.empty()) {
                if (!StringMatch(rec.source, q.source)) return false;
            }

            // Host
            if (!q.host.empty()) {
                if (!StringMatch(rec.host, q.host)) return false;
            }

            // Message (Substring)
            if (!q.message_contains.empty()) {
                if (strstr(rec.message, q.message_contains.c_str()) == nullptr) return false;
            }

            return true;
        }

        // Helper for Wildcard Logic
        bool StringMatch(const char* text, const std::string& pattern) {
            bool is_prefix = (pattern.back() == '*');
            size_t len = is_prefix ? pattern.size() - 1 : pattern.size();

            if (strncmp(text, pattern.c_str(), len) == 0) {
                if (is_prefix) return true;
                return strlen(text) == len; // Exact match length check
            }
            return false;
        }
    };

} // namespace cmse