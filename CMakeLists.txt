cmake_minimum_required(VERSION 3.10)
project(CMSE_Project VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# DEBUG SYMBOLS CONFIGURATION - ADD THIS SECTION
# ------------------------------------------------------------------------------
# Ensure debug symbols are generated for all configurations
if(MSVC)
    # Visual Studio: Generate full debug info
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /Od")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Zi")
    
    # Linker debug flags
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG")
else()
    # GCC/Clang: Generate debug symbols, disable optimizations
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g")
endif()

# 1. Find Threads Package (REQUIRED for concurrency tests)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

# Set output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ------------------------------------------------------------------------------
# 1. Core Library
# ------------------------------------------------------------------------------
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
    src/version/version_manager.h
    src/utils/log_parser.cpp
    src/utils/log_parser.h
    src/index/btree_index.cpp
    src/index/btree_index.h
    src/index/trie_index.cpp
    src/index/btree_iterator.cpp
    src/version/version_manager.cpp
    src/utils/time_utils.h
    src/utils/time_utils.cpp
)

target_include_directories(cmse_core PUBLIC src)

# Ensure debug symbols for the library
if(MSVC)
    target_compile_options(cmse_core PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_compile_options(cmse_core PRIVATE $<$<CONFIG:RelWithDebInfo>:/Zi>)
endif()

# --- Main CLI Application ---
add_executable(cmse_app src/main.cpp)
target_link_libraries(cmse_app PRIVATE cmse_core)

# Ensure debug symbols for the executable
if(MSVC)
    target_compile_options(cmse_app PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_compile_options(cmse_app PRIVATE $<$<CONFIG:RelWithDebInfo>:/Zi>)
endif()

add_executable(1.2_btree_internal_split demos/1.2_btree_internal_split.cpp)
target_link_libraries(1.2_btree_internal_split PRIVATE cmse_core)

# Ensure debug symbols for the demo
if(MSVC)
    target_compile_options(1.2_btree_internal_split PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_compile_options(1.2_btree_internal_split PRIVATE $<$<CONFIG:RelWithDebInfo>:/Zi>)
endif()