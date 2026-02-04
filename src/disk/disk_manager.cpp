#include "disk_manager.h"
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <thread>
#include <chrono>

namespace cmse {
    namespace disk {

        DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
            // 1. Check existence safely
            bool file_exists = std::filesystem::exists(file_name_);

            if (file_exists) {
                // Case A: File Exists -> Try to open in "r+b" mode (Read/Update binary).
                int retries = 10;
                errno_t err = 0;

                while (retries > 0) {
                    err = fopen_s(&db_file_, file_name_.c_str(), "r+b");
                    if (err == 0 && db_file_ != nullptr) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    retries--;
                }

                if (db_file_ == nullptr) {
                    throw std::runtime_error("FATAL: Could not open existing DB file: " + file_name_);
                }
            }
            else {
                // Case B: File Missing -> Create new with "w+b" (Truncate/Create binary).
                errno_t err = fopen_s(&db_file_, file_name_.c_str(), "w+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("Failed to create new DB file: " + file_name_);
                }

                // Close and reopen in standard "r+b" mode so we don't accidentally truncate later
                fclose(db_file_);
                err = fopen_s(&db_file_, file_name_.c_str(), "r+b");
                if (err != 0 || db_file_ == nullptr) {
                    throw std::runtime_error("Failed to reopen new DB file: " + file_name_);
                }
            }

            // --- CRITICAL FIX: RESUME LOGIC ---
            // We must set next_page_id_ based on the actual file size.
            fseek(db_file_, 0, SEEK_END);
            long file_size = ftell(db_file_);

            if (file_size > 0) {
                next_page_id_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
            }
            else {
                next_page_id_ = 0;
            }

            // Reset cursor to start just in case
            fseek(db_file_, 0, SEEK_SET);

            // Debug Print
            // std::cout << "[DiskManager] Opened " << db_file 
            //           << " | Size: " << file_size << " bytes"
            //           << " | Next Page ID: " << next_page_id_ << std::endl;
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

            // Check file bounds to prevent reading past EOF
            fseek(db_file_, 0, SEEK_END);
            long file_size = ftell(db_file_);

            if (static_cast<long>(offset) >= file_size) {
                // Reading a page that hasn't been written yet -> Return zeroes
                std::memset(data, 0, PAGE_SIZE);
                return;
            }

            fseek(db_file_, (long)offset, SEEK_SET);
            size_t read_count = fread(data, 1, PAGE_SIZE, db_file_);

            // If partial read (unexpected), pad with zeroes
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
                // Serious error: disk full or permission denied
                throw std::runtime_error("I/O error while writing page " + std::to_string(page_id));
            }

            // Note: We do NOT call fflush here for performance. 
            // OS buffers handles it. Use FlushPage() explicitly if needed.
            // But for VersionManager safety, we trust the OS or call explicit sync.
            // fflush(db_file_); 

            num_flushes_++;
        }

        page_id_t DiskManager::AllocatePage() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            // Simple atomic increment. The actual file grows when WritePage is called.
            return next_page_id_++;
        }

        int DiskManager::GetNumFlushes() const {
            return num_flushes_;
        }

        // Helper to force sync (useful for Commit)
        void DiskManager::Sync() {
            std::lock_guard<std::mutex> lock(db_io_latch_);
            fflush(db_file_);
        }

    } // namespace disk
} // namespace cmse