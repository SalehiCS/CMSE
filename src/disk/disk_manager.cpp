#include "disk_manager.h"
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <cstring> // for std::memset

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // 1. Try to open existing file in Read/Write mode
            db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);

            // 2. If file does not exist, create it
            if (!db_io_.is_open()) {
                db_io_.clear(); // Reset flags just in case
                // Create new file with trunc (wipes content if exists, but here we know it doesn't)
                db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
                db_io_.close();

                // 3. Reopen in Read/Write mode
                db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);

                if (!db_io_.is_open()) {
                    throw std::runtime_error("Failed to open database file: " + file_name_);
                }
            }
        }

        DiskManager::~DiskManager() {
            if (db_io_.is_open()) {
                db_io_.close();
            }
        }

        void DiskManager::ReadPage(page_id_t page_id, char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            // CRITICAL FIX: Always clear error flags (like EOF) before seeking.
            // If we don't do this, seekg/tellg might fail silently.
            db_io_.clear();

            // Check file size using the Read Pointer (seekg/tellg)
            db_io_.seekg(0, std::ios::end);
            size_t file_size = static_cast<size_t>(db_io_.tellg());

            if (offset >= file_size) {
                // Reading beyond file end -> return zeros
                std::memset(data, 0, PAGE_SIZE);
            }
            else {
                // Move pointer to the correct page
                db_io_.seekg(offset);
                db_io_.read(data, PAGE_SIZE);

                if (db_io_.bad()) {
                    throw std::runtime_error("I/O error while reading page");
                }

                // If we read partial page (end of file), zero out the rest
                int read_count = static_cast<int>(db_io_.gcount());
                if (read_count < PAGE_SIZE) {
                    std::memset(data + read_count, 0, PAGE_SIZE - read_count);
                }
            }
        }

        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            // CRITICAL FIX: Clear flags before writing too
            db_io_.clear();

            // Move Write Pointer
            db_io_.seekp(offset);
            db_io_.write(data, PAGE_SIZE);

            if (db_io_.bad()) {
                throw std::runtime_error("I/O error while writing page");
            }

            // Force write to disk immediately
            db_io_.flush();
            num_flushes_++;
        }

        page_id_t DiskManager::AllocatePage() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const {
            return num_flushes_;
        }

    } // namespace disk
} // namespace cmse