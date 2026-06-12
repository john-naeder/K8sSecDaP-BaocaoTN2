#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dsa::graph {

// Directed graph with adjacency list representation.
// Vertices are uint32_t IDs (e.g., IP addresses or pod indices).
class DirectedGraph {
public:
    // Add vertex (no-op if already exists).
    void add_vertex(uint32_t v);

    // Add directed edge u -> v. Adds vertices if needed.
    void add_edge(uint32_t u, uint32_t v);

    // Remove edge u -> v.
    void remove_edge(uint32_t u, uint32_t v);

    // Check if vertex exists.
    bool has_vertex(uint32_t v) const;

    // Check if edge exists.
    bool has_edge(uint32_t u, uint32_t v) const;

    // Get neighbors of vertex.
    const std::unordered_set<uint32_t>& neighbors(uint32_t v) const;

    // Get all vertices.
    std::vector<uint32_t> vertices() const;

    // Counts.
    size_t num_vertices() const;
    size_t num_edges() const;

    // Build reverse graph.
    DirectedGraph transpose() const;

    // Clear all data.
    void clear();

private:
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> adj_;
    size_t edge_count_ = 0;
    static const std::unordered_set<uint32_t> empty_set_;
};

} // namespace dsa::graph
