#include "dsa/graph/directed_graph.h"

namespace dsa::graph {

const std::unordered_set<uint32_t> DirectedGraph::empty_set_;

void DirectedGraph::add_vertex(uint32_t v) {
    adj_.try_emplace(v);
}

void DirectedGraph::add_edge(uint32_t u, uint32_t v) {
    add_vertex(u);
    add_vertex(v);
    auto [_, inserted] = adj_[u].insert(v);
    if (inserted) {
        edge_count_++;
    }
}

void DirectedGraph::remove_edge(uint32_t u, uint32_t v) {
    auto it = adj_.find(u);
    if (it != adj_.end()) {
        if (it->second.erase(v)) {
            edge_count_--;
        }
    }
}

bool DirectedGraph::has_vertex(uint32_t v) const {
    return adj_.count(v) > 0;
}

bool DirectedGraph::has_edge(uint32_t u, uint32_t v) const {
    auto it = adj_.find(u);
    if (it == adj_.end()) return false;
    return it->second.count(v) > 0;
}

const std::unordered_set<uint32_t>& DirectedGraph::neighbors(uint32_t v) const {
    auto it = adj_.find(v);
    if (it == adj_.end()) return empty_set_;
    return it->second;
}

std::vector<uint32_t> DirectedGraph::vertices() const {
    std::vector<uint32_t> result;
    result.reserve(adj_.size());
    for (const auto& [v, _] : adj_) {
        result.push_back(v);
    }
    return result;
}

size_t DirectedGraph::num_vertices() const {
    return adj_.size();
}

size_t DirectedGraph::num_edges() const {
    return edge_count_;
}

DirectedGraph DirectedGraph::transpose() const {
    DirectedGraph gt;
    for (const auto& [u, neighbors] : adj_) {
        gt.add_vertex(u);
        for (uint32_t v : neighbors) {
            gt.add_edge(v, u);
        }
    }
    return gt;
}

void DirectedGraph::clear() {
    adj_.clear();
    edge_count_ = 0;
}

} // namespace dsa::graph
