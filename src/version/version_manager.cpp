#include "version_manager.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include "../common/logger.h"

namespace cmse {

    // --- CONSTRUCTOR: Establishes the link between metadata and the buffer pool ---
    VersionManager::VersionManager(const std::string& meta_file, bufferpool::BufferPoolManager* bpm)
        : meta_file_(meta_file), bpm_(bpm) {
        // Upon startup, immediately scan the .meta file to rebuild the version history cache
        LoadVersions();
    }

    // --- BEGIN TRANSACTION: The isolation entry point ---
    TransactionContext VersionManager::BeginTransaction() {
        // Thread safety: Ensure only one thread prepares a transaction at a time
        std::lock_guard<std::mutex> guard(latch_);
        TransactionContext txn;

        // Assign the next available version number; this remains "pending" until commit
        txn.version_id = next_version_id_;

        // Check if there is existing history to inherit from
        if (!versions_.empty()) {
            // 1. Inherit the B+Tree Root from the most recent successful commit
            txn.pending_root_id = versions_.rbegin()->second.root_page_id;

            // 2. Inherit the Trie Root (CRITICAL FIX): Ensures keyword index is continuous
            txn.pending_trie_root_id = versions_.rbegin()->second.trie_root_page_id;
        }
        else {
            // No previous versions found: initialize as an empty database
            txn.pending_root_id = INVALID_PAGE_ID;
            txn.pending_trie_root_id = INVALID_PAGE_ID;
        }

        return txn;
    }

    // --- COMMIT: The atomic "point of no return" ---
    void VersionManager::Commit(TransactionContext& txn, int64_t max_log_ts, size_t file_offset) {
        // Prevent concurrent commits from interleaving
        std::lock_guard<std::mutex> guard(latch_);

        LOG_DEBUG("[Version] Committing Txn: " << txn.version_id
            << " Root: " << txn.pending_root_id
            << " Offset: " << file_offset);

        // 1. PERSISTENCE STEP: Force all dirty pages (B+Tree/Trie nodes) from RAM to physical disk
        bpm_->FlushAllPages();

        // 2. PREPARE METADATA: Construct the record representing this point-in-time snapshot
        VersionMetadata meta;
        meta.version_id = txn.version_id;                // ID of this state
        meta.root_page_id = txn.pending_root_id;         // B+Tree Entry point
        meta.trie_root_page_id = txn.pending_trie_root_id; // Trie Entry point
        meta.max_log_ts = max_log_ts;                    // Highest log timestamp ingested
        meta.timestamp = std::time(nullptr);             // Physical commit time
        meta.file_offset = file_offset;                  // Bookmark for log file resume

        LOG_DEBUG("[Version] Meta Created. Meta.TrieRoot=" << meta.trie_root_page_id);

        // 3. UPDATE MEMORY & DISK: Finalize the commit
        versions_[meta.version_id] = meta; // Add to in-memory history map

        // Advance the global version counter
        if (meta.version_id >= next_version_id_) next_version_id_ = meta.version_id + 1;

        // Persist the metadata record itself to the side-car file
        AppendVersionToDisk(meta);

        LOG_DEBUG("[Version] Commit Complete.");
    }

    // --- LOAD VERSIONS: Recovery logic for system restart ---
    void VersionManager::LoadVersions() {
        std::lock_guard<std::mutex> guard(latch_);
        versions_.clear();

        // Open metadata file in binary mode
        std::ifstream in(meta_file_, std::ios::binary);
        if (!in.is_open()) return; // If file doesn't exist, we assume a fresh database

        VersionMetadata meta;
        // Read fixed-size structs sequentially from the file
        while (in.read(reinterpret_cast<char*>(&meta), sizeof(VersionMetadata))) {
            versions_[meta.version_id] = meta; // Populate the history map
            LOG_DEBUG("[Version] Loaded v" << meta.version_id << " TrieRoot=" << meta.trie_root_page_id);

            if (!versions_.empty()) {
                LOG_DEBUG("[Version] Final State: Latest TrieRoot=" << versions_.rbegin()->second.trie_root_page_id);
            }
            else {
                LOG_DEBUG("[Version] No versions loaded (Fresh Start).");
            }

            // Sync the next_version_id_ to be higher than anything found on disk
            if (meta.version_id >= next_version_id_) {
                next_version_id_ = meta.version_id + 1;
            }
        }

        if (!versions_.empty()) {
            std::cout << "[VersionManager] Recovered " << versions_.size()
                << " versions. Head is v" << versions_.rbegin()->first << std::endl;
        }
    }

    // --- APPEND TO DISK: Low-level file serialization ---
    void VersionManager::AppendVersionToDisk(const VersionMetadata& meta) {
        // Open file in Append mode so we never overwrite previous version history
        std::ofstream out(meta_file_, std::ios::binary | std::ios::app);
        // Cast the struct to a byte array and write the raw memory to disk
        out.write(reinterpret_cast<const char*>(&meta), sizeof(VersionMetadata));
        out.flush(); // Ensure the data leaves the application buffer
    }

    // --- GETTERS: Accessors for the current head state ---

    // Returns the ID of the most recent commit
    version_id_t VersionManager::GetLatestVersionId() const {
        if (versions_.empty()) return 0;
        return versions_.rbegin()->first;
    }

    // Returns the root page of the latest B+Tree state
    page_id_t VersionManager::GetLatestRootPageId() const {
        if (versions_.empty()) return INVALID_PAGE_ID;
        return versions_.rbegin()->second.root_page_id;
    }

    // Returns the root page of the latest Trie state
    page_id_t VersionManager::GetLatestTrieRootPageId() const {
        if (versions_.empty()) return INVALID_PAGE_ID;
        return versions_.rbegin()->second.trie_root_page_id;
    }

    // Returns the high-water mark timestamp for data ingestion tracking
    int64_t VersionManager::GetLastCommittedLogTimestamp() const {
        if (versions_.empty()) return -1;
        return versions_.rbegin()->second.max_log_ts;
    }

    // Time-Travel: Returns the B+Tree root for any specific historical version
    page_id_t VersionManager::GetRootForVersion(version_id_t v_id) const {
        auto it = versions_.find(v_id);
        if (it != versions_.end()) {
            return it->second.root_page_id;
        }
        return INVALID_PAGE_ID;
    }

} // namespace cmse