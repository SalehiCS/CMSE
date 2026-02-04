#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/common/types.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <iomanip>

using namespace cmse;

// ==========================================
// CORRECT STRUCT DEFINITIONS (Matching Adapter)
// ==========================================

struct BPlusNodeHeader {
    bool is_leaf;
    int16_t key_count;
    int64_t min_key;
    int64_t max_key;
    float density;
    int32_t total_keys;
};

constexpr int HEADER_SIZE = sizeof(BPlusNodeHeader);
constexpr int PAGE_SIZE = 4096;
constexpr int SAFETY_MARGIN = 32;

// Calculations must match btree_adapter.h exactly
constexpr int MAX_KEYS_INTERNAL = (cmse::PAGE_SIZE - HEADER_SIZE - SAFETY_MARGIN) / (sizeof(int64_t) + sizeof(page_id_t));
constexpr int MAX_KEYS_LEAF = (cmse::PAGE_SIZE - HEADER_SIZE - sizeof(page_id_t) - SAFETY_MARGIN) / (sizeof(int64_t) + 280);

struct BPlusInternalNode {
    BPlusNodeHeader header;
    int64_t keys[MAX_KEYS_INTERNAL];
    page_id_t children[MAX_KEYS_INTERNAL + 1];
};

struct LogRecord {
    int64_t timestamp;
    int32_t priority;
    int32_t pid;
    char source[32];
    char host[32];
    char message[200];
};

struct BPlusLeafNode {
    BPlusNodeHeader header;
    int64_t keys[MAX_KEYS_LEAF];
    cmse::LogRecord values[MAX_KEYS_LEAF];
    page_id_t next_leaf_id;
};

// ==========================================

void PrintTree(bufferpool::BufferPoolManager* bpm, page_id_t root_id) {
    if (root_id == INVALID_PAGE_ID) {
        std::cout << "[Visualizer] Tree Root ID is INVALID." << std::endl;
        return;
    }

    std::queue<std::pair<page_id_t, int>> q;
    q.push({ root_id, 0 });

    int current_level = -1;

    while (!q.empty()) {
        auto [pid, level] = q.front();
        q.pop();

        if (level > current_level) {
            current_level = level;
            std::cout << "\n" << std::string(40, '=') << " LEVEL " << current_level << " " << std::string(40, '=') << std::endl;
        }

        Page* page = bpm->FetchPage(pid);
        if (!page) {
            std::cout << "[ERROR] Could not fetch Page " << pid << std::endl;
            continue;
        }

        // We rely on the internal header now, as it matches our structs
        auto* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());
        bool is_leaf = header->is_leaf;
        int count = header->key_count;

        std::cout << "[Page " << std::setw(4) << pid << "] "
            << (is_leaf ? "LEAF    " : "INTERNAL")
            << " | Count: " << std::setw(3) << count
            << " | Total: " << header->total_keys;

        if (is_leaf) {
            auto* leaf = reinterpret_cast<BPlusLeafNode*>(page->GetData());
            std::cout << " | Range: [" << (count > 0 ? std::to_string(leaf->keys[0]) : "Empty")
                << " ... "
                << (count > 0 ? std::to_string(leaf->keys[count - 1]) : "") << "]";

            // Debug: Check Next Pointer
            if (leaf->next_leaf_id != INVALID_PAGE_ID)
                std::cout << " -> Next: " << leaf->next_leaf_id;
        }
        else {
            auto* internal = reinterpret_cast<BPlusInternalNode*>(page->GetData());
            std::cout << " -> Children: { ";
            for (int i = 0; i <= count; i++) { // Internal has count+1 children
                std::cout << internal->children[i] << " ";
                q.push({ internal->children[i], level + 1 });
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

    if (!std::ifstream(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "Files missing." << std::endl;
        return 1;
    }

    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    std::cout << "Visualizing B+Tree (Root: " << btree_root << ")" << std::endl;

    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    PrintTree(bpm, btree_root);

    delete bpm;
    delete disk;
    return 0;
}