package store

import (
	"encoding/json"
	"time"
)

type Incident struct {
	ID              int64           `json:"id"`
	IncidentKey     string          `json:"incident_key"`
	Type            string          `json:"type"`
	SourceIP        string          `json:"source_ip"`
	PodName         *string         `json:"pod_name,omitempty"`       // K8s pod that held SourceIP (best-effort)
	PodNamespace    *string         `json:"pod_namespace,omitempty"`
	Severity        string          `json:"severity"`
	Status          string          `json:"status"`
	NodesObserved   []string        `json:"nodes_observed"`
	FirstSeenNS     int64           `json:"first_seen_ns"`
	LastSeenNS      int64           `json:"last_seen_ns"`
	OccurrenceCount int             `json:"occurrence_count"`
	BlastRadius     json.RawMessage `json:"blast_radius,omitempty"`
	Tags            []string        `json:"tags"`
	CrossNode       bool            `json:"cross_node"`
	AckAt           *time.Time      `json:"ack_at,omitempty"`
	AckBy           *string         `json:"ack_by,omitempty"`
	ResolvedAt      *time.Time      `json:"resolved_at,omitempty"`
	ResolvedBy      *string         `json:"resolved_by,omitempty"`
	Notes           *string         `json:"notes,omitempty"`
	CreatedAt       time.Time       `json:"created_at"`
	UpdatedAt       time.Time       `json:"updated_at"`
}

// Action types persisted in the actions.type column.
const (
	ActionTypeQuarantinePod = "quarantine_pod" // Layer-1: NetworkPolicy block
	ActionTypeDeletePod     = "delete_pod"     // Layer-2: drop the pod, let controller recreate
	ActionTypeShutdownPod   = "shutdown_pod"   // Layer-2: graceful delete + zt-soc/shutdown label
)

// PodOpPayload is the JSON body stored in actions.yaml_draft for non-NP
// action types (delete_pod, shutdown_pod). Quarantine actions still hold
// raw NetworkPolicy YAML in yaml_draft.
type PodOpPayload struct {
	Namespace string `json:"namespace"`
	PodName   string `json:"pod_name"`
	Reason    string `json:"reason,omitempty"`
}

type Action struct {
	ID              int64      `json:"id"`
	IncidentID      int64      `json:"incident_id"`
	Type            string     `json:"type"`
	Status          string     `json:"status"`
	YAMLDraft       string     `json:"yaml_draft"`
	TargetNamespace *string    `json:"target_namespace,omitempty"`
	TargetPodLabel  *string    `json:"target_pod_label,omitempty"`
	ApprovedBy      *string    `json:"approved_by,omitempty"`
	ApprovedAt      *time.Time `json:"approved_at,omitempty"`
	RejectedBy      *string    `json:"rejected_by,omitempty"`
	RejectedAt      *time.Time `json:"rejected_at,omitempty"`
	ExecutedAt      *time.Time `json:"executed_at,omitempty"`
	Error           *string    `json:"error,omitempty"`
	ExpiresAt       time.Time  `json:"expires_at"`
	CreatedAt       time.Time  `json:"created_at"`
}

type AuditEntry struct {
	ID         int64           `json:"id"`
	Actor      string          `json:"actor"`
	ActorRole  *string         `json:"actor_role,omitempty"`
	Action     string          `json:"action"`
	TargetType string          `json:"target_type"`
	TargetID   int64           `json:"target_id"`
	Diff       json.RawMessage `json:"diff,omitempty"`
	OccurredAt time.Time       `json:"occurred_at"`
}

// IncomingAlert is the payload published on zt.alerts.enriched by the aggregator.
type IncomingAlert struct {
	Type        string          `json:"type"`
	Source      string          `json:"source"`
	TimestampNS int64           `json:"timestamp_ns"`
	Severity    string          `json:"severity"`
	Status      string          `json:"status"`
	NodeName    string          `json:"node_name"`
	Namespace   string          `json:"namespace"`
	Tags        []string        `json:"tags"`
	Details     json.RawMessage `json:"details,omitempty"`
	Correlation *struct {
		IncidentKey      string   `json:"incident_key"`
		FirstSeenNS      int64    `json:"first_seen_ns"`
		LastSeenNS       int64    `json:"last_seen_ns"`
		OccurrenceCount  int      `json:"occurrence_count"`
		NodesObserved    []string `json:"nodes_observed"`
		CrossNode        bool     `json:"cross_node"`
		OriginalSeverity string   `json:"original_severity"`
	} `json:"correlation,omitempty"`
}
