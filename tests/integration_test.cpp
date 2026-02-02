#include "../src/index/btree_index.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/disk/disk_manager.h"
#include "../src/utils/log_parser.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <iomanip> // For std::fixed, std::setprecision

// Using declarations
using cmse::index::BTreeIndex;
using cmse::bufferpool::BufferPoolManager;
using cmse::disk::DiskManager;
using cmse::utils::LogParser;
using cmse::LogRecord;

// Constant filenames
const std::string DB_FILE = "huge_storage.db";
const std::string META_FILE = "cmse.meta";
const std::string LOG_FILE = "journal_verbose.log"; // Rename your 1.2GB file to this

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "       CMSE LARGE SCALE IMPORT TOOL         " << std::endl;
    std::cout << "============================================" << std::endl;

    // 1. Setup & Configuration
    // We remove the old DB to ensure a fresh start for this test
    if (std::filesystem::exists(DB_FILE)) {
        std::cout << "[INFO] Removing old database file..." << std::endl;
        std::filesystem::remove(DB_FILE);
    }
    if (std::filesystem::exists(META_FILE)) {
        std::filesystem::remove(META_FILE);
    }

    if (!std::filesystem::exists(LOG_FILE)) {
        std::cerr << "[ERROR] Log file '" << LOG_FILE << "' not found!" << std::endl;
        std::cerr << "Please rename your 1.2GB file to '" << LOG_FILE << "' and place it in the build directory." << std::endl;
        return 1;
    }

    try {
        // 2. Initialize System
        std::cout << "[1] Initializing Storage Engine..." << std::endl;

        DiskManager* disk_manager = new DiskManager(DB_FILE);

        // INCREASED BUFFER SIZE:
        // 50 pages is too small for 1.2GB data. It will cause excessive I/O thrashing.
        // We use 50,000 pages (~200MB RAM) for better insert performance.
        constexpr size_t BUFFER_SIZE = 50000;
        BufferPoolManager* bpm = new BufferPoolManager(BUFFER_SIZE, disk_manager);

        BTreeIndex* btree = new BTreeIndex(bpm);

        // 3. Parse Log File
        std::cout << "[2] Parsing Log File (This might take memory)..." << std::endl;
        // Note: For production, we should stream this. For now, vector is fine if you have >4GB RAM.
        std::vector<LogRecord> records = LogParser::parseLogFile(LOG_FILE);
        size_t total_records = records.size();
        std::cout << "    -> Parsed " << total_records << " records." << std::endl;

        if (total_records == 0) {
            std::cerr << "[ERROR] File is empty." << std::endl;
            return 1;
        }

        // 4. Insert Loop with Progress Bar
        std::cout << "[3] Starting Insertion Process..." << std::endl;

        int success_count = 0;
        size_t report_interval = total_records / 100; // Report every 1%
        if (report_interval == 0) report_interval = 1;

        for (size_t i = 0; i < total_records; ++i) {
            bool res = btree->Insert(records[i].timestamp, records[i]);
            if (res) success_count++;

            // Progress Reporting
            if ((i + 1) % report_interval == 0) {
                float percentage = (static_cast<float>(i + 1) / total_records) * 100.0f;
                std::cout << "\r    Progress: " << std::fixed << std::setprecision(1)
                    << percentage << "% (" << (i + 1) << "/" << total_records << ")" << std::flush;
            }
        }
        std::cout << std::endl << "    -> Insertion Complete." << std::endl;

        // 5. Save Metadata (Root Page ID)
        // This is crucial! Without the root ID, we cannot traverse the tree later.
        cmse::page_id_t root_id = btree->GetRootPageId();
        std::cout << "[4] Persisting Metadata..." << std::endl;
        std::cout << "    -> Root Page ID: " << root_id << std::endl;

        std::ofstream meta_out(META_FILE);
        if (meta_out.is_open()) {
            meta_out << root_id;
            meta_out.close();
            std::cout << "    -> Saved root ID to '" << META_FILE << "'" << std::endl;
        }
        else {
            std::cerr << "    [ERROR] Failed to save metadata file!" << std::endl;
        }

        // 6. Graceful Shutdown (Flush all dirty pages to disk)
        std::cout << "[5] Flushing buffers to disk (Don't close window)..." << std::endl;
        delete btree;
        delete bpm;         // This triggers FlushAllPages
        delete disk_manager; // This closes the file handle

        std::cout << "============================================" << std::endl;
        std::cout << "       DATABASE SAVED SUCCESSFULLY          " << std::endl;
        std::cout << "============================================" << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}