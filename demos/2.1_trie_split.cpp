/**
 * Test Case 2.1: Trie Prefix Split Visualizer
 * * OBJECTIVE:
 * Demonstrate the space-saving "Prefix Compression" of the Trie.
 * * LOGIC:
 * 1. Insert a rich dataset of overlapping words (sys, system, syslog, etc.).
 * 2. Trace the traversal path MANUALLY using the Buffer Pool and TriePage casting.
 * 3. Prove that 's' -> 'y' -> 's' point to the exact same Page IDs for different words.
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include <algorithm>

 // Core Engine Headers
#include "../src/disk/disk_manager.h"
#include "../src/bufferpool/buffer_pool_manager.h"
#include "../src/index/trie_index.h"
#include "../src/bufferpool/page_guard.h" 
// We must include the internal page layouts to "peek" inside
#include "../src/index/trie_page.h" 

using namespace cmse;

// Structure to hold a step in the traversal
struct TrieStep {
    char key;
    page_id_t page_id;
    bool is_terminal;
};

// --- HELPER: MANUAL PATH TRACER ---
// This function bypasses the TrieIndex abstraction and reads the disk pages directly.
std::vector<TrieStep> TracePath(bufferpool::BufferPoolManager* bpm, page_id_t root_id, const std::string& word) {
    std::vector<TrieStep> path;
    page_id_t curr_id = root_id;

    if (curr_id == INVALID_PAGE_ID) return path;

    // Record Root
    path.push_back({ '.', curr_id, false });

    for (char c : word) {
        // 1. Fetch the raw page from Buffer Pool
        Page* raw_page = bpm->FetchPage(curr_id);
        PageGuard guard(bpm, raw_page);

        // 2. Cast raw bytes to your TriePage structure
        // Note: GetData() returns the payload after the generic PageHeader.
        auto* node = reinterpret_cast<TriePage*>(guard->GetData());

        // 3. Update the last step's terminal status based on the *current* node state
        // (Visualizing the path node-by-node)
        if (!path.empty()) {
            path.back().is_terminal = node->IsTerminal();
        }

        // 4. Find the child pointer using the TriePage API
        page_id_t next_id = node->GetChild(c);

        if (next_id == INVALID_PAGE_ID) break; // Path ends

        // 5. Record step
        path.push_back({ c, next_id, false }); // Terminal status will be checked in next iteration or end
        curr_id = next_id;
    }

    // Check terminal status of the final node we landed on
    Page* raw_last = bpm->FetchPage(curr_id);
    PageGuard guard_last(bpm, raw_last);
    auto* node_last = reinterpret_cast<TriePage*>(guard_last->GetData());
    if (!path.empty()) {
        path.back().is_terminal = node_last->IsTerminal();
    }

    return path;
}

void PrintBanner(const std::string& msg) {
    std::cout << "\n======================================================\n";
    std::cout << "  " << msg << "\n";
    std::cout << "======================================================\n";
}

void VisualizeGroup(const std::string& title, const std::vector<std::string>& words,
    bufferpool::BufferPoolManager* bpm, page_id_t root_id) {

    std::cout << "\n--- " << title << " ---" << std::endl;
    std::cout << std::left << std::setw(15) << "Word" << " | " << "Traversal Path (Char:PageID)" << std::endl;
    std::cout << "-------------------------------------------------------------------------------" << std::endl;

    for (const auto& w : words) {
        auto path = TracePath(bpm, root_id, w);

        std::cout << std::left << std::setw(15) << w << " | ";

        for (const auto& step : path) {
            std::string color = "";

            // Heuristic coloring: Root is Gray, Nodes are standard, Terminals are Green
            if (step.key == '.') {
                std::cout << "\033[90m[Root:" << step.page_id << "]\033[0m ";
            }
            else {
                if (step.is_terminal) color = "\033[1;32m"; // Bold Green
                std::cout << color << "->" << step.key << ":" << step.page_id << "\033[0m ";
            }
        }
        std::cout << std::endl;
    }
}

int main() {
    // 1. SETUP
    std::string db_file = "test_trie_visual.db";
    std::remove(db_file.c_str());

    auto disk = std::make_unique<disk::DiskManager>(db_file);
    auto bpm = std::make_unique<bufferpool::BufferPoolManager>(100, disk.get());
    auto trie = std::make_unique<index::TrieIndex>(bpm.get());

    // 2. INSERT RICH DATASET
    PrintBanner("PHASE 1: DICTIONARY INSERTION");
    std::vector<std::string> dictionary = {
        // Sys group
        "sys", "syslog", "system", "sysadmin", "syntax",
        // Net group
        "net", "network", "netflix", "netmask",
        // Data group
        "data", "database", "datastore", "datum", "date",
        // Short group
        "a", "an", "ant", "any"
    };

    std::cout << "Inserting " << dictionary.size() << " words..." << std::endl;
    for (const auto& w : dictionary) {
        // Insert with dummy timestamp (1000) and priority (1)
        trie->Insert(w, 1000, 1);
    }

    // 3. VISUALIZATION
    PrintBanner("PHASE 2: PHYSICAL PATH TRACING");
    std::cout << "Tracing Page IDs to prove 'Prefix Sharing'..." << std::endl;

    page_id_t root_id = trie->GetRootId();

    VisualizeGroup("GROUP 1: System prefixes",
        { "sys", "system", "syslog", "sysadmin" }, bpm.get(), root_id);

    VisualizeGroup("GROUP 2: Network prefixes",
        { "net", "network", "netflix" }, bpm.get(), root_id);

    VisualizeGroup("GROUP 3: Data prefixes",
        { "data", "date", "datum", "database" }, bpm.get(), root_id);

    std::cout << "\n[Analysis] Observe the Page IDs:" << std::endl;
    std::cout << "1. 'sys' (s->y->s) shares the same Page IDs in Group 1." << std::endl;
    std::cout << "2. 'data' vs 'date' split only after 'dat' (d->a->t)." << std::endl;
    std::cout << "3. Green nodes indicate 'IsTerminal=true'." << std::endl;

    std::remove(db_file.c_str());
    return 0;
}