#pragma once
#include "logger.h"
#include "../page/page.h"
#include "../adapter/btree_adapter.h"

inline void WatchPage4036(cmse::Page* page, const char* where) {
    if (!page) return;
    if (page->GetPageId() != 4036) return;

    auto* hdr = reinterpret_cast<cmse::adapter::BPlusNodeHeader*>(page->GetData());

    LOG_DEBUG_QUERY("[WATCH 4036] " << where
        << " type=" << (hdr->is_leaf ? "LEAF" : "INTERNAL")
        << " keys=" << hdr->key_count
        << " ptr=" << page);
}
