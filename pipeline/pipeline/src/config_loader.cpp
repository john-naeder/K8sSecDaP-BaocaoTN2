#include "config_loader.h"
#include "dsa/common.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace pipeline {

// ─── CIDR parser ───────────────────────────────────────────────────────────

bool parse_cidr(const std::string& cidr, uint32_t& prefix, uint8_t& prefix_len) {
    // Format: "A.B.C.D/N"
    auto slash = cidr.find('/');
    if (slash == std::string::npos) return false;

    std::string ip_str = cidr.substr(0, slash);
    int len = std::stoi(cidr.substr(slash + 1));
    if (len < 0 || len > 32) return false;
    prefix_len = static_cast<uint8_t>(len);

    // Parse dotted-decimal
    uint8_t octets[4] = {};
    int idx = 0;
    std::istringstream iss(ip_str);
    std::string part;
    while (std::getline(iss, part, '.') && idx < 4) {
        int val = std::stoi(part);
        if (val < 0 || val > 255) return false;
        octets[idx++] = static_cast<uint8_t>(val);
    }
    if (idx != 4) return false;

    prefix = dsa::ip_from_octets(octets[0], octets[1], octets[2], octets[3]);
    return true;
}

// ─── Simple YAML-like parser ───────────────────────────────────────────────
// Handles the flat structure of pipeline.yaml without a full YAML library.

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(start, end - start + 1);
}

static std::string extract_value(const std::string& line) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return "";
    return trim(line.substr(colon + 1));
}

static std::string extract_key(const std::string& line) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return trim(line);
    return trim(line.substr(0, colon));
}

PipelineConfig load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config: " + path);
    }

    PipelineConfig cfg;

    // State machine for parsing nested YAML
    enum Section {
        NONE, ALGORITHMS, FREQ, SCC, REACH, IPCLASS,
        NETWORKS, NET_ENTRY, EVENT_SOURCE, ANALYSIS, OUTPUT,
        ALERT_HANDLERS, SEVERITY_RULES, SOC
    };
    Section section = NONE;
    Section algo_sub = NONE;

    CIDREntry current_net;
    bool in_params = false;
    AlertHandlerEntry* current_handler = nullptr;  // points into cfg.alert_handlers
    bool in_handler_params = false;
    SeverityRule* current_rule = nullptr;          // points into cfg.severity_rules

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments
        auto comment = line.find('#');
        if (comment != std::string::npos)
            line = line.substr(0, comment);

        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Count leading spaces for indent level
        size_t indent = line.find_first_not_of(' ');

        std::string key = extract_key(trimmed);
        std::string val = extract_value(trimmed);

        // Top-level sections
        if (indent == 0) {
            in_params = false;
            in_handler_params = false;
            current_handler = nullptr;
            if (key == "algorithms")     { section = ALGORITHMS; continue; }
            if (key == "networks")       { section = NETWORKS; continue; }
            if (key == "event_source")   { section = EVENT_SOURCE; continue; }
            if (key == "analysis")       { section = ANALYSIS; continue; }
            if (key == "output")         { section = OUTPUT; continue; }
            if (key == "alert_handlers") { section = ALERT_HANDLERS; continue; }
            if (key == "severity_rules") { section = SEVERITY_RULES; continue; }
            if (key == "soc")            { section = SOC; continue; }
            continue;
        }

        // Algorithms sub-sections
        if (section == ALGORITHMS && indent <= 4) {
            in_params = false;
            if (key == "frequency_estimator") { algo_sub = FREQ; continue; }
            if (key == "scc_detector")        { algo_sub = SCC; continue; }
            if (key == "reachability")         { algo_sub = REACH; continue; }
            if (key == "ip_classifier")        { algo_sub = IPCLASS; continue; }
        }

        // Algorithm params
        if (section == ALGORITHMS) {
            if (key == "params") { in_params = true; continue; }

            AlgorithmConfig* target = nullptr;
            if (algo_sub == FREQ)    target = &cfg.frequency;
            if (algo_sub == SCC)     target = &cfg.scc;
            if (algo_sub == REACH)   target = &cfg.reachability;
            if (algo_sub == IPCLASS) target = &cfg.ip_classifier;

            if (target) {
                if (key == "name") target->name = val;
                if (in_params || indent >= 6) {
                    if (key == "width")     target->width = std::stoi(val);
                    if (key == "depth")     target->depth = std::stoi(val);
                    if (key == "threshold") target->threshold = std::stoi(val);
                    if (key == "max_hops")  target->max_hops = std::stoi(val);
                }
            }
        }

        // Networks
        if (section == NETWORKS) {
            if (key == "- cidr" || (trimmed.front() == '-' && trimmed.find("cidr") != std::string::npos)) {
                // New entry — save previous if valid
                if (!current_net.name.empty()) {
                    cfg.networks.push_back(current_net);
                }
                current_net = {};
                // Extract CIDR value
                auto pos = trimmed.find("cidr:");
                if (pos != std::string::npos) {
                    std::string cidr_val = trim(trimmed.substr(pos + 5));
                    parse_cidr(cidr_val, current_net.prefix, current_net.prefix_len);
                }
                continue;
            }
            if (key == "label") { current_net.label = std::stoi(val); }
            if (key == "name")  { current_net.name = val; }
        }

        // Event source
        if (section == EVENT_SOURCE) {
            if (key == "type") cfg.event_source_type = val;
            if (key == "path") cfg.event_source_path = val;
        }

        // Analysis
        if (section == ANALYSIS) {
            if (key == "window_seconds")          cfg.window_seconds = std::stoi(val);
            if (key == "scc_interval_seconds")    cfg.scc_interval_seconds = std::stoi(val);
            if (key == "snapshot_interval_seconds") cfg.snapshot_interval_seconds = std::stoi(val);
        }

        // Output
        if (section == OUTPUT) {
            if (key == "alerts_file") cfg.alerts_file = val;
            if (key == "graph_file")  cfg.graph_file = val;
            if (key == "stats_file")  cfg.stats_file = val;
        }

        // Alert handlers — list of dict entries:
        //   alert_handlers:
        //     - name: "file"
        //       params:
        //         path: "output/alerts.json"
        //     - name: "nats"
        //       params:
        //         url: "nats://nats.soc:4222"
        //         subject: "zt.alerts.raw"
        if (section == ALERT_HANDLERS) {
            // New list entry: "- name: xxx"  (or "- name:" then next line has value)
            if (trimmed.front() == '-') {
                cfg.alert_handlers.emplace_back();
                current_handler = &cfg.alert_handlers.back();
                in_handler_params = false;

                auto name_pos = trimmed.find("name:");
                if (name_pos != std::string::npos) {
                    current_handler->name = trim(trimmed.substr(name_pos + 5));
                }
                continue;
            }
            if (!current_handler) continue;

            if (key == "name") {
                current_handler->name = val;
                in_handler_params = false;
            } else if (key == "params") {
                in_handler_params = true;
            } else if (in_handler_params) {
                current_handler->params[key] = val;
            }
        }

        // Severity rules — list of: "- type: xxx\n  severity: high"
        if (section == SEVERITY_RULES) {
            if (trimmed.front() == '-') {
                cfg.severity_rules.emplace_back();
                current_rule = &cfg.severity_rules.back();
                auto t = trimmed.find("type:");
                if (t != std::string::npos) {
                    current_rule->alert_type = trim(trimmed.substr(t + 5));
                }
                continue;
            }
            if (!current_rule) continue;
            if (key == "type")     current_rule->alert_type = val;
            if (key == "severity") current_rule->severity   = val;
        }

        // SOC block
        if (section == SOC) {
            if (key == "node_name") cfg.node_name  = val;
            if (key == "namespace") cfg.namespace_ = val;
        }
    }

    // Don't forget the last network entry
    if (!current_net.name.empty()) {
        cfg.networks.push_back(current_net);
    }

    return cfg;
}

} // namespace pipeline
