#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <limits> 

#include "../disk/disk_manager.h"
#include "../bufferpool/buffer_pool_manager.h"
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../version/version_manager.h"
#include "../query/query_engine.h"
#include "../utils/log_parser.h"
#include "../common/logger.h"

namespace cmse {

    class CMSEngine {
    public:
        CMSEngine(const std::string& db_file)
            : db_file_(db_file), meta_file_(db_file + ".meta")
        {
            // Init components
            disk_ = std::make_unique<disk::DiskManager>(db_file_);
            bpm_ = std::make_unique<bufferpool::BufferPoolManager>(2000, disk_.get());
            btree_ = std::make_unique<index::BTreeIndex>(bpm_.get());
            trie_ = std::make_unique<index::TrieIndex>(bpm_.get());
            vm_ = std::make_unique<VersionManager>(meta_file_, bpm_.get());

            // Restore State
            page_id_t last_root = vm_->GetLatestRootPageId();
            if (last_root != INVALID_PAGE_ID) {
                btree_->SetRootPageId(last_root);
            }
            page_id_t last_trie_root = vm_->GetLatestTrieRootPageId();
            // Add logging to verify this step runs
            if (last_trie_root != INVALID_PAGE_ID) {
                std::cout << "[Engine] Restoring Trie Root: " << last_trie_root << std::endl;
                trie_->SetRootPageId(last_trie_root);
            }
            else {
                std::cout << "[Engine] No Trie Root found in metadata." << std::endl;
            }

            // Init Query Engine
            query_engine_ = std::make_unique<QueryEngine>(btree_.get(), trie_.get());
        }

        void LoadLogFile(const std::string& log_file_path) {
            std::cout << "[Load] Opening " << log_file_path << "..." << std::endl;

            utils::LogParser parser(log_file_path);

            // --- FAST RESUME ---
            size_t last_offset = vm_->GetLastFileOffset();
            if (last_offset > 0) {
                std::cout << "[Resume] Jumping to byte offset " << last_offset << " (Instant Resume)..." << std::endl;
                parser.SeekToPosition(last_offset);
            }

            std::cout << "[Index] Indexing the logs. Every 10000 logs a version is committed." << std::endl;

            const size_t BATCH_SIZE = 5000;
            const size_t COMMIT_THRESHOLD = 10000;

            std::vector<LogRecord> batch;
            size_t records_since_commit = 0;
            size_t total_imported = 0;

            auto txn = vm_->BeginTransaction();
            auto start_time = std::chrono::high_resolution_clock::now();

            while (parser.GetNextBatch(batch, BATCH_SIZE)) {
                for (const auto& rec : batch) {
                    btree_->InsertCoW(rec.timestamp, rec, txn);
                    if (rec.source[0] != '\0') trie_->Insert(rec.source, rec.timestamp, rec.priority);
                    if (rec.host[0] != '\0')   trie_->Insert(rec.host, rec.timestamp, rec.priority);

                    records_since_commit++;
                    total_imported++;
                }

                // --- COMMIT CHECK ---
                if (records_since_commit >= COMMIT_THRESHOLD) {
                    // UPDATE TXN with latest Trie State
                    txn.pending_trie_root_id = trie_->GetRootId();

                    LOG_DEBUG("[Engine] Pre-Commit: TrieRoot=" << txn.pending_trie_root_id);

                    int64_t current_max_ts = batch.back().timestamp;
                    size_t current_offset = parser.GetCurrentPosition(); // Get Byte Offset

                    std::cout << "[Commit] Saving " << records_since_commit << " records... ";

                    // Pass Offset to VersionManager
                    vm_->Commit(txn, current_max_ts, current_offset);

                    txn = vm_->BeginTransaction();

                    txn.pending_trie_root_id = trie_->GetRootId();

                    LOG_DEBUG("[Engine] New Txn Started. Inherited TrieRoot=" << txn.pending_trie_root_id);

                    records_since_commit = 0;
                    std::cout << "Done." << std::endl;
                }
            }

            // Final Commit
            if (records_since_commit > 0) {
                // Be careful with empty batch edge case
                int64_t final_ts = batch.empty() ? 0 : batch.back().timestamp;
                vm_->Commit(txn, final_ts, parser.GetCurrentPosition());
                std::cout << "[Commit] Final batch saved." << std::endl;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double>(end_time - start_time).count();

            std::cout << "=== Load Complete ===" << std::endl;
            std::cout << "  Imported: " << total_imported << std::endl;
            std::cout << "  Time:     " << duration << "s" << std::endl;
        }

        void RunQueryInteractive() {
            std::cout << "\n--- Query Mode ---" << std::endl;
            std::cout << "  help: show supported patterns" << std::endl;
            std::cout << "  back: back to main menu\n" << std::endl;
            PrintQueryHelp();

            while (true) {
                std::cout << "\nQuery > ";
                std::string line;
                if (!std::getline(std::cin, line) || line.empty()) continue;

                if (line == "back") return;
                if (line == "help") {
                    PrintQueryHelp();
                    continue;
                }

                // --- INPUT SANITIZATION ---
                Query q;
                try {
                    q = ParseQueryString(line);
                    // Basic validation: At least one filter must be active
                    if (q.source.empty() && q.host.empty() && q.priority == -1 &&
                        q.min_timestamp == 0 && q.max_timestamp == INT64_MAX && q.message_contains.empty()) {
                        std::cout << "[Error] Invalid or empty query. Please provide at least one filter." << std::endl;
                        continue;
                    }
                }
                catch (const std::exception& e) {
                    std::cout << "[Error] Parsing failed: " << e.what() << std::endl;
                    continue;
                }

                std::cout << "Executing..." << std::endl;
                auto results = query_engine_->Execute(q);
                std::cout << "[Results] Found " << results.size() << " records." << std::endl;

                // --- PAGINATION ---
                size_t count = 0;
                const size_t PAGE_SIZE = 20;

                for (const auto& r : results) {
                    std::cout << r.toString() << " | SRC:" << r.source << " | HOST:" << r.host
                        << " | PRI:" << r.priority << std::endl;

                    count++;
                    if (count % PAGE_SIZE == 0 && count < results.size()) {
                        std::cout << "-- Press Enter for next 20, 'q' to stop listing --";
                        char c = std::cin.get();
                        if (c == 'q') {
                            // Consume rest of line if user typed 'q' + Enter
                            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            break;
                        }
                        // If just Enter, c is '\n', loop continues.
                    }
                }
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

        void PrintQueryHelp() {
            std::cout << "Supported Patterns:" << std::endl;
            std::cout << "  source=<val>   (e.g., source=nginx)" << std::endl;
            std::cout << "  host=<val>     (e.g., host=web01)" << std::endl;
            std::cout << "  prio=<int>     (e.g., prio=2)" << std::endl;
            std::cout << "  msg=<keyword>  (e.g., msg=error)" << std::endl;
            std::cout << "  time=<min-max> (e.g., time=1000-2000)" << std::endl;
        }

        Query ParseQueryString(const std::string& input) {
            Query q;
            std::stringstream ss(input);
            std::string segment;
            bool valid = false;

            while (ss >> segment) {
                size_t eq_pos = segment.find('=');
                if (eq_pos == std::string::npos) continue;

                std::string key = segment.substr(0, eq_pos);
                std::string val = segment.substr(eq_pos + 1);

                if (key == "source") { q.source = val; valid = true; }
                else if (key == "host") { q.host = val; valid = true; }
                else if (key == "msg") { q.message_contains = val; valid = true; }
                else if (key == "prio") { q.priority = std::stoi(val); valid = true; }
                else if (key == "time") {
                    size_t dash = val.find('-');
                    if (dash != std::string::npos) {
                        q.min_timestamp = std::stoll(val.substr(0, dash));
                        q.max_timestamp = std::stoll(val.substr(dash + 1));
                        valid = true;
                    }
                }
            }
            if (!valid) throw std::runtime_error("No valid keys found");
            return q;
        }
    };

} // namespace cmse