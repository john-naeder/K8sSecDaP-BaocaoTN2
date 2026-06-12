// Audit + report export handlers.
//
//   GET  /api/v1/audit/export?from=&to=&actor=&action=&format=csv|json
//        admin-only — stream rows ordered by occurred_at ASC. Header
//        X-Export-Total-Rows is set after streaming completes.
//
//   GET  /api/v1/reports/weekly?week=YYYY-WW&format=html|json
//        any authenticated user. Renders SummariseRange + html_template.
//
//   POST /api/v1/reports/weekly
//        admin-only. Optional body {"week":"YYYY-WW","upload":true} —
//        triggered by the weekly CronJob, returns the rendered artifact
//        and (when upload=true and MinIO is wired) writes to
//        zt-reports/week-YYYY-WW.html.
package api

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/johnnaeder/zt-soc/incident-service/internal/rbac"
	"github.com/johnnaeder/zt-soc/incident-service/internal/reports"
	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

// ReportSink uploads a rendered report. Implementations: minio, no-op.
type ReportSink interface {
	UploadHTML(ctx context.Context, bucket, key, body string) (url string, err error)
}

func (s *Server) exportAudit(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	from, err := parseTime(q.Get("from"))
	if err != nil {
		writeErr(w, 400, "bad 'from': "+err.Error())
		return
	}
	to, err := parseTime(q.Get("to"))
	if err != nil {
		writeErr(w, 400, "bad 'to': "+err.Error())
		return
	}
	if to.IsZero() {
		to = time.Now().UTC()
	}
	if from.IsZero() {
		from = to.Add(-30 * 24 * time.Hour)
	}
	format := q.Get("format")
	if format == "" {
		format = "csv"
	}
	filter := store.AuditFilter{
		From:   from,
		To:     to,
		Actor:  q.Get("actor"),
		Action: q.Get("action"),
	}

	rows := 0
	switch format {
	case "csv":
		w.Header().Set("Content-Type", "text/csv; charset=utf-8")
		w.Header().Set("Content-Disposition",
			fmt.Sprintf(`attachment; filename="audit-%s.csv"`, time.Now().UTC().Format("20060102-150405")))
		cw := csv.NewWriter(w)
		_ = cw.Write([]string{"id", "occurred_at", "actor", "actor_role", "action", "target_type", "target_id", "diff"})
		err = s.Store.StreamAuditRange(r.Context(), filter, func(e *store.AuditEntry) error {
			rows++
			role := ""
			if e.ActorRole != nil {
				role = *e.ActorRole
			}
			diff := ""
			if e.Diff != nil {
				diff = string(e.Diff)
			}
			return cw.Write([]string{
				strconv.FormatInt(e.ID, 10),
				e.OccurredAt.UTC().Format(time.RFC3339),
				e.Actor,
				role,
				e.Action,
				e.TargetType,
				strconv.FormatInt(e.TargetID, 10),
				diff,
			})
		})
		cw.Flush()
	case "json":
		w.Header().Set("Content-Type", "application/x-ndjson; charset=utf-8")
		enc := json.NewEncoder(w)
		err = s.Store.StreamAuditRange(r.Context(), filter, func(e *store.AuditEntry) error {
			rows++
			return enc.Encode(e)
		})
	default:
		writeErr(w, 400, "format must be csv or json")
		return
	}
	w.Header().Set("X-Export-Total-Rows", strconv.Itoa(rows))
	if err != nil {
		// Best-effort: stream may have partially completed. Log via the
		// audit subsystem so operators see truncation.
		s.Audit.Record(r.Context(), rbac.User(r.Context()), string(rbac.RoleOf(r.Context())),
			"audit.export_failed", "audit", 0, map[string]any{"error": err.Error(), "rows_written": rows})
		return
	}
	s.Audit.Record(r.Context(), rbac.User(r.Context()), string(rbac.RoleOf(r.Context())),
		"audit.exported", "audit", 0, map[string]any{
			"from": from, "to": to, "actor": filter.Actor, "rows": rows, "format": format,
		})
}

type weeklyReportRequest struct {
	Week   string `json:"week"`   // YYYY-WW; empty => current completed week
	Upload bool   `json:"upload"` // true => write to MinIO if Sink is set
}

func (s *Server) getWeeklyReport(w http.ResponseWriter, r *http.Request) {
	year, week, err := resolveWeek(r.URL.Query().Get("week"), time.Now().UTC())
	if err != nil {
		writeErr(w, 400, err.Error())
		return
	}
	rep, err := reports.GenerateWeekly(r.Context(), s.Store, year, week)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	if r.URL.Query().Get("format") == "json" {
		writeJSON(w, 200, rep)
		return
	}
	html, err := reports.RenderHTML(rep)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = io.WriteString(w, html)
}

func (s *Server) generateAndStoreWeeklyReport(w http.ResponseWriter, r *http.Request) {
	var body weeklyReportRequest
	_ = json.NewDecoder(r.Body).Decode(&body) // empty body is fine
	year, week, err := resolveWeek(body.Week, time.Now().UTC())
	if err != nil {
		writeErr(w, 400, err.Error())
		return
	}
	rep, err := reports.GenerateWeekly(r.Context(), s.Store, year, week)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	html, err := reports.RenderHTML(rep)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	resp := map[string]any{"week": rep.WeekISO, "uploaded_to": "", "summary": rep}
	if body.Upload && s.ReportSink != nil {
		key := fmt.Sprintf("week-%s.html", rep.WeekISO)
		url, upErr := s.ReportSink.UploadHTML(r.Context(), "zt-reports", key, html)
		if upErr != nil {
			s.Audit.Record(r.Context(), rbac.User(r.Context()), string(rbac.RoleOf(r.Context())),
				"report.upload_failed", "report", 0, map[string]any{"week": rep.WeekISO, "error": upErr.Error()})
			writeErr(w, 502, "upload failed: "+upErr.Error())
			return
		}
		resp["uploaded_to"] = url
	}
	s.Audit.Record(r.Context(), rbac.User(r.Context()), string(rbac.RoleOf(r.Context())),
		"report.generated", "report", 0, map[string]any{
			"week": rep.WeekISO, "uploaded": body.Upload,
		})
	writeJSON(w, 200, resp)
}

// ─── Helpers ───────────────────────────────────────────────────────────────

func parseTime(s string) (time.Time, error) {
	if s == "" {
		return time.Time{}, nil
	}
	for _, layout := range []string{time.RFC3339, "2006-01-02"} {
		if t, err := time.Parse(layout, s); err == nil {
			return t.UTC(), nil
		}
	}
	return time.Time{}, errors.New("expected RFC3339 or 2006-01-02")
}

// resolveWeek accepts "YYYY-WW", "YYYY-Www", or empty (=> previous
// completed week relative to `now`).
func resolveWeek(spec string, now time.Time) (int, int, error) {
	if spec == "" {
		y, w := reports.CurrentISOWeek(now)
		return y, w, nil
	}
	spec = strings.ToUpper(spec)
	spec = strings.ReplaceAll(spec, "W", "")
	parts := strings.Split(spec, "-")
	if len(parts) != 2 {
		return 0, 0, errors.New("week must look like YYYY-WW")
	}
	y, err := strconv.Atoi(parts[0])
	if err != nil {
		return 0, 0, err
	}
	wk, err := strconv.Atoi(parts[1])
	if err != nil {
		return 0, 0, err
	}
	return y, wk, nil
}
