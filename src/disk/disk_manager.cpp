#include "disk_manager.h"
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <filesystem>

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // 1. Try to open existing file in Read/Write Binary mode (r+b)
            // errno_t is used for secure error handling in MSVC
            errno_t err = fopen_s(&db_file_, file_name_.c_str(), "r+b");

            if (err != 0 || db_file_ == nullptr) {
                // 2. File doesn't exist, create it using Write Binary (w+b)
                // This creates a new empty file.
                err = fopen_s(&db_file_, file_name_.c_str(), "w+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("Failed to create database file: " + file_name_);
                }

                // 3. Close the file immediately after creation to release the handle
                fclose(db_file_);

                // 4. Reopen in Read/Write mode (r+b) to match the standard operating mode
                err = fopen_s(&db_file_, file_name_.c_str(), "r+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("Failed to reopen database file: " + file_name_);
                }
            }
        }

        DiskManager::~DiskManager() {
            if (db_file_ != nullptr) {
                fclose(db_file_);
            }
        }

        void DiskManager::ReadPage(page_id_t page_id, char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            // Determine file size to prevent reading past EOF
            fseek(db_file_, 0, SEEK_END);
            size_t file_size = ftell(db_file_);

            // If requested offset is beyond file size, return an empty (zeroed) page
            if (offset >= file_size) {
                std::memset(data, 0, PAGE_SIZE);
                return;
            }

            // Seek to the correct page location
            fseek(db_file_, (long)offset, SEEK_SET);

            // Read data into the buffer
            size_t read_count = fread(data, 1, PAGE_SIZE, db_file_);

            // If we read fewer bytes than a full page (e.g., EOF), zero out the rest
            if (read_count < PAGE_SIZE) {
                std::memset(data + read_count, 0, PAGE_SIZE - read_count);
            }
        }

        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            // Seek to the correct page location
            fseek(db_file_, (long)offset, SEEK_SET);

            // Write the data
            size_t written = fwrite(data, 1, PAGE_SIZE, db_file_);

            // Check for partial writes (should not happen in normal conditions)
            if (written != PAGE_SIZE) {
                throw std::runtime_error("I/O error while writing page");
            }

            // CRITICAL: Flush the stream buffer to the physical disk immediately.
            // This ensures data persistence even if the program crashes shortly after.
            fflush(db_file_);
            num_flushes_++;
        }

        page_id_t DiskManager::AllocatePage() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            // Simple linear allocation strategy
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const {
            return num_flushes_;
        }

    } // namespace disk
} // namespace cmse