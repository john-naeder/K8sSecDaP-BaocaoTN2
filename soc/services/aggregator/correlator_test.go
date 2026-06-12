package main

import (
	"testing"
	"time"
)

func mkAlert(typ, src, node, sev string, ts int64) *Alert {
	return &Alert{Type: typ, Source: src, NodeName: node, Severity: sev, TimestampNS: ts}
}

func TestCorrelator_FirstAlertEmits(t *testing.T) {
	c := NewCorrelator(60 * time.Second)
	defer c.Stop()
	out, emit := c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", 1))
	if !emit || out == nil {
		t.Fatalf("expected emit=true, got emit=%v out=%v", emit, out)
	}
	if out.Correlation == nil || out.Correlation.OccurrenceCount != 1 {
		t.Fatalf("bad correlation: %+v", out.Correlation)
	}
}

func TestCorrelator_DuplicateSameNodeSuppressed(t *testing.T) {
	c := NewCorrelator(60 * time.Second)
	defer c.Stop()
	c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", 1))
	out, emit := c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", 2))
	if emit || out != nil {
		t.Fatalf("expected duplicate to be suppressed, got %+v", out)
	}
}

func TestCorrelator_CrossNodeUpgradesSeverity(t *testing.T) {
	c := NewCorrelator(60 * time.Second)
	defer c.Stop()
	c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", 1))
	out, emit := c.Process(mkAlert("port_scan", "10.0.0.1", "n2", "medium", 2))
	if !emit || out == nil {
		t.Fatalf("expected cross-node alert to be emitted")
	}
	if out.Severity != "high" {
		t.Errorf("expected severity=high, got %s", out.Severity)
	}
	if !out.Correlation.CrossNode {
		t.Errorf("expected CrossNode=true")
	}
	if len(out.Correlation.NodesObserved) != 2 {
		t.Errorf("expected 2 nodes observed, got %v", out.Correlation.NodesObserved)
	}
	// Has cross_node_attacker tag
	hasTag := false
	for _, tg := range out.Tags {
		if tg == "cross_node_attacker" {
			hasTag = true
		}
	}
	if !hasTag {
		t.Errorf("expected cross_node_attacker tag, got %v", out.Tags)
	}
}

func TestCorrelator_WindowExpiry(t *testing.T) {
	c := NewCorrelator(100 * time.Millisecond)
	defer c.Stop()
	now := int64(0)
	c.NowNS = func() int64 { return now }

	c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", now))
	now += int64(200 * time.Millisecond)
	out, emit := c.Process(mkAlert("port_scan", "10.0.0.1", "n1", "medium", now))
	if !emit || out == nil {
		t.Fatalf("expected re-emit after window expiry")
	}
	if out.Correlation.OccurrenceCount != 1 {
		t.Errorf("expected fresh incident count=1, got %d", out.Correlation.OccurrenceCount)
	}
}

func TestUpgradeSeverity(t *testing.T) {
	cases := map[string]string{"low": "medium", "medium": "high", "high": "critical", "": "high"}
	for in, want := range cases {
		if got := upgradeSeverity(in); got != want {
			t.Errorf("upgradeSeverity(%q) = %q, want %q", in, got, want)
		}
	}
}
