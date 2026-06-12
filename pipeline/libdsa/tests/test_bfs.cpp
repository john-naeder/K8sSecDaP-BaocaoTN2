#include "dsa/graph/bfs.h"
#include <gtest/gtest.h>

using namespace dsa::graph;

TEST(BFS, SimpleChain) {
    // 1 -> 2 -> 3 -> 4 -> 5
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 4);
    g.add_edge(4, 5);

    BFSReachability bfs;
    auto result = bfs.reachable_within(g, 1, 2);

    EXPECT_EQ(result.size(), 3u); // {1, 2, 3}
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 2);
    EXPECT_EQ(result.count(4), 0u); // out of range
}

TEST(BFS, BlastRadius) {
    // Hub topology:
    // compromised -> A -> B
    //             -> C -> D -> E
    //             -> F
    DirectedGraph g;
    g.add_edge(0, 1); // compromised -> A
    g.add_edge(0, 2); // compromised -> C
    g.add_edge(0, 5); // compromised -> F
    g.add_edge(1, 3); // A -> B (actually node 3)
    g.add_edge(2, 4); // C -> D
    g.add_edge(4, 6); // D -> E (actually node 6)

    BFSReachability bfs;

    // k=1: immediate neighbors
    auto r1 = bfs.reachable_within(g, 0, 1);
    EXPECT_EQ(r1.size(), 4u); // {0, 1, 2, 5}

    // k=2: secondary
    auto r2 = bfs.reachable_within(g, 0, 2);
    EXPECT_EQ(r2.size(), 6u); // {0, 1, 2, 3, 4, 5}

    // k=3: tertiary
    auto r3 = bfs.reachable_within(g, 0, 3);
    EXPECT_EQ(r3.size(), 7u); // all nodes
}

TEST(BFS, ZeroHops) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);

    BFSReachability bfs;
    auto result = bfs.reachable_within(g, 1, 0);

    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[1], 0);
}

TEST(BFS, DisconnectedGraph) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(3, 4); // disconnected component

    BFSReachability bfs;
    auto result = bfs.reachable_within(g, 1, 10);

    EXPECT_EQ(result.size(), 2u); // only {1, 2}
    EXPECT_EQ(result.count(3), 0u);
}

TEST(BFS, CycleHandling) {
    // 1 -> 2 -> 3 -> 1
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);

    BFSReachability bfs;
    auto result = bfs.reachable_within(g, 1, 100);

    // Should visit each exactly once
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[1], 0);
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 2);
}

TEST(BFS, Name) {
    BFSReachability bfs;
    EXPECT_EQ(bfs.name(), "bfs");
}
