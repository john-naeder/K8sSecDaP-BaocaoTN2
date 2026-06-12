// Zero-Trust Network Mapper — Pipeline Engine
//
// Reads network events (from eBPF collector or JSON file),
// processes them through DSA algorithms (CMS, Tarjan, BFS, LPM),
// and outputs alerts + graph snapshots.
//
// Usage:
//   ./zt-pipeline --config config/pipeline.yaml
//   echo '{"src_ip":"10.244.1.5",...}' | ./zt-pipeline --config config/pipeline.yaml
//
// Or with synthetic events:
//   python scripts/generate_synthetic_events.py > /tmp/events.json
//   ./zt-pipeline --config config/pipeline.yaml

#include "config_loader.h"
#include "dsa/common.h"
#include "dsa/registry.h"
#include "dsa/frequency/estimator.h"
#include "dsa/graph/directed_graph.h"
#include "dsa/graph/scc_detector.h"
#include "dsa/graph/reachability.h"
#include "dsa/trie/ip_classifier.h"
#include "dsa/soc/alert.h"
#include "dsa/soc/alert_handler.h"
#include "dsa/soc/alert_handler_factory.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declaration (defined in libdsa/src/registry.cpp)
namespace dsa { void register_all_algorithms(); }

// ─── JSON helpers (minimal, no external library) ───────────────────────────

static std::string json_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    // Find the colon after the key
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos = json.find_first_not_of(" \t", pos + 1);
    if (pos == std::string::npos) return "";

    if (json[pos] == '"') {
        // String value
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    // Numeric value
    auto end = json.find_first_of(",} \t\n", pos);
    return json.substr(pos, end - pos);
}

static uint32_t parse_ip(const std::string& ip_str) {
    uint8_t octets[4] = {};
    int idx = 0;
    std::istringstream iss(ip_str);
    std::string part;
    while (std::getline(iss, part, '.') && idx < 4) {
        octets[idx++] = static_cast<uint8_t>(std::stoi(part));
    }
    return dsa::ip_from_octets(octets[0], octets[1], octets[2], octets[3]);
}

static std::string ip_to_string(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string(ip & 0xFF);
}

static bool parse_event(const std::string& line, dsa::NetworkEvent& evt) {
    if (line.empty() || line[0] != '{') return false;

    std::string src = json_string_value(line, "src_ip");
    std::string dst = json_string_value(line, "dst_ip");
    std::string port = json_string_value(line, "dst_port");
    std::string pid = json_string_value(line, "pid");
    std::string ts = json_string_value(line, "timestamp_ns");

    if (src.empty() || dst.empty()) return false;

    evt.src_ip = parse_ip(src);
    evt.dst_ip = parse_ip(dst);
    evt.dst_port = port.empty() ? 0 : static_cast<uint16_t>(std::stoul(port));
    evt.pid = pid.empty() ? 0 : static_cast<uint32_t>(std::stoul(pid));
    evt.timestamp_ns = ts.empty() ? 0 : std::stoull(ts);

    return true;
}

// ─── Alert helpers ─────────────────────────────────────────────────────────
// Alert is now dsa::soc::Alert; emission goes through AlertHandlerChain.

static dsa::soc::Severity parse_severity(const std::string& s,
                                         dsa::soc::Severity fallback) {
    if (s == "low")      return dsa::soc::Severity::Low;
    if (s == "medium")   return dsa::soc::Severity::Medium;
    if (s == "high")     return dsa::soc::Severity::High;
    if (s == "critical") return dsa::soc::Severity::Critical;
    return fallback;
}

// Resolve severity for an alert type using config overrides, with sensible defaults.
static dsa::soc::Severity severity_for(const std::string& type,
                                       const std::vector<pipeline::SeverityRule>& rules) {
    for (const auto& r : rules) {
        if (r.alert_type == type) return parse_severity(r.severity, dsa::soc::Severity::Medium);
    }
    if (type == "blast_radius") return dsa::soc::Severity::High;
    if (type == "scc_anomaly")  return dsa::soc::Severity::High;
    if (type == "port_scan")    return dsa::soc::Severity::Medium;
    return dsa::soc::Severity::Medium;
}

// Pick node_name from config, falling back to env vars (Downward API).
static std::string resolve_node_name(const std::string& from_config) {
    if (!from_config.empty()) return from_config;
    if (const char* e = std::getenv("NODE_NAME"))     return e;
    if (const char* e = std::getenv("HOSTNAME"))      return e;
    return "";
}

static std::string resolve_namespace(const std::string& from_config) {
    if (!from_config.empty()) return from_config;
    if (const char* e = std::getenv("POD_NAMESPACE")) return e;
    return "";
}

// ─── Graph snapshot output ─────────────────────────────────────────────────

static void write_graph_snapshot(const std::string& path,
                                 const dsa::graph::DirectedGraph& graph,
                                 const std::vector<dsa::graph::Component>& sccs) {
    std::ofstream out(path);
    out << "{\n  \"vertices\": [";

    auto verts = graph.vertices();
    bool first = true;
    for (uint32_t v : verts) {
        if (!first) out << ",";
        out << "\"" << ip_to_string(v) << "\"";
        first = false;
    }

    out << "],\n  \"edges\": [";
    first = true;
    for (uint32_t v : verts) {
        for (uint32_t u : graph.neighbors(v)) {
            if (!first) out << ",";
            out << "\n    {\"from\":\"" << ip_to_string(v)
                << "\",\"to\":\"" << ip_to_string(u) << "\"}";
            first = false;
        }
    }

    out << "\n  ],\n  \"sccs\": [";
    first = true;
    for (const auto& scc : sccs) {
        if (scc.size() <= 1) continue;  // Only report non-trivial SCCs
        if (!first) out << ",";
        out << "\n    [";
        bool f2 = true;
        for (uint32_t v : scc) {
            if (!f2) out << ",";
            out << "\"" << ip_to_string(v) << "\"";
            f2 = false;
        }
        out << "]";
        first = false;
    }

    out << "\n  ]\n}\n";
}

// ─── Stats output ──────────────────────────────────────────────────────────

static void write_stats(const std::string& path,
                        uint64_t total_events,
                        uint64_t alerts_count,
                        size_t graph_vertices,
                        size_t graph_edges,
                        size_t scc_count,
                        size_t cms_memory,
                        const std::string& freq_algo,
                        const std::string& scc_algo,
                        const std::string& reach_algo) {
    std::ofstream out(path);
    out << "{\n"
        << "  \"total_events\": " << total_events << ",\n"
        << "  \"alerts_count\": " << alerts_count << ",\n"
        << "  \"graph_vertices\": " << graph_vertices << ",\n"
        << "  \"graph_edges\": " << graph_edges << ",\n"
        << "  \"nontrivial_sccs\": " << scc_count << ",\n"
        << "  \"cms_memory_bytes\": " << cms_memory << ",\n"
        << "  \"algorithms\": {\n"
        << "    \"frequency\": \"" << freq_algo << "\",\n"
        << "    \"scc\": \"" << scc_algo << "\",\n"
        << "    \"reachability\": \"" << reach_algo << "\"\n"
        << "  }\n"
        << "}\n";
}

// ─── Main Pipeline ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    std::string config_path = "config/pipeline.yaml";
    for (int i = 1; i < argc; i++) {
        if ((std::string(argv[i]) == "--config" || std::string(argv[i]) == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        }
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            std::cerr << "Usage: " << argv[0] << " [--config <path>]\n"
                      << "\nZero-Trust Network Mapper — Pipeline Engine\n"
                      << "Processes network events through DSA algorithms.\n";
            return 0;
        }
    }

    // Register all algorithms + SOC alert handlers
    dsa::register_all_algorithms();
    dsa::soc::register_all_alert_handlers();

    // Load configuration — try multiple locations if relative path fails
    pipeline::PipelineConfig cfg;
    std::vector<std::string> candidates = {
        config_path,
        "../" + config_path,           // from build/
        "../../" + config_path,        // from build/subdir/
        std::string("/home/john/dev/Gradution-Reports/") + config_path,
    };

    std::string resolved;
    for (const auto& path : candidates) {
        std::ifstream test(path);
        if (test.is_open()) {
            resolved = path;
            break;
        }
    }

    if (resolved.empty()) {
        std::cerr << "[pipeline] Error: Cannot open config: " << config_path << "\n";
        std::cerr << "[pipeline] Tried:\n";
        for (const auto& p : candidates) std::cerr << "[pipeline]   " << p << "\n";
        return 1;
    }

    std::cerr << "[pipeline] Loading config: " << resolved << "\n";
    try {
        cfg = pipeline::load_config(resolved);
    } catch (const std::exception& e) {
        std::cerr << "[pipeline] Error: " << e.what() << "\n";
        return 1;
    }

    // Create output directory
    {
        // Extract directory from output path
        auto slash = cfg.alerts_file.rfind('/');
        if (slash != std::string::npos) {
            std::string dir = cfg.alerts_file.substr(0, slash);
            std::string cmd = "mkdir -p " + dir;
            (void)system(cmd.c_str());
        }
    }

    // ─── Create algorithms via Registry ────────────────────────────────────

    // Frequency Estimator
    auto freq_estimator = dsa::Registry<dsa::frequency::Estimator>::instance()
                              .create(cfg.frequency.name);
    std::cerr << "[pipeline] Frequency estimator: " << freq_estimator->name() << "\n";

    // SCC Detector
    auto scc_detector = dsa::Registry<dsa::graph::SCCDetector>::instance()
                            .create(cfg.scc.name);
    std::cerr << "[pipeline] SCC detector: " << scc_detector->name() << "\n";

    // Reachability Analyzer
    auto reachability = dsa::Registry<dsa::graph::ReachabilityAnalyzer>::instance()
                            .create(cfg.reachability.name);
    std::cerr << "[pipeline] Reachability: " << reachability->name() << "\n";

    // IP Classifier
    auto ip_classifier = dsa::Registry<dsa::trie::IPClassifier>::instance()
                             .create(cfg.ip_classifier.name);
    std::cerr << "[pipeline] IP classifier: " << ip_classifier->name() << "\n";

    // Populate IP classifier with configured networks
    for (const auto& net : cfg.networks) {
        ip_classifier->insert(net.prefix, net.prefix_len, net.label);
        std::cerr << "[pipeline]   Network: " << net.name
                  << " (label=" << net.label << ")\n";
    }

    // ─── Open event source ─────────────────────────────────────────────────

    std::istream* input = nullptr;
    std::ifstream file_input;

    if (cfg.event_source_type == "file") {
        file_input.open(cfg.event_source_path);
        if (!file_input.is_open()) {
            std::cerr << "[pipeline] Error: cannot open event file: "
                      << cfg.event_source_path << "\n";
            return 1;
        }
        input = &file_input;
        std::cerr << "[pipeline] Reading events from: " << cfg.event_source_path << "\n";
    } else {
        // Read from stdin (pipe from eBPF collector)
        input = &std::cin;
        std::cerr << "[pipeline] Reading events from stdin (pipe from collector)\n";
    }

    // ─── Build alert handler chain ─────────────────────────────────────────
    // Backwards-compat: if alert_handlers list is empty, fall back to a single
    // file handler pointing at cfg.alerts_file (legacy Phase-5 behavior).

    dsa::soc::AlertHandlerChain chain;
    {
        std::vector<pipeline::AlertHandlerEntry> entries = cfg.alert_handlers;
        if (entries.empty()) {
            pipeline::AlertHandlerEntry legacy;
            legacy.name = "file";
            legacy.params["path"] = cfg.alerts_file;
            entries.push_back(std::move(legacy));
        }
        for (const auto& e : entries) {
            dsa::soc::HandlerConfig hc{e.name, e.params};
            try {
                auto h = dsa::soc::AlertHandlerRegistry::instance().create(hc);
                std::cerr << "[pipeline] Alert handler: " << h->name() << "\n";
                chain.add(std::move(h));
            } catch (const std::exception& ex) {
                std::cerr << "[pipeline] Error creating handler '" << e.name
                          << "': " << ex.what() << "\n";
                return 1;
            }
        }
    }

    // SOC metadata attached to every alert
    const std::string node_name  = resolve_node_name(cfg.node_name);
    const std::string namespace_ = resolve_namespace(cfg.namespace_);

    // ─── Processing state ──────────────────────────────────────────────────

    dsa::graph::DirectedGraph graph;
    std::vector<dsa::graph::Component> last_sccs;

    uint64_t total_events = 0;
    uint64_t alerts_count = 0;
    uint64_t window_start_ns = 0;

    int threshold = cfg.frequency.threshold;
    int max_hops = cfg.reachability.max_hops;

    // Track unique source IPs that exceeded threshold (per window)
    std::unordered_set<uint32_t> alerted_sources;

    // Previous SCC count for change detection
    size_t prev_nontrivial_sccs = 0;

    std::cerr << "[pipeline] Processing events...\n";

    // ─── Event loop ────────────────────────────────────────────────────────

    std::string line;
    while (std::getline(*input, line)) {
        dsa::NetworkEvent evt;
        if (!parse_event(line, evt)) continue;

        total_events++;

        // Initialize window start
        if (window_start_ns == 0) window_start_ns = evt.timestamp_ns;

        // ── Sliding window reset ───────────────────────────────────────
        uint64_t window_ns = static_cast<uint64_t>(cfg.window_seconds) * 1'000'000'000ULL;
        if (evt.timestamp_ns - window_start_ns > window_ns) {
            freq_estimator->reset();
            alerted_sources.clear();
            window_start_ns = evt.timestamp_ns;
            std::cerr << "[pipeline] Window reset at event #" << total_events << "\n";
        }

        // ── 1. IP Classification (LPM Trie) ───────────────────────────
        int32_t src_label = ip_classifier->lookup(evt.src_ip);
        int32_t dst_label = ip_classifier->lookup(evt.dst_ip);

        // ── 2. Frequency Estimation (CMS) ──────────────────────────────
        // Record (src_ip) as key — detect sources with too many connections
        freq_estimator->record_u32(evt.src_ip);
        uint64_t freq = freq_estimator->estimate_u32(evt.src_ip);

        // Alert: port scan detection
        if (freq > static_cast<uint64_t>(threshold) &&
            alerted_sources.find(evt.src_ip) == alerted_sources.end()) {
            alerted_sources.insert(evt.src_ip);

            std::ostringstream details;
            details << "{\"estimated_frequency\":" << freq
                    << ",\"threshold\":" << threshold
                    << ",\"src_label\":" << src_label
                    << ",\"src_ip\":\"" << ip_to_string(evt.src_ip) << "\""
                    << ",\"description\":\"Source IP exceeded connection frequency threshold — possible port scan\"}";

            dsa::soc::Alert alert;
            alert.type         = "port_scan";
            alert.source       = ip_to_string(evt.src_ip);
            alert.details      = details.str();
            alert.timestamp_ns = evt.timestamp_ns;
            alert.severity     = severity_for(alert.type, cfg.severity_rules);
            alert.node_name    = node_name;
            alert.namespace_   = namespace_;
            alert.tags         = {"port_scan"};
            chain.send(alert);
            alerts_count++;

            std::cerr << "[ALERT] Port scan detected: " << ip_to_string(evt.src_ip)
                      << " (freq=" << freq << " > " << threshold << ")\n";

            // ── 3. Blast Radius (BFS) ──────────────────────────────────
            if (graph.has_vertex(evt.src_ip)) {
                auto blast = reachability->reachable_within(graph, evt.src_ip, max_hops);

                std::ostringstream br_details;
                br_details << "{\"source\":\"" << ip_to_string(evt.src_ip)
                           << "\",\"max_hops\":" << max_hops
                           << ",\"reachable_count\":" << blast.size()
                           << ",\"reachable\":[";
                bool first = true;
                for (const auto& [v, dist] : blast) {
                    if (!first) br_details << ",";
                    br_details << "{\"ip\":\"" << ip_to_string(v)
                               << "\",\"hops\":" << dist << "}";
                    first = false;
                }
                br_details << "]}";

                dsa::soc::Alert br_alert;
                br_alert.type         = "blast_radius";
                br_alert.source       = ip_to_string(evt.src_ip);
                br_alert.details      = br_details.str();
                br_alert.timestamp_ns = evt.timestamp_ns;
                br_alert.severity     = severity_for(br_alert.type, cfg.severity_rules);
                br_alert.node_name    = node_name;
                br_alert.namespace_   = namespace_;
                br_alert.tags         = {"blast_radius", "lateral_movement"};
                chain.send(br_alert);
                alerts_count++;

                std::cerr << "[ALERT] Blast radius from " << ip_to_string(evt.src_ip)
                          << ": " << blast.size() << " nodes within " << max_hops << " hops\n";
            }
        }

        // ── 4. Build communication graph ───────────────────────────────
        if (!graph.has_vertex(evt.src_ip)) graph.add_vertex(evt.src_ip);
        if (!graph.has_vertex(evt.dst_ip)) graph.add_vertex(evt.dst_ip);
        graph.add_edge(evt.src_ip, evt.dst_ip);

        // ── 5. Periodic SCC detection ──────────────────────────────────
        // Run SCC every scc_interval events (approximate; real system uses timer)
        if (total_events % static_cast<uint64_t>(cfg.scc_interval_seconds * 10) == 0) {
            last_sccs = scc_detector->find_sccs(graph);

            // Count non-trivial SCCs (size > 1)
            size_t nontrivial = 0;
            for (const auto& scc : last_sccs) {
                if (scc.size() > 1) nontrivial++;
            }

            // Alert if new non-trivial SCCs appeared
            if (nontrivial > prev_nontrivial_sccs) {
                for (const auto& scc : last_sccs) {
                    if (scc.size() <= 1) continue;

                    std::ostringstream scc_details;
                    scc_details << "{\"scc_size\":" << scc.size()
                                << ",\"members\":[";
                    bool first = true;
                    for (uint32_t v : scc) {
                        if (!first) scc_details << ",";
                        scc_details << "\"" << ip_to_string(v) << "\"";
                        first = false;
                    }
                    scc_details << "],\"description\":\"Strongly connected component detected — possible architecture violation or lateral movement\"}";

                    dsa::soc::Alert scc_alert;
                    scc_alert.type         = "scc_anomaly";
                    scc_alert.source       = ip_to_string(scc[0]);
                    scc_alert.details      = scc_details.str();
                    scc_alert.timestamp_ns = evt.timestamp_ns;
                    scc_alert.severity     = severity_for(scc_alert.type, cfg.severity_rules);
                    scc_alert.node_name    = node_name;
                    scc_alert.namespace_   = namespace_;
                    scc_alert.tags         = {"scc", "architecture_violation"};
                    chain.send(scc_alert);
                    alerts_count++;

                    std::cerr << "[ALERT] SCC detected: " << scc.size() << " nodes in cycle\n";
                }
                prev_nontrivial_sccs = nontrivial;
            }
        }

        // Progress reporting
        if (total_events % 10000 == 0) {
            std::cerr << "[pipeline] Processed " << total_events << " events"
                      << " | graph: " << graph.num_vertices() << "V, "
                      << graph.num_edges() << "E"
                      << " | alerts: " << alerts_count << "\n";
        }
    }

    // ─── Final SCC detection + output ──────────────────────────────────────

    chain.flush();

    last_sccs = scc_detector->find_sccs(graph);
    write_graph_snapshot(cfg.graph_file, graph, last_sccs);

    size_t nontrivial_sccs = 0;
    for (const auto& scc : last_sccs)
        if (scc.size() > 1) nontrivial_sccs++;

    write_stats(cfg.stats_file, total_events, alerts_count,
                graph.num_vertices(), graph.num_edges(), nontrivial_sccs,
                freq_estimator->memory_usage(),
                freq_estimator->name(), scc_detector->name(), reachability->name());

    // ─── Summary ───────────────────────────────────────────────────────────

    std::cerr << "\n[pipeline] ═══════════════════════════════════════════\n"
              << "[pipeline] Processing complete.\n"
              << "[pipeline]   Total events:     " << total_events << "\n"
              << "[pipeline]   Alerts generated:  " << alerts_count << "\n"
              << "[pipeline]   Graph vertices:    " << graph.num_vertices() << "\n"
              << "[pipeline]   Graph edges:       " << graph.num_edges() << "\n"
              << "[pipeline]   Non-trivial SCCs:  " << nontrivial_sccs << "\n"
              << "[pipeline]   CMS memory:        " << freq_estimator->memory_usage() << " bytes\n"
              << "[pipeline]   Algorithms:        " << freq_estimator->name()
              << " / " << scc_detector->name()
              << " / " << reachability->name() << "\n"
              << "[pipeline] ═══════════════════════════════════════════\n"
              << "[pipeline] Output:\n"
              << "[pipeline]   Alerts: " << cfg.alerts_file << "\n"
              << "[pipeline]   Graph:  " << cfg.graph_file << "\n"
              << "[pipeline]   Stats:  " << cfg.stats_file << "\n";

    return 0;
}
