// Package api implements the incident-service REST handlers.
package api

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"strconv"

	"github.com/go-chi/chi/v5"
	"github.com/johnnaeder/zt-soc/incident-service/internal/audit"
	"github.com/johnnaeder/zt-soc/incident-service/internal/rbac"
	"github.com/johnnaeder/zt-soc/incident-service/internal/response"
	"github.com/johnnaeder/zt-soc/incident-service/internal/store"
)

type Server struct {
	Store      *store.Store
	Audit      *audit.Recorder
	Applier    response.Applier
	PodOps     *response.PodOps // nil in dryrun mode
	ReportSink ReportSink       // nil => weekly report upload disabled
}

func (s *Server) Routes(tokens rbac.TokenMap) http.Handler {
	r := chi.NewRouter()

	// Probes — never gated by RBAC, kubelet does not pass tokens.
	r.Get("/health", func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte("ok")) })
	r.Get("/ready",  func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte("ok")) })

	// All /api/* endpoints require authentication.
	r.Group(func(r chi.Router) {
		r.Use(rbac.Middleware(tokens))

	// Incidents
	r.Route("/api/v1/incidents", func(r chi.Router) {
		r.Get("/",        s.listIncidents)
		r.Get("/{id}",    s.getIncident)
		r.Get("/{id}/audit", s.listIncidentAudit)

		r.With(rbac.RequireRole(rbac.RoleEditor)).Patch("/{id}/status", s.updateIncidentStatus)
	})

	// Actions
	r.Route("/api/v1/actions", func(r chi.Router) {
		r.Get("/",     s.listActions)
		r.Get("/{id}", s.getAction)

		r.With(rbac.RequireRole(rbac.RoleAdmin)).Post("/{id}/approve", s.approveAction)
		r.With(rbac.RequireRole(rbac.RoleAdmin)).Post("/{id}/reject",  s.rejectAction)
	})

	// Layer-2 follow-up: admin schedules a delete_pod / shutdown_pod
	// against an incident that already has a NetworkPolicy applied.
	r.With(rbac.RequireRole(rbac.RoleAdmin)).
		Post("/api/v1/incidents/{id}/follow-up-actions", s.createFollowUpAction)

	// Audit + report exports (Phase D).
	r.With(rbac.RequireRole(rbac.RoleAdmin)).Get("/api/v1/audit/export", s.exportAudit)
	r.Get("/api/v1/reports/weekly", s.getWeeklyReport)
	r.With(rbac.RequireRole(rbac.RoleAdmin)).Post("/api/v1/reports/weekly", s.generateAndStoreWeeklyReport)
	}) // end RBAC-protected group

	return r
}

// ─── Helpers ───────────────────────────────────────────────────────────────

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": msg})
}

func parseID(r *http.Request) (int64, error) {
	return strconv.ParseInt(chi.URLParam(r, "id"), 10, 64)
}

// ─── Incidents ─────────────────────────────────────────────────────────────

func (s *Server) listIncidents(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	limit, _ := strconv.Atoi(q.Get("limit"))
	offset, _ := strconv.Atoi(q.Get("offset"))
	out, err := s.Store.ListIncidents(r.Context(), store.IncidentFilter{
		Status:   q.Get("status"),
		Severity: q.Get("severity"),
		Type:     q.Get("type"),
		Limit:    limit,
		Offset:   offset,
	})
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, out)
}

func (s *Server) getIncident(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	inc, err := s.Store.GetIncident(r.Context(), id)
	if err != nil {
		if errors.Is(err, store.ErrNotFound) {
			writeErr(w, 404, "not found")
			return
		}
		writeErr(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, inc)
}

type statusUpdate struct {
	Status string  `json:"status"`
	Notes  *string `json:"notes,omitempty"`
}

func (s *Server) updateIncidentStatus(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	var body statusUpdate
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		writeErr(w, 400, "bad json")
		return
	}
	user := rbac.User(r.Context())
	role := string(rbac.RoleOf(r.Context()))

	prev, err := s.Store.GetIncident(r.Context(), id)
	if err != nil {
		if errors.Is(err, store.ErrNotFound) {
			writeErr(w, 404, "not found")
			return
		}
		writeErr(w, 500, err.Error())
		return
	}
	upd, err := s.Store.UpdateIncidentStatus(r.Context(), id, body.Status, user, body.Notes)
	if err != nil {
		writeErr(w, 400, err.Error())
		return
	}
	s.Audit.Record(r.Context(), user, role, "incident."+body.Status, "incident", id, map[string]any{
		"prev_status": prev.Status,
		"new_status":  upd.Status,
		"notes":       body.Notes,
	})

	// On severity=high|critical AND status transition new→ack: auto-generate
	// a NetworkPolicy draft so analyst has it ready to approve.
	if prev.Status == "new" && upd.Status == "ack" &&
		(prev.Severity == "high" || prev.Severity == "critical") {
		_ = s.maybeCreateAction(r.Context(), upd, user)
	}

	writeJSON(w, 200, upd)
}

func (s *Server) listIncidentAudit(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	out, err := s.Store.ListAudit(r.Context(), "incident", id, limit)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, out)
}

func (s *Server) maybeCreateAction(ctx context.Context, inc *store.Incident, actor string) error {
	yaml := response.GenerateQuarantine(response.QuarantineRequest{
		IncidentID: inc.ID,
		SourceIP:   inc.SourceIP,
		Severity:   inc.Severity,
		Reason:     inc.Type,
	})
	a := &store.Action{
		IncidentID: inc.ID,
		Type:       "quarantine_pod",
		YAMLDraft:  yaml,
	}
	if err := s.Store.CreateAction(ctx, a); err != nil {
		return err
	}
	s.Audit.Record(ctx, actor, "system", "action.created", "action", a.ID, map[string]any{
		"incident_id": inc.ID,
		"type":        a.Type,
	})
	return nil
}

// ─── Actions ───────────────────────────────────────────────────────────────

func (s *Server) listActions(w http.ResponseWriter, r *http.Request) {
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	out, err := s.Store.ListActions(r.Context(), r.URL.Query().Get("status"), limit)
	if err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, out)
}

func (s *Server) getAction(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	a, err := s.Store.GetAction(r.Context(), id)
	if err != nil {
		if errors.Is(err, store.ErrNotFound) {
			writeErr(w, 404, "not found")
			return
		}
		writeErr(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, a)
}

func (s *Server) approveAction(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	user := rbac.User(r.Context())
	role := string(rbac.RoleOf(r.Context()))

	a, err := s.Store.DecideAction(r.Context(), id, "approve", user)
	if err != nil {
		writeErr(w, 400, err.Error())
		return
	}

	path, applyErr := s.executeAction(r.Context(), a)
	var execErr *string
	if applyErr != nil {
		msg := applyErr.Error()
		execErr = &msg
	}
	if err := s.Store.MarkActionExecuted(r.Context(), id, execErr); err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	s.Audit.Record(r.Context(), user, role, "action.approved", "action", id, map[string]any{
		"type":  a.Type,
		"path":  path,
		"error": execErr,
	})
	final, _ := s.Store.GetAction(r.Context(), id)
	writeJSON(w, 200, map[string]any{
		"action":     final,
		"applied_to": path,
		"error":      execErr,
	})
}

// executeAction dispatches to the right handler based on action.Type.
// quarantine_pod uses the YAML applier; delete_pod / shutdown_pod use
// the PodOps client (nil-checked because dryrun mode does not wire it).
func (s *Server) executeAction(ctx context.Context, a *store.Action) (string, error) {
	switch a.Type {
	case store.ActionTypeQuarantinePod:
		// Label the offending pod (status.PodIP == incident source) so the
		// egress-deny NetworkPolicy can select it. Best-effort: if the pod is
		// gone we still apply the policy (it adopts the pod once re-labelled).
		if s.PodOps != nil {
			if inc, err := s.Store.GetIncident(ctx, a.IncidentID); err == nil && inc.SourceIP != "" {
				ns := response.DefaultQuarantineNamespace
				if pod, lerr := s.PodOps.QuarantinePodByIP(ctx, ns, inc.SourceIP, response.LabelKey(a.IncidentID)); lerr != nil {
					log.Printf("[apply] quarantine label incident=%d ip=%s: %v", a.IncidentID, inc.SourceIP, lerr)
				} else {
					log.Printf("[apply] labelled pod %s/%s for quarantine incident=%d", ns, pod, a.IncidentID)
				}
			}
		}
		return s.Applier.Apply(ctx, a.YAMLDraft, a.IncidentID)
	case store.ActionTypeDeletePod, store.ActionTypeShutdownPod:
		if s.PodOps == nil {
			return "", errors.New("pod operations disabled (APPLY_MODE != apply)")
		}
		var p store.PodOpPayload
		if err := json.Unmarshal([]byte(a.YAMLDraft), &p); err != nil {
			return "", fmt.Errorf("decode pod-op payload: %w", err)
		}
		if p.Namespace == "" || p.PodName == "" {
			return "", errors.New("pod-op payload missing namespace or pod_name")
		}
		if a.Type == store.ActionTypeDeletePod {
			if err := s.PodOps.DeletePod(ctx, p.Namespace, p.PodName); err != nil {
				return "", err
			}
			return fmt.Sprintf("pod/%s -n %s [deleted]", p.PodName, p.Namespace), nil
		}
		if err := s.PodOps.ShutdownPod(ctx, p.Namespace, p.PodName); err != nil {
			return "", err
		}
		return fmt.Sprintf("pod/%s -n %s [shutdown grace=300s]", p.PodName, p.Namespace), nil
	default:
		return "", fmt.Errorf("unknown action type %q", a.Type)
	}
}

// followUpRequest is the JSON body for POST /incidents/{id}/follow-up-actions.
type followUpRequest struct {
	Type      string `json:"type"`
	Namespace string `json:"namespace"`
	PodName   string `json:"pod_name"`
	Reason    string `json:"reason"`
}

func (s *Server) createFollowUpAction(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad incident id")
		return
	}
	var body followUpRequest
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		writeErr(w, 400, "bad json")
		return
	}
	if body.Type != store.ActionTypeDeletePod && body.Type != store.ActionTypeShutdownPod {
		writeErr(w, 400, "type must be delete_pod or shutdown_pod")
		return
	}
	if body.Namespace == "" || body.PodName == "" {
		writeErr(w, 400, "namespace and pod_name are required")
		return
	}
	inc, err := s.Store.GetIncident(r.Context(), id)
	if err != nil {
		if errors.Is(err, store.ErrNotFound) {
			writeErr(w, 404, "incident not found")
			return
		}
		writeErr(w, 500, err.Error())
		return
	}
	payload, _ := json.Marshal(store.PodOpPayload{
		Namespace: body.Namespace,
		PodName:   body.PodName,
		Reason:    body.Reason,
	})
	a := &store.Action{
		IncidentID: inc.ID,
		Type:       body.Type,
		YAMLDraft:  string(payload),
	}
	if err := s.Store.CreateAction(r.Context(), a); err != nil {
		writeErr(w, 500, err.Error())
		return
	}
	user := rbac.User(r.Context())
	role := string(rbac.RoleOf(r.Context()))
	s.Audit.Record(r.Context(), user, role, "action.follow_up_created", "action", a.ID, map[string]any{
		"incident_id": inc.ID,
		"type":        body.Type,
		"namespace":   body.Namespace,
		"pod_name":    body.PodName,
	})
	writeJSON(w, 201, a)
}

func (s *Server) rejectAction(w http.ResponseWriter, r *http.Request) {
	id, err := parseID(r)
	if err != nil {
		writeErr(w, 400, "bad id")
		return
	}
	user := rbac.User(r.Context())
	role := string(rbac.RoleOf(r.Context()))

	a, err := s.Store.DecideAction(r.Context(), id, "reject", user)
	if err != nil {
		writeErr(w, 400, err.Error())
		return
	}
	s.Audit.Record(r.Context(), user, role, "action.rejected", "action", id, nil)
	writeJSON(w, 200, a)
}

// helper to silence "unused" while we compile package skeleton
var _ = fmt.Sprintf
