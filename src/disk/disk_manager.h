#pragma once
#include <string>
#include <mutex>
#include <cstdio> // Use C-style I/O for better control
#include "../common/types.h"

namespace cmse {
    namespace disk {

        /**
         * DiskManager
         * Handles the actual reading and writing of pages to the disk file.
         * * NOTE: We use C-style FILE* API (fopen, fwrite) instead of std::fstream
         * to avoid platform-specific file locking issues on Windows during testing.
         */
        class DiskManager {
        public:
            explicit DiskManager(const std::string& db_file);
            ~DiskManager();

            // Reads a page from the database file
            void ReadPage(page_id_t page_id, char* data);

            // Writes a page to the database file
            void WritePage(page_id_t page_id, const char* data);

            // Allocates a new page ID (increments a counter)
            page_id_t AllocatePage();

            // Returns total number of pages flushed to disk
            int GetNumFlushes() const;

        private:
            std::string file_name_;
            FILE* db_file_ = nullptr; // Low-level file handle

            page_id_t next_page_id_ = 0;
            int num_flushes_ = 0;
            std::mutex db_io_latch_; // Protects concurrent file access
        };

    } // namespace disk
} // namespace cmse