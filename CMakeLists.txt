cmake_minimum_required(VERSION 3.10)
project(CMSE_Project VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ------------------------------------------------------------------------------
# 1. Core Library
# ------------------------------------------------------------------------------
# Using STATIC library to avoid Windows DLL export complexity
add_library(cmse_core STATIC
    src/disk/disk_manager.cpp
    src/disk/disk_manager.h
    src/bufferpool/buffer_pool_manager.cpp
    src/bufferpool/buffer_pool_manager.h
    src/adapter/bpm_adapter.h
    src/adapter/tree_adapter.h
    src/page/page.h
    src/common/types.h
)

target_include_directories(cmse_core PUBLIC src)

# ------------------------------------------------------------------------------
# 2. Tests
# ------------------------------------------------------------------------------
enable_testing()

add_executable(buffer_pool_test tests/buffer_pool_test.cpp)

target_link_libraries(buffer_pool_test PRIVATE cmse_core)

add_test(NAME BufferPoolTest COMMAND buffer_pool_test)