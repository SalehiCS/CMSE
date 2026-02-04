#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/common/types.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <algorithm>

using namespace cmse;

// ==========================================
// Struct Definitions (Must match Adapter)
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

// Calculations for offset usage
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

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    // The specific key that failed in Integrity Check
    const int64_t TARGET_KEY = 1766837068867996;

    // 1. Setup Environment
    if (!std::ifstream(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "Files missing." << std::endl;
        return 1;
    }

    page_id_t btree_root, s_root, h_root;
    std::ifstream meta_in(META_FILE);
    meta_in >> btree_root >> s_root >> h_root;
    meta_in.close();

    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    std::cout << "=== DEBUG PATH TRACE ===" << std::endl;
    std::cout << "Target Key: " << TARGET_KEY << std::endl;
    std::cout << "Root Page:  " << btree_root << std::endl;

    page_id_t current_id = btree_root;
    int level = 0;

    // 2. Traverse the Tree
    while (current_id != INVALID_PAGE_ID) {
        Page* page = bpm->FetchPage(current_id);
        if (!page) {
            std::cout << "[CRITICAL] Could not fetch Page " << current_id << std::endl;
            break;
        }

        auto* header = reinterpret_cast<BPlusNodeHeader*>(page->GetData());

        std::cout << "\n[Step " << level << "] At Page " << current_id
            << " (" << (header->is_leaf ? "LEAF" : "INTERNAL") << ")" << std::endl;

        // Check Metadata Validity
        std::cout << "   Stats -> Count: " << header->key_count
            << " | Min: " << header->min_key
            << " | Max: " << header->max_key << std::endl;

        // Diagnostic: Is the key logically supposed to be in this subtree?
        // Note: Even if this is false, standard B+Trees usually traverse anyway based on separators.
        // But if you implemented an optimization to skip based on Min/Max, this is why it fails.
        bool in_range = (TARGET_KEY >= header->min_key && TARGET_KEY <= header->max_key);
        if (!in_range) {
            std::cout << "   [WARNING] Target Key is OUTSIDE the Min/Max range of this node!" << std::endl;
        }

        if (header->is_leaf) {
            // --- Leaf Node Search ---
            auto* leaf = reinterpret_cast<BPlusLeafNode*>(page->GetData());
            std::cout << "   -> Reached Leaf. Scanning keys..." << std::endl;

            bool found = false;
            for (int i = 0; i < header->key_count; i++) {
                if (leaf->keys[i] == TARGET_KEY) {
                    std::cout << "   [SUCCESS] FOUND KEY at index " << i << "!" << std::endl;
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::cout << "   [FAILURE] Key NOT found in this leaf." << std::endl;
                if (header->key_count > 0) {
                    std::cout << "             First Key: " << leaf->keys[0] << std::endl;
                    std::cout << "             Last Key:  " << leaf->keys[header->key_count - 1] << std::endl;
                }
                else {
                    std::cout << "             Leaf is EMPTY." << std::endl;
                }
            }
            bpm->UnpinPage(current_id, false);
            break; // Stop traversal
        }
        else {
            // --- Internal Node Routing ---
            auto* internal = reinterpret_cast<BPlusInternalNode*>(page->GetData());

            // Print first few split keys for context
            std::cout << "   -> Split Keys (First 5): ";
            for (int i = 0; i < std::min((int)header->key_count, 5); i++) {
                std::cout << internal->keys[i] << " ";
            }
            std::cout << std::endl;

            // Simulate Standard Upper Bound / Scan Logic
            // We need to see exactly which index matches your findChild logic.
            // Usually: Find first key > target, then go to that index.
            // OR: Find last key <= target.

            int idx = 0;
            // Assuming logic: find the first key strictly greater than target
            // children[i] covers range (-inf, keys[i])
            // children[i+1] covers range [keys[i], keys[i+1]) ...

            // Let's simulate a simple linear scan to see where it lands:
            while (idx < header->key_count && internal->keys[idx] <= TARGET_KEY) {
                idx++;
            }

            std::cout << "   -> Algorithm Logic: keys[" << (idx - 1 >= 0 ? idx - 1 : 0) << "] <= "
                << TARGET_KEY << " < keys[" << (idx < header->key_count ? idx : header->key_count - 1) << "]" << std::endl;
            std::cout << "   -> Selected Child Index: " << idx << std::endl;

            page_id_t next_id = internal->children[idx];
            std::cout << "   -> Jumping to Page " << next_id << std::endl;

            bpm->UnpinPage(current_id, false);
            current_id = next_id;
            level++;
        }
    }

    delete bpm;
    delete disk;
    return 0;
}