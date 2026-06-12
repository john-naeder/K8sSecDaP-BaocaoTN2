#pragma once
#include "dsa/graph/scc_detector.h"

namespace dsa::graph {

class Tarjan : public SCCDetector {
public:
    std::vector<Component> find_sccs(const DirectedGraph& g) override;
    std::string name() const override { return "tarjan"; }
};

} // namespace dsa::graph
