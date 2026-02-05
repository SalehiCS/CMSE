// src/disk/disk_manager.cpp

#include "disk_manager.h"
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            db_file_ = nullptr;
            int retries = 10; // 1 second total wait (10 * 100ms)

            // --- STRATEGY: Optimistic Open ---
            // Try to open as existing ("r+b") first. 
            // This handles cases where the file exists but the OS claims it doesn't yet (Zombie Lock).
            while (retries > 0) {
                errno_t err = fopen_s(&db_file_, file_name_.c_str(), "r+b");

                if (err == 0 && db_file_ != nullptr) {
                    // [Success] File opened successfully.
                    break;
                }

                if (err == ENOENT) {
                    // ENOENT means "File Not Found". 
                    // This is the ONLY case where we should stop retrying and create a new file.
                    break;
                }

                // If EACCES (Permission/Locked), we wait and retry.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                retries--;
            }

            // --- DECISION POINT ---

            if (db_file_ != nullptr) {
                // [CASE A: Existing File Opened]
                // Move to end to find the next page ID
                fseek(db_file_, 0, SEEK_END);
                long file_size = ftell(db_file_);

                if (file_size > 0) {
                    next_page_id_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
                }
                else {
                    next_page_id_ = 0;
                }

                // CRITICAL: Reset cursor to start
                fseek(db_file_, 0, SEEK_SET);
            }
            else {
                // [CASE B: Create New File]
                // Only happens if it truly didn't exist (ENOENT) or we timed out.

                errno_t err = fopen_s(&db_file_, file_name_.c_str(), "w+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("FATAL: Failed to create DB file: " + file_name_);
                }

                // Immediate Safety: Close and reopen in standard "r+b" mode
                fclose(db_file_);
                err = fopen_s(&db_file_, file_name_.c_str(), "r+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("FATAL: Failed to reopen new DB file: " + file_name_);
                }
                next_page_id_ = 0;
            }
        }

        DiskManager::~DiskManager() {
            if (db_file_ != nullptr) {
                fflush(db_file_);
                fclose(db_file_);
                db_file_ = nullptr;
            }
        }

        void DiskManager::ReadPage(page_id_t page_id, char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            fseek(db_file_, 0, SEEK_END);
            long file_size = ftell(db_file_);

            if (static_cast<long>(offset) >= file_size) {
                std::memset(data, 0, PAGE_SIZE);
                return;
            }

            fseek(db_file_, (long)offset, SEEK_SET);
            size_t read_count = fread(data, 1, PAGE_SIZE, db_file_);
            if (read_count < PAGE_SIZE) {
                std::memset(data + read_count, 0, PAGE_SIZE - read_count);
            }
        }

        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;


            fseek(db_file_, (long)offset, SEEK_SET);
            size_t written = fwrite(data, 1, PAGE_SIZE, db_file_);

            if (written != PAGE_SIZE) {
                std::cerr << "[Disk] FATAL WRITE ERROR on Page " << page_id << std::endl;
            }

            // Note: We intentionally DO NOT flush here for performance.
            // The FlushAllPages or Destructor will handle the final sync.
            num_flushes_++;
        }

        page_id_t DiskManager::AllocatePage() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const { return num_flushes_; }

        // Keep Sync available for manual commits (Checkpointing)
        void DiskManager::Sync() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            if (db_file_) fflush(db_file_);
        }

    } // namespace disk
} // namespace cmse