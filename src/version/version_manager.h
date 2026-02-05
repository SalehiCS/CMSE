#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include "../common/types.h"
#include "../bufferpool/buffer_pool_manager.h"
#include "transaction_context.h"

namespace cmse {

    /**
     * VersionMetadata
     * The 24-byte record we write to "versions.meta" for every commit.
     */
    struct VersionMetadata {
        version_id_t version_id;
        page_id_t root_page_id;
        page_id_t trie_root_page_id;
        int64_t timestamp;     // Wall clock time
        int64_t max_log_ts;    // The highest Log Timestamp ingested (for Resume)
        size_t file_offset;
    };

    class VersionManager {
    public:
        /**
         * @param meta_file path to the side-car file (e.g., "my_db.meta")
         * @param bpm needed to Flush data pages before committing
         */
        VersionManager(const std::string& meta_file, bufferpool::BufferPoolManager* bpm);

        /**
         * Start a new Transaction.
         * Automatically sets the 'pending_root' to the latest committed root.
         */
        TransactionContext BeginTransaction();

        /**
         * Commit the transaction.
         * 1. Flushes all new shadow pages to disk.
         * 2. Appends the VersionMetadata to the meta file.
         * @param max_log_ts The highest timestamp processed in this batch (for resume).
         */
         // Update Commit signature
        void Commit(TransactionContext& txn, int64_t max_log_ts, size_t file_offset);

        // Add Getter
        size_t GetLastFileOffset() const {
            if (versions_.empty()) return 0;
            return versions_.rbegin()->second.file_offset;
        }

        version_id_t GetLatestVersionId() const;

        page_id_t GetLatestRootPageId() const;
        page_id_t GetLatestTrieRootPageId() const;

        int64_t GetLastCommittedLogTimestamp() const;

        // Get specific version (Time Travel)
        page_id_t GetRootForVersion(version_id_t v_id) const;

    private:
        void LoadVersions();
        void AppendVersionToDisk(const VersionMetadata& meta);

        std::string meta_file_;
        bufferpool::BufferPoolManager* bpm_;

        std::map<version_id_t, VersionMetadata> versions_; // Cache of history
        version_id_t next_version_id_ = 1;

        std::mutex latch_;
    };

} // namespace cmse