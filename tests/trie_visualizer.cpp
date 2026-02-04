#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/trie_page.h"
#include "../src/index/trie_value_page.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

using namespace cmse;

// Recursive function to print the Trie structure
void PrintTrieNode(bufferpool::BufferPoolManager* bpm, page_id_t page_id, std::string prefix, char parent_char) {
    Page* page = bpm->FetchPage(page_id);
    if (page == nullptr) {
        std::cout << prefix << "[ERROR] Could not fetch Page " << page_id << std::endl;
        return;
    }

    auto* node = reinterpret_cast<TriePage*>(page->GetData());

    // Print current node info
    std::cout << prefix << "|- [" << (parent_char == 0 ? "ROOT" : std::string(1, parent_char))
        << "] Page " << page_id;

    if (node->IsTerminal()) {
        std::cout << " [TERMINAL]";

        // Inspect Value Page Chain
        page_id_t vp_id = node->GetValuePageId();
        int total_records = 0;
        int chain_length = 0;

        while (vp_id != INVALID_PAGE_ID) {
            Page* vp_raw = bpm->FetchPage(vp_id);
            if (vp_raw) {
                auto* vp = reinterpret_cast<TrieValuePage*>(vp_raw->GetData());
                total_records += vp->GetCount();
                chain_length++;
                page_id_t next = vp->GetNextPageId();
                bpm->UnpinPage(vp_id, false);
                vp_id = next;
            }
            else {
                break;
            }
        }
        std::cout << " -> Records: " << total_records << " (Chain: " << chain_length << " pages)";
    }
    std::cout << std::endl;

    // Recursive Step: Check all 256 possible children
    // In a real visualizer, we might limit depth, but for debugging we want to see immediate children
    for (int i = 0; i < 256; i++) {
        char ch = static_cast<char>(i);
        if (node->HasChild(ch)) {
            page_id_t child_id = node->GetChild(ch);

            // Recurse with increased indentation
            // We Unpin current page BEFORE recursion to avoid holding too many pages
            bpm->UnpinPage(page_id, false);

            PrintTrieNode(bpm, child_id, prefix + "   ", ch);

            // Re-fetch current page to continue loop (Crabbing)
            page = bpm->FetchPage(page_id);
            node = reinterpret_cast<TriePage*>(page->GetData());
        }
    }

    bpm->UnpinPage(page_id, false);
}

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    if (!std::filesystem::exists(DB_FILE) || !std::ifstream(META_FILE)) {
        std::cerr << "Files missing." << std::endl;
        return 1;
    }

    // 1. Load Root IDs
    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    // 2. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);

    // 3. Visualize Source Tree
    std::cout << "========================================" << std::endl;
    std::cout << " VISUALIZING SOURCE TRIE (Root: " << source_root << ")" << std::endl;
    std::cout << "========================================" << std::endl;

    // Check if root is effectively empty before recursing
    Page* root_page = bpm->FetchPage(source_root);
    auto* root_node = reinterpret_cast<TriePage*>(root_page->GetData());
    bool has_children = false;
    for (int i = 0; i < 256; i++) {
        if (root_node->HasChild((char)i)) { has_children = true; break; }
    }
    bpm->UnpinPage(source_root, false);

    if (!has_children) {
        std::cout << "[WARNING] Root Page " << source_root << " exists but has NO children." << std::endl;
        std::cout << "          It seems the 'Insert' function never updated the Root." << std::endl;
    }
    else {
        PrintTrieNode(bpm, source_root, "", 0);
    }

    // 4. Visualize Host Tree
    std::cout << "\n========================================" << std::endl;
    std::cout << " VISUALIZING HOST TRIE (Root: " << host_root << ")" << std::endl;
    std::cout << "========================================" << std::endl;
    PrintTrieNode(bpm, host_root, "", 0);

    delete bpm;
    delete disk;
    return 0;
}