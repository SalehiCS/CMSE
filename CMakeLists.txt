cmake_minimum_required(VERSION 3.16)

project(CMSE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cmse
    src/main.cpp
 "src/common/types.h" "src/page/page.h" "src/adapter/bpmadapter.h" "src/adapter/tree_adapter.h")

 # --- Test Configuration ---
enable_testing()

# Create the test executable
add_executable(buffer_pool_test tests/buffer_pool_test.cpp 
    src/bufferpool/buffer_pool_manager.cpp
    src/disk/disk_manager.cpp
    # Add other necessary source files here if needed later
)

# Register the test with CTest (allows running tests via 'ctest' command)
add_test(NAME BufferPoolTest COMMAND buffer_pool_test)
