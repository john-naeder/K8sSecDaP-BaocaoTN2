#include "dsa/graph/dijkstra.h"
#include <gtest/gtest.h>

using namespace dsa::graph;

TEST(Dijkstra, SimpleChain) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 4);

    DijkstraReachability dijk;
    auto result = dijk.reachable_within(g, 1, 2);

    EXPECT_EQ(result.size(), 3u); // {1, 2, 3}
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 2);
}

TEST(Dijkstra, AgreesWithBFS) {
    // Same graph as BFS blast radius test
    DirectedGraph g;
    g.add_edge(0, 1);
    g.add_edge(0, 2);
    g.add_edge(0, 5);
    g.add_edge(1, 3);
    g.add_edge(2, 4);
    g.add_edge(4, 6);

    DijkstraReachability dijk;
    auto r2 = dijk.reachable_within(g, 0, 2);

    EXPECT_EQ(r2.size(), 6u); // {0,1,2,3,4,5}
    EXPECT_EQ(r2[0], 0);
    EXPECT_EQ(r2[1], 1);
    EXPECT_EQ(r2[3], 2);
}

TEST(Dijkstra, Name) {
    DijkstraReachability dijk;
    EXPECT_EQ(dijk.name(), "dijkstra");
}
