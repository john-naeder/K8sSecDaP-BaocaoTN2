#include "dsa/graph/tarjan.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <set>

using namespace dsa::graph;

// Helper: convert SCCs to sets of sets for order-independent comparison
static std::set<std::set<uint32_t>> to_set_of_sets(const std::vector<Component>& sccs) {
    std::set<std::set<uint32_t>> result;
    for (const auto& comp : sccs) {
        result.insert(std::set<uint32_t>(comp.begin(), comp.end()));
    }
    return result;
}

TEST(Tarjan, SingleSCC) {
    // 1 -> 2 -> 3 -> 1 (cycle)
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);

    Tarjan t;
    auto sccs = t.find_sccs(g);

    EXPECT_EQ(sccs.size(), 1u);
    auto expected = std::set<uint32_t>{1, 2, 3};
    auto actual = std::set<uint32_t>(sccs[0].begin(), sccs[0].end());
    EXPECT_EQ(actual, expected);
}

TEST(Tarjan, DAG) {
    // 1 -> 2 -> 3 (no cycles)
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);

    Tarjan t;
    auto sccs = t.find_sccs(g);

    // Each vertex is its own SCC
    EXPECT_EQ(sccs.size(), 3u);
}

TEST(Tarjan, MultipleSCCs) {
    // SCC1: {1,2,3}, SCC2: {4,5}, isolated: {6}
    // 1->2->3->1, 3->4, 4->5->4, 5->6
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);
    g.add_edge(3, 4);
    g.add_edge(4, 5);
    g.add_edge(5, 4);
    g.add_edge(5, 6);

    Tarjan t;
    auto sccs = t.find_sccs(g);
    auto result = to_set_of_sets(sccs);

    std::set<std::set<uint32_t>> expected = {
        {1, 2, 3},
        {4, 5},
        {6}
    };

    EXPECT_EQ(result, expected);
}

TEST(Tarjan, IsolatedVertices) {
    DirectedGraph g;
    g.add_vertex(1);
    g.add_vertex(2);
    g.add_vertex(3);

    Tarjan t;
    auto sccs = t.find_sccs(g);
    EXPECT_EQ(sccs.size(), 3u);
}

TEST(Tarjan, SelfLoop) {
    DirectedGraph g;
    g.add_edge(1, 1);
    g.add_edge(2, 3);

    Tarjan t;
    auto sccs = t.find_sccs(g);
    auto result = to_set_of_sets(sccs);

    // Self-loop makes {1} an SCC, {2} and {3} separate
    EXPECT_TRUE(result.count({1}));
    EXPECT_TRUE(result.count({2}));
    EXPECT_TRUE(result.count({3}));
}

TEST(Tarjan, EmptyGraph) {
    DirectedGraph g;
    Tarjan t;
    auto sccs = t.find_sccs(g);
    EXPECT_TRUE(sccs.empty());
}

TEST(Tarjan, Name) {
    Tarjan t;
    EXPECT_EQ(t.name(), "tarjan");
}
