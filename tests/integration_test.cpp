#include "../src/index/btree_index.h"
#include "../src/index/trie_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/utils/log_parser.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <cstring> // Required for C-string operations if needed

// Using declarations
using cmse::index::BTreeIndex;
using cmse::index::TrieIndex;
using cmse::bufferpool::BufferPoolManager;
using cmse::disk::DiskManager;
using cmse::utils::LogParser;
using cmse::LogRecord;

const std::string DB_FILE = "huge_storage.db";
const std::string META_FILE = "cmse.meta";
const std::string LOG_FILE = "journal_verbose.log";

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "      CMSE MULTI-INDEX IMPORT SYSTEM        " << std::endl;
    std::cout << "============================================" << std::endl;

    // Cleanup previous runs to ensure a fresh start
    if (std::filesystem::exists(DB_FILE)) std::filesystem::remove(DB_FILE);
    if (std::filesystem::exists(META_FILE)) std::filesystem::remove(META_FILE);

    if (!std::filesystem::exists(LOG_FILE)) {
        std::cerr << "[ERROR] Please put '" << LOG_FILE << "' in build folder." << std::endl;
        return 1;
    }

    try {
        // 1. Init System Components
        // Shared Disk and Buffer Pool for all indexes
        DiskManager* disk_manager = new DiskManager(DB_FILE);

        // Allocate 50k pages (~200MB RAM)
        BufferPoolManager* bpm = new BufferPoolManager(50000, disk_manager);

        // 2. Init Indexes
        // Primary Index: B+Tree (Key: Timestamp)
        BTreeIndex* btree = new BTreeIndex(bpm);

        // Secondary Index 1: Trie (Key: Source) - Formerly 'Service'
        TrieIndex* source_trie = new TrieIndex(bpm);

        // Secondary Index 2: Trie (Key: Host)
        TrieIndex* host_trie = new TrieIndex(bpm);

        // 3. Init Parser
        LogParser parser(LOG_FILE);
        size_t total_bytes = parser.GetTotalFileSize();
        std::cout << "[INFO] Log File Size: " << (total_bytes / (1024 * 1024)) << " MB" << std::endl;

        // 4. Process in Batches
        std::vector<LogRecord> batch;
        size_t total_inserted = 0;

        while (parser.GetNextBatch(batch, 10000)) {
            for (const auto& rec : batch) {
                // --- A. Insert into Primary Index (Time) ---
                btree->Insert(rec.timestamp, rec);

                // --- B. Insert into Source Trie (Text) ---
                // Check if source is not empty (C-string check)
                if (rec.source[0] != '\0') {
                    // Implicit conversion from char[] to std::string happens here
                    // We cast priority (int32) to uint8 for compact storage in Trie
                    source_trie->Insert(rec.source, rec.timestamp, static_cast<uint8_t>(rec.priority));
                }

                // --- C. Insert into Host Trie (Text) ---
                // Check if host is not empty
                if (rec.host[0] != '\0') {
                    host_trie->Insert(rec.host, rec.timestamp, static_cast<uint8_t>(rec.priority));
                }
                else {
                    // Optional: Fallback for empty host if your logic requires it
                    // host_trie->Insert("unknown-host", rec.timestamp, static_cast<uint8_t>(rec.priority));
                }
            }

            total_inserted += batch.size();

            // Progress Bar Logic
            float percent = (static_cast<float>(parser.GetCurrentPosition()) / total_bytes) * 100.0f;
            std::cout << "\r    [Indexing] " << std::fixed << std::setprecision(1) << percent
                << "% - Records: " << total_inserted << std::flush;
        }

        std::cout << std::endl << "[SUCCESS] Insertion Complete. Total Records: " << total_inserted << std::endl;

        // 5. Save Metadata
        // Format:
        // Line 1: BTree Root ID
        // Line 2: Source Trie Root ID
        // Line 3: Host Trie Root ID
        std::ofstream meta_out(META_FILE);
        meta_out << btree->GetRootPageId() << std::endl;
        meta_out << source_trie->GetRootId() << std::endl;
        meta_out << host_trie->GetRootId() << std::endl;
        meta_out.close();

        std::cout << "[INFO] Metadata saved to " << META_FILE << std::endl;
        std::cout << "   -> BTree Root: " << btree->GetRootPageId() << std::endl;
        std::cout << "   -> Source Trie Root: " << source_trie->GetRootId() << std::endl;
        std::cout << "   -> Host Trie Root: " << host_trie->GetRootId() << std::endl;

        // 6. Cleanup
        delete btree;
        delete source_trie;
        delete host_trie;
        delete bpm;
        delete disk_manager;

    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}