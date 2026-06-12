#include "dsa/frequency/heavy_hitters.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace dsa::frequency;

namespace {

void record_str(HeavyHitters& hh, const std::string& s) {
    hh.record(s.data(), s.size());
}
uint64_t est_str(const HeavyHitters& hh, const std::string& s) {
    return hh.estimate(s.data(), s.size());
}

} // namespace

TEST(HeavyHitters, SmallKThrows) {
    EXPECT_THROW(HeavyHitters(1), std::invalid_argument);
}

TEST(HeavyHitters, Name) {
    HeavyHitters hh(4);
    EXPECT_EQ(hh.name(), "heavy_hitters");
}

TEST(HeavyHitters, FitsInCountersExact) {
    // k=4 means up to 3 active counters; 3 distinct keys fit exactly.
    HeavyHitters hh(4);
    for (int i = 0; i < 5; ++i) record_str(hh, "a");
    for (int i = 0; i < 3; ++i) record_str(hh, "b");
    record_str(hh, "c");

    EXPECT_EQ(est_str(hh, "a"), 5u);
    EXPECT_EQ(est_str(hh, "b"), 3u);
    EXPECT_EQ(est_str(hh, "c"), 1u);
    EXPECT_EQ(hh.stream_length(), 9u);
}

TEST(HeavyHitters, DecrementsAllOnOverflow) {
    // k=3 means up to 2 active counters. Classic worked example.
    // Stream: a, b, c, a, d, a, b, e
    HeavyHitters hh(3);
    for (char c : std::string("abcadabe")) {
        std::string s(1, c);
        record_str(hh, s);
    }
    EXPECT_EQ(hh.stream_length(), 8u);

    // Dominant 'a' survives; others should be <= exact.
    EXPECT_LE(est_str(hh, "a"), 3u);
    EXPECT_LE(est_str(hh, "b"), 2u);
    EXPECT_LE(est_str(hh, "c"), 1u);
    EXPECT_LE(est_str(hh, "d"), 1u);
    EXPECT_LE(est_str(hh, "e"), 1u);

    // Heavy hitter (a, with exact freq 3 > m/k = 8/3) must be present.
    EXPECT_GT(est_str(hh, "a"), 0u);
}

TEST(HeavyHitters, HeavyHitterGuarantee) {
    // Any key with f(x) > m/k must survive with f_hat(x) > 0.
    HeavyHitters hh(10);
    const int heavy = 500;
    const int m = 900;
    for (int i = 0; i < heavy; ++i) record_str(hh, "HEAVY");
    for (int i = 0; i < m - heavy; ++i) record_str(hh, "x" + std::to_string(i));
    // heavy / m = 500/900 > 1/10 => guaranteed survive.
    EXPECT_GT(est_str(hh, "HEAVY"), 0u);
}

TEST(HeavyHitters, UnderestimateProperty) {
    // Misra-Gries never overestimates: f_hat(x) <= f(x) for all x.
    HeavyHitters hh(5);
    std::vector<std::string> stream = {
        "a","b","a","c","a","d","b","a","e","f","a","g","b","h","i","a",
    };
    std::unordered_map<std::string, uint64_t> exact;
    for (const auto& s : stream) {
        record_str(hh, s);
        ++exact[s];
    }
    for (const auto& [k, f] : exact) {
        EXPECT_LE(est_str(hh, k), f) << "key=" << k;
    }
}

TEST(HeavyHitters, ErrorBoundMOverK) {
    // f(x) - f_hat(x) <= m / k for every x.
    const uint32_t k = 8;
    HeavyHitters hh(k);
    std::vector<std::string> stream;
    for (int i = 0; i < 200; ++i) stream.emplace_back("hot");
    for (int i = 0; i < 100; ++i) stream.emplace_back("warm" + std::to_string(i % 3));
    for (int i = 0; i < 50; ++i) stream.emplace_back("cold" + std::to_string(i));
    std::unordered_map<std::string, uint64_t> exact;
    for (const auto& s : stream) {
        record_str(hh, s);
        ++exact[s];
    }
    const uint64_t bound = stream.size() / k;
    for (const auto& [key, f] : exact) {
        uint64_t est = est_str(hh, key);
        EXPECT_LE(f - est, bound) << "key=" << key << " f=" << f << " est=" << est;
    }
}

TEST(HeavyHitters, Reset) {
    HeavyHitters hh(4);
    record_str(hh, "a");
    record_str(hh, "b");
    hh.reset();
    EXPECT_EQ(hh.stream_length(), 0u);
    EXPECT_EQ(hh.active_counters(), 0u);
    EXPECT_EQ(est_str(hh, "a"), 0u);
}

TEST(HeavyHitters, SnapshotReflectsState) {
    HeavyHitters hh(4);
    record_str(hh, "x");
    record_str(hh, "x");
    record_str(hh, "y");
    auto snap = hh.snapshot();
    EXPECT_EQ(snap["x"], 2u);
    EXPECT_EQ(snap["y"], 1u);
}

TEST(HeavyHitters, MemoryUsageGrowsWithKeys) {
    HeavyHitters hh(64);
    const size_t empty_mem = hh.memory_usage();
    for (int i = 0; i < 10; ++i) {
        std::string s = "item_" + std::to_string(i);
        record_str(hh, s);
    }
    EXPECT_GT(hh.memory_usage(), empty_mem);
}
