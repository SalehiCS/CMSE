#pragma once

// Provides std::string for storing the physical file path.
#include <string>
// Provides std::mutex to ensure thread-safety during concurrent Disk I/O operations.
#include <mutex>
// Provides C-style file handling (FILE*, fopen_s, fseek, etc.) for high-performance binary I/O.
#include <cstdio> 
// Includes project-specific definitions like page_id_t and PAGE_SIZE.
#include "../common/types.h"

// Organized under the cmse (Centralized Monitoring System Engine) namespace.
namespace cmse {
    // Sub-namespace for the Storage Layer components.
    namespace disk {

        /**
         * DiskManager
         * * PURPOSE:
         * Manages the physical persistence of data. It treats the database file
         * as a contiguous array of fixed-size slots called "Pages."
         * * RESPONSIBILITY:
         * 1. Mapping logical Page IDs to physical file offsets.
         * 2. Guaranteeing that data is physically written to the platter/SSD.
         * 3. Managing the allocation of new space within the file.
         */
        class DiskManager {
        public:
            // Constructor: Opens the file and handles the "Windows Zombie Handle" retry logic.
            explicit DiskManager(const std::string& db_file);

            // Destructor: Ensures the file handle is properly closed to prevent resource leaks.
            ~DiskManager();

            // Deleted Copy Constructor: Prevents multiple managers from fighting over one file handle.
            DiskManager(const DiskManager&) = delete;
            // Deleted Assignment Operator: Enforces the Singleton-like ownership of the DB file.
            DiskManager& operator=(const DiskManager&) = delete;

            /**
             * ReadPage
             * Logic: Calculates (page_id * PAGE_SIZE) to find the offset,
             * seeks to that point, and reads exactly PAGE_SIZE bytes into the buffer.
             */
            void ReadPage(page_id_t page_id, char* data);

            /**
             * WritePage
             * Logic: Seeks to the calculated offset and performs a binary write.
             * Note: Includes an internal fflush() to move data from the app to the OS.
             */
            void WritePage(page_id_t page_id, const char* data);

            /**
             * AllocatePage
             * Logic: Increments the internal counter to provide a unique ID
             * for a new page, effectively growing the logical file size.
             */
            page_id_t AllocatePage();

            /**
             * GetNumFlushes
             * Metrics: Returns the total number of physical writes performed.
             */
            int GetNumFlushes() const;

            /**
             * Sync
             * Logic: Invokes the OS-level flush to ensure data in the OS
             * buffers is physically committed to the hardware.
             */
            void Sync();

        private:
            // The system path to the .db file.
            std::string file_name_;
            // The low-level C file pointer used for all binary operations.
            FILE* db_file_ = nullptr;

            // Tracks the next available ID to ensure sequential page growth.
            page_id_t next_page_id_ = 0;
            // Counter for performance monitoring and testing validation.
            int num_flushes_ = 0;
            // The "Gatekeeper": Prevents multiple threads from seeking/writing at the same time.
            std::mutex db_io_latch_;
        };

    } // namespace disk
} // namespace cmse