#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/btree_index.h"
#include "../src/common/types.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <iomanip>

using namespace cmse;

// --- Definitions to interpret raw data ---
// We assume your Internal Nodes store pairs of {Key, PageID}
struct InternalEntry {
    int64_t key;
    page_id_t page_id;
};

// We assume Leaf Nodes store pairs of {Key, Value}
// Based on your LogRecord definition
struct LogRecord {
    int64_t timestamp;
    int32_t priority;
    int32_t pid;
    char source[32];
    char host[32];
    char message[200];
};

struct LeafEntry {
    int64_t key;
    cmse::LogRecord value;
};

void PrintTree(bufferpool::BufferPoolManager* bpm, page_id_t root_id) {
    if (root_id == INVALID_PAGE_ID) {
        std::cout << "[Visualizer] Tree Root ID is INVALID. Tree is empty." << std::endl;
        return;
    }

    std::queue<std::pair<page_id_t, int>> q; // {PageID, Level}
    q.push({ root_id, 0 });

    int current_level = -1;

    while (!q.empty()) {
        auto [pid, level] = q.front();
        q.pop();

        // Print Level Header
        if (level > current_level) {
            current_level = level;
            std::cout << "\n" << std::string(40, '=') << " LEVEL " << current_level << " " << std::string(40, '=') << std::endl;
        }

        // Fetch Page
        Page* page = bpm->FetchPage(pid);
        if (!page) {
            std::cout << "[ERROR] Could not fetch Page " << pid << std::endl;
            continue;
        }

        // Read Header
        PageHeader* header = page->GetHeader();
        bool is_leaf = (header->is_leaf == 1);
        uint32_t count = header->key_count;

        // Print Node Info
        std::cout << "[Page " << std::setw(4) << pid << "] "
            << (is_leaf ? "LEAF    " : "INTERNAL")
            << " | Count: " << std::setw(3) << count
            << " | Pin: " << page->GetPinCount(); // Only works if you added GetPinCount()

        // --- DATA DUMP ---
        char* raw_data = page->GetData();

        if (is_leaf) {
            // Visualize Leaf Data
            auto* entries = reinterpret_cast<LeafEntry*>(raw_data);
            std::cout << " | Keys: [ ";
            for (uint32_t i = 0; i < count; i++) {
                // Print only first and last few to save space
                if (i < 3 || i > count - 3) {
                    std::cout << entries[i].key << " ";
                }
                else if (i == 3) {
                    std::cout << "... ";
                }
            }
            std::cout << "]";
        }
        else {
            // Visualize Internal Node & Queue Children
            // WARNING: This assumes your internal node layout is just an array of InternalEntry.
            // If you have a separate "Leftmost Pointer", this might need adjustment.
            auto* entries = reinterpret_cast<InternalEntry*>(raw_data);

            std::cout << " -> Points to Pages: { ";
            for (uint32_t i = 0; i < count; i++) {
                std::cout << entries[i].page_id << " ";

                // Add child to queue for next level
                q.push({ entries[i].page_id, level + 1 });
            }
            std::cout << "}";
        }

        std::cout << std::endl;
        bpm->UnpinPage(pid, false);
    }
}

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    // 1. Check Files
    if (!std::ifstream(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "Database or Meta file missing." << std::endl;
        return 1;
    }

    // 2. Load Metadata
    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    std::cout << "Visualizing B+Tree (Root Page: " << btree_root << ")" << std::endl;

    // 3. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    // 4. Run Visualizer
    PrintTree(bpm, btree_root);

    delete bpm;
    delete disk;
    return 0;
}