package main

import (
	"sort"
	"sync"
	"time"
)

// Correlator deduplicates alerts within a sliding window and decides
// when to upgrade severity (cross-node attacker, recurring source).
//
// Key semantics:
//   incident_key = type + "|" + source
// Two alerts with the same key arriving within `Window` are considered
// the same incident.
type Correlator struct {
	Window     time.Duration
	NowNS      func() int64 // injectable for tests; defaults to time.Now().UnixNano()
	mu         sync.Mutex
	incidents  map[string]*incidentState
	cleanupCh  chan struct{}
}

type incidentState struct {
	Key              string
	FirstSeenNS      int64
	LastSeenNS       int64
	Count            int
	Nodes            map[string]struct{}
	OriginalSeverity string
}

func NewCorrelator(window time.Duration) *Correlator {
	c := &Correlator{
		Window:    window,
		NowNS:     func() int64 { return time.Now().UnixNano() },
		incidents: make(map[string]*incidentState),
		cleanupCh: make(chan struct{}),
	}
	go c.cleanupLoop()
	return c
}

func (c *Correlator) Stop() { close(c.cleanupCh) }

// Process takes a raw Alert and returns the same alert enriched with
// correlation info. The returned `emit` flag tells the caller whether
// this should be re-published downstream:
//   - true  : first occurrence OR severity upgrade event
//   - false : suppressed duplicate within the window
func (c *Correlator) Process(a *Alert) (out *Alert, emit bool) {
	if a.Source == "" || a.Type == "" {
		return a, true // malformed — pass through; let consumers decide
	}

	key := a.Type + "|" + a.Source
	now := a.TimestampNS
	if now == 0 {
		now = c.NowNS()
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	st, exists := c.incidents[key]
	if !exists || now-st.LastSeenNS > c.Window.Nanoseconds() {
		// New incident OR window expired — start fresh.
		st = &incidentState{
			Key:              key,
			FirstSeenNS:      now,
			LastSeenNS:       now,
			Count:            1,
			Nodes:            make(map[string]struct{}),
			OriginalSeverity: a.Severity,
		}
		if a.NodeName != "" {
			st.Nodes[a.NodeName] = struct{}{}
		}
		c.incidents[key] = st
		out = a
		out.Correlation = c.snapshot(st, false)
		return out, true
	}

	// Existing incident within the window — update state.
	prevCrossNode := len(st.Nodes) >= 2
	st.LastSeenNS = now
	st.Count++
	if a.NodeName != "" {
		st.Nodes[a.NodeName] = struct{}{}
	}
	nowCrossNode := len(st.Nodes) >= 2

	// Suppress most duplicates, BUT emit on the moment severity changes.
	if !prevCrossNode && nowCrossNode {
		// Cross-node transition — upgrade severity to high and re-emit.
		out = a
		out.Severity = upgradeSeverity(a.Severity)
		out.Tags = appendUnique(out.Tags, "cross_node_attacker")
		out.Correlation = c.snapshot(st, true)
		return out, true
	}

	// Plain duplicate — drop.
	return nil, false
}

func (c *Correlator) snapshot(st *incidentState, crossNode bool) *CorrelationInfo {
	nodes := make([]string, 0, len(st.Nodes))
	for n := range st.Nodes {
		nodes = append(nodes, n)
	}
	sort.Strings(nodes)
	return &CorrelationInfo{
		IncidentKey:      st.Key,
		FirstSeenNS:      st.FirstSeenNS,
		LastSeenNS:       st.LastSeenNS,
		OccurrenceCount:  st.Count,
		NodesObserved:    nodes,
		CrossNode:        crossNode || len(st.Nodes) >= 2,
		OriginalSeverity: st.OriginalSeverity,
	}
}

func (c *Correlator) cleanupLoop() {
	t := time.NewTicker(c.Window)
	defer t.Stop()
	for {
		select {
		case <-c.cleanupCh:
			return
		case <-t.C:
			c.evictExpired()
		}
	}
}

func (c *Correlator) evictExpired() {
	now := c.NowNS()
	c.mu.Lock()
	defer c.mu.Unlock()
	for k, st := range c.incidents {
		if now-st.LastSeenNS > c.Window.Nanoseconds() {
			delete(c.incidents, k)
		}
	}
}

// Stats returns counters used by /metrics.
func (c *Correlator) Stats() (active, crossNode int) {
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, st := range c.incidents {
		active++
		if len(st.Nodes) >= 2 {
			crossNode++
		}
	}
	return
}

func upgradeSeverity(s string) string {
	switch s {
	case "low":
		return "medium"
	case "medium", "":
		return "high"
	default:
		return "critical"
	}
}

func appendUnique(s []string, v string) []string {
	for _, x := range s {
		if x == v {
			return s
		}
	}
	return append(s, v)
}
