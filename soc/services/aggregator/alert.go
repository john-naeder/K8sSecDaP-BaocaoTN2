package main

import "encoding/json"

// Alert mirrors dsa::soc::Alert::to_json output. Fields are kept loose
// (json.RawMessage for details) so we never break on payload schema drift.
type Alert struct {
	Type        string          `json:"type"`
	Source      string          `json:"source"`
	TimestampNS int64           `json:"timestamp_ns"`
	Severity    string          `json:"severity"`
	Status      string          `json:"status"`
	IncidentID  string          `json:"incident_id,omitempty"`
	NodeName    string          `json:"node_name,omitempty"`
	Namespace   string          `json:"namespace,omitempty"`
	Tags        []string        `json:"tags,omitempty"`
	Details     json.RawMessage `json:"details,omitempty"`

	// Aggregator-only enrichment fields (added when re-publishing).
	Correlation *CorrelationInfo `json:"correlation,omitempty"`
}

// CorrelationInfo records what the aggregator decided about an alert.
type CorrelationInfo struct {
	IncidentKey      string   `json:"incident_key"`
	FirstSeenNS      int64    `json:"first_seen_ns"`
	LastSeenNS       int64    `json:"last_seen_ns"`
	OccurrenceCount  int      `json:"occurrence_count"`
	NodesObserved    []string `json:"nodes_observed"`
	CrossNode        bool     `json:"cross_node"`
	OriginalSeverity string   `json:"original_severity"`
}
