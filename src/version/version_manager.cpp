#include "version_manager.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include "../common/logger.h"

namespace cmse {

    VersionManager::VersionManager(const std::string& meta_file, bufferpool::BufferPoolManager* bpm)
        : meta_file_(meta_file), bpm_(bpm) {
        LoadVersions();
    }

    TransactionContext VersionManager::BeginTransaction() {
        std::lock_guard<std::mutex> guard(latch_);
        TransactionContext txn;
        txn.version_id = next_version_id_; // Note: We don't increment yet. Only on commit.

        // Resume from latest (if exists), else start clean
        if (!versions_.empty()) {

            // 1. Inherit B+Tree Root
            txn.pending_root_id = versions_.rbegin()->second.root_page_id;

            // 2. Inherit Trie Root (CRITICAL FIX)
            txn.pending_trie_root_id = versions_.rbegin()->second.trie_root_page_id;
        }
        else {
            txn.pending_root_id = INVALID_PAGE_ID;
            txn.pending_trie_root_id = INVALID_PAGE_ID;
        }

        return txn;
    }

    // In src/version/version_manager.cpp

    // Update the function implementation
    void VersionManager::Commit(TransactionContext& txn, int64_t max_log_ts, size_t file_offset) {
        std::lock_guard<std::mutex> guard(latch_);

        LOG_DEBUG("[Version] Committing Txn: " << txn.version_id
            << " Root: " << txn.pending_root_id
            << " Offset: " << file_offset);

        // 1. Flush to disk (User confirmed this works now)
        bpm_->FlushAllPages();

        // 2. Prepare Metadata
        VersionMetadata meta;
        meta.version_id = txn.version_id;
        meta.root_page_id = txn.pending_root_id;
        meta.trie_root_page_id = txn.pending_trie_root_id;
        meta.max_log_ts = max_log_ts;
        meta.timestamp = std::time(nullptr);
        meta.file_offset = file_offset; // <--- Store the offset

        LOG_DEBUG("[Version] Meta Created. Meta.TrieRoot=" << meta.trie_root_page_id);

        // 3. Update Memory & Disk
        versions_[meta.version_id] = meta;
        if (meta.version_id >= next_version_id_) next_version_id_ = meta.version_id + 1;

        AppendVersionToDisk(meta);

        LOG_DEBUG("[Version] Commit Complete.");
    }

    void VersionManager::LoadVersions() {
        std::lock_guard<std::mutex> guard(latch_);
        versions_.clear();

        std::ifstream in(meta_file_, std::ios::binary);
        if (!in.is_open()) return; // No file = Fresh DB

        VersionMetadata meta;
        // Read struct by struct
        while (in.read(reinterpret_cast<char*>(&meta), sizeof(VersionMetadata))) {
            versions_[meta.version_id] = meta;
            LOG_DEBUG("[Version] Loaded v" << meta.version_id << " TrieRoot=" << meta.trie_root_page_id);
            if (!versions_.empty()) {
                LOG_DEBUG("[Version] Final State: Latest TrieRoot=" << versions_.rbegin()->second.trie_root_page_id);
            }
            else {
                LOG_DEBUG("[Version] No versions loaded (Fresh Start).");
            }
            if (meta.version_id >= next_version_id_) {
                next_version_id_ = meta.version_id + 1;
            }
        }

        if (!versions_.empty()) {
            std::cout << "[VersionManager] Recovered " << versions_.size()
                << " versions. Head is v" << versions_.rbegin()->first << std::endl;
        }
    }

    void VersionManager::AppendVersionToDisk(const VersionMetadata& meta) {
        // Open in Append + Binary mode
        std::ofstream out(meta_file_, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char*>(&meta), sizeof(VersionMetadata));
        out.flush(); // Force write to OS buffer
        // In a real DB, we would also call fsync() here.
    }

    // --- Getters ---

    version_id_t VersionManager::GetLatestVersionId() const {
        if (versions_.empty()) return 0;
        return versions_.rbegin()->first;
    }

    page_id_t VersionManager::GetLatestRootPageId() const {
        if (versions_.empty()) return INVALID_PAGE_ID;
        return versions_.rbegin()->second.root_page_id;
    }

    page_id_t VersionManager::GetLatestTrieRootPageId() const {
        if (versions_.empty()) return INVALID_PAGE_ID;
        return versions_.rbegin()->second.trie_root_page_id;
    }

    int64_t VersionManager::GetLastCommittedLogTimestamp() const {
        if (versions_.empty()) return -1;
        return versions_.rbegin()->second.max_log_ts;
    }

    page_id_t VersionManager::GetRootForVersion(version_id_t v_id) const {
        auto it = versions_.find(v_id);
        if (it != versions_.end()) {
            return it->second.root_page_id;
        }
        return INVALID_PAGE_ID;
    }

} // namespace cmse