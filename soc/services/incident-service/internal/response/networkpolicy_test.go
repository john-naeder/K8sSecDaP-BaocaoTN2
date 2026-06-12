package response

import (
	"strings"
	"testing"
)

func TestGenerateQuarantine_Shape(t *testing.T) {
	yaml := GenerateQuarantine(QuarantineRequest{
		IncidentID: 42,
		SourceIP:   "10.244.1.99",
		Namespace:  "zt-targets",
		Severity:   "high",
		Reason:     "port_scan",
	})

	mustContain := []string{
		"apiVersion: networking.k8s.io/v1",
		"kind: NetworkPolicy",
		"name: zt-quarantine-incident-42",
		"namespace: zt-targets",
		"matchLabels:",
		"zt-soc-quarantine-42: \"true\"",
		"policyTypes: [Egress]",
		"egress: []",
	}
	for _, m := range mustContain {
		if !strings.Contains(yaml, m) {
			t.Errorf("output missing fragment %q\n---\n%s", m, yaml)
		}
	}
}

func TestGenerateQuarantine_DefaultNamespace(t *testing.T) {
	yaml := GenerateQuarantine(QuarantineRequest{IncidentID: 1, SourceIP: "10.0.0.1", Severity: "high"})
	if !strings.Contains(yaml, "namespace: zt-targets") {
		t.Errorf("expected default namespace zt-targets, got:\n%s", yaml)
	}
}

func TestLabelKey(t *testing.T) {
	if got := LabelKey(7); got != "zt-soc-quarantine-7" {
		t.Errorf("got %q", got)
	}
}
