#include "dsa/graph/dijkstra.h"
#include <queue>
#include <utility>

namespace dsa::graph {

ReachabilityResult DijkstraReachability::reachable_within(
    const DirectedGraph& g, uint32_t source, int max_hops) {

    ReachabilityResult result;

    // Min-heap: (distance, vertex)
    using Entry = std::pair<int, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;

    pq.push({0, source});

    while (!pq.empty()) {
        auto [d_u, u] = pq.top();
        pq.pop();

        if (d_u > max_hops) break; // early termination

        // Skip stale entries
        auto it = result.find(u);
        if (it != result.end()) continue;

        result[u] = d_u;

        for (uint32_t v : g.neighbors(u)) {
            int d_v = d_u + 1; // unweighted: all edges cost 1
            if (result.find(v) == result.end() && d_v <= max_hops) {
                pq.push({d_v, v});
            }
        }
    }

    return result;
}

} // namespace dsa::graph
