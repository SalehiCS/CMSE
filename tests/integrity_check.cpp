#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/btree_index.h"
#include "../src/index/trie_index.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>

using namespace cmse;

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    // 1. Load Metadata
    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    if (!meta_in) { std::cerr << "Meta missing." << std::endl; return 1; }
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    // 2. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    auto* btree = new index::BTreeIndex(bpm);
    btree->SetRootPageId(btree_root);

    auto* source_trie = new index::TrieIndex(bpm);
    source_trie->SetRootPageId(source_root);

    std::cout << "=== INTEGRITY CHECK START ===" << std::endl;

    // 3. Get ALL keys from Source Trie (using Prefix "")
    // This fetches every single record indexed by Source
    std::cout << "[1/3] Fetching all records from Source Trie..." << std::endl;
    auto all_logs = source_trie->SearchPrefix("");
    std::cout << "      -> Found " << all_logs.size() << " index entries." << std::endl;

    if (all_logs.empty()) {
        std::cerr << "[FAILURE] Trie is empty!" << std::endl;
        return 1;
    }

    // 4. Verify against B+Tree
    std::cout << "[2/3] Verifying existence in B+Tree..." << std::endl;
    int missing_count = 0;
    int checked_count = 0;

    for (const auto& entry : all_logs) {
        // Try to find this timestamp in B+Tree
        // We use Scan(ts, ts) for point query
        std::vector<LogRecord> result = btree->Scan(entry.timestamp, entry.timestamp);

        if (result.empty()) {
            if (missing_count < 5) {
                std::cout << "      [ERROR] TS " << entry.timestamp
                    << " found in Trie but MISSING in B+Tree!" << std::endl;
            }
            missing_count++;
        }

        checked_count++;
        if (checked_count % 5000 == 0) std::cout << "      ... checked " << checked_count << " records" << std::endl;
    }

    // 5. Report
    std::cout << "=== REPORT ===" << std::endl;
    if (missing_count == 0) {
        std::cout << "[SUCCESS] Database is 100% CONSISTENT." << std::endl;
        std::cout << "          All " << all_logs.size() << " Trie entries point to valid B+Tree records." << std::endl;
    }
    else {
        std::cout << "[FAILURE] Integrity Violation!" << std::endl;
        std::cout << "          " << missing_count << " records are orphaned (Index points to nothing)." << std::endl;
    }

    delete btree;
    delete source_trie;
    delete bpm;
    delete disk;

    return missing_count == 0 ? 0 : 1;
}