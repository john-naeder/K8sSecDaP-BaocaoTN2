cmake_minimum_required(VERSION 3.16)
project(zt-pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# libdsa — the DSA library
# Only add as subdirectory if not already included (standalone build)
if(NOT TARGET dsa)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libdsa ${CMAKE_CURRENT_BINARY_DIR}/libdsa)
endif()

# Pipeline engine
add_executable(zt-pipeline
    src/config_loader.cpp
    src/engine.cpp
)

target_include_directories(zt-pipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zt-pipeline PRIVATE dsa)
