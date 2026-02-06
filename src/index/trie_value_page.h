#pragma once

#include "../src/common/types.h"
#include "../src/page/page.h"
#include <cstring>
#include <vector>

namespace cmse {

    /**
     * TrieLogEntry: The payload structure within a Trie search bucket.
     * We store the Timestamp as the unique identifier/key to allow
     * cross-referencing with the B+Tree (Clustered Index).
     */
    struct TrieLogEntry {
        /** 8-byte Unix microsecond timestamp; serves as the primary key. */
        int64_t timestamp;
        /** 1-byte metadata for fast server-side filtering (INFO, WARN, etc.). */
        uint8_t log_level;
    };

    /**
     * TrieValuePageHeader: Metadata for the data bucket.
     * Occupies the very beginning of the TrieValuePage.
     */
    struct TrieValuePageHeader {
        /** Pointer to the next page in the chain for this specific prefix (Overflow/Linked List). */
        page_id_t next_page_id;
        /** The current number of active records stored in this page. */
        int16_t count;
    };

    // --- Dynamic Capacity Calculations ---

    /** The size of the bucket's management metadata. */
    constexpr int VP_HEADER_SIZE = sizeof(TrieValuePageHeader);

    /** Padding to ensure we never overflow the 4096-byte hardware page boundary. */
    constexpr int VP_SAFETY_MARGIN = 32;

    /** Maximum entries per page (~250-300 entries depending on struct alignment). */
    constexpr int VP_MAX_ENTRIES = (4096 - VP_HEADER_SIZE - VP_SAFETY_MARGIN) / sizeof(TrieLogEntry);

    /**
     * TrieValuePage
     * A page-based storage container representing a leaf node in the string prefix index.
     * Architecture: If many logs share the same prefix, they are chained via next_page_id
     * in a disk-based linked list.
     */
    class TrieValuePage {
    public:
        // --- Initialization ---

        /** * Resets the bucket state.
         * Important: Initializes the next_page_id to INVALID to terminate the list.
         */
        void Init() {
            auto* header = GetHeader();
            header->next_page_id = INVALID_PAGE_ID;
            header->count = 0;
        }

        // --- Getters & Setters ---

        /** Returns the number of logs stored in this specific bucket page. */
        int16_t GetCount() const {
            return GetHeader()->count;
        }

        /** Returns the pointer to the next page if this prefix spans multiple pages. */
        page_id_t GetNextPageId() const {
            return GetHeader()->next_page_id;
        }

        /** Links this page to an overflow page. */
        void SetNextPageId(page_id_t next_page_id) {
            GetHeader()->next_page_id = next_page_id;
        }

        /** Checks if the page has reached its physical capacity (VP_MAX_ENTRIES). */
        bool IsFull() const {
            return GetCount() >= VP_MAX_ENTRIES;
        }

        // --- Operations ---

        /**
         * Inserts a new log entry into the first available slot.
         * @return true if inserted, false if page is full (caller must handle overflow).
         */
        bool Insert(int64_t timestamp, uint8_t log_level) {
            if (IsFull()) {
                return false; // Page reached limit
            }

            auto* header = GetHeader();
            // Directly modify the entry in the mapped buffer
            entries_[header->count].timestamp = timestamp;
            entries_[header->count].log_level = log_level;
            header->count++; // Atomically track occupancy

            return true;
        }

        /**
         * Iterates through the entries and returns a vector for application-level logic.
         * Useful for the Query Engine to gather results after a Trie traversal.
         */
        std::vector<TrieLogEntry> GetEntries() const {
            std::vector<TrieLogEntry> result;
            int count = GetCount();
            result.reserve(count); // Pre-allocate to avoid multiple reallocations
            for (int i = 0; i < count; ++i) {
                result.push_back(entries_[i]);
            }
            return result;
        }

        /** Retrieves a single record by its internal array index. */
        TrieLogEntry GetEntry(int index) const {
            return entries_[index];
        }

    private:
        /** * Internal helper to cast the raw memory buffer into a manageable header.
         * Reinterpret_cast allows direct manipulation of disk bytes.
         */
        TrieValuePageHeader* GetHeader() const {
            return reinterpret_cast<TrieValuePageHeader*>(const_cast<char*>(data_));
        }

        // --- Memory Layout Mapping ---

        /** * The Union Trick: Maps the 4KB raw buffer and the structured layout to
         * the same physical memory location. Zero-copy abstraction.
         */
        union {
            /** Raw byte array consumed by the Disk Manager. */
            char data_[4096];
            /** Structured view used by the C++ logic. */
            struct {
                /** Fixed-offset header. */
                TrieValuePageHeader header_chunk;
                /** Sequence of fixed-size log entries. */
                TrieLogEntry entries_[VP_MAX_ENTRIES];
            };
        };
    };

} // namespace cmse