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
     * Represents a single snapshot of the entire database state.
     * This 48-byte struct (on 64-bit systems) is the atomic unit of persistence
     * for the version history file (.meta).
     */
    struct VersionMetadata {
        version_id_t version_id;     // Sequential ID of the commit (1, 2, 3...)
        page_id_t root_page_id;      // The physical Page ID of the B+Tree root
        page_id_t trie_root_page_id; // The physical Page ID of the Trie root
        int64_t timestamp;           // System wall-clock time at commit
        int64_t max_log_ts;          // High-water mark of ingested log timestamps
        size_t file_offset;          // Byte offset in the source log file (for resume)
    };

    /**
     * VersionManager
     * Orchestrates the commitment of transactions and manages the lifecycle
     * of database versions. It ensures Atomicity and Durability (the A and D in ACID).
     */
    class VersionManager {
    public:
        /**
         * Constructor
         * @param meta_file Path to the persistent metadata file (e.g., "storage.meta").
         * @param bpm The BufferPoolManager used to flush dirty pages during commit.
         */
        VersionManager(const std::string& meta_file, bufferpool::BufferPoolManager* bpm);

        /**
         * BeginTransaction
         * Initializes a new private sandbox for modifications.
         * It captures the current committed roots to provide a consistent view
         * of the database as a starting point.
         * @return A TransactionContext ready for writing.
         */
        TransactionContext BeginTransaction();

        /**
         * Commit
         * Finalizes a transaction by making its shadow pages permanent.
         * 1. Flushes all modified pages in the txn to the physical disk.
         * 2. Persists a new VersionMetadata entry to the meta_file.
         * 3. Updates the in-memory history cache.
         * @param txn The transaction context to commit.
         * @param max_log_ts The highest log timestamp successfully processed.
         * @param file_offset The current read-position in the source log file.
         */
        void Commit(TransactionContext& txn, int64_t max_log_ts, size_t file_offset);

        /**
         * GetLastFileOffset
         * Retrieves the log file position of the most recent successful commit.
         * Used to resume ingestion without re-reading the entire log file.
         */
        size_t GetLastFileOffset() const {
            if (versions_.empty()) return 0;
            return versions_.rbegin()->second.file_offset; // Access latest entry in the map
        }

        /**
         * GetLatestVersionId
         * Returns the ID of the most recently committed version.
         */
        version_id_t GetLatestVersionId() const;

        /**
         * GetLatestRootPageId
         * Returns the entry point for the B+Tree of the current state.
         */
        page_id_t GetLatestRootPageId() const;

        /**
         * GetLatestTrieRootPageId
         * Returns the entry point for the Trie of the current state.
         */
        page_id_t GetLatestTrieRootPageId() const;

        /**
         * GetLastCommittedLogTimestamp
         * Returns the max_log_ts of the latest version for high-water mark checks.
         */
        int64_t GetLastCommittedLogTimestamp() const;

        /**
         * GetRootForVersion
         * Facilitates "Time Travel" queries.
         * @param v_id The historical version ID requested.
         * @return The Page ID of the B+Tree root at that point in time.
         */
        page_id_t GetRootForVersion(version_id_t v_id) const;

    private:
        /**
         * LoadVersions
         * Bootstraps the manager by reading the .meta file from disk on startup.
         */
        void LoadVersions();

        /**
         * AppendVersionToDisk
         * Directly writes the VersionMetadata struct to the end of the meta file.
         */
        void AppendVersionToDisk(const VersionMetadata& meta);

        std::string meta_file_;                   // Path to the side-car metadata file
        bufferpool::BufferPoolManager* bpm_;      // Pointer to the global Buffer Pool

        std::map<version_id_t, VersionMetadata> versions_; // In-memory map of version history
        version_id_t next_version_id_ = 1;        // Counter for the next commit

        mutable std::mutex latch_;                // Latch to protect the manager during concurrent commits
    };

} // namespace cmse