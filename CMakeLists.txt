cmake_minimum_required(VERSION 3.10)
project(CMSE_Project VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find Threads Package (REQUIRED for concurrency tests)
# This is necessary to use Threads::Threads in target_link_libraries
set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

# Set output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ------------------------------------------------------------------------------
# 1. Core Library
# ------------------------------------------------------------------------------
# We create a static library 'cmse_core' containing all shared source files.
# This prevents recompiling the same sources for every test executable.
add_library(cmse_core STATIC
    src/disk/disk_manager.cpp
    src/disk/disk_manager.h
    src/bufferpool/buffer_pool_manager.cpp
    src/bufferpool/buffer_pool_manager.h
    src/bufferpool/lru_replacer.cpp
    src/bufferpool/lru_replacer.h
    src/page/page.h
    src/common/types.h
    src/utils/log_manager.cpp
    src/utils/log_manager.h
    src/adapter/btree_adapter.cpp
    src/adapter/btree_adapter.h
    src/adapter/trie_adapter.h
    src/version/version_manager.h
    src/utils/log_parser.cpp
    src/utils/log_parser.h
    src/index/btree_index.cpp
    src/index/btree_index.h
    src/index/trie_index.cpp
    src/index/btree_iterator.cpp
)

target_include_directories(cmse_core PUBLIC src)

# ------------------------------------------------------------------------------
# 2. Tests
# ------------------------------------------------------------------------------
enable_testing()

# --- Buffer Pool Test ---
add_executable(buffer_pool_test tests/buffer_pool_test.cpp)
target_link_libraries(buffer_pool_test PRIVATE cmse_core)
add_test(NAME BufferPoolTest COMMAND buffer_pool_test)

# --- Concurrency Stress Test ---
# Note: We only list the test file here. The logic is linked via 'cmse_core'.
add_executable(concurrency_test tests/concurrency_test.cpp)

# Link against both the core project library and the system threads library
target_link_libraries(concurrency_test PRIVATE 
    cmse_core 
    Threads::Threads
)

add_test(NAME ConcurrencyTest COMMAND concurrency_test)


# --- Persistence Test ---
add_executable(persistence_test tests/persistence_test.cpp)
target_link_libraries(persistence_test PRIVATE cmse_core)
add_test(NAME PersistenceTest COMMAND persistence_test)


# --- Memory Edge Test ---
add_executable(memory_edge_test tests/memory_edge_test.cpp)
target_link_libraries(memory_edge_test PRIVATE cmse_core)
add_test(NAME MemoryEdgeTest COMMAND memory_edge_test)

# --- Fuzz Test ---
add_executable(fuzz_test tests/fuzz_test.cpp)
target_link_libraries(fuzz_test PRIVATE cmse_core)
add_test(NAME FuzzTest COMMAND fuzz_test)

# --- Disk Robustness Test ---
add_executable(disk_robustness_test tests/disk_robustness_test.cpp)
target_link_libraries(disk_robustness_test PRIVATE cmse_core)
add_test(NAME DiskRobustnessTest COMMAND disk_robustness_test)

# --- LRU Unit Test ---
add_executable(lru_replacer_test tests/lru_replacer_test.cpp)
target_link_libraries(lru_replacer_test PRIVATE cmse_core)
add_test(NAME LRUReplacerTest COMMAND lru_replacer_test)

# --- Log Manager Test ---
add_executable(log_manager_test tests/log_manager_test.cpp)
target_link_libraries(log_manager_test PRIVATE cmse_core)
add_test(NAME LogManagerTest COMMAND log_manager_test)

# --- BTree Adapter Test ---
add_executable(btree_adapter_test tests/btree_adapter_test.cpp)
target_link_libraries(btree_adapter_test PRIVATE cmse_core)
add_test(NAME BTreeAdapterTest COMMAND btree_adapter_test)

# --- Log Parser Test (Real Data) ---
add_executable(log_parser_test tests/log_parser_test.cpp)
target_link_libraries(log_parser_test PRIVATE cmse_core)
add_test(NAME LogParserTest COMMAND log_parser_test)

# --- Integration Test (The Big One) ---
add_executable(integration_test tests/integration_test.cpp)
target_link_libraries(integration_test PRIVATE cmse_core)
add_test(NAME IntegrationTest COMMAND integration_test)

# --- Tree Visualizer ---
add_executable(tree_visualizer tests/tree_visualizer.cpp)
target_link_libraries(tree_visualizer PRIVATE cmse_core)

# Range Scan Test
add_executable(range_scan_test tests/range_scan_test.cpp)
target_link_libraries(range_scan_test cmse_core)
add_test(NAME RangeScanTest COMMAND range_scan_test) # <--- ADDED THIS

# Trie Index Test
add_executable(trie_test tests/trie_test.cpp)
target_link_libraries(trie_test PRIVATE cmse_core)
add_test(NAME TrieTest COMMAND trie_test)

# =========================================================
#  INTERACTIVE TOOLS (Build Only - Do NOT add to CTest)
#  These tools wait for user input (cin). If you add_test
#  for them, your test suite will HANG forever!
# =========================================================

add_executable(query_runner tests/query_runner.cpp)
target_link_libraries(query_runner PRIVATE cmse_core)

add_executable(trie_visualizer tests/trie_visualizer.cpp)
target_link_libraries(trie_visualizer PRIVATE cmse_core)

add_executable(btree_visualizer tests/btree_visualizer.cpp)
target_link_libraries(btree_visualizer PRIVATE cmse_core)

add_executable(debug_search tests/debug_search.cpp)
target_link_libraries(debug_search PRIVATE cmse_core)

add_executable(debug_path tests/debug_path.cpp)
target_link_libraries(debug_path PRIVATE cmse_core)


add_executable(db_pager tests/db_pager.cpp)
target_link_libraries(db_pager PRIVATE cmse_core)


# =========================================================
#  AUTOMATED TESTS (Add to CTest)
#  These run automatically and return Pass/Fail.
# =========================================================

add_executable(integrity_check tests/integrity_check.cpp)
target_link_libraries(integrity_check PRIVATE cmse_core)
add_test(NAME IntegrityCheck COMMAND integrity_check) # <--- ADDED THIS

add_executable(btree_iterator_test tests/btree_iterator_test.cpp)
target_link_libraries(btree_iterator_test PRIVATE cmse_core)
add_test(NAME BTreeIteratorTest COMMAND btree_iterator_test) # <--- ADDED THIS

add_executable(btree_cow_test tests/btree_cow_test.cpp)
target_link_libraries(btree_cow_test PRIVATE cmse_core)
add_test(NAME BTreeCoWTest COMMAND btree_cow_test)

add_executable(btree_cow_stress_test tests/btree_cow_stress_test.cpp)
target_link_libraries(btree_cow_stress_test PRIVATE cmse_core)
add_test(NAME BTreeCoWStressTest COMMAND btree_cow_test)

