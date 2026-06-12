// Package reports turns store.SummaryRange aggregates into operator-
// readable artifacts (HTML for humans, JSON for tooling).
//
// Used by:
//   - GET  /api/v1/reports/weekly?week=YYYY-WW&format=html|json
//   - POST /api/v1/reports/weekly (admin, CronJob trigger)
package reports

import (
	"context"
	"fmt"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

// Summariser is the read-side dependency. Lets us swap a fake in tests.
type Summariser interface {
	SummariseRange(ctx context.Context, from, to time.Time) (*store.SummaryRange, error)
}

// WeeklyReport bundles the summary range with metadata used by the renderer.
type WeeklyReport struct {
	WeekISO     string // e.g. "2026-W20"
	GeneratedAt time.Time
	*store.SummaryRange
}

// GenerateWeekly computes a [Monday 00:00, next-Monday 00:00) UTC window
// for the given ISO week and runs SummariseRange against it.
func GenerateWeekly(ctx context.Context, s Summariser, year, week int) (*WeeklyReport, error) {
	from, to := isoWeekRange(year, week)
	sum, err := s.SummariseRange(ctx, from, to)
	if err != nil {
		return nil, fmt.Errorf("summarise %d-W%02d: %w", year, week, err)
	}
	return &WeeklyReport{
		WeekISO:      fmt.Sprintf("%d-W%02d", year, week),
		GeneratedAt:  time.Now().UTC(),
		SummaryRange: sum,
	}, nil
}

// CurrentISOWeek returns (year, week) for the most-recently-completed ISO
// week — i.e. the one ending at the previous Monday 00:00 UTC. Used by
// the weekly CronJob so a Monday-morning report covers Mon..Sun of the
// week that just ended.
func CurrentISOWeek(now time.Time) (int, int) {
	monday := previousMondayUTC(now)
	prevWeekAnchor := monday.Add(-24 * time.Hour) // any day inside that week
	return prevWeekAnchor.ISOWeek()
}

func previousMondayUTC(t time.Time) time.Time {
	t = t.UTC().Truncate(24 * time.Hour)
	weekday := int(t.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	return t.AddDate(0, 0, -(weekday - 1))
}

// isoWeekRange returns the UTC [start, end) for the given ISO week. ISO
// week 1 is the week containing the first Thursday of the year.
func isoWeekRange(year, week int) (time.Time, time.Time) {
	// Jan 4th is always in ISO week 1; walk to its Monday, then forward.
	jan4 := time.Date(year, time.January, 4, 0, 0, 0, 0, time.UTC)
	weekday := int(jan4.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	weekOneMonday := jan4.AddDate(0, 0, -(weekday - 1))
	from := weekOneMonday.AddDate(0, 0, (week-1)*7)
	to := from.AddDate(0, 0, 7)
	return from, to
}
