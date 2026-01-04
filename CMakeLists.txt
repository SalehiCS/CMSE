cmake_minimum_required(VERSION 3.16)

project(CMSE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cmse
    src/main.cpp
 "src/common/types.h" "src/page/page.h" "src/adapter/bpmadapter.h" "src/adapter/tree_adapter.h")
