#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/index/trie_index.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <filesystem>

using namespace cmse;

void TestBasicInsertSearch() {
    std::cout << "[Test] Basic Insert & Search..." << std::endl;

    // Setup Environment
    std::filesystem::remove("test_trie.db");
    auto* disk_manager = new disk::DiskManager("test_trie.db");
    auto* bpm = new bufferpool::BufferPoolManager(50, disk_manager);

    // Create Index
    index::TrieIndex trie(bpm);

    // 1. Insert distinct keys
    trie.Insert("ssh", 100, 1); // Timestamp 100, Level 1 (WARN)
    trie.Insert("ssh", 105, 2); // Timestamp 105, Level 2 (ERROR)
    trie.Insert("http", 200, 0); // Timestamp 200, Level 0 (INFO)

    // 2. Search "ssh"
    auto results_ssh = trie.Search("ssh");
    assert(results_ssh.size() == 2);
    assert(results_ssh[0].timestamp == 100 || results_ssh[0].timestamp == 105);

    // 3. Search "http"
    auto results_http = trie.Search("http");
    assert(results_http.size() == 1);
    assert(results_http[0].timestamp == 200);

    // 4. Search non-existent
    auto results_ftp = trie.Search("ftp");
    assert(results_ftp.empty());

    std::cout << "   -> Passed!" << std::endl;

    delete bpm;
    delete disk_manager;
    std::filesystem::remove("test_trie.db");
}

void TestValuePageChaining() {
    std::cout << "[Test] Value Page Chaining (Stress Test)..." << std::endl;

    std::filesystem::remove("test_trie_stress.db");
    auto* disk_manager = new disk::DiskManager("test_trie_stress.db");
    // Small buffer pool to force eviction and I/O
    auto* bpm = new bufferpool::BufferPoolManager(10, disk_manager);

    index::TrieIndex trie(bpm);

    // Insert 1000 records for the SAME key "flood"
    // Each ValuePage holds approx ~250 entries.
    // So this should create a chain of ~4 pages.
    int num_records = 1000;
    for (int i = 0; i < num_records; i++) {
        trie.Insert("flood", i * 10, 0);
    }

    // Retrieve
    auto results = trie.Search("flood");

    std::cout << "   -> Inserted " << num_records << ", Found " << results.size() << std::endl;

    assert(results.size() == num_records);

    // Verify data integrity (Check a few values)
    bool found_first = false;
    bool found_last = false;
    for (const auto& entry : results) {
        if (entry.timestamp == 0) found_first = true;
        if (entry.timestamp == (num_records - 1) * 10) found_last = true;
    }
    assert(found_first);
    assert(found_last);

    std::cout << "   -> Passed!" << std::endl;

    delete bpm;
    delete disk_manager;
    std::filesystem::remove("test_trie_stress.db");
}

int main() {
    TestBasicInsertSearch();
    TestValuePageChaining();
    return 0;
}