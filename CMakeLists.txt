cmake_minimum_required(VERSION 3.16)
project(K8sSecDaP VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# All executables → build/bin (stable paths for Docker COPY)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_EBPF       "Build eBPF collector (needs libbpf)" OFF)
option(BUILD_TOOLS      "Build report trace tool"             ON)
option(BUILD_TESTS      "Build unit tests"                    OFF)
option(BUILD_BENCHMARKS "Build benchmarks"                    OFF)

# pipeline submodule builds libdsa (target `dsa`) + zt-pipeline
add_subdirectory(pipeline)

if(BUILD_TOOLS)
    add_subdirectory(tools)        # trace_algorithms links `dsa`
endif()

if(BUILD_EBPF)
    add_subdirectory(collector)
endif()
