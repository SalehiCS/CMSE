#include "../src/index/btree_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

// This tool visualizes the tree structure stored in 'huge_storage.db'

int main() {
    const std::string DB_FILE = "huge_storage.db";
    const std::string META_FILE = "cmse.meta";

    // 1. Check files
    if (!std::filesystem::exists(DB_FILE) || !std::filesystem::exists(META_FILE)) {
        std::cerr << "[ERROR] DB or Meta file missing. Run integration_test first!" << std::endl;
        return 1;
    }

    // 2. Read Root ID
    cmse::page_id_t root_id = 0;
    std::ifstream meta_in(META_FILE);
    meta_in >> root_id;
    meta_in.close();

    std::cout << "[Visualizer] Loading DB: " << DB_FILE << std::endl;
    std::cout << "[Visualizer] Root Page ID: " << root_id << std::endl;

    // 3. Init Engine
    // We only need a small buffer pool for reading
    cmse::disk::DiskManager* disk_manager = new cmse::disk::DiskManager(DB_FILE);
    cmse::bufferpool::BufferPoolManager* bpm = new cmse::bufferpool::BufferPoolManager(50, disk_manager);
    cmse::index::BTreeIndex* btree = new cmse::index::BTreeIndex(bpm, root_id);

    // 4. Print Tree
    // Limit depth to 4 levels to avoid flooding the console
    btree->PrintTree(4);

    // 5. Cleanup
    delete btree;
    delete bpm;
    delete disk_manager;

    return 0;
}