#include "../src/index/btree_index.h"
#include "../src/index/btree_iterator.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/btree_index.h"
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>

using namespace cmse;
using namespace cmse::index;

// --- Linux-Style Pager Function ---
void RunPagerQuery(BTreeIndex* index, int64_t start_ts) {
    std::cout << "\n>>> PAGER STARTED (Seeking to " << start_ts << ") <<<\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "   TIMESTAMP      | PID  | SOURCE       | MESSAGE\n";
    std::cout << "------------------------------------------------------------\n";

    // 1. Initialize Iterator
    auto it = index->Begin(start_ts);

    if (it.IsEnd()) {
        std::cout << "[INFO] No records found starting from " << start_ts << ".\n";
        return;
    }

    int count = 0;
    const int PAGE_SIZE = 20; // Rows per page
    bool quit = false;

    while (!it.IsEnd() && !quit) {
        // 2. Fetch Current Record
        LogRecord rec = it.Current();

        // 3. Print Formatted Row
        std::cout << "[" << std::setw(14) << rec.timestamp << "] | "
            << std::setw(4) << rec.pid << " | "
            << std::setw(12) << std::string(rec.source) << " | "
            << rec.message << "\n";

        // 4. Move to Next
        ++it;
        count++;

        // 5. Paging Logic
        if (count % PAGE_SIZE == 0) {
            std::cout << "------------------------------------------------------------\n";
            std::cout << "--More-- (Press ENTER for next page, 'q' to quit, 'j' to jump): ";

            std::string input;
            std::getline(std::cin, input);

            if (input == "q") {
                quit = true;
            }
            else if (input == "j") {
                std::cout << "Enter new timestamp to jump to: ";
                int64_t jump_ts;
                std::cin >> jump_ts;
                std::cin.ignore(); // Consume newline

                // Re-initialize iterator to new spot
                it = index->Begin(jump_ts);
                count = 0; // Reset page counter
                std::cout << "\n>>> JUMPING TO " << jump_ts << " <<<\n";
            }
        }
    }

    std::cout << "------------------------------------------------------------\n";
    std::cout << ">>> END OF RESULTS <<<\n";
}

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    // 1. Setup Environment
    if (!std::ifstream(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "[FATAL] 'huge_storage.db' or 'cmse.meta' not found!\n";
        std::cerr << "        Please run 'integration_test' first to generate data.\n";
        return 1;
    }

    std::cout << "[Init] Loading Disk Manager...\n";
    auto* disk = new disk::DiskManager(DB_FILE);

    std::cout << "[Init] Allocating Buffer Pool (50MB)...\n";
    auto* bpm = new bufferpool::BufferPoolManager(12500, disk); // 50MB

    std::cout << "[Init] Loading B+Tree Index...\n";
    auto* index = new BTreeIndex(bpm);

    // 2. Load Root from Metadata
    std::ifstream meta_in(META_FILE);
    page_id_t btree_root, s_root, h_root;
    if (meta_in >> btree_root >> s_root >> h_root) {
        index->SetRootPageId(btree_root);
        std::cout << "[Info] B+Tree Root loaded: Page " << btree_root << "\n";
    }
    else {
        std::cerr << "[Error] Failed to read metadata. Assuming empty tree.\n";
    }
    meta_in.close();

    // 3. Interactive Loop
    while (true) {
        std::cout << "\n============================================\n";
        std::cout << "   LOG STORAGE PAGER TOOL\n";
        std::cout << "============================================\n";
        std::cout << "Enter start timestamp (or -1 to quit): ";

        int64_t start_ts;
        if (!(std::cin >> start_ts)) break;
        std::cin.ignore(); // Consume newline

        if (start_ts == -1) break;

        RunPagerQuery(index, start_ts);
    }

    // Cleanup
    delete index;
    delete bpm;
    delete disk;
    return 0;
}