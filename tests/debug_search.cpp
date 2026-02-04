#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/trie_index.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

using namespace cmse;

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    if (!std::filesystem::exists(DB_FILE)) {
        std::cerr << "DB File missing." << std::endl;
        return 1;
    }

    // 1. Load Root IDs
    page_id_t btree_root, source_root, host_root;
    std::ifstream meta_in(META_FILE);
    if (!meta_in) { std::cerr << "Meta file missing." << std::endl; return 1; }
    meta_in >> btree_root >> source_root >> host_root;
    meta_in.close();

    std::cout << "=== DEBUG SEARCH DIAGNOSTIC ===" << std::endl;
    std::cout << "Target Root Page: " << source_root << std::endl;

    // 2. Init Engine
    auto* disk = new disk::DiskManager(DB_FILE);
    auto* bpm = new bufferpool::BufferPoolManager(50000, disk);
    auto* trie = new index::TrieIndex(bpm);

    // CRITICAL STEP: Manually inject the root ID
    // If you haven't implemented SetRootPageId, this is where it fails.
    trie->SetRootPageId(source_root);

    // 3. Manual "Step-by-Step" Search for "systemd"
    std::string target = "systemd";
    std::cout << "Attempting to find key: '" << target << "'" << std::endl;

    page_id_t current_page_id = source_root;

    // Fetch Root
    Page* raw_page = bpm->FetchPage(current_page_id);
    if (!raw_page) {
        std::cerr << "[FATAL] Could not fetch Root Page " << current_page_id << std::endl;
        return 1;
    }
    auto* node = reinterpret_cast<TriePage*>(raw_page->GetData());

    std::cout << "[Step 0] At Root (Page " << current_page_id << ")" << std::endl;

    for (size_t i = 0; i < target.length(); i++) {
        char ch = target[i];

        if (node->HasChild(ch)) {
            page_id_t next_id = node->GetChild(ch);
            std::cout << "[Step " << (i + 1) << "] Found char '" << ch << "' -> Jumping to Page " << next_id << std::endl;

            // Move to next page
            bpm->UnpinPage(current_page_id, false);
            current_page_id = next_id;

            raw_page = bpm->FetchPage(current_page_id);
            node = reinterpret_cast<TriePage*>(raw_page->GetData());
        }
        else {
            std::cout << "[FAILURE] Stopped at Step " << (i + 1) << ". Node (Page " << current_page_id
                << ") does not have child '" << ch << "'." << std::endl;

            // Debug: Print what children it DOES have
            std::cout << "         Available children here: ";
            for (int k = 0; k < 256; k++) {
                if (node->HasChild((char)k)) std::cout << "[" << (char)k << "] ";
            }
            std::cout << std::endl;
            return 1;
        }
    }

    // 4. Check Terminal
    if (node->IsTerminal()) {
        std::cout << "[SUCCESS] Reached end of key. Node is TERMINAL." << std::endl;
        page_id_t vp_id = node->GetValuePageId();
        std::cout << "          Value Page ID: " << vp_id << std::endl;

        // Check actual TrieIndex::Search function
        std::cout << "\nRunning trie->Search(\"systemd\")..." << std::endl;
        auto results = trie->Search("systemd");
        std::cout << "Result Count: " << results.size() << std::endl;
    }
    else {
        std::cout << "[FAILURE] Key exists path-wise, but 'IsTerminal' is FALSE." << std::endl;
    }

    delete trie;
    delete bpm;
    delete disk;
    return 0;
}