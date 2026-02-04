#include "version_manager.h"
#include <fstream>
#include <iostream>
#include <ctime>

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
            txn.pending_root_id = versions_.rbegin()->second.root_page_id;
        }
        else {
            txn.pending_root_id = INVALID_PAGE_ID;
        }

        return txn;
    }

    void VersionManager::Commit(TransactionContext& txn, int64_t max_log_ts) {
        std::lock_guard<std::mutex> guard(latch_);

        // 1. SAFETY CRITICAL: Flush Data Pages First!
        // If we write the commit record but the data pages are still only in RAM,
        // a crash now would corrupt the DB.
        for (page_id_t pid : txn.created_pages) {
            bpm_->FlushPage(pid);
        }

        // 2. Prepare Metadata
        VersionMetadata meta;
        meta.version_id = txn.version_id;
        meta.root_page_id = txn.pending_root_id;
        meta.max_log_ts = max_log_ts;
        meta.timestamp = std::time(nullptr);

        // 3. Update Memory State
        versions_[meta.version_id] = meta;

        // Ensure next ID is always higher than what we just committed
        if (meta.version_id >= next_version_id_) {
            next_version_id_ = meta.version_id + 1;
        }

        // 4. Persist Commit Record
        AppendVersionToDisk(meta);

        // Clear transaction state (optional safety)
        txn.created_pages.clear();
        txn.shadow_map.clear();

        std::cout << "[VersionManager] Committed v" << meta.version_id
            << " | Root: " << meta.root_page_id
            << " | MaxLogTS: " << max_log_ts << std::endl;
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