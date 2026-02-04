#include "../src/index/btree_index.h"
#include "../src/index/trie_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

using namespace cmse;

// Helper to print a single record
void PrintRecord(index::BTreeIndex* btree, int64_t timestamp) {
    std::vector<LogRecord> result;
    // We scan a tiny range [timestamp, timestamp] to fetch the specific record
    // In a real system, we would have a PointQuery function.
    result = btree->Scan(timestamp, timestamp);

    if (!result.empty()) {
        std::cout << "   Found: " << result[0].toString()
            << " [Priority: " << result[0].priority << "]"
            << " [Source: " << result[0].source << "]" << std::endl;
    }
}

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    if (!std::ifstream(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "Database or Metadata missing. Run integration_test first." << std::endl;
        return 1;
    }

    // 1. Load Metadata (Root IDs)
    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    // 2. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    auto* btree = new index::BTreeIndex(bpm);
    auto* source_idx = new index::TrieIndex(bpm);
    auto* host_idx = new index::TrieIndex(bpm);

    // Manually set root IDs (Backdoor for Phase 4)
    // In Phase 5, the Catalog will handle this.
    // We assume your Index classes allow setting root_id via constructor or friend class.
    // IF NOT, you might need to add `void SetRootId(page_id_t)` to your Index classes temporarily.
    // For now, let's assume we can modify the class or you add a setter.
    // *Implementation Note*: You need to add SetRootPageId() to your headers if not present.
    btree->SetRootPageId(btree_root);
    source_idx->SetRootPageId(source_root);
    host_idx->SetRootPageId(host_root);

    std::cout << "=== CMSE Query Engine (Phase 4) ===" << std::endl;
    std::cout << "Commands: " << std::endl;
    std::cout << "  source <name>          -> Find by Source" << std::endl;
    std::cout << "  host <name>            -> Find by Host" << std::endl;
    std::cout << "  prio <level> <source>  -> Find Source with min Priority" << std::endl;
    std::cout << "  exit                   -> Quit" << std::endl;

    std::string line, cmd, arg1;
    int arg2;

    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, line);
        std::stringstream ss(line);
        ss >> cmd;

        if (cmd == "exit") break;

        if (cmd == "source") {
            ss >> arg1;
            auto results = source_idx->SearchPrefix(arg1); // Use Prefix Search
            std::cout << "Found " << results.size() << " IDs in Index." << std::endl;

            // Fetch first 5 results from B+Tree to show data
            int count = 0;
            for (const auto& entry : results) {
                if (count++ >= 5) break;
                PrintRecord(btree, entry.timestamp);
            }
        }
        else if (cmd == "host") {
            ss >> arg1;
            auto results = host_idx->SearchPrefix(arg1);
            std::cout << "Found " << results.size() << " IDs in Index." << std::endl;

            int count = 0;
            for (const auto& entry : results) {
                if (count++ >= 5) break;
                PrintRecord(btree, entry.timestamp);
            }
        }
        else if (cmd == "prio") {
            // Composite Query: Source = X AND Priority >= Y
            ss >> arg2 >> arg1; // priority, source_name

            auto full_list = source_idx->Search(arg1);
            int matches = 0;

            std::cout << "Filtering " << full_list.size() << " records for Priority >= " << arg2 << "..." << std::endl;

            for (const auto& entry : full_list) {
                // LIGHTWEIGHT FILTER: We check priority BEFORE touching B+Tree
                if (entry.log_level >= arg2) {
                    matches++;
                    if (matches <= 5) PrintRecord(btree, entry.timestamp);
                }
            }
            std::cout << "Total Matches: " << matches << std::endl;
        }
    }

    delete btree; delete source_idx; delete host_idx;
    delete bpm; delete disk;
    return 0;
}