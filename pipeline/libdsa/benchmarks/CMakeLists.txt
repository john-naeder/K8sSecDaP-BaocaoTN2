include(FetchContent)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(bench_frequency bench_frequency.cpp)
target_link_libraries(bench_frequency dsa benchmark::benchmark benchmark::benchmark_main)

add_executable(bench_scc bench_scc.cpp)
target_link_libraries(bench_scc dsa benchmark::benchmark benchmark::benchmark_main)

add_executable(bench_reachability bench_reachability.cpp)
target_link_libraries(bench_reachability dsa benchmark::benchmark benchmark::benchmark_main)

add_executable(bench_lpm bench_lpm.cpp)
target_link_libraries(bench_lpm dsa benchmark::benchmark benchmark::benchmark_main)
