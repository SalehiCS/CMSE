#pragma once
#include "../common/types.h"
#include <cstring>

namespace cmse {

    // Forward declaration to grant friendship to the BufferPoolManager
    namespace bufferpool { class BufferPoolManager; }

    /**
     * PageHeader
     * Binary header stored at the very beginning (Offset 0) of every physical page.
     * This metadata is persistent and survives across system restarts.
     */
    struct PageHeader {
        /** The unique identifier for this page on the physical disk. */
        page_id_t page_id = INVALID_PAGE_ID;

        /** MVCC tracking: The version ID of the transaction that last modified this page. */
        version_id_t creation_version = INVALID_VERSION;

        /** Current occupancy: How many keys/records are currently stored in this page. */
        uint32_t key_count = 0;

        /** Type flag: 1 represents a Leaf node (stores data), 0 represents an Internal node (stores pivots). */
        uint8_t is_leaf = 0;

        /** Explicit padding to ensure 4-byte alignment of the structure. */
        uint8_t reserved[3];
    };

    /**
     * Page
     * The fundamental container for 4KB data blocks.
     * Logic: A Page acts as a wrapper for raw bytes. Upper layers cast the GetData()
     * result to specific types (like BTreeNodes or TrieNodes).
     */
    class Page {
        // Grant the BufferPoolManager exclusive rights to modify internal pin counts and dirty flags.
        friend class cmse::bufferpool::BufferPoolManager;

    public:
        /** * Accesses the usable data area of the page.
         * @return Pointer to the payload following the PageHeader.
         */
        inline char* GetData() { return data_ + sizeof(PageHeader); }

        /** Constant version of GetData() for read-only access. */
        inline const char* GetData() const { return data_ + sizeof(PageHeader); }

        /** * Accesses the persistent metadata.
         * Reinterprets the start of the data buffer as a PageHeader struct.
         */
        inline PageHeader* GetHeader() { return reinterpret_cast<PageHeader*>(data_); }

        /** * Helper to retrieve the Page ID directly from the embedded header.
         */
        inline page_id_t GetPageId() { return GetHeader()->page_id; }

        /** * Diagnostic accessor: Returns how many components are currently "pinning" this page in RAM.
         */
        inline int GetPinCount() const { return pin_count_; }

        /** * Wipes the entire 4KB buffer (header + payload) with zeros.
         * Typically used when a page is first allocated or recycled.
         */
        void ResetMemory() { std::memset(data_, 0, PAGE_SIZE); }

        /** * Returns true if the memory content differs from the version stored on disk.
         */
        bool IsDirty() { return is_dirty_; }

    private:
        /** The physical 4096-byte memory buffer. This is what gets read/written to disk. */
        char data_[PAGE_SIZE];

        // --- IN-MEMORY METADATA (Not persisted to disk) ---

        /** Flag indicating if the page has been modified since it was fetched from disk. */
        bool is_dirty_ = false;

        /** Counter for active references; prevents the BufferPoolManager from evicting a page in use. */
        int pin_count_ = 0;
    };

} // namespace cmse