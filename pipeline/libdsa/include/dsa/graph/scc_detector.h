#pragma once
#include "dsa/graph/directed_graph.h"
#include <string>
#include <vector>

namespace dsa::graph {

using Component = std::vector<uint32_t>;

// Abstract interface for Strongly Connected Component detection.
// Implementations: Tarjan, Kosaraju
class SCCDetector {
public:
    virtual ~SCCDetector() = default;

    // Find all SCCs in the graph. Returns list of components.
    virtual std::vector<Component> find_sccs(const DirectedGraph& g) = 0;

    virtual std::string name() const = 0;
};

} // namespace dsa::graph
