#pragma once

#include <vector>
#include <memory>
#include <string>
#include <algorithm>

#include "../common/types.h"
#include "../index/btree_index.h"
#include "../index/btree_iterator.h"

namespace cmse {

    /**
     * QueryCursor
     * ---------------------------------------------------------
     * Represents the active state of a running query.
     * Replaces std::vector<LogRecord> to prevent memory explosions.
     * Fetches one record at a time (Lazy Loading).
     */
    class QueryCursor {
    public:
        enum class Mode { SEQUENTIAL_SCAN, SPARSE_LOOKUP };

        // --- CONSTRUCTOR FOR PLAN A (SPARSE / INDEX SCAN) ---
        QueryCursor(index::BTreeIndex* btree, std::vector<int64_t> candidates, Query q)
            : mode_(Mode::SPARSE_LOOKUP),
            btree_(btree),
            candidates_(std::move(candidates)),
            query_(std::move(q)),
            current_idx_(0) {
        }

        // --- CONSTRUCTOR FOR PLAN B (SEQUENTIAL / RANGE SCAN) ---
        // Takes ownership of the BTreeIterator
        QueryCursor(index::BTreeIterator&& iter, Query q)
            : mode_(Mode::SEQUENTIAL_SCAN),
            iter_ptr_(std::make_unique<index::BTreeIterator>(std::move(iter))),
            query_(std::move(q)),
            btree_(nullptr),
            current_idx_(0) {
        }

        /**
         * Next
         * ---------------------------------------------------------
         * Advances the cursor to the next matching record.
         * @param out_rec: The record object to fill if found.
         * @return true if a record was found, false if End-Of-Results.
         */
        bool Next(LogRecord& out_rec) {
            if (mode_ == Mode::SPARSE_LOOKUP) {
                return NextSparse(out_rec);
            }
            else {
                return NextSequential(out_rec);
            }
        }

    private:
        Mode mode_;
        Query query_;

        // --- STATE FOR SPARSE LOOKUP ---
        index::BTreeIndex* btree_;
        std::vector<int64_t> candidates_;
        size_t current_idx_;

        // --- STATE FOR SEQUENTIAL SCAN ---
        // Wrapped in unique_ptr because BTreeIterator might not be default-constructible
        std::unique_ptr<index::BTreeIterator> iter_ptr_;

        // --- FILTERING LOGIC ---
        bool Matches(const LogRecord& r) {
            // 1. Timestamp (Implicitly handled by BTree, but good for sanity)
            if (r.timestamp < query_.min_timestamp || r.timestamp > query_.max_timestamp) return false;

            // 2. Priority
            if (query_.priority != -1 && r.priority != query_.priority) return false;

            // 3. Source (Prefix or Exact)
            if (!query_.source.empty()) {
                if (query_.source.back() == '*') {
                    // Prefix Match
                    size_t len = query_.source.size() - 1;
                    if (std::strncmp(r.source, query_.source.c_str(), len) != 0) return false;
                }
                else {
                    // Exact Match
                    if (std::strcmp(r.source, query_.source.c_str()) != 0) return false;
                }
            }

            // 4. Host (Prefix or Exact)
            if (!query_.host.empty()) {
                if (query_.host.back() == '*') {
                    size_t len = query_.host.size() - 1;
                    if (std::strncmp(r.host, query_.host.c_str(), len) != 0) return false;
                }
                else {
                    if (std::strcmp(r.host, query_.host.c_str()) != 0) return false;
                }
            }

            // 5. Message Content (Substring)
            if (!query_.message_contains.empty()) {
                // simple strstr for substring search
                if (std::strstr(r.message, query_.message_contains.c_str()) == nullptr) return false;
            }

            return true;
        }

        bool NextSparse(LogRecord& out_rec) {
            while (current_idx_ < candidates_.size()) {
                int64_t ts = candidates_[current_idx_++];

                // Fetch Point Lookup
                if (btree_->GetValue(ts, out_rec)) {
                    if (Matches(out_rec)) {
                        return true; // Found valid record
                    }
                }
            }
            return false; // Exhausted candidates
        }

        bool NextSequential(LogRecord& out_rec) {
            if (!iter_ptr_) return false;

            while (!iter_ptr_->IsEnd()) {
                // Get current
                out_rec = *(*iter_ptr_);

                // EXIT OPTIMIZATION: B+Tree is sorted
                if (out_rec.timestamp > query_.max_timestamp) return false;

                // Advance Iterator
                ++(*iter_ptr_);

                // Check Match
                if (Matches(out_rec)) {
                    return true;
                }
            }
            return false;
        }
    };

} // namespace cmse