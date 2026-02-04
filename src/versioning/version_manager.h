#pragma once

#include <string>
#include <fstream>
#include <map>
#include <iostream>
#include "../common/types.h"
#include "../bufferpool/buffer_pool_manager.h"
#include "transaction_context.h"

namespace cmse {

    // Metadata for a single committed version
    struct VersionMetadata {
        version_id_t version_id;
        page_id_t root_page_id;
        int64_t timestamp;     // System time of commit
        int64_t max_log_ts;    // The highest Log Timestamp (Key) in this version
        // ^^^ THIS enables Resume-on-Crash!
    };

    class VersionManager {
    public:
        VersionManager(const std::string& meta_file, bufferpool::BufferPoolManager* bpm)
            : meta_file_(meta_file), bpm_(bpm) {
            LoadVersions();
        }

        // --- Core Operations ---

        // 1. Start a new transaction
        TransactionContext BeginTransaction() {
            TransactionContext txn;
            txn.version_id = next_version_id_++;
            // If we have a previous version, start from its root.
            // If not, start from scratch (INVALID).
            if (!versions_.empty()) {
                txn.pending_root_id = versions_.rbegin()->second.root_page_id;
            }
            else {
                txn.pending_root_id = INVALID_PAGE_ID;
            }
            return txn;
        }

        // 2. Commit the transaction (Persist state)
        void Commit(TransactionContext& txn, int64_t max_log_ts) {
            // Create metadata
            VersionMetadata meta;
            meta.version_id = txn.version_id;
            meta.root_page_id = txn.pending_root_id;
            meta.max_log_ts = max_log_ts;
            meta.timestamp = std::time(nullptr); // Current wall time

            // Update In-Memory Map
            versions_[meta.version_id] = meta;

            // Update "Next Version" counter
            if (meta.version_id >= next_version_id_) {
                next_version_id_ = meta.version_id + 1;
            }

            // Persist to Disk immediately
            AppendVersionToDisk(meta);

            std::cout << "[VersionManager] Committed Version " << meta.version_id
                << " (Root: " << meta.root_page_id
                << ", MaxTS: " << max_log_ts << ")" << std::endl;
        }

        // --- Resume Logic ---

        // Returns the latest committed max_timestamp. 
        // If db is empty, returns -1.
        int64_t GetLastCommittedTimestamp() const {
            if (versions_.empty()) return -1;
            return versions_.rbegin()->second.max_log_ts;
        }

        page_id_t GetLatestRoot() const {
            if (versions_.empty()) return INVALID_PAGE_ID;
            return versions_.rbegin()->second.root_page_id;
        }

    private:
        std::string meta_file_;
        bufferpool::BufferPoolManager* bpm_;
        std::map<version_id_t, VersionMetadata> versions_; // Ordered by ID
        version_id_t next_version_id_ = 1;

        // --- Persistence Helpers ---

        void LoadVersions() {
            std::ifstream in(meta_file_, std::ios::binary);
            if (!in.is_open()) return;

            VersionMetadata meta;
            while (in.read(reinterpret_cast<char*>(&meta), sizeof(VersionMetadata))) {
                versions_[meta.version_id] = meta;
                if (meta.version_id >= next_version_id_) {
                    next_version_id_ = meta.version_id + 1;
                }
            }
            if (!versions_.empty()) {
                std::cout << "[VersionManager] Loaded " << versions_.size()
                    << " versions. Latest is v" << versions_.rbegin()->first << std::endl;
            }
        }

        void AppendVersionToDisk(const VersionMetadata& meta) {
            std::ofstream out(meta_file_, std::ios::binary | std::ios::app);
            out.write(reinterpret_cast<const char*>(&meta), sizeof(VersionMetadata));
            out.flush(); // Ensure it hits the disk!
        }
    };

} // namespace cmse