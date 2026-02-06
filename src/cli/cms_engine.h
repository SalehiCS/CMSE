#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <limits> 
#include <algorithm>

#include "../disk/disk_manager.h"
#include "../bufferpool/buffer_pool_manager.h"
#include "../index/btree_index.h"
#include "../index/trie_index.h"
#include "../version/version_manager.h"
#include "../query/query_engine.h"
#include "../utils/log_parser.h"
#include "../common/logger.h"
#include "../utils/time_utils.h"

namespace cmse {

    /**
     * CMSEngine
     * The top-level facade for the Cloud-Managed Storage Engine.
     * It manages component lifecycles, handles data ingestion batches,
     * and provides the interactive query interface.
     */
    class CMSEngine {
    public:
        /**
         * Constructor: Bootstraps the entire system hierarchy.
         * @param db_file The path to the main binary data file.
         */
        CMSEngine(const std::string& db_file)
            : db_file_(db_file), meta_file_(db_file + ".meta")
        {
            // --- Component Initialization Stack ---
            // 1. Physical Layer: Handles raw file I/O
            disk_ = std::make_unique<disk::DiskManager>(db_file_);
            // 2. Caching Layer: Keeps 2000 pages (approx 8MB) in RAM
            bpm_ = std::make_unique<bufferpool::BufferPoolManager>(2000, disk_.get());
            // 3. Indexing Layer: B+Tree for time ranges, Trie for text search
            btree_ = std::make_unique<index::BTreeIndex>(bpm_.get());
            trie_ = std::make_unique<index::TrieIndex>(bpm_.get());
            // 4. Persistence Layer: Manages snapshots and commit logs
            vm_ = std::make_unique<VersionManager>(meta_file_, bpm_.get());

            // --- STATE RECOVERY: Restoring from the last clean shutdown ---
            page_id_t last_root = vm_->GetLatestRootPageId();
            if (last_root != INVALID_PAGE_ID) {
                btree_->SetRootPageId(last_root); // Point B+Tree to its last known head
            }

            page_id_t last_trie_root = vm_->GetLatestTrieRootPageId();
            if (last_trie_root != INVALID_PAGE_ID) {
                std::cout << "[Engine] Restoring Trie Root: " << last_trie_root << std::endl;
                trie_->SetRootPageId(last_trie_root); // Point Trie to its last known head
            }
            else {
                std::cout << "[Engine] No Trie Root found in metadata." << std::endl;
            }

            // 5. Intelligence Layer: Plan executor for user searches
            query_engine_ = std::make_unique<QueryEngine>(btree_.get(), trie_.get());
        }

        /**
         * LoadLogFile
         * Streams raw text logs into the binary indexed storage.
         * Implements Fast Resume and Batch Commits for crash-safe ingestion.
         */
        void LoadLogFile(const std::string& log_file_path) {
            std::cout << "[Load] Opening " << log_file_path << "..." << std::endl;

            utils::LogParser parser(log_file_path);

            // --- FAST RESUME LOGIC ---
            // Check where we left off in this specific log file
            size_t last_offset = vm_->GetLastFileOffset();
            if (last_offset > 0) {
                std::cout << "[Resume] Jumping to byte offset " << last_offset << " (Instant Resume)..." << std::endl;
                parser.SeekToPosition(last_offset); // Skip already-indexed bytes
            }

            std::cout << "[Index] Indexing the logs. Every 10000 logs a version is committed." << std::endl;

            const size_t BATCH_SIZE = 5000;      // Rows to parse at once
            const size_t COMMIT_THRESHOLD = 10000; // Rows to index before a disk flush

            std::vector<LogRecord> batch;
            size_t records_since_commit = 0;
            size_t total_imported = 0;

            // Start an isolated transaction context for this ingestion session
            auto txn = vm_->BeginTransaction();
            auto start_time = std::chrono::high_resolution_clock::now();

            // Main ingestion loop
            while (parser.GetNextBatch(batch, BATCH_SIZE)) {
                for (const auto& rec : batch) {
                    // Perform Copy-on-Write insertion into B+Tree
                    btree_->InsertCoW(rec.timestamp, rec, txn);

                    // Insert into Trie for keyword/host/source lookups
                    if (rec.source[0] != '\0') trie_->Insert(rec.source, rec.timestamp, rec.priority);
                    if (rec.host[0] != '\0')   trie_->Insert(rec.host, rec.timestamp, rec.priority);

                    records_since_commit++;
                    total_imported++;
                }

                // --- ATOMIC COMMIT CHECK ---
                if (records_since_commit >= COMMIT_THRESHOLD) {
                    // Update the transaction with the latest generated Trie root page
                    txn.pending_trie_root_id = trie_->GetRootId();

                    LOG_DEBUG("[Engine] Pre-Commit: TrieRoot=" << txn.pending_trie_root_id);

                    int64_t current_max_ts = batch.back().timestamp;
                    size_t current_offset = parser.GetCurrentPosition(); // Record current byte offset

                    std::cout << "[Commit] Saving " << records_since_commit << " records... ";

                    // Finalize the version: Flushes pages and appends to .meta
                    vm_->Commit(txn, current_max_ts, current_offset);

                    // Re-open a new transaction for the next batch
                    txn = vm_->BeginTransaction();
                    txn.pending_trie_root_id = trie_->GetRootId(); // Maintain state link

                    LOG_DEBUG("[Engine] New Txn Started. Inherited TrieRoot=" << txn.pending_trie_root_id);

                    records_since_commit = 0;
                    std::cout << "Done." << std::endl;
                }
            }

            // Clean up any remaining records that didn't hit the threshold
            if (records_since_commit > 0) {
                int64_t final_ts = batch.empty() ? 0 : batch.back().timestamp;
                vm_->Commit(txn, final_ts, parser.GetCurrentPosition());
                std::cout << "[Commit] Final batch saved." << std::endl;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double>(end_time - start_time).count();

            std::cout << "=== Load Complete ===" << std::endl;
            std::cout << "  Imported: " << total_imported << std::endl;
            std::cout << "  Time:      " << duration << "s" << std::endl;
        }

        /**
         * RunQueryInteractive
         * REPL (Read-Eval-Print Loop) for user querying.
         */
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

                // --- INPUT SANITIZATION & PARSING ---
                Query q;
                try {
                    q = ParseQueryString(line);
                    // Constraint: Queries must filter by at least one dimension
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
                // Dispatch query to the execution engine
                auto results = query_engine_->Execute(q);
                std::cout << "[Results] Found " << results.size() << " records." << std::endl;

                // --- PAGINATION INTERFACE ---
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
                            // Clear stream if user quits pagination
                            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            break;
                        }
                    }
                }
            }
        }

    private:
        std::string db_file_;     // Data storage path
        std::string meta_file_;   // Metadata storage path

        // --- Core Smart Pointer Components ---
        std::unique_ptr<disk::DiskManager> disk_;
        std::unique_ptr<bufferpool::BufferPoolManager> bpm_;
        std::unique_ptr<index::BTreeIndex> btree_;
        std::unique_ptr<index::TrieIndex> trie_;
        std::unique_ptr<VersionManager> vm_;
        std::unique_ptr<QueryEngine> query_engine_;

        /**
         * PrintQueryHelp
         * Displays the syntax documentation for the CLI.
         */
        void PrintQueryHelp() {
            std::cout << "Supported Patterns:" << std::endl;
            std::cout << "  source=<val>   (e.g., source=nginx)" << std::endl;
            std::cout << "  host=<val>      (e.g., host=web01)" << std::endl;
            std::cout << "  prio=<int>      (e.g., prio=2)" << std::endl;
            std::cout << "" << std::endl;
            std::cout << "  --- Time Filters (Use Raw Ints or YYYY-MM-DD_HH:MM:SS) ---" << std::endl;
            std::cout << "  min=<val>       (e.g., min=2025-12-25_23:00:00)" << std::endl;
            std::cout << "  max=<val>       (e.g., max=2025-12-26_01:00:00.500)" << std::endl;
            std::cout << "  time=<min-max> (e.g., time=1700000-1800000) *Integers Only*" << std::endl;
            std::cout << "  * Note: Use '_' instead of spaces in dates." << std::endl;
        }

        /**
         * ParseQueryString
         * Internal DSL (Domain Specific Language) parser for the CLI.
         * Transforms strings like "source=nginx min=2026-01-01" into a Query object.
         */
        Query ParseQueryString(const std::string& input) {
            Query q;
            std::stringstream ss(input);
            std::string segment;
            bool valid = false;

            // Helper: Handles polymorphic time input (Numeric Microseconds OR Formatted Date)
            auto ParseTimeOrDate = [](std::string val) -> int64_t {
                // Check for raw integer input
                if (std::all_of(val.begin(), val.end(), ::isdigit)) {
                    return std::stoll(val);
                }

                // Handle CLI-friendly underscore substitution
                std::replace(val.begin(), val.end(), '_', ' ');

                // Convert ISO-style date string to internal microsecond timestamp
                int64_t ts = cmse::utils::TimeUtils::StringToTimestamp(val);
                if (ts == -1) {
                    throw std::runtime_error("Invalid date format: " + val);
                }
                return ts;
                };

            // Tokenize by spaces
            while (ss >> segment) {
                size_t eq_pos = segment.find('=');
                if (eq_pos == std::string::npos) continue;

                std::string key = segment.substr(0, eq_pos);
                std::string val = segment.substr(eq_pos + 1);

                if (key == "source") { q.source = val; valid = true; }
                else if (key == "host") { q.host = val; valid = true; }
                else if (key == "msg") { q.message_contains = val; valid = true; }
                else if (key == "prio") { q.priority = std::stoi(val); valid = true; }
                // Time range min boundary
                else if (key == "min_timestamp" || key == "min") {
                    q.min_timestamp = ParseTimeOrDate(val);
                    valid = true;
                }
                // Time range max boundary
                else if (key == "max_timestamp" || key == "max") {
                    q.max_timestamp = ParseTimeOrDate(val);
                    valid = true;
                }
                // Legacy hyphen-based range
                else if (key == "time") {
                    size_t dash = val.find('-');
                    if (dash != std::string::npos) {
                        std::string start_s = val.substr(0, dash);
                        std::string end_s = val.substr(dash + 1);
                        q.min_timestamp = ParseTimeOrDate(start_s);
                        q.max_timestamp = ParseTimeOrDate(end_s);
                        valid = true;
                    }
                }
            }

            if (!valid) throw std::runtime_error("No valid keys found");
            return q;
        }
    };

} // namespace cmse