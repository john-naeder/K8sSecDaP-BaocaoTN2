#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dsa::soc {

enum class Severity {
    Low,
    Medium,
    High,
    Critical,
};

enum class Status {
    New,
    Acknowledged,
    Resolved,
    Suppressed,
};

inline const char* to_string(Severity s) {
    switch (s) {
        case Severity::Low:      return "low";
        case Severity::Medium:   return "medium";
        case Severity::High:     return "high";
        case Severity::Critical: return "critical";
    }
    return "low";
}

inline const char* to_string(Status s) {
    switch (s) {
        case Status::New:          return "new";
        case Status::Acknowledged: return "ack";
        case Status::Resolved:     return "resolved";
        case Status::Suppressed:   return "suppressed";
    }
    return "new";
}

// SOC-extended Alert — envelope for Phase-6 incident lifecycle.
// Kept serialization-friendly so it maps 1:1 to NATS/Postgres payloads.
struct Alert {
    // Original pipeline fields
    std::string type;          // "port_scan", "scc_anomaly", "blast_radius", ...
    std::string source;        // Source IP (dotted decimal)
    std::string details;       // Raw JSON details (algorithm-specific)
    uint64_t    timestamp_ns = 0;

    // SOC envelope
    std::string incident_id;   // Populated by aggregator/incident-service; empty at origin
    Severity    severity = Severity::Medium;
    Status      status   = Status::New;
    std::vector<std::string> tags;
    std::string node_name;     // K8s node producing the alert
    std::string namespace_;    // Source pod namespace (if known)

    // Serialize to a single-line JSON string suitable for NATS / ndjson file.
    std::string to_json() const;
};

} // namespace dsa::soc
