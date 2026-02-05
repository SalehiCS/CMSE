#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>

// Include your components
#include "../disk/disk_manager.h"
#include "../bufferpool/buffer_pool_manager.h"
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../version/version_manager.h"
#include "../query/query_engine.h"
#include "../utils/log_parser.h"

namespace cmse {

    class CMSEngine {
    public:
        CMSEngine(const std::string& db_file)
            : db_file_(db_file), meta_file_(db_file + ".meta")
        {
            // 1. Initialize Subsystems
            disk_ = std::make_unique<disk::DiskManager>(db_file_);
            bpm_ = std::make_unique<bufferpool::BufferPoolManager>(1000, disk_.get()); // 1000 Pages = 4MB RAM

            // 2. Indexes
            btree_ = std::make_unique<index::BTreeIndex>(bpm_.get());
            trie_ = std::make_unique<index::TrieIndex>(bpm_.get());

            // 3. Version Manager (Restores State)
            vm_ = std::make_unique<VersionManager>(meta_file_, bpm_.get());

            // 4. Restore Root
            page_id_t last_root = vm_->GetLatestRootPageId();
            if (last_root != INVALID_PAGE_ID) {
                btree_->SetRootPageId(last_root);
                std::cout << "[System] Restored B+Tree Root: " << last_root << std::endl;
            }

            // 5. Query Engine
            query_engine_ = std::make_unique<QueryEngine>(btree_.get(), trie_.get());
        }

        // --- COMMAND: LOAD ---
        void LoadLogFile(const std::string& log_file_path) {
            std::cout << "[Load] Opening " << log_file_path << "..." << std::endl;

            utils::LogParser parser(log_file_path);

            // 1. Resume Logic
            int64_t last_ts = vm_->GetLastCommittedLogTimestamp();
            if (last_ts > 0) {
                std::cout << "[Resume] Skipping logs <= Timestamp " << last_ts << "..." << std::endl;
            }

            // 2. Batch Processing Setup
            const size_t BATCH_SIZE = 10000;
            const size_t COMMIT_THRESHOLD = 50000; // Commit every 50k

            std::vector<LogRecord> batch;
            size_t records_since_commit = 0;
            size_t total_imported = 0;
            size_t total_skipped = 0;

            auto txn = vm_->BeginTransaction();
            auto start_time = std::chrono::high_resolution_clock::now();

            // 3. Ingestion Loop
            while (parser.GetNextBatch(batch, BATCH_SIZE)) {
                for (const auto& rec : batch) {

                    // RESUME CHECK: Skip old data
                    if (rec.timestamp <= last_ts) {
                        total_skipped++;
                        continue;
                    }

                    // A. Insert to B+Tree (Copy-on-Write Transaction)
                    btree_->InsertCoW(rec.timestamp, rec, txn);

                    // B. Insert to Trie (Auxiliary Index)
                    // Note: Trie updates are currently in-place.
                    if (rec.source[0] != '\0') trie_->Insert(rec.source, rec.timestamp, rec.priority);
                    if (rec.host[0] != '\0')   trie_->Insert(rec.host, rec.timestamp, rec.priority);

                    records_since_commit++;
                    total_imported++;
                }

                // 4. Commit Check
                if (records_since_commit >= COMMIT_THRESHOLD) {
                    // Get the max timestamp in this batch for the resume point
                    int64_t current_max_ts = batch.back().timestamp;

                    std::cout << "[Commit] Saving " << records_since_commit << " records... ";
                    vm_->Commit(txn, current_max_ts);

                    // Start new transaction on top of the new root
                    txn = vm_->BeginTransaction();
                    records_since_commit = 0;

                    std::cout << "Done." << std::endl;
                }
            }

            // 5. Final Commit (Leftovers)
            if (records_since_commit > 0) {
                int64_t final_ts = batch.back().timestamp;
                vm_->Commit(txn, final_ts);
                std::cout << "[Commit] Final batch saved." << std::endl;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double>(end_time - start_time).count();

            std::cout << "=== Load Complete ===" << std::endl;
            std::cout << "  Imported: " << total_imported << std::endl;
            std::cout << "  Skipped:  " << total_skipped << " (Duplicates/Old)" << std::endl;
            std::cout << "  Time:     " << duration << "s" << std::endl;
            std::cout << "  Speed:    " << (total_imported / duration) << " rec/s" << std::endl;
        }

        // --- COMMAND: QUERY ---
        void RunQueryInteractive() {
            std::cout << "\n--- Query Mode ---" << std::endl;
            std::cout << "Supported Patterns:" << std::endl;
            std::cout << "  source=<val>   (e.g., source=nginx or source=sys*)" << std::endl;
            std::cout << "  host=<val>     (e.g., host=web01)" << std::endl;
            std::cout << "  prio=<0-7>     (e.g., prio=2)" << std::endl;
            std::cout << "  msg=<keyword>  (e.g., msg=error)" << std::endl;
            std::cout << "  time=<min-max> (e.g., time=1000-2000)" << std::endl;
            std::cout << "Example: source=sys* prio=3 time=10000-20000" << std::endl;

            std::cout << "\nEnter Query > ";
            std::string line;
            if (!std::getline(std::cin, line) || line.empty()) return;

            Query q = ParseQueryString(line);

            std::cout << "Executing..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();

            std::vector<LogRecord> results = query_engine_->Execute(q);

            auto end = std::chrono::high_resolution_clock::now();
            double dt = std::chrono::duration<double>(end - start).count();

            // Display Results
            std::cout << "\n[Results] Found " << results.size() << " records in " << dt << "s" << std::endl;
            int limit = 0;
            for (const auto& r : results) {
                if (limit++ > 20) {
                    std::cout << "... (and " << (results.size() - 20) << " more)" << std::endl;
                    break;
                }
                std::cout << r.toString() << " | SRC:" << r.source << " | HOST:" << r.host << std::endl;
            }
        }

    private:
        std::string db_file_;
        std::string meta_file_;

        std::unique_ptr<disk::DiskManager> disk_;
        std::unique_ptr<bufferpool::BufferPoolManager> bpm_;
        std::unique_ptr<index::BTreeIndex> btree_;
        std::unique_ptr<index::TrieIndex> trie_;
        std::unique_ptr<VersionManager> vm_;
        std::unique_ptr<QueryEngine> query_engine_;

        // Helper: Parse "key=value key=value" string
        Query ParseQueryString(const std::string& input) {
            Query q;
            std::stringstream ss(input);
            std::string segment;

            while (ss >> segment) {
                size_t eq_pos = segment.find('=');
                if (eq_pos == std::string::npos) continue;

                std::string key = segment.substr(0, eq_pos);
                std::string val = segment.substr(eq_pos + 1);

                if (key == "source") q.source = val;
                else if (key == "host") q.host = val;
                else if (key == "msg") q.message_contains = val;
                else if (key == "prio") q.priority = std::stoi(val);
                else if (key == "time") {
                    size_t dash = val.find('-');
                    if (dash != std::string::npos) {
                        q.min_timestamp = std::stoll(val.substr(0, dash));
                        q.max_timestamp = std::stoll(val.substr(dash + 1));
                    }
                }
            }
            return q;
        }
    };

} // namespace cmse