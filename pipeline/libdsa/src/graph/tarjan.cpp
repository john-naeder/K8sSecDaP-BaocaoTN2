#include "dsa/graph/tarjan.h"
#include <algorithm>
#include <stack>
#include <unordered_map>

namespace dsa::graph {

std::vector<Component> Tarjan::find_sccs(const DirectedGraph& g) {
    std::unordered_map<uint32_t, int> disc, low;
    std::unordered_map<uint32_t, bool> on_stack;
    std::stack<uint32_t> stk;
    std::vector<Component> sccs;
    int timer = 0;

    // Iterative DFS to avoid stack overflow on large graphs.
    // Each frame stores: (vertex, neighbor_iterator, is_returning)
    struct Frame {
        uint32_t u;
        std::vector<uint32_t> neighbors;
        size_t idx; // current neighbor index
    };

    auto vertices = g.vertices();

    for (uint32_t start : vertices) {
        if (disc.count(start)) continue;

        std::stack<Frame> call_stack;

        // Initialize start vertex
        disc[start] = low[start] = timer++;
        on_stack[start] = true;
        stk.push(start);

        Frame f;
        f.u = start;
        const auto& nbrs = g.neighbors(start);
        f.neighbors.assign(nbrs.begin(), nbrs.end());
        f.idx = 0;
        call_stack.push(std::move(f));

        while (!call_stack.empty()) {
            auto& cur = call_stack.top();

            if (cur.idx < cur.neighbors.size()) {
                uint32_t v = cur.neighbors[cur.idx];
                cur.idx++;

                if (!disc.count(v)) {
                    // Tree edge: "recurse" into v
                    disc[v] = low[v] = timer++;
                    on_stack[v] = true;
                    stk.push(v);

                    Frame nf;
                    nf.u = v;
                    const auto& vnbrs = g.neighbors(v);
                    nf.neighbors.assign(vnbrs.begin(), vnbrs.end());
                    nf.idx = 0;
                    call_stack.push(std::move(nf));
                } else if (on_stack[v]) {
                    // Back edge
                    low[cur.u] = std::min(low[cur.u], disc[v]);
                }
            } else {
                // All neighbors processed — check if root of SCC
                if (low[cur.u] == disc[cur.u]) {
                    Component comp;
                    uint32_t w;
                    do {
                        w = stk.top();
                        stk.pop();
                        on_stack[w] = false;
                        comp.push_back(w);
                    } while (w != cur.u);
                    sccs.push_back(std::move(comp));
                }

                uint32_t u = cur.u;
                int u_low = low[u];
                call_stack.pop();

                // Update parent's low-link
                if (!call_stack.empty()) {
                    low[call_stack.top().u] = std::min(low[call_stack.top().u], u_low);
                }
            }
        }
    }

    return sccs;
}

} // namespace dsa::graph
