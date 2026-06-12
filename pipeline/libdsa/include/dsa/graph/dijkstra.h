#pragma once
#include "dsa/graph/reachability.h"

namespace dsa::graph {

// Dijkstra on unweighted graph (all edges weight=1).
// Included for benchmark comparison with BFS.
class DijkstraReachability : public ReachabilityAnalyzer {
public:
    ReachabilityResult reachable_within(
        const DirectedGraph& g, uint32_t source, int max_hops) override;
    std::string name() const override { return "dijkstra"; }
};

} // namespace dsa::graph
