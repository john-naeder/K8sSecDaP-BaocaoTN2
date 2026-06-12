#include "dsa/registry.h"
#include "dsa/frequency/estimator.h"
#include "dsa/graph/scc_detector.h"
#include "dsa/graph/reachability.h"
#include "dsa/trie/ip_classifier.h"
#include <gtest/gtest.h>

// Declared in registry.cpp
namespace dsa { void register_all_algorithms(); }

class RegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        dsa::register_all_algorithms();
    }
};

TEST_F(RegistryTest, FrequencyEstimators) {
    auto& reg = dsa::Registry<dsa::frequency::Estimator>::instance();

    EXPECT_TRUE(reg.has("count_min_sketch"));
    EXPECT_TRUE(reg.has("hash_map_exact"));

    auto cms = reg.create("count_min_sketch");
    EXPECT_EQ(cms->name(), "count_min_sketch");

    auto hm = reg.create("hash_map_exact");
    EXPECT_EQ(hm->name(), "hash_map_exact");
}

TEST_F(RegistryTest, SCCDetectors) {
    auto& reg = dsa::Registry<dsa::graph::SCCDetector>::instance();

    EXPECT_TRUE(reg.has("tarjan"));
    EXPECT_TRUE(reg.has("kosaraju"));

    auto t = reg.create("tarjan");
    EXPECT_EQ(t->name(), "tarjan");
}

TEST_F(RegistryTest, ReachabilityAnalyzers) {
    auto& reg = dsa::Registry<dsa::graph::ReachabilityAnalyzer>::instance();

    EXPECT_TRUE(reg.has("bfs"));
    EXPECT_TRUE(reg.has("dijkstra"));

    auto bfs = reg.create("bfs");
    EXPECT_EQ(bfs->name(), "bfs");
}

TEST_F(RegistryTest, IPClassifiers) {
    auto& reg = dsa::Registry<dsa::trie::IPClassifier>::instance();

    EXPECT_TRUE(reg.has("lpm_trie"));

    auto trie = reg.create("lpm_trie");
    EXPECT_EQ(trie->name(), "lpm_trie");
}

TEST_F(RegistryTest, UnknownAlgorithmThrows) {
    auto& reg = dsa::Registry<dsa::frequency::Estimator>::instance();
    EXPECT_THROW(reg.create("nonexistent"), std::runtime_error);
}

TEST_F(RegistryTest, AvailableList) {
    auto& reg = dsa::Registry<dsa::frequency::Estimator>::instance();
    auto list = reg.available();

    EXPECT_GE(list.size(), 2u);
}
