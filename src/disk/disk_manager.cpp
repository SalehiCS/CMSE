#include "disk_manager.h"
#include <stdexcept>
#include <iostream>
#include <filesystem>

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // Open file for reading and writing (binary mode)
            // If file doesn't exist, create it
            db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
            if (!db_io_.is_open()) {
                db_io_.clear();
                // Create a new file
                db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
                db_io_.close();
                // Reopen in read/write mode
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

            // Check file size
            db_io_.seekp(0, std::ios::end);
            size_t file_size = db_io_.tellp();

            if (offset >= file_size) {
                // Reading beyond file (e.g. new page not written yet), return zeros
                std::memset(data, 0, PAGE_SIZE);
            }
            else {
                db_io_.seekp(offset);
                db_io_.read(data, PAGE_SIZE);
                if (db_io_.bad()) {
                    throw std::runtime_error("I/O error while reading page");
                }
                // If read fewer bytes than PAGE_SIZE, zero out the rest
                int read_count = static_cast<int>(db_io_.gcount());
                if (read_count < PAGE_SIZE) {
                    std::memset(data + read_count, 0, PAGE_SIZE - read_count);
                }
            }
        }

        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            db_io_.seekp(offset);
            db_io_.write(data, PAGE_SIZE);

            if (db_io_.bad()) {
                throw std::runtime_error("I/O error while writing page");
            }
            db_io_.flush();
            num_flushes_++;
        }

        page_id_t DiskManager::AllocatePage() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            // Simple linear allocation. In a real DB, we would track free pages.
            // We assume page IDs are sequential for this project.
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const {
            return num_flushes_;
        }

    } // namespace disk
} // namespace cmse