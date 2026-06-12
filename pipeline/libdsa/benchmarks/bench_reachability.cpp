#include "dsa/graph/directed_graph.h"
#include "dsa/graph/bfs.h"
#include "dsa/graph/dijkstra.h"
#include <benchmark/benchmark.h>
#include <random>

using namespace dsa::graph;

static DirectedGraph generate_random_graph(size_t n, size_t m, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, n - 1);

    DirectedGraph g;
    for (uint32_t i = 0; i < n; ++i) g.add_vertex(i);

    size_t added = 0;
    while (added < m) {
        uint32_t u = dist(rng);
        uint32_t v = dist(rng);
        if (u != v && !g.has_edge(u, v)) {
            g.add_edge(u, v);
            added++;
        }
    }
    return g;
}

// --- BFS Benchmarks ---

static void BM_BFS_Reachability(benchmark::State& state) {
    size_t n = state.range(0);
    int k = state.range(1);
    auto g = generate_random_graph(n, n * 3);

    BFSReachability bfs;
    for (auto _ : state) {
        auto result = bfs.reachable_within(g, 0, k);
        benchmark::DoNotOptimize(result);
    }
    state.counters["vertices"] = n;
    state.counters["max_hops"] = k;
}
BENCHMARK(BM_BFS_Reachability)
    ->Args({1000, 1})->Args({1000, 2})->Args({1000, 3})->Args({1000, 5})
    ->Args({10000, 1})->Args({10000, 2})->Args({10000, 3})
    ->Args({50000, 1})->Args({50000, 2})->Args({50000, 3});

// --- Dijkstra Benchmarks ---

static void BM_Dijkstra_Reachability(benchmark::State& state) {
    size_t n = state.range(0);
    int k = state.range(1);
    auto g = generate_random_graph(n, n * 3);

    DijkstraReachability dijk;
    for (auto _ : state) {
        auto result = dijk.reachable_within(g, 0, k);
        benchmark::DoNotOptimize(result);
    }
    state.counters["vertices"] = n;
    state.counters["max_hops"] = k;
}
BENCHMARK(BM_Dijkstra_Reachability)
    ->Args({1000, 1})->Args({1000, 2})->Args({1000, 3})->Args({1000, 5})
    ->Args({10000, 1})->Args({10000, 2})->Args({10000, 3})
    ->Args({50000, 1})->Args({50000, 2})->Args({50000, 3});
