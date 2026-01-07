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
    src/page/page.h
    src/common/types.h
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