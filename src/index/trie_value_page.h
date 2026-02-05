#pragma once

#include "../src/common/types.h"
#include "../src/page/page.h"
#include <cstring>
#include <vector>

namespace cmse {

    /**
     * TrieLogEntry
     * Represents a single record in the search result.
     * We store the Timestamp (Key) and the LogLevel (Metadata) for efficient filtering.
     * Total size: ~9-16 bytes depending on alignment.
     */
    struct TrieLogEntry {
        int64_t timestamp;  // Acts as the Record ID for the B+Tree
        uint8_t log_level;  // 0:INFO, 1:WARN, 2:ERROR, etc.
    };

    /**
     * TrieValuePageHeader
     * Header for the Value Page containing metadata about the page itself.
     */
    struct TrieValuePageHeader {
        page_id_t next_page_id; // Pointer to the next Value Page (chaining)
        int16_t count;          // Current number of entries in this page
    };

    // Calculate capacity dynamically based on Page Size (4096 bytes)
    constexpr int VP_HEADER_SIZE = sizeof(TrieValuePageHeader);
    constexpr int VP_SAFETY_MARGIN = 32; // Safety padding
    constexpr int VP_MAX_ENTRIES = (4096 - VP_HEADER_SIZE - VP_SAFETY_MARGIN) / sizeof(TrieLogEntry);

    /**
     * TrieValuePage
     * A page-based container that stores a list of log entries.
     * It functions like a Linked-List node on the disk.
     */
    class TrieValuePage {
    public:
        // --- Initialization ---
        void Init() {
            auto* header = GetHeader();
            header->next_page_id = INVALID_PAGE_ID;
            header->count = 0;
        }

        // --- Getters & Setters ---

        int16_t GetCount() const {
            return GetHeader()->count;
        }

        page_id_t GetNextPageId() const {
            return GetHeader()->next_page_id;
        }

        void SetNextPageId(page_id_t next_page_id) {
            GetHeader()->next_page_id = next_page_id;
        }

        bool IsFull() const {
            return GetCount() >= VP_MAX_ENTRIES;
        }

        // --- Operations ---

        /**
         * Insert a new log entry into this page.
         * @return true if inserted successfully, false if the page is full.
         */
        bool Insert(int64_t timestamp, uint8_t log_level) {
            if (IsFull()) {
                return false;
            }

            auto* header = GetHeader();
            entries_[header->count].timestamp = timestamp;
            entries_[header->count].log_level = log_level;
            header->count++;

            return true;
        }

        /**
         * Returns all entries currently stored in this specific page.
         */
        std::vector<TrieLogEntry> GetEntries() const {
            std::vector<TrieLogEntry> result;
            int count = GetCount();
            result.reserve(count);
            for (int i = 0; i < count; ++i) {
                result.push_back(entries_[i]);
            }
            return result;
        }

        TrieLogEntry GetEntry(int index) const {
            return entries_[index];
        }

    private:
        // Helper to interpret the raw page data as the Header
        TrieValuePageHeader* GetHeader() const {
            return reinterpret_cast<TrieValuePageHeader*>(const_cast<char*>(data_));
        }

        // Memory Layout Mapping
        // We use a union to map the header and the entries array directly onto the 4KB buffer.
        union {
            char data_[4096];
            struct {
                TrieValuePageHeader header_chunk;
                TrieLogEntry entries_[VP_MAX_ENTRIES];
            };
        };
    };

} // namespace cmse