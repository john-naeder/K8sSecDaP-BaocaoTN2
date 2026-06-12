#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pipeline {

// One entry in the alert_handlers list. Arbitrary string params so new
// handlers (e.g. webhook) can be added without touching the loader.
struct AlertHandlerEntry {
    std::string name;   // "file" | "stdout" | "stderr" | "nats"
    std::unordered_map<std::string, std::string> params;
};

// Per-alert-type severity override. Defaults live in engine.cpp.
struct SeverityRule {
    std::string alert_type;  // "port_scan", "scc_anomaly", "blast_radius"
    std::string severity;    // "low" | "medium" | "high" | "critical"
};

// Parsed CIDR entry from config
struct CIDREntry {
    uint32_t prefix;       // Network address (host byte order)
    uint8_t  prefix_len;   // Prefix length (e.g., 16 for /16)
    int32_t  label;        // Classification label
    std::string name;      // Human-readable name
};

// Parsed algorithm config
struct AlgorithmConfig {
    std::string name;           // Algorithm name (e.g., "count_min_sketch")
    int width = 2048;           // CMS width
    int depth = 5;              // CMS depth
    int threshold = 100;        // Alert threshold
    int max_hops = 3;           // BFS max hops
};

// Full pipeline configuration
struct PipelineConfig {
    // Algorithms
    AlgorithmConfig frequency;
    AlgorithmConfig scc;
    AlgorithmConfig reachability;
    AlgorithmConfig ip_classifier;

    // Networks
    std::vector<CIDREntry> networks;

    // Event source
    std::string event_source_type;   // "ebpf" or "file"
    std::string event_source_path;   // File path when type = "file"

    // Analysis
    int window_seconds = 60;
    int scc_interval_seconds = 10;
    int snapshot_interval_seconds = 5;

    // Output
    std::string alerts_file = "output/alerts.json";
    std::string graph_file  = "output/graph.json";
    std::string stats_file  = "output/stats.json";

    // SOC / Phase-6 extensions
    std::vector<AlertHandlerEntry> alert_handlers;  // empty → legacy file-only
    std::vector<SeverityRule>      severity_rules;
    std::string node_name;    // usually from $NODE_NAME env (DaemonSet downward API)
    std::string namespace_;   // usually from $POD_NAMESPACE
};

// Parse pipeline.yaml into PipelineConfig.
// Uses simple line-based parsing (no YAML library dependency).
PipelineConfig load_config(const std::string& path);

// Parse CIDR string "10.244.0.0/16" → (prefix, prefix_len)
bool parse_cidr(const std::string& cidr, uint32_t& prefix, uint8_t& prefix_len);

} // namespace pipeline
