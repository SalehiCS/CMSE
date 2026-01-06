#pragma once

#include <string>
#include <mutex>
#include <cstdio> // FILE*, fopen_s, etc.
#include "../common/types.h"

namespace cmse {
    namespace disk {

        /**
         * DiskManager takes care of the allocation and deallocation of pages within a database.
         * It performs the reading and writing of pages to and from disk.
         */
        class DiskManager {
        public:
            explicit DiskManager(const std::string& db_file);
            ~DiskManager();

            DiskManager(const DiskManager&) = delete;
            DiskManager& operator=(const DiskManager&) = delete;

            void ReadPage(page_id_t page_id, char* data);
            void WritePage(page_id_t page_id, const char* data);
            page_id_t AllocatePage();
            int GetNumFlushes() const;

        private:
            std::string file_name_;
            FILE* db_file_ = nullptr;

            page_id_t next_page_id_ = 0;
            int num_flushes_ = 0;
            std::mutex db_io_latch_;
        };

    } // namespace disk
} // namespace cmse