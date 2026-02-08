// src/disk/disk_manager.cpp

// Implementation of the DiskManager class defined in the header.
#include "disk_manager.h"
// Provides the LOG_DEBUG macro for conditional logging used with the -g flag.
#include "../common/logger.h"
// Standard exception handling for runtime errors.
#include <stdexcept>
// Provides std::memset to zero-out buffers for safety.
#include <cstring>
// Used to check file existence on the filesystem.
#include <filesystem>
// Provides sleep_for functionality for the retry logic.
#include <thread>
// Provides time units (milliseconds) for the retry delay.
#include <chrono>
// Standard I/O for fallback error reporting.
#include <iostream>

namespace cmse {
    namespace disk {

        /**
         * Constructor: Handles the complex logic of opening or creating the DB file.
         * It uses an "Optimistic Open" strategy to survive Windows file locking issues.
         */
        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // Initialize the file pointer to null before attempting to open.
            db_file_ = nullptr;
            // Set a retry limit. This gives the OS 1 full second to release any "Zombie Handles".
            int retries = 10;

            // --- STRATEGY: Optimistic Open ---
            // We try to open in Read/Write Binary ("r+b") mode first.
            while (retries > 0) {
                // fopen_s is the secure version of fopen, preferred on Windows/MSVC.
                errno_t err = fopen_s(&db_file_, file_name_.c_str(), "r+b");

                if (err == 0 && db_file_ != nullptr) {
                    // [Success] The file was found and opened without conflict.
                    break;
                }

                if (err == ENOENT) {
                    // ENOENT specifically means the file does not exist on disk.
                    // We stop retrying because no amount of waiting will create the file.
                    break;
                }

                // If we reach here, the error is likely EACCES (Locked by another process).
                // We pause for 100ms to let the other process finish.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                retries--;
            }

            // --- DECISION POINT: Did we open an existing file or do we need a new one? ---

            if (db_file_ != nullptr) {
                // [CASE A: Existing File Opened]
                // We need to determine how many pages are already in the file.
                // Seek to the very last byte of the file.
                fseek(db_file_, 0, SEEK_END);
                // Get the current byte position (which is the total file size).
                long file_size = ftell(db_file_);

                if (file_size > 0) {
                    // Calculate the next ID: Total Bytes divided by Page Size (e.g., 4096).
                    next_page_id_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
                }
                else {
                    // File exists but is empty.
                    next_page_id_ = 0;
                }

                // CRITICAL: Reset the file cursor to the beginning so future reads start correctly.
                fseek(db_file_, 0, SEEK_SET);
            }
            else {
                // [CASE B: Create New File]
                // Either the file didn't exist (ENOENT) or we timed out waiting for a lock.

                // "w+b" creates a new empty file for reading and writing.
                errno_t err = fopen_s(&db_file_, file_name_.c_str(), "w+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("FATAL: Failed to create DB file: " + file_name_);
                }

                // Immediate Safety: We close the "creator" handle and reopen in "r+b".
                // This ensures the file is treated as a standard existing database file.
                // "w+b" truncates all data!
                fclose(db_file_);
                err = fopen_s(&db_file_, file_name_.c_str(), "r+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("FATAL: Failed to reopen new DB file: " + file_name_);
                }
                // Starting a fresh database from Page 0.
                next_page_id_ = 0;
            }
        }

        /**
         * Destructor: Ensures data integrity during shutdown.
         */
        DiskManager::~DiskManager() {
            if (db_file_ != nullptr) {
                // Final push to ensure the OS doesn't lose the last few writes.
                fflush(db_file_);
                // Release the file handle back to the Operating System.
                fclose(db_file_);
                db_file_ = nullptr;
            }
        }

        /**
         * ReadPage: Pulls a fixed-size block of data from the disk into a memory buffer.
         */
        void DiskManager::ReadPage(page_id_t page_id, char* data) {
            // Latch ensures that only one thread is moving the file cursor at a time.
            std::lock_guard<std::mutex> lock(db_io_latch_);
            // Calculate physical byte offset: Page ID 2 with 4KB pages starts at byte 8192.
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            // Check if we are trying to read past the end of the file.
            fseek(db_file_, 0, SEEK_END);
            long file_size = ftell(db_file_);

            if (static_cast<long>(offset) >= file_size) {
                // Logic: If the page doesn't exist on disk yet, we return a zeroed-out page.
                
                std::memset(data, 0, PAGE_SIZE);
                return;
            }

            // Normal Execution: Seek to the offset and read one full PAGE_SIZE.
            

            fseek(db_file_, (long)offset, SEEK_SET);
            size_t read_count = fread(data, 1, PAGE_SIZE, db_file_);

            // If the read was partial (hit EOF mid-page), zero-out the remainder of the buffer.
            if (read_count < PAGE_SIZE) {
                std::memset(data + read_count, 0, PAGE_SIZE - read_count);
            }
        }

        /**
         * WritePage: Commits a memory buffer to a specific slot on the disk.
         */
        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            // Thread-safety: Prevents simultaneous writes from corrupting the file pointer.
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            

            // Jump to the specific page location.
            fseek(db_file_, (long)offset, SEEK_SET);
            // Perform the binary write.
            size_t written = fwrite(data, 1, PAGE_SIZE, db_file_);

            if (written != PAGE_SIZE) {
                // Critical error: Disk might be full or permissions changed.
                
            }

            // Note: We intentionally DO NOT call Sync/fflush here for high-throughput.
            // We rely on the BufferPoolManager to call Sync() at the end of a transaction.
            num_flushes_++;
        }

        /**
         * AllocatePage: Logical allocation of a new page.
         */
        page_id_t DiskManager::AllocatePage() {
            // Thread-safe increment of the page counter.
            std::lock_guard<std::mutex> lock(db_io_latch_);
            return next_page_id_++;
        }

        /**
         * GetNumFlushes: Getter for performance metrics.
         */
        int DiskManager::GetNumFlushes() const { return num_flushes_; }

        /**
         * Sync: Forces the OS to physically write its internal buffers to the hardware.
         * Essential for the "Durability" part of ACID compliance.
         */
        void DiskManager::Sync() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            if (db_file_) fflush(db_file_);
        }

    } // namespace disk
} // namespace cmse