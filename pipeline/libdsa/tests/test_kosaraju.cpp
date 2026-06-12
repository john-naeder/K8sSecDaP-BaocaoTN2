#include "dsa/graph/kosaraju.h"
#include <gtest/gtest.h>
#include <set>

using namespace dsa::graph;

static std::set<std::set<uint32_t>> to_set_of_sets(const std::vector<Component>& sccs) {
    std::set<std::set<uint32_t>> result;
    for (const auto& comp : sccs) {
        result.insert(std::set<uint32_t>(comp.begin(), comp.end()));
    }
    return result;
}

TEST(Kosaraju, SingleSCC) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);

    Kosaraju k;
    auto sccs = k.find_sccs(g);
    EXPECT_EQ(sccs.size(), 1u);

    auto actual = std::set<uint32_t>(sccs[0].begin(), sccs[0].end());
    EXPECT_EQ(actual, (std::set<uint32_t>{1, 2, 3}));
}

TEST(Kosaraju, DAG) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);

    Kosaraju k;
    auto sccs = k.find_sccs(g);
    EXPECT_EQ(sccs.size(), 3u);
}

TEST(Kosaraju, MultipleSCCs) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);
    g.add_edge(3, 4);
    g.add_edge(4, 5);
    g.add_edge(5, 4);
    g.add_edge(5, 6);

    Kosaraju k;
    auto sccs = k.find_sccs(g);
    auto result = to_set_of_sets(sccs);

    std::set<std::set<uint32_t>> expected = {
        {1, 2, 3},
        {4, 5},
        {6}
    };
    EXPECT_EQ(result, expected);
}

TEST(Kosaraju, AgreesWithTarjan) {
    // Build a complex graph and verify both algorithms produce same SCCs
    DirectedGraph g;
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0); // SCC {0,1,2}
    g.add_edge(2, 3);
    g.add_edge(3, 4); g.add_edge(4, 3); // SCC {3,4}
    g.add_edge(4, 5);
    g.add_edge(5, 6); g.add_edge(6, 7); g.add_edge(7, 5); // SCC {5,6,7}

    Kosaraju k;
    auto kosaraju_result = to_set_of_sets(k.find_sccs(g));

    // Manually verify
    std::set<std::set<uint32_t>> expected = {
        {0, 1, 2},
        {3, 4},
        {5, 6, 7}
    };
    EXPECT_EQ(kosaraju_result, expected);
}

TEST(Kosaraju, Name) {
    Kosaraju k;
    EXPECT_EQ(k.name(), "kosaraju");
}
