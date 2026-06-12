cmake_minimum_required(VERSION 3.16)
project(k8ssecdap-pipeline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS      "Build unit tests"  OFF)
option(BUILD_BENCHMARKS "Build benchmarks"  OFF)

# pipeline/CMakeLists.txt transitively builds libdsa (target `dsa`) via ../libdsa
add_subdirectory(pipeline)
