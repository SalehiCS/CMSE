#pragma once
#include "../page/page.h"

namespace cmse::adapter {

    /**
     * BufferPoolAdapter
     * Abstract interface that VersionManager uses to interact with the Buffer Pool.
     * This decouples the versioning logic from the specific BufferPool implementation.
     */
    class BufferPoolAdapter {
    public:
        virtual ~BufferPoolAdapter() = default;

        // Fetches a page from disk/cache and pins it. Returns nullptr on failure.
        virtual Page* FetchPage(page_id_t page_id) = 0;

        // Unpins a page. If is_dirty is true, the page is marked for write-back.
        virtual bool UnpinPage(page_id_t page_id, bool is_dirty) = 0;

        // Allocates a new page on disk and returns it pinned.
        // out_page_id is updated with the new page's ID.
        virtual Page* NewPage(page_id_t& out_page_id) = 0;

        // Forces a page to be written to disk immediately.
        virtual bool FlushPage(page_id_t page_id) = 0;

        // Flushes all dirty pages to disk.
        virtual void FlushAll() = 0;
    };

} // namespace cmse::adapter