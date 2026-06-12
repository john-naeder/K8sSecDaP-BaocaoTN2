#include "dsa/graph/directed_graph.h"
#include "dsa/graph/tarjan.h"
#include "dsa/graph/kosaraju.h"
#include <benchmark/benchmark.h>
#include <random>

using namespace dsa::graph;

// Generate a random directed graph with n vertices and m edges
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

// Generate a graph with known SCCs (clusters with internal cycles + inter-cluster edges)
static DirectedGraph generate_clustered_graph(size_t num_clusters, size_t cluster_size, uint32_t seed = 42) {
    DirectedGraph g;
    std::mt19937 rng(seed);

    for (size_t c = 0; c < num_clusters; ++c) {
        uint32_t base = c * cluster_size;
        // Create cycle within cluster
        for (uint32_t i = 0; i < cluster_size; ++i) {
            g.add_edge(base + i, base + (i + 1) % cluster_size);
        }
        // Add random edges within cluster
        std::uniform_int_distribution<uint32_t> dist(0, cluster_size - 1);
        for (size_t i = 0; i < cluster_size; ++i) {
            g.add_edge(base + dist(rng), base + dist(rng));
        }
    }

    // Inter-cluster edges (one direction only → no merging SCCs)
    for (size_t c = 0; c + 1 < num_clusters; ++c) {
        g.add_edge(c * cluster_size, (c + 1) * cluster_size);
    }

    return g;
}

// --- Tarjan Benchmarks ---

static void BM_Tarjan_Random(benchmark::State& state) {
    size_t n = state.range(0);
    size_t m = n * 3; // sparse graph
    auto g = generate_random_graph(n, m);

    Tarjan t;
    for (auto _ : state) {
        auto sccs = t.find_sccs(g);
        benchmark::DoNotOptimize(sccs);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["vertices"] = n;
    state.counters["edges"] = g.num_edges();
}
BENCHMARK(BM_Tarjan_Random)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000);

static void BM_Tarjan_Clustered(benchmark::State& state) {
    size_t clusters = state.range(0);
    auto g = generate_clustered_graph(clusters, 10);

    Tarjan t;
    for (auto _ : state) {
        auto sccs = t.find_sccs(g);
        benchmark::DoNotOptimize(sccs);
    }
    state.counters["vertices"] = g.num_vertices();
    state.counters["sccs_expected"] = clusters;
}
BENCHMARK(BM_Tarjan_Clustered)->Arg(10)->Arg(100)->Arg(1000)->Arg(5000);

// --- Kosaraju Benchmarks ---

static void BM_Kosaraju_Random(benchmark::State& state) {
    size_t n = state.range(0);
    size_t m = n * 3;
    auto g = generate_random_graph(n, m);

    Kosaraju k;
    for (auto _ : state) {
        auto sccs = k.find_sccs(g);
        benchmark::DoNotOptimize(sccs);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["vertices"] = n;
}
BENCHMARK(BM_Kosaraju_Random)->Arg(100)->Arg(1000)->Arg(10000)->Arg(50000);

static void BM_Kosaraju_Clustered(benchmark::State& state) {
    size_t clusters = state.range(0);
    auto g = generate_clustered_graph(clusters, 10);

    Kosaraju k;
    for (auto _ : state) {
        auto sccs = k.find_sccs(g);
        benchmark::DoNotOptimize(sccs);
    }
    state.counters["vertices"] = g.num_vertices();
}
BENCHMARK(BM_Kosaraju_Clustered)->Arg(10)->Arg(100)->Arg(1000)->Arg(5000);
