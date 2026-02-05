#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/btree_index.h"
#include "../src/index/trie_index.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

using namespace cmse;

int main() {
    const std::string DB_FILE = "integrity_test.db";
    const std::string META_FILE = "integrity_test.meta";

    // 1. CLEANUP (Start Fresh)
    std::remove(DB_FILE.c_str());
    std::remove(META_FILE.c_str());

    // 2. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(5000, disk);
    auto* btree = new index::BTreeIndex(bpm);
    auto* source_trie = new index::TrieIndex(bpm);

    std::cout << "=== INTEGRITY CHECK START ===" << std::endl;

    // 3. GENERATE DATA
    std::cout << "[0/3] Generating 2,000 records..." << std::endl;
    for (int i = 0; i < 2000; i++) {
        LogRecord rec;
        rec.timestamp = i;
        rec.priority = 1;
        std::string src = (i % 2 == 0) ? "nginx" : "mysql";
        strncpy_s(rec.source, src.c_str(), _TRUNCATE);

        btree->Insert(i, rec);
        source_trie->Insert(src, i, 1);
    }

    // 4. VERIFY
    std::cout << "[1/3] Fetching all records from Source Trie..." << std::endl;
    auto all_logs = source_trie->SearchPrefix("");

    if (all_logs.size() != 2000) {
        std::cerr << "[FAIL] Count Mismatch! Expected 2000, Got " << all_logs.size() << std::endl;
        return 1;
    }

    std::cout << "[2/3] Checking B+Tree links..." << std::endl;
    int missing = 0;
    for (const auto& entry : all_logs) {
        std::vector<LogRecord> result = btree->Scan(entry.timestamp, entry.timestamp);
        if (result.empty()) missing++;
    }

    if (missing == 0) std::cout << "[SUCCESS] Integrity Verified." << std::endl;
    else std::cout << "[FAIL] " << missing << " broken links." << std::endl;

    delete btree; delete source_trie; delete bpm; delete disk;
    return missing == 0 ? 0 : 1;
}