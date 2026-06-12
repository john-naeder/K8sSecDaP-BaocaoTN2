#pragma once
#include "dsa/graph/directed_graph.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace dsa::graph {

// Result of a reachability query: vertex ID -> distance from source.
using ReachabilityResult = std::unordered_map<uint32_t, int>;

// Abstract interface for K-hop reachability analysis (blast radius).
// Implementations: BFS, Dijkstra
class ReachabilityAnalyzer {
public:
    virtual ~ReachabilityAnalyzer() = default;

    // Find all vertices reachable from source within max_hops.
    // Returns map of vertex -> distance.
    virtual ReachabilityResult reachable_within(
        const DirectedGraph& g, uint32_t source, int max_hops) = 0;

    virtual std::string name() const = 0;
};

} // namespace dsa::graph
