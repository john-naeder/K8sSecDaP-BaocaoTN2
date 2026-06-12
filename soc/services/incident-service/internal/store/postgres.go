package store

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"sort"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

//go:embed migrations/*.sql
var migrationsFS embed.FS

// Store wraps a pgx pool. Methods are designed to be small and explicit so
// the API handler stays thin.
type Store struct {
	Pool *pgxpool.Pool
}

func New(ctx context.Context, dsn string) (*Store, error) {
	cfg, err := pgxpool.ParseConfig(dsn)
	if err != nil {
		return nil, fmt.Errorf("parse dsn: %w", err)
	}
	cfg.MaxConns = 10
	cfg.MinConns = 1
	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("connect: %w", err)
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("ping: %w", err)
	}
	s := &Store{Pool: pool}
	if err := s.applyMigrations(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("migrate: %w", err)
	}
	return s, nil
}

func (s *Store) Close() { s.Pool.Close() }

func (s *Store) applyMigrations(ctx context.Context) error {
	entries, err := fs.ReadDir(migrationsFS, "migrations")
	if err != nil {
		return err
	}
	files := []string{}
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(e.Name(), ".sql") {
			files = append(files, e.Name())
		}
	}
	sort.Strings(files)
	for _, f := range files {
		body, err := fs.ReadFile(migrationsFS, "migrations/"+f)
		if err != nil {
			return err
		}
		if _, err := s.Pool.Exec(ctx, string(body)); err != nil {
			return fmt.Errorf("migration %s: %w", f, err)
		}
	}
	return nil
}

// UpsertIncident records a new occurrence. If an incident with the same
// incident_key exists, last_seen_ns/count/nodes/severity are updated.
// Returns the resulting Incident plus a `created` flag indicating whether
// a fresh row was inserted (caller may use this to fire a notification).
func (s *Store) UpsertIncident(ctx context.Context, a *IncomingAlert) (*Incident, bool, error) {
	if a.Source == "" || a.Type == "" {
		return nil, false, errors.New("alert missing type/source")
	}
	key := a.Type + "|" + a.Source

	nodes := []string{}
	if a.NodeName != "" {
		nodes = []string{a.NodeName}
	}
	if a.Correlation != nil && len(a.Correlation.NodesObserved) > 0 {
		nodes = a.Correlation.NodesObserved
	}
	occCount := 1
	firstSeen := a.TimestampNS
	lastSeen := a.TimestampNS
	cross := false
	if a.Correlation != nil {
		occCount = a.Correlation.OccurrenceCount
		firstSeen = a.Correlation.FirstSeenNS
		lastSeen = a.Correlation.LastSeenNS
		cross = a.Correlation.CrossNode
	}

	var blastRadius []byte
	if a.Type == "blast_radius" && len(a.Details) > 0 {
		blastRadius = a.Details
	}

	tags := a.Tags
	if tags == nil {
		tags = []string{}
	}

	const q = `
INSERT INTO incidents (
    incident_key, type, source_ip, severity, status,
    nodes_observed, first_seen_ns, last_seen_ns, occurrence_count,
    blast_radius, tags, cross_node
) VALUES ($1,$2,$3,$4,'new',$5,$6,$7,$8,$9::jsonb,$10,$11)
ON CONFLICT (incident_key) DO UPDATE SET
    severity        = CASE WHEN severityRank($4) > severityRank(incidents.severity)
                            THEN $4 ELSE incidents.severity END,
    nodes_observed  = (
        SELECT array_agg(DISTINCT n) FROM unnest(incidents.nodes_observed || $5) AS n
    ),
    last_seen_ns    = GREATEST(incidents.last_seen_ns, $7),
    occurrence_count= incidents.occurrence_count + 1,
    blast_radius    = COALESCE($9::jsonb, incidents.blast_radius),
    cross_node      = incidents.cross_node OR $11,
    tags            = (
        SELECT array_agg(DISTINCT t) FROM unnest(incidents.tags || $10) AS t
    ),
    updated_at      = NOW()
RETURNING id, incident_key, type, source_ip, severity, status,
          nodes_observed, first_seen_ns, last_seen_ns, occurrence_count,
          blast_radius, tags, cross_node, ack_at, ack_by, resolved_at,
          resolved_by, notes, created_at, updated_at,
          (xmax = 0) AS inserted`

	row := s.Pool.QueryRow(ctx, q,
		key, a.Type, a.Source, a.Severity,
		nodes, firstSeen, lastSeen, occCount,
		blastRadius, tags, cross,
	)
	var inc Incident
	var inserted bool
	if err := row.Scan(&inc.ID, &inc.IncidentKey, &inc.Type, &inc.SourceIP,
		&inc.Severity, &inc.Status, &inc.NodesObserved, &inc.FirstSeenNS,
		&inc.LastSeenNS, &inc.OccurrenceCount, &inc.BlastRadius, &inc.Tags,
		&inc.CrossNode, &inc.AckAt, &inc.AckBy, &inc.ResolvedAt, &inc.ResolvedBy,
		&inc.Notes, &inc.CreatedAt, &inc.UpdatedAt, &inserted); err != nil {
		return nil, false, fmt.Errorf("upsert: %w", err)
	}
	return &inc, inserted, nil
}

// AppendAlert stores the raw alert JSON for traceability.
func (s *Store) AppendAlert(ctx context.Context, incidentID int64, body json.RawMessage) error {
	_, err := s.Pool.Exec(ctx,
		`INSERT INTO alerts (incident_id, alert_json) VALUES ($1, $2)`,
		incidentID, body)
	return err
}

// ListIncidents returns a slice of incidents matching the filters. All filter
// fields are optional; empty string / zero means "no filter".
type IncidentFilter struct {
	Status   string
	Severity string
	Type     string
	Limit    int
	Offset   int
}

func (s *Store) ListIncidents(ctx context.Context, f IncidentFilter) ([]Incident, error) {
	if f.Limit <= 0 || f.Limit > 500 {
		f.Limit = 100
	}
	args := []any{}
	conds := []string{}
	add := func(cond string, val any) {
		args = append(args, val)
		conds = append(conds, fmt.Sprintf(cond, len(args)))
	}
	if f.Status != "" {
		add("status = $%d", f.Status)
	}
	if f.Severity != "" {
		add("severity = $%d", f.Severity)
	}
	if f.Type != "" {
		add("type = $%d", f.Type)
	}
	where := ""
	if len(conds) > 0 {
		where = "WHERE " + strings.Join(conds, " AND ")
	}
	q := fmt.Sprintf(`
SELECT id, incident_key, type, source_ip, severity, status,
       nodes_observed, first_seen_ns, last_seen_ns, occurrence_count,
       blast_radius, tags, cross_node, ack_at, ack_by, resolved_at,
       resolved_by, notes, created_at, updated_at, pod_name, pod_namespace
FROM incidents %s
ORDER BY last_seen_ns DESC
LIMIT %d OFFSET %d`, where, f.Limit, f.Offset)
	rows, err := s.Pool.Query(ctx, q, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []Incident{}
	for rows.Next() {
		var inc Incident
		if err := rows.Scan(&inc.ID, &inc.IncidentKey, &inc.Type, &inc.SourceIP,
			&inc.Severity, &inc.Status, &inc.NodesObserved, &inc.FirstSeenNS,
			&inc.LastSeenNS, &inc.OccurrenceCount, &inc.BlastRadius, &inc.Tags,
			&inc.CrossNode, &inc.AckAt, &inc.AckBy, &inc.ResolvedAt, &inc.ResolvedBy,
			&inc.Notes, &inc.CreatedAt, &inc.UpdatedAt, &inc.PodName, &inc.PodNamespace); err != nil {
			return nil, err
		}
		out = append(out, inc)
	}
	return out, rows.Err()
}

func (s *Store) GetIncident(ctx context.Context, id int64) (*Incident, error) {
	q := `SELECT id, incident_key, type, source_ip, severity, status,
       nodes_observed, first_seen_ns, last_seen_ns, occurrence_count,
       blast_radius, tags, cross_node, ack_at, ack_by, resolved_at,
       resolved_by, notes, created_at, updated_at, pod_name, pod_namespace
FROM incidents WHERE id=$1`
	row := s.Pool.QueryRow(ctx, q, id)
	var inc Incident
	if err := row.Scan(&inc.ID, &inc.IncidentKey, &inc.Type, &inc.SourceIP,
		&inc.Severity, &inc.Status, &inc.NodesObserved, &inc.FirstSeenNS,
		&inc.LastSeenNS, &inc.OccurrenceCount, &inc.BlastRadius, &inc.Tags,
		&inc.CrossNode, &inc.AckAt, &inc.AckBy, &inc.ResolvedAt, &inc.ResolvedBy,
		&inc.Notes, &inc.CreatedAt, &inc.UpdatedAt, &inc.PodName, &inc.PodNamespace); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return nil, ErrNotFound
		}
		return nil, err
	}
	return &inc, nil
}

// SetIncidentPodContext records the K8s pod (name/namespace) that held the
// incident's source IP. Best-effort enrichment — callers may ignore the error.
func (s *Store) SetIncidentPodContext(ctx context.Context, id int64, podName, podNamespace string) error {
	_, err := s.Pool.Exec(ctx,
		`UPDATE incidents SET pod_name=$2, pod_namespace=$3, updated_at=NOW() WHERE id=$1`,
		id, podName, podNamespace)
	return err
}

func (s *Store) UpdateIncidentStatus(ctx context.Context, id int64, status, actor string, notes *string) (*Incident, error) {
	now := time.Now()
	var ackAt, resAt *time.Time
	var ackBy, resBy *string
	switch status {
	case "ack", "acknowledged":
		status = "ack"
		ackAt = &now
		ackBy = &actor
	case "resolved":
		resAt = &now
		resBy = &actor
	case "suppressed", "new":
		// allowed; no timestamps
	default:
		return nil, fmt.Errorf("invalid status %q", status)
	}

	q := `UPDATE incidents SET
        status      = $2,
        ack_at      = COALESCE($3, ack_at),
        ack_by      = COALESCE($4, ack_by),
        resolved_at = COALESCE($5, resolved_at),
        resolved_by = COALESCE($6, resolved_by),
        notes       = COALESCE($7, notes),
        updated_at  = NOW()
        WHERE id = $1`
	tag, err := s.Pool.Exec(ctx, q, id, status, ackAt, ackBy, resAt, resBy, notes)
	if err != nil {
		return nil, err
	}
	if tag.RowsAffected() == 0 {
		return nil, ErrNotFound
	}
	return s.GetIncident(ctx, id)
}

// CreateAction stores a NetworkPolicy draft for analyst review.
func (s *Store) CreateAction(ctx context.Context, a *Action) error {
	q := `INSERT INTO actions (incident_id, type, status, yaml_draft,
        target_namespace, target_pod_label)
        VALUES ($1,$2,'pending',$3,$4,$5)
        RETURNING id, status, expires_at, created_at`
	return s.Pool.QueryRow(ctx, q,
		a.IncidentID, a.Type, a.YAMLDraft, a.TargetNamespace, a.TargetPodLabel,
	).Scan(&a.ID, &a.Status, &a.ExpiresAt, &a.CreatedAt)
}

func (s *Store) ListActions(ctx context.Context, status string, limit int) ([]Action, error) {
	if limit <= 0 || limit > 500 {
		limit = 100
	}
	args := []any{}
	where := ""
	if status != "" {
		args = append(args, status)
		where = "WHERE status = $1"
	}
	q := fmt.Sprintf(`SELECT id, incident_id, type, status, yaml_draft,
        target_namespace, target_pod_label, approved_by, approved_at,
        rejected_by, rejected_at, executed_at, error, expires_at, created_at
        FROM actions %s ORDER BY created_at DESC LIMIT %d`, where, limit)
	rows, err := s.Pool.Query(ctx, q, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []Action{}
	for rows.Next() {
		var a Action
		if err := rows.Scan(&a.ID, &a.IncidentID, &a.Type, &a.Status, &a.YAMLDraft,
			&a.TargetNamespace, &a.TargetPodLabel, &a.ApprovedBy, &a.ApprovedAt,
			&a.RejectedBy, &a.RejectedAt, &a.ExecutedAt, &a.Error, &a.ExpiresAt,
			&a.CreatedAt); err != nil {
			return nil, err
		}
		out = append(out, a)
	}
	return out, rows.Err()
}

func (s *Store) GetAction(ctx context.Context, id int64) (*Action, error) {
	q := `SELECT id, incident_id, type, status, yaml_draft,
        target_namespace, target_pod_label, approved_by, approved_at,
        rejected_by, rejected_at, executed_at, error, expires_at, created_at
        FROM actions WHERE id=$1`
	row := s.Pool.QueryRow(ctx, q, id)
	var a Action
	if err := row.Scan(&a.ID, &a.IncidentID, &a.Type, &a.Status, &a.YAMLDraft,
		&a.TargetNamespace, &a.TargetPodLabel, &a.ApprovedBy, &a.ApprovedAt,
		&a.RejectedBy, &a.RejectedAt, &a.ExecutedAt, &a.Error, &a.ExpiresAt,
		&a.CreatedAt); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return nil, ErrNotFound
		}
		return nil, err
	}
	return &a, nil
}

func (s *Store) DecideAction(ctx context.Context, id int64, decision, actor string) (*Action, error) {
	now := time.Now()
	var setSQL string
	switch decision {
	case "approve":
		setSQL = `status='approved', approved_by=$2, approved_at=$3`
	case "reject":
		setSQL = `status='rejected', rejected_by=$2, rejected_at=$3`
	default:
		return nil, fmt.Errorf("invalid decision %q", decision)
	}
	q := fmt.Sprintf(`UPDATE actions SET %s WHERE id=$1 AND status='pending'`, setSQL)
	tag, err := s.Pool.Exec(ctx, q, id, actor, now)
	if err != nil {
		return nil, err
	}
	if tag.RowsAffected() == 0 {
		return nil, fmt.Errorf("action %d not in pending state (or not found)", id)
	}
	return s.GetAction(ctx, id)
}

func (s *Store) MarkActionExecuted(ctx context.Context, id int64, errMsg *string) error {
	now := time.Now()
	status := "executed"
	if errMsg != nil {
		status = "failed"
	}
	_, err := s.Pool.Exec(ctx,
		`UPDATE actions SET status=$2, executed_at=$3, error=$4 WHERE id=$1`,
		id, status, now, errMsg)
	return err
}

// AppendAudit records a single audit entry.
func (s *Store) AppendAudit(ctx context.Context, e *AuditEntry) error {
	q := `INSERT INTO audit_log (actor, actor_role, action, target_type, target_id, diff)
          VALUES ($1,$2,$3,$4,$5,$6) RETURNING id, occurred_at`
	return s.Pool.QueryRow(ctx, q, e.Actor, e.ActorRole, e.Action, e.TargetType,
		e.TargetID, e.Diff).Scan(&e.ID, &e.OccurredAt)
}

func (s *Store) ListAudit(ctx context.Context, targetType string, targetID int64, limit int) ([]AuditEntry, error) {
	if limit <= 0 || limit > 500 {
		limit = 100
	}
	q := `SELECT id, actor, actor_role, action, target_type, target_id, diff, occurred_at
          FROM audit_log WHERE target_type=$1 AND target_id=$2
          ORDER BY occurred_at DESC LIMIT $3`
	rows, err := s.Pool.Query(ctx, q, targetType, targetID, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []AuditEntry{}
	for rows.Next() {
		var e AuditEntry
		if err := rows.Scan(&e.ID, &e.Actor, &e.ActorRole, &e.Action,
			&e.TargetType, &e.TargetID, &e.Diff, &e.OccurredAt); err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, rows.Err()
}

var ErrNotFound = errors.New("not found")

// AuditFilter narrows ListAuditRange. Empty fields mean "no filter".
type AuditFilter struct {
	From   time.Time
	To     time.Time
	Actor  string
	Action string
}

// StreamAuditRange invokes fn for every audit_log row matching the filter,
// in occurred_at ASC order so exports read forward through time. Streaming
// keeps memory bounded when a forensic dump spans months. fn returning a
// non-nil error halts iteration.
func (s *Store) StreamAuditRange(ctx context.Context, f AuditFilter, fn func(*AuditEntry) error) error {
	args := []any{}
	conds := []string{}
	add := func(cond string, val any) {
		args = append(args, val)
		conds = append(conds, fmt.Sprintf(cond, len(args)))
	}
	if !f.From.IsZero() {
		add("occurred_at >= $%d", f.From)
	}
	if !f.To.IsZero() {
		add("occurred_at < $%d", f.To)
	}
	if f.Actor != "" {
		add("actor = $%d", f.Actor)
	}
	if f.Action != "" {
		add("action = $%d", f.Action)
	}
	where := ""
	if len(conds) > 0 {
		where = "WHERE " + strings.Join(conds, " AND ")
	}
	q := fmt.Sprintf(`SELECT id, actor, actor_role, action, target_type, target_id, diff, occurred_at
                      FROM audit_log %s
                      ORDER BY occurred_at ASC`, where)
	rows, err := s.Pool.Query(ctx, q, args...)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		var e AuditEntry
		if err := rows.Scan(&e.ID, &e.Actor, &e.ActorRole, &e.Action,
			&e.TargetType, &e.TargetID, &e.Diff, &e.OccurredAt); err != nil {
			return err
		}
		if err := fn(&e); err != nil {
			return err
		}
	}
	return rows.Err()
}

// SummaryRange is computed by GenerateWeekly. Pure aggregations — no
// per-row data leaves the DB.
type SummaryRange struct {
	From              time.Time
	To                time.Time
	TotalIncidents    int
	BySeverity        map[string]int
	TopAttackers      []AttackerCount
	ActionsBreakdown  map[string]int
	MTTRMedianSeconds float64
	AuditEventsCount  int
}

type AttackerCount struct {
	SourceIP string
	Count    int
}

// SummariseRange runs aggregate queries for the [from, to) window.
func (s *Store) SummariseRange(ctx context.Context, from, to time.Time) (*SummaryRange, error) {
	out := &SummaryRange{
		From:             from,
		To:               to,
		BySeverity:       map[string]int{},
		ActionsBreakdown: map[string]int{},
	}

	if err := s.Pool.QueryRow(ctx,
		`SELECT COUNT(*) FROM incidents WHERE created_at >= $1 AND created_at < $2`,
		from, to).Scan(&out.TotalIncidents); err != nil {
		return nil, fmt.Errorf("total incidents: %w", err)
	}

	rows, err := s.Pool.Query(ctx,
		`SELECT severity, COUNT(*) FROM incidents
         WHERE created_at >= $1 AND created_at < $2
         GROUP BY severity`,
		from, to)
	if err != nil {
		return nil, fmt.Errorf("severity breakdown: %w", err)
	}
	for rows.Next() {
		var sev string
		var n int
		if err := rows.Scan(&sev, &n); err != nil {
			rows.Close()
			return nil, err
		}
		out.BySeverity[sev] = n
	}
	rows.Close()

	rows, err = s.Pool.Query(ctx,
		`SELECT source_ip, COUNT(*) AS c FROM incidents
         WHERE created_at >= $1 AND created_at < $2
         GROUP BY source_ip ORDER BY c DESC LIMIT 10`,
		from, to)
	if err != nil {
		return nil, fmt.Errorf("top attackers: %w", err)
	}
	for rows.Next() {
		var a AttackerCount
		if err := rows.Scan(&a.SourceIP, &a.Count); err != nil {
			rows.Close()
			return nil, err
		}
		out.TopAttackers = append(out.TopAttackers, a)
	}
	rows.Close()

	rows, err = s.Pool.Query(ctx,
		`SELECT type, COUNT(*) FROM actions
         WHERE created_at >= $1 AND created_at < $2
         GROUP BY type`,
		from, to)
	if err != nil {
		return nil, fmt.Errorf("actions breakdown: %w", err)
	}
	for rows.Next() {
		var t string
		var n int
		if err := rows.Scan(&t, &n); err != nil {
			rows.Close()
			return nil, err
		}
		out.ActionsBreakdown[t] = n
	}
	rows.Close()

	// MTTR median (seconds) — diff between created_at and ack_at across acked incidents.
	if err := s.Pool.QueryRow(ctx,
		`SELECT COALESCE(
            percentile_cont(0.5) WITHIN GROUP (
                ORDER BY EXTRACT(EPOCH FROM (ack_at - created_at))
            ), 0)
         FROM incidents
         WHERE ack_at IS NOT NULL
           AND created_at >= $1 AND created_at < $2`,
		from, to).Scan(&out.MTTRMedianSeconds); err != nil {
		return nil, fmt.Errorf("mttr: %w", err)
	}

	if err := s.Pool.QueryRow(ctx,
		`SELECT COUNT(*) FROM audit_log
         WHERE occurred_at >= $1 AND occurred_at < $2`,
		from, to).Scan(&out.AuditEventsCount); err != nil {
		return nil, fmt.Errorf("audit count: %w", err)
	}

	return out, nil
}
