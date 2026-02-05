#include "../src/index/btree_index.h"
#include "../src/index/trie_index.h"
#include "../src/query/query_engine.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

using namespace cmse;

void AssertEq(size_t actual, size_t expected, const std::string& msg) {
    if (actual != expected) {
        std::cerr << "[FAIL] " << msg << " | Expected: " << expected << ", Got: " << actual << std::endl;
        exit(1);
    }
}

int main() {
    std::cout << "=== HYBRID QUERY ENGINE TEST ===" << std::endl;

    std::string db_file = "query_test.db";
    remove(db_file.c_str());

    disk::DiskManager disk(db_file);
    bufferpool::BufferPoolManager bpm(500, &disk);
    index::BTreeIndex btree(&bpm);
    index::TrieIndex trie(&bpm);
    QueryEngine engine(&btree, &trie);

    // --- SETUP DATA ---
    std::cout << "[Setup] Inserting Data..." << std::endl;

    // 1. "Sparse" Source: "RareService" (Only 5 records)
    for (int i = 0; i < 5; i++) {
        LogRecord r;
        r.Set(i, 1, "RareService", "host1", "error");
        btree.Insert(i, r);
        trie.Insert("RareService", i, 1);
        trie.Insert("host1", i, 1);
    }

    // 2. "Dense" Source: "CommonService" (200 records -> Should trigger Overflow)
    for (int i = 100; i < 300; i++) {
        LogRecord r;
        r.Set(i, 2, "CommonService", "host2", "info");
        btree.Insert(i, r);
        trie.Insert("CommonService", i, 2);
        trie.Insert("host2", i, 2);
    }

    // --- TEST 1: Sparse Query (Should use Trie) ---
    {
        std::cout << "[Test 1] Sparse Query (RareService)..." << std::endl;
        Query q;
        q.source = "RareService";

        auto results = engine.Execute(q);
        AssertEq(results.size(), 5, "Should find 5 records");
        std::cout << "[PASS] Found 5 records efficiently." << std::endl;
    }

    // --- TEST 2: Dense Query (Should Fallback to Scan) ---
    {
        std::cout << "[Test 2] Dense Query (CommonService)..." << std::endl;
        Query q;
        q.source = "CommonService";

        // Internally, Trie should overflow (>100) and engine should switch to BTree Scan
        auto results = engine.Execute(q);
        AssertEq(results.size(), 200, "Should find 200 records");
        std::cout << "[PASS] Found 200 records via Scan." << std::endl;
    }

    // --- TEST 3: Composite + Time Range ---
    {
        std::cout << "[Test 3] Composite (Source + Host + Time)..." << std::endl;
        Query q;
        q.source = "CommonService";
        q.host = "host2";
        q.min_timestamp = 150; // Should skip first 50
        q.max_timestamp = 199; // Should skip last 100

        auto results = engine.Execute(q);
        AssertEq(results.size(), 50, "Should find exactly 50 records in range");
        std::cout << "[PASS] Found 50 records." << std::endl;
    }

    // --- TEST 4: Wildcard Prefix ---
    {
        std::cout << "[Test 4] Wildcard Prefix (Common*)..." << std::endl;
        Query q;
        q.source = "Common*";

        auto results = engine.Execute(q);
        AssertEq(results.size(), 200, "Should match 'CommonService'");
        std::cout << "[PASS] Wildcard works." << std::endl;
    }

    std::cout << "=== ALL QUERY TESTS PASSED ===" << std::endl;
    return 0;
}