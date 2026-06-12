#include "dsa/frequency/count_min_sketch.h"
#include <gtest/gtest.h>
#include <cstdint>

using namespace dsa::frequency;

TEST(CountMinSketch, BasicRecordAndEstimate) {
    CountMinSketch cms(1024, 5);
    uint32_t key = 42;

    // Record 100 times
    for (int i = 0; i < 100; ++i) {
        cms.record(&key, sizeof(key));
    }

    // Estimate should be >= 100 (overestimate property)
    EXPECT_GE(cms.estimate(&key, sizeof(key)), 100u);
}

TEST(CountMinSketch, OverestimateProperty) {
    CountMinSketch cms(2048, 5);

    // Insert many distinct keys
    for (uint32_t i = 0; i < 10000; ++i) {
        cms.record(&i, sizeof(i));
    }

    // Each key inserted once -> estimate >= 1
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_GE(cms.estimate(&i, sizeof(i)), 1u);
    }
}

TEST(CountMinSketch, NeverInsertedKeyCanOvercount) {
    CountMinSketch cms(1024, 5);

    for (uint32_t i = 0; i < 1000; ++i) {
        cms.record(&i, sizeof(i));
    }

    // Key 99999 was never inserted — estimate may be > 0 due to hash collisions
    uint32_t missing = 99999;
    uint64_t est = cms.estimate(&missing, sizeof(missing));
    // But should be small relative to total insertions
    EXPECT_LE(est, 100u); // with w=1024, d=5, should be very small
}

TEST(CountMinSketch, AccuracyBound) {
    // epsilon = e/w ~= 2.718/2048 ~= 0.00133
    // After m insertions, error <= epsilon * m with high probability
    CountMinSketch cms(2048, 5);
    const uint32_t m = 100000;

    // Insert key 0 exactly 500 times
    uint32_t target = 0;
    for (uint32_t i = 0; i < 500; ++i) {
        cms.record(&target, sizeof(target));
    }

    // Insert m-500 other keys
    for (uint32_t i = 1; i < m - 500 + 1; ++i) {
        cms.record(&i, sizeof(i));
    }

    uint64_t est = cms.estimate(&target, sizeof(target));
    EXPECT_GE(est, 500u); // never underestimates

    // Error bound: epsilon * m ~= 0.00133 * 100000 ~= 133
    // So estimate should be <= 500 + 133 = 633 with high probability
    EXPECT_LE(est, 700u); // generous bound
}

TEST(CountMinSketch, Reset) {
    CountMinSketch cms(512, 3);
    uint32_t key = 1;

    cms.record(&key, sizeof(key));
    EXPECT_GE(cms.estimate(&key, sizeof(key)), 1u);

    cms.reset();
    EXPECT_EQ(cms.estimate(&key, sizeof(key)), 0u);
}

TEST(CountMinSketch, MemoryUsage) {
    CountMinSketch cms(2048, 5);
    // 2048 * 5 * 8 bytes (uint64_t) + seeds
    EXPECT_GE(cms.memory_usage(), 2048u * 5 * 8);
}

TEST(CountMinSketch, ConvenienceMethods) {
    CountMinSketch cms(1024, 5);

    cms.record_u32(0x0A010203); // 10.1.2.3
    cms.record_u32(0x0A010203);
    cms.record_u32(0x0A010203);

    EXPECT_GE(cms.estimate_u32(0x0A010203), 3u);

    cms.record_pair(0x0A010203, 0x0A020304);
    EXPECT_GE(cms.estimate_pair(0x0A010203, 0x0A020304), 1u);
}

TEST(CountMinSketch, Name) {
    CountMinSketch cms;
    EXPECT_EQ(cms.name(), "count_min_sketch");
}
