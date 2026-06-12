#include "dsa/registry.h"
#include "dsa/frequency/count_min_sketch.h"
#include "dsa/frequency/hash_map_exact.h"
#include "dsa/frequency/heavy_hitters.h"
#include "dsa/graph/tarjan.h"
#include "dsa/graph/kosaraju.h"
#include "dsa/graph/bfs.h"
#include "dsa/graph/dijkstra.h"
#include "dsa/trie/lpm_trie.h"

namespace dsa {

// Explicit registration function — call once at startup.
void register_all_algorithms() {
    // Frequency estimators
    Registry<frequency::Estimator>::instance().register_algorithm(
        "count_min_sketch",
        [] { return std::make_unique<frequency::CountMinSketch>(2048, 5); });

    Registry<frequency::Estimator>::instance().register_algorithm(
        "hash_map_exact",
        [] { return std::make_unique<frequency::HashMapExact>(); });

    Registry<frequency::Estimator>::instance().register_algorithm(
        "heavy_hitters",
        [] { return std::make_unique<frequency::HeavyHitters>(64); });

    // SCC detectors
    Registry<graph::SCCDetector>::instance().register_algorithm(
        "tarjan",
        [] { return std::make_unique<graph::Tarjan>(); });

    Registry<graph::SCCDetector>::instance().register_algorithm(
        "kosaraju",
        [] { return std::make_unique<graph::Kosaraju>(); });

    // Reachability analyzers
    Registry<graph::ReachabilityAnalyzer>::instance().register_algorithm(
        "bfs",
        [] { return std::make_unique<graph::BFSReachability>(); });

    Registry<graph::ReachabilityAnalyzer>::instance().register_algorithm(
        "dijkstra",
        [] { return std::make_unique<graph::DijkstraReachability>(); });

    // IP classifiers
    Registry<trie::IPClassifier>::instance().register_algorithm(
        "lpm_trie",
        [] { return std::make_unique<trie::LPMTrie>(); });
}

} // namespace dsa
