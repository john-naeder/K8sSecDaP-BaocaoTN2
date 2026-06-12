#pragma once
#include "dsa/graph/reachability.h"

namespace dsa::graph {

class BFSReachability : public ReachabilityAnalyzer {
public:
    ReachabilityResult reachable_within(
        const DirectedGraph& g, uint32_t source, int max_hops) override;
    std::string name() const override { return "bfs"; }
};

} // namespace dsa::graph
