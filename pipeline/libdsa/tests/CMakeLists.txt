include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Test executables
add_executable(test_frequency
    test_count_min_sketch.cpp
    test_hash_map_exact.cpp
    test_heavy_hitters.cpp
)
target_link_libraries(test_frequency dsa GTest::gtest_main)

add_executable(test_graph
    test_directed_graph.cpp
    test_tarjan.cpp
    test_kosaraju.cpp
    test_bfs.cpp
    test_dijkstra.cpp
)
target_link_libraries(test_graph dsa GTest::gtest_main)

add_executable(test_trie
    test_lpm_trie.cpp
)
target_link_libraries(test_trie dsa GTest::gtest_main)

add_executable(test_registry
    test_registry.cpp
)
target_link_libraries(test_registry dsa GTest::gtest_main)

add_executable(test_soc
    test_soc.cpp
)
target_link_libraries(test_soc dsa GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_frequency)
gtest_discover_tests(test_graph)
gtest_discover_tests(test_trie)
gtest_discover_tests(test_registry)
gtest_discover_tests(test_soc)
