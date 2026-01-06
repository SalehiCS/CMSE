#include "disk_manager.h"
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <cstring> 

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // Use std::filesystem to check existence safely
            bool file_exists = std::filesystem::exists(file_name_);

            if (!file_exists) {
                // Only create/truncate if it genuinely doesn't exist
                db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
                db_io_.close();
            }

            // Open the file in Read/Write mode
            db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);

            if (!db_io_.is_open()) {
                throw std::runtime_error("Failed to open database file: " + file_name_);
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

            // Always reset flags
            db_io_.clear();

            // Simplified Logic: Just try to seek and read.
            // Don't manually check file size with seekg(end), it can be buggy during rapid IO.
            db_io_.seekg(offset);

            // If seek fails (e.g., past EOF), it sets failbit
            if (db_io_.fail()) {
                std::memset(data, 0, PAGE_SIZE);
                return;
            }

            db_io_.read(data, PAGE_SIZE);

            // Handle partial reads or EOF during read
            int read_count = static_cast<int>(db_io_.gcount());
            if (read_count < PAGE_SIZE) {
                std::memset(data + read_count, 0, PAGE_SIZE - read_count);
            }

            // If read completely failed (but seek worked), ensure data is zeroed
            if (db_io_.bad()) {
                throw std::runtime_error("I/O error while reading page");
            }
        }

        void DiskManager::WritePage(page_id_t page_id, const char* data) {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

            db_io_.clear();
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
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const {
            return num_flushes_;
        }

    } // namespace disk
} // namespace cmse