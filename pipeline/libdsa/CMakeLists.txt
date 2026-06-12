cmake_minimum_required(VERSION 3.16)
project(libdsa VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Library sources
add_library(dsa STATIC
    src/registry.cpp
    src/frequency/count_min_sketch.cpp
    src/frequency/hash_map_exact.cpp
    src/frequency/heavy_hitters.cpp
    src/graph/directed_graph.cpp
    src/graph/tarjan.cpp
    src/graph/kosaraju.cpp
    src/graph/bfs.cpp
    src/graph/dijkstra.cpp
    src/trie/lpm_trie.cpp
    # SOC (Phase 6)
    src/soc/alert.cpp
    src/soc/alert_handler_chain.cpp
    src/soc/file_alert_handler.cpp
    src/soc/stdout_alert_handler.cpp
    src/soc/nats_alert_handler.cpp
    src/soc/register_handlers.cpp
)

target_include_directories(dsa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Optional: link against cnats (nats-c) for real NATS publishing.
# When OFF, the "nats" handler logs to stderr with [nats-stub] prefix.
option(BUILD_SOC_NATS "Link libdsa against nats-c for real NATS publishing" OFF)
if(BUILD_SOC_NATS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NATS REQUIRED libnats)
    target_include_directories(dsa PRIVATE ${NATS_INCLUDE_DIRS})
    target_link_libraries(dsa PUBLIC ${NATS_LIBRARIES})
    target_compile_definitions(dsa PUBLIC DSA_HAVE_NATS=1)
endif()

# Tests
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS OR CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

# Benchmarks
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
