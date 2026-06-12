#include "dsa/graph/kosaraju.h"
#include <stack>
#include <unordered_set>

namespace dsa::graph {

std::vector<Component> Kosaraju::find_sccs(const DirectedGraph& g) {
    auto vertices = g.vertices();
    std::unordered_set<uint32_t> visited;
    std::stack<uint32_t> finish_order;

    // Pass 1: DFS on original graph, record finish order (iterative)
    for (uint32_t start : vertices) {
        if (visited.count(start)) continue;

        // Iterative DFS
        std::stack<std::pair<uint32_t, bool>> dfs; // (vertex, is_postvisit)
        dfs.push({start, false});

        while (!dfs.empty()) {
            auto [u, post] = dfs.top();
            dfs.pop();

            if (post) {
                finish_order.push(u);
                continue;
            }

            if (visited.count(u)) continue;
            visited.insert(u);

            // Push postvisit marker
            dfs.push({u, true});

            // Push neighbors
            for (uint32_t v : g.neighbors(u)) {
                if (!visited.count(v)) {
                    dfs.push({v, false});
                }
            }
        }
    }

    // Build transpose graph
    DirectedGraph gt = g.transpose();

    // Pass 2: DFS on transpose in reverse finish order
    visited.clear();
    std::vector<Component> sccs;

    while (!finish_order.empty()) {
        uint32_t start = finish_order.top();
        finish_order.pop();

        if (visited.count(start)) continue;

        // Iterative DFS on transpose
        Component comp;
        std::stack<uint32_t> dfs;
        dfs.push(start);

        while (!dfs.empty()) {
            uint32_t u = dfs.top();
            dfs.pop();

            if (visited.count(u)) continue;
            visited.insert(u);
            comp.push_back(u);

            for (uint32_t v : gt.neighbors(u)) {
                if (!visited.count(v)) {
                    dfs.push(v);
                }
            }
        }

        sccs.push_back(std::move(comp));
    }

    return sccs;
}

} // namespace dsa::graph
