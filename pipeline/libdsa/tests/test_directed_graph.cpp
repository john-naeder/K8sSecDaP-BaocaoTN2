#include "dsa/graph/directed_graph.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace dsa::graph;

TEST(DirectedGraph, AddVertexAndEdge) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);

    EXPECT_EQ(g.num_vertices(), 3u);
    EXPECT_EQ(g.num_edges(), 2u);
    EXPECT_TRUE(g.has_edge(1, 2));
    EXPECT_TRUE(g.has_edge(2, 3));
    EXPECT_FALSE(g.has_edge(2, 1)); // directed
}

TEST(DirectedGraph, DuplicateEdge) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(1, 2); // duplicate

    EXPECT_EQ(g.num_edges(), 1u);
}

TEST(DirectedGraph, RemoveEdge) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(1, 3);

    g.remove_edge(1, 2);
    EXPECT_FALSE(g.has_edge(1, 2));
    EXPECT_TRUE(g.has_edge(1, 3));
    EXPECT_EQ(g.num_edges(), 1u);
}

TEST(DirectedGraph, Neighbors) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(1, 3);
    g.add_edge(1, 4);

    auto nbrs = g.neighbors(1);
    EXPECT_EQ(nbrs.size(), 3u);
    EXPECT_TRUE(nbrs.count(2));
    EXPECT_TRUE(nbrs.count(3));
    EXPECT_TRUE(nbrs.count(4));

    // No neighbors for isolated vertex
    g.add_vertex(99);
    EXPECT_TRUE(g.neighbors(99).empty());
}

TEST(DirectedGraph, Transpose) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.add_edge(2, 3);
    g.add_edge(3, 1);

    auto gt = g.transpose();
    EXPECT_TRUE(gt.has_edge(2, 1));
    EXPECT_TRUE(gt.has_edge(3, 2));
    EXPECT_TRUE(gt.has_edge(1, 3));
    EXPECT_FALSE(gt.has_edge(1, 2));
    EXPECT_EQ(gt.num_edges(), 3u);
}

TEST(DirectedGraph, Clear) {
    DirectedGraph g;
    g.add_edge(1, 2);
    g.clear();
    EXPECT_EQ(g.num_vertices(), 0u);
    EXPECT_EQ(g.num_edges(), 0u);
}
