cmake_minimum_required(VERSION 3.10)
project(CMSE_Project VERSION 1.0 LANGUAGES CXX)

# --- 1. FORCE DEBUG MODE ---
# This ensures symbols are generated (-g) and optimizations are disabled (-O0)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Threads (Required for buffer pool latches)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- 2. DEFINE SOURCE LIST ---
# We define this variable so we can reuse the file list easily
set(CORE_SOURCES
    src/disk/disk_manager.cpp
    src/bufferpool/buffer_pool_manager.cpp
    src/bufferpool/lru_replacer.cpp
    src/utils/log_manager.cpp
    src/adapter/btree_adapter.cpp
    src/utils/log_parser.cpp
    src/index/btree_index.cpp
    src/index/trie_index.cpp
    src/index/btree_iterator.cpp
    src/version/version_manager.cpp
    src/utils/time_utils.cpp
)

# --- 3. STANDARD LIBRARY TARGET ---
# Good for the main app and release builds
add_library(cmse_core STATIC ${CORE_SOURCES})
target_include_directories(cmse_core PUBLIC src)

# --- Main CLI Application ---
add_executable(cmse_app src/main.cpp)
target_link_libraries(cmse_app PRIVATE cmse_core)


# =========================================================
#  DEBUG DEMOS (Direct Source Compilation)
# =========================================================

# 1. Standard Link (Might be hard to debug depending on IDE settings)
add_executable(1.1_btree_split demos/1.1_btree_split.cpp)
target_link_libraries(1.1_btree_split PRIVATE cmse_core)

# 2. DEBUG TARGET (The "White-Box" Version)
# We compile the core sources DIRECTLY into this executable.
# This guarantees the debugger can step into 'BTreeIndex::Insert'.
add_executable(1.1_btree_split_debug 
    demos/1.1_btree_split.cpp 
    ${CORE_SOURCES} 
)

# Include the 'src' folder so includes work
target_include_directories(1.1_btree_split_debug PRIVATE src)
# Link threads (since we aren't using cmse_core which usually handles this)
target_link_libraries(1.1_btree_split_debug PRIVATE Threads::Threads)


# Demo 1.2: Internal Split Debug Target
add_executable(1.2_btree_internal_split_debug 
    demos/1.2_btree_internal_split.cpp 
    ${CORE_SOURCES} 
)

target_include_directories(1.2_btree_internal_split_debug PRIVATE src)
target_link_libraries(1.2_btree_internal_split_debug PRIVATE Threads::Threads)


# Demo 1.3: Statistics Propagation
add_executable(1.3_btree_stats_debug 
    demos/1.3_btree_stats.cpp 
    ${CORE_SOURCES} 
)

target_include_directories(1.3_btree_stats_debug PRIVATE src)
target_link_libraries(1.3_btree_stats_debug PRIVATE Threads::Threads)


# Demo 2.1: Trie Split
add_executable(2.1_trie_split_debug 
    demos/2.1_trie_split.cpp 
    ${CORE_SOURCES} 
)
target_include_directories(2.1_trie_split_debug PRIVATE src)
target_link_libraries(2.1_trie_split_debug PRIVATE Threads::Threads)

# Demo 2.2: Trie Cap Safety
add_executable(2.2_trie_cap_debug 
    demos/2.2_trie_cap_safety.cpp 
    ${CORE_SOURCES} 
)
target_include_directories(2.2_trie_cap_debug PRIVATE src)
target_link_libraries(2.2_trie_cap_debug PRIVATE Threads::Threads)

# Demo 2.3: Shadow Paging
add_executable(3.1_shadow_paging_debug 
    demos/3.1_mvcc_shadow_paging.cpp 
    ${CORE_SOURCES} 
)
target_include_directories(3.1_shadow_paging_debug PRIVATE src)
target_link_libraries(3.1_shadow_paging_debug PRIVATE Threads::Threads)