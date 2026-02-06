/**
 * Test Case 3.2: Persistence & Resume (The Crash Test)
 * --------------------------------------------------------------------------------------
 * OBJECTIVE: Verify ACID Persistence.
 * FIX APPLIED: Enforce std::ios::binary to prevent tellg()/seekg() drift on newlines.
 * --------------------------------------------------------------------------------------
 */

#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/btree_index.h"
#include "../src/version/version_manager.h"
#include "../src/utils/log_parser.h"
#include "../src/common/types.h"
#include "../src/common/logger.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

using namespace cmse;

const std::string LOG_FILE_PATH = "system_events.log";
const std::string DB_FILE_PATH = "persistence.db";
const std::string META_FILE_PATH = "persistence.meta";
const int TOTAL_LOGS = 100;

// --- HELPER: GENERATE LOG FILE (BINARY MODE) ---
void CreateRealLogFile() {
    // FIX: Open in Binary mode to ensure \n is exactly 1 byte (0x0A) or consistent system write
    std::ofstream out(LOG_FILE_PATH, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open()) exit(1);

    for (int i = 0; i < TOTAL_LOGS; i++) {
        std::string entry;
        entry += "Mon 2026-02-06 12:00:" + std::to_string(i % 60) + "\n";
        entry += "_SOURCE_REALTIME_TIMESTAMP=" + std::to_string(i) + "\n";
        entry += "PRIORITY=1\n";
        entry += "_PID=100\n";
        entry += "SYSLOG_IDENTIFIER=cms_test\n";
        entry += "_HOSTNAME=localhost\n";
        entry += "MESSAGE=Log Record Number " + std::to_string(i) + "\n\n";

        out.write(entry.c_str(), entry.size());
    }
    out.flush(); out.close();
    std::cout << "[Setup] Generated " << TOTAL_LOGS << " records (Binary Mode)." << std::endl;
}

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n  " << msg << "\n======================================================\n";
}

int main() {
    Logger::GetInstance().SetEnabled(true);

    std::remove(DB_FILE_PATH.c_str());
    std::remove(META_FILE_PATH.c_str());
    CreateRealLogFile();

    page_id_t committed_root = INVALID_PAGE_ID;
    size_t committed_offset = 0;

    // =================================================================================
    // PHASE 1: INGEST -> COMMIT -> CRASH
    // =================================================================================
    PrintBanner("PHASE 1: INGEST 50 -> COMMIT -> CRASH");
    {
        auto disk = std::make_unique<disk::DiskManager>(DB_FILE_PATH);
        auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
        auto vm = std::make_unique<VersionManager>(META_FILE_PATH, bpm.get());
        auto btree = std::make_unique<index::BTreeIndex>(bpm.get());

        // LogParser will open in Binary Mode inside its updated constructor (see below)
        utils::LogParser parser(LOG_FILE_PATH);

        std::cout << "[Ingest] Processing Batch 1 (0-49)..." << std::endl;
        TransactionContext txn = vm->BeginTransaction();
        std::vector<LogRecord> batch;

        // FETCH BATCH 1
        if (parser.GetNextBatch(batch, 50)) {
            int64_t max_ts = 0;
            for (const auto& rec : batch) {
                btree->InsertCoW(rec.timestamp, rec, txn);
                max_ts = rec.timestamp;
            }
            std::cout << "    -> Indexed " << batch.size() << " records." << std::endl;

            // COMMIT
            // This offset MUST be exact. Binary mode ensures it maps to physical bytes.
            committed_offset = parser.GetCurrentPosition();

            // [Debug Check]
            if (batch.back().timestamp != 49) {
                std::cout << "[WARN] Batch 1 ended at " << batch.back().timestamp << " expected 49." << std::endl;
            }

            vm->Commit(txn, max_ts, committed_offset);
            committed_root = vm->GetLatestRootPageId();

            std::cout << "[Commit] Checkpoint Saved. Offset: " << committed_offset << std::endl;
        }

        // SIMULATE BATCH 2 & CRASH
        std::cout << "\n[Ingest] Processing Batch 2..." << std::endl;
        TransactionContext txn_crash = vm->BeginTransaction();
        batch.clear();
        if (parser.GetNextBatch(batch, 20)) {
            std::cout << "    -> Indexed " << batch.size() << " records (Total 70)." << std::endl;
        }
        std::cout << "[System]  SIMULATING POWER FAILURE " << std::endl;
    }


    // =================================================================================
    // PHASE 2: RESUME
    // =================================================================================
    PrintBanner("PHASE 2: RESUME");
    {
        auto disk = std::make_unique<disk::DiskManager>(DB_FILE_PATH);
        auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
        auto vm = std::make_unique<VersionManager>(META_FILE_PATH, bpm.get());

        // 1. RECOVERY
        page_id_t recovered_root = vm->GetLatestRootPageId();
        size_t recovered_offset = vm->GetLastFileOffset();

        std::cout << "[Boot] Recovered State:" << std::endl;
        std::cout << "    -> Root: " << recovered_root << std::endl;
        std::cout << "    -> Offset: " << recovered_offset << std::endl;

        if (recovered_root == committed_root && recovered_offset == committed_offset) {
            std::cout << "\033[1;32m[PASS]\033[0m Metadata Persistence Verified." << std::endl;
        }
        else {
            std::cout << "\033[1;31m[FAIL]\033[0m Metadata Mismatch." << std::endl;
            return 1;
        }

        // 2. RESUME
        utils::LogParser parser(LOG_FILE_PATH); // Opens in Binary

        std::cout << "[Resume] Seeking to " << recovered_offset << "..." << std::endl;
        parser.SeekToPosition(recovered_offset);

        std::vector<LogRecord> resume_batch;
        if (parser.GetNextBatch(resume_batch, 1)) {
            int64_t resume_ts = resume_batch[0].timestamp;
            std::cout << "    -> First record after resume: " << resume_ts << std::endl;

            if (resume_ts == 50) {
                std::cout << "\033[1;32m[PASS]\033[0m Resume Successful. Exact Match." << std::endl;
            }
            else {
                std::cout << "\033[1;31m[FAIL]\033[0m Resume Failed. Expected 50, got " << resume_ts << std::endl;
            }
        }
        else {
            std::cout << "\033[1;31m[FAIL]\033[0m Resume yielded no data." << std::endl;
        }
    }

    std::remove(LOG_FILE_PATH.c_str());
    std::remove(DB_FILE_PATH.c_str());
    std::remove(META_FILE_PATH.c_str());
    return 0;
}