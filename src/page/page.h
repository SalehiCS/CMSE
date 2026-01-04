#pragma once
#include "../common/types.h"
#include <cstring>

namespace cmse {

    // Helper forward declaration
    namespace bufferpool { class BufferPoolManager; }

    /**
     * PageHeader
     * Stored at the beginning of every page data.
     * Contains metadata required for versioning and page management.
     */
    struct PageHeader {
        page_id_t page_id = INVALID_PAGE_ID;
        version_t creation_version = INVALID_VERSION; // Version that created/modified this page
        uint32_t key_count = 0;                       // Number of keys/entries in the page
        uint8_t is_leaf = 0;                          // 1 if leaf, 0 if internal
        uint8_t reserved[3];                          // Padding for alignment
    };

    /**
     * Page
     * Wrapper around the raw 4KB data array.
     * Note: This class is merely a container. It does not own the memory (managed by BufferPool).
     */
    class Page {
        // Allow BufferPoolManager to access private members like pin_count_
        friend class cmse::bufferpool::BufferPoolManager;

    public:
        // Returns pointer to the data payload (skipping the header)
        inline char* GetData() { return data_ + sizeof(PageHeader); }
        inline const char* GetData() const { return data_ + sizeof(PageHeader); }

        // Returns pointer to the header
        inline PageHeader* GetHeader() { return reinterpret_cast<PageHeader*>(data_); }

        // Convenience accessor for Page ID
        inline page_id_t GetPageId() { return GetHeader()->page_id; }

        // Zeros out the page data
        void ResetMemory() { std::memset(data_, 0, PAGE_SIZE); }

    private:
        char data_[PAGE_SIZE]; // Actual 4KB data

        // In-memory metadata (not written to disk)
        bool is_dirty_ = false;
        int pin_count_ = 0;
    };

} // namespace cmse