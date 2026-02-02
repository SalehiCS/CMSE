#include "../src/index/btree_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/utils/log_parser.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <filesystem> // <--- Missing header added here!

// Using declarations
using cmse::index::BTreeIndex;
using cmse::bufferpool::BufferPoolManager;
using cmse::disk::DiskManager;
using cmse::utils::LogParser;
using cmse::LogRecord;

const std::string DB_FILE = "huge_storage.db";
const std::string META_FILE = "cmse.meta";
const std::string LOG_FILE = "journal_verbose.log";

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "       CMSE LARGE IMPORT (BATCHED)          " << std::endl;
    std::cout << "============================================" << std::endl;

    // Cleanup
    if (std::filesystem::exists(DB_FILE)) std::filesystem::remove(DB_FILE);
    if (std::filesystem::exists(META_FILE)) std::filesystem::remove(META_FILE);

    if (!std::filesystem::exists(LOG_FILE)) {
        std::cerr << "[ERROR] Please put '" << LOG_FILE << "' in build folder." << std::endl;
        return 1;
    }

    try {
        // 1. Init System
        DiskManager* disk_manager = new DiskManager(DB_FILE);
        // Give 50k pages (~200MB RAM) for buffer pool
        BufferPoolManager* bpm = new BufferPoolManager(50000, disk_manager);
        BTreeIndex* btree = new BTreeIndex(bpm);

        // 2. Init Stateful Parser
        LogParser parser(LOG_FILE);
        size_t total_bytes = parser.GetTotalFileSize();
        std::cout << "[INFO] File Size: " << (total_bytes / (1024 * 1024)) << " MB" << std::endl;

        // 3. Process in Batches
        // We read 10,000 records at a time to keep RAM usage low
        std::vector<LogRecord> batch;
        size_t total_inserted = 0;

        while (parser.GetNextBatch(batch, 10000)) {
            for (const auto& rec : batch) {
                btree->Insert(rec.timestamp, rec);
            }
            total_inserted += batch.size();

            // Progress Bar (Approximate based on file position)
            float percent = (static_cast<float>(parser.GetCurrentPosition()) / total_bytes) * 100.0f;
            std::cout << "\r    [Importing] " << std::fixed << std::setprecision(1) << percent
                << "% - Records: " << total_inserted << std::flush;
        }

        std::cout << std::endl << "[SUCCESS] Insertion Complete. Total: " << total_inserted << std::endl;

        // 4. Save Metadata
        cmse::page_id_t root_id = btree->GetRootPageId();
        std::ofstream meta_out(META_FILE);
        meta_out << root_id;
        meta_out.close();

        // 5. Cleanup
        delete btree;
        delete bpm;
        delete disk_manager;

    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}