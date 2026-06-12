#include "dsa/frequency/count_min_sketch.h"
#include "dsa/frequency/hash_map_exact.h"
#include <benchmark/benchmark.h>
#include <cstdint>
#include <random>

using namespace dsa::frequency;

// Generate random uint32 keys
static std::vector<uint32_t> generate_keys(size_t count, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::vector<uint32_t> keys(count);
    for (auto& k : keys) k = rng();
    return keys;
}

// --- Count-Min Sketch Benchmarks ---

static void BM_CMS_Record(benchmark::State& state) {
    CountMinSketch cms(state.range(0), 5);
    auto keys = generate_keys(100000);
    size_t i = 0;

    for (auto _ : state) {
        cms.record(&keys[i % keys.size()], sizeof(uint32_t));
        i++;
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["memory_kb"] = cms.memory_usage() / 1024.0;
}
BENCHMARK(BM_CMS_Record)->Arg(512)->Arg(1024)->Arg(2048)->Arg(4096);

static void BM_CMS_Estimate(benchmark::State& state) {
    CountMinSketch cms(2048, 5);
    auto keys = generate_keys(100000);
    for (auto& k : keys) cms.record(&k, sizeof(k));

    size_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(cms.estimate(&keys[i % keys.size()], sizeof(uint32_t)));
        i++;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CMS_Estimate);

// --- Hash Map Exact Benchmarks ---

static void BM_HashMap_Record(benchmark::State& state) {
    HashMapExact hm;
    auto keys = generate_keys(100000);
    size_t i = 0;

    for (auto _ : state) {
        hm.record(&keys[i % keys.size()], sizeof(uint32_t));
        i++;
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["memory_kb"] = hm.memory_usage() / 1024.0;
}
BENCHMARK(BM_HashMap_Record);

static void BM_HashMap_Estimate(benchmark::State& state) {
    HashMapExact hm;
    auto keys = generate_keys(100000);
    for (auto& k : keys) hm.record(&k, sizeof(k));

    size_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(hm.estimate(&keys[i % keys.size()], sizeof(uint32_t)));
        i++;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HashMap_Estimate);

// --- Accuracy comparison (not timing, but error measurement) ---

static void BM_CMS_ErrorRate(benchmark::State& state) {
    uint32_t width = state.range(0);
    CountMinSketch cms(width, 5);
    HashMapExact exact;

    auto keys = generate_keys(100000);
    for (auto& k : keys) {
        cms.record(&k, sizeof(k));
        exact.record(&k, sizeof(k));
    }

    double total_error = 0;
    size_t count = 0;
    auto query_keys = generate_keys(1000, 99);

    for (auto _ : state) {
        for (auto& k : query_keys) {
            int64_t cms_est = cms.estimate(&k, sizeof(k));
            int64_t exact_val = exact.estimate(&k, sizeof(k));
            total_error += (cms_est - exact_val);
            count++;
        }
    }
    state.counters["avg_overcount"] = total_error / count;
    state.counters["width"] = width;
}
BENCHMARK(BM_CMS_ErrorRate)->Arg(256)->Arg(512)->Arg(1024)->Arg(2048)->Arg(4096);
