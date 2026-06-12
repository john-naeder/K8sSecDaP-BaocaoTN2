#include "dsa/frequency/hash_map_exact.h"
#include <gtest/gtest.h>

using namespace dsa::frequency;

TEST(HashMapExact, ExactCounts) {
    HashMapExact hm;
    uint32_t key = 42;

    for (int i = 0; i < 100; ++i) {
        hm.record(&key, sizeof(key));
    }

    EXPECT_EQ(hm.estimate(&key, sizeof(key)), 100u);
}

TEST(HashMapExact, MissingKey) {
    HashMapExact hm;
    uint32_t key = 42;
    uint32_t missing = 99;

    hm.record(&key, sizeof(key));
    EXPECT_EQ(hm.estimate(&missing, sizeof(missing)), 0u);
}

TEST(HashMapExact, Reset) {
    HashMapExact hm;
    uint32_t key = 1;
    hm.record(&key, sizeof(key));
    hm.reset();
    EXPECT_EQ(hm.estimate(&key, sizeof(key)), 0u);
}

TEST(HashMapExact, Name) {
    HashMapExact hm;
    EXPECT_EQ(hm.name(), "hash_map_exact");
}
