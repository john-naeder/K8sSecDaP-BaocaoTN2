#include "dsa/trie/lpm_trie.h"
#include "dsa/common.h"
#include <benchmark/benchmark.h>
#include <random>

using namespace dsa::trie;
using dsa::ip_from_octets;

// Populate trie with realistic CIDR ranges
static void populate_trie(LPMTrie& trie, size_t num_prefixes, uint32_t seed = 42) {
    std::mt19937 rng(seed);

    // Add some well-known ranges first
    trie.insert(ip_from_octets(10, 244, 0, 0), 16, 1);  // Pod net
    trie.insert(ip_from_octets(10, 96, 0, 0), 12, 2);   // Svc net
    trie.insert(ip_from_octets(172, 16, 0, 0), 12, 3);   // Private
    trie.insert(ip_from_octets(192, 168, 0, 0), 16, 4);  // Private

    // Add random /24 prefixes
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (size_t i = 4; i < num_prefixes; ++i) {
        uint32_t prefix = ip_from_octets(dist(rng), dist(rng), dist(rng), 0);
        trie.insert(prefix, 24, static_cast<int32_t>(i));
    }
}

static void BM_LPM_Lookup(benchmark::State& state) {
    LPMTrie trie;
    populate_trie(trie, state.range(0));

    std::mt19937 rng(99);
    std::vector<uint32_t> ips(10000);
    for (auto& ip : ips) ip = rng();

    size_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(trie.lookup(ips[i % ips.size()]));
        i++;
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["prefixes"] = trie.size();
    state.counters["memory_kb"] = trie.memory_usage() / 1024.0;
}
BENCHMARK(BM_LPM_Lookup)->Arg(10)->Arg(100)->Arg(1000)->Arg(5000);

static void BM_LPM_Insert(benchmark::State& state) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    for (auto _ : state) {
        LPMTrie trie;
        for (int i = 0; i < state.range(0); ++i) {
            uint32_t prefix = ip_from_octets(dist(rng), dist(rng), dist(rng), 0);
            trie.insert(prefix, 24, i);
        }
        benchmark::DoNotOptimize(trie.size());
    }
    state.counters["prefixes"] = state.range(0);
}
BENCHMARK(BM_LPM_Insert)->Arg(10)->Arg(100)->Arg(1000)->Arg(5000);
