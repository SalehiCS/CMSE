#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include "../common/types.h"

namespace cmse {
    namespace disk {

        /**
         * DiskManager
         * Handles the actual reading and writing of pages to the disk file.
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
            std::fstream db_io_;
            page_id_t next_page_id_ = 0;
            int num_flushes_ = 0;
            std::mutex db_io_latch_; // Protects file operations
        };

    } // namespace disk
} // namespace cmse