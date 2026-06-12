#include "dsa/graph/bfs.h"
#include <queue>

namespace dsa::graph {

ReachabilityResult BFSReachability::reachable_within(
    const DirectedGraph& g, uint32_t source, int max_hops) {

    ReachabilityResult result;
    result[source] = 0;

    std::queue<uint32_t> q;
    q.push(source);

    while (!q.empty()) {
        uint32_t u = q.front();
        q.pop();

        int d = result[u];
        if (d >= max_hops) continue; // don't expand beyond k

        for (uint32_t v : g.neighbors(u)) {
            if (result.find(v) == result.end()) {
                result[v] = d + 1;
                q.push(v);
            }
        }
    }

    return result;
}

} // namespace dsa::graph
