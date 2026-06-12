package reports

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

type fakeSummariser struct {
	out  *store.SummaryRange
	from time.Time
	to   time.Time
}

func (f *fakeSummariser) SummariseRange(_ context.Context, from, to time.Time) (*store.SummaryRange, error) {
	f.from = from
	f.to = to
	return f.out, nil
}

func sampleRange(from, to time.Time) *store.SummaryRange {
	return &store.SummaryRange{
		From:           from,
		To:             to,
		TotalIncidents: 12,
		BySeverity:     map[string]int{"high": 7, "critical": 3, "medium": 2},
		TopAttackers: []store.AttackerCount{
			{SourceIP: "10.244.1.99", Count: 5},
			{SourceIP: "10.244.2.77", Count: 4},
		},
		ActionsBreakdown:  map[string]int{"quarantine_pod": 9, "delete_pod": 2},
		MTTRMedianSeconds: 180,
		AuditEventsCount:  47,
	}
}

func TestGenerateWeekly_AsksRightWindow(t *testing.T) {
	f := &fakeSummariser{out: sampleRange(time.Time{}, time.Time{})}
	r, err := GenerateWeekly(context.Background(), f, 2026, 20)
	if err != nil {
		t.Fatal(err)
	}
	if r.WeekISO != "2026-W20" {
		t.Errorf("week ISO = %q", r.WeekISO)
	}
	// Monday of ISO week 20, 2026 is 2026-05-11.
	wantFrom := time.Date(2026, 5, 11, 0, 0, 0, 0, time.UTC)
	if !f.from.Equal(wantFrom) {
		t.Errorf("from = %s, want %s", f.from, wantFrom)
	}
	if !f.to.Equal(wantFrom.AddDate(0, 0, 7)) {
		t.Errorf("to = %s, want %s", f.to, wantFrom.AddDate(0, 0, 7))
	}
}

func TestRenderHTML_Contains(t *testing.T) {
	from := time.Date(2026, 5, 11, 0, 0, 0, 0, time.UTC)
	to := from.AddDate(0, 0, 7)
	r := &WeeklyReport{
		WeekISO:      "2026-W20",
		GeneratedAt:  time.Date(2026, 5, 18, 6, 0, 0, 0, time.UTC),
		SummaryRange: sampleRange(from, to),
	}
	html, err := RenderHTML(r)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{
		"ZT-SOC weekly report — 2026-W20",
		"10.244.1.99",
		"quarantine_pod",
		"3.0 min", // 180s → "3.0 min"
		"Severity breakdown",
	} {
		if !strings.Contains(html, want) {
			t.Errorf("html missing %q", want)
		}
	}
}

func TestCurrentISOWeek_RegressesByOne(t *testing.T) {
	// Tue 2026-05-19 → prev complete week is week 20 (Mon 11 May → Sun 17 May).
	now := time.Date(2026, 5, 19, 6, 0, 0, 0, time.UTC)
	y, w := CurrentISOWeek(now)
	if y != 2026 || w != 20 {
		t.Errorf("got %d-W%02d, want 2026-W20", y, w)
	}
}
