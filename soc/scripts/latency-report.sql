-- Closed-loop latency report for the eBPF → detect → prevent pipeline (M2/M3).
--
-- Stages (all WALL-CLOCK, Postgres TIMESTAMPTZ — directly subtractable):
--   t_alert   = the alert that triggered this draft (latest alerts.received_at
--               at or before the action was created — anchors correctly even
--               for recurring/deduped incidents, unlike incidents.created_at
--               which is the FIRST-ever occurrence)
--   t_draft   = quarantine NetworkPolicy drafted (actions.created_at)
--   t_approve = analyst approved the action      (actions.approved_at)
--   t_enforce = policy applied to the cluster    (actions.executed_at)
--
-- "System response" = machine time excluding the human review gate
--                   = (t_draft - t_alert) + (t_enforce - t_approve).
-- "alert→approve"   = human-in-the-loop review time (dominates total; not a
--                     property of the system).
--
-- NOTE: meaningful on a CONTROLLED run (fresh scan → draft → approve → enforce
-- within one session). Historical rows with manual approvals days apart will
-- show large alert→approve but small system-response numbers.
--
-- CLOCK-DOMAIN NOTE: the eBPF event time (incidents.first_seen_ns /
-- last_seen_ns, from bpf_ktime_get_ns) is MONOTONIC per-node and is NOT
-- comparable to the wall-clock columns above. It is used here only for the
-- intra-incident scan span (a valid monotonic delta).

\echo '== Per-incident closed-loop latency (wall-clock, ms) =='
WITH q AS (
  SELECT i.id, i.type, i.source_ip, i.pod_name,
         i.first_seen_ns, i.last_seen_ns,
         a.created_at  AS t_draft,
         a.approved_at AS t_approve,
         a.executed_at AS t_enforce,
         (SELECT max(al.received_at) FROM alerts al
          WHERE al.incident_id = i.id AND al.received_at <= a.created_at) AS t_alert
  FROM incidents i
  JOIN actions a ON a.incident_id = i.id AND a.type = 'quarantine_pod'
)
SELECT id, type, source_ip, pod_name,
  round(extract(epoch FROM (t_draft   - t_alert))   * 1000)::int AS alert_to_draft_ms,
  round(extract(epoch FROM (t_approve - t_draft))   * 1000)::int AS draft_to_approve_ms,
  round(extract(epoch FROM (t_enforce - t_approve)) * 1000)::int AS approve_to_enforce_ms,
  round((extract(epoch FROM (t_draft - t_alert))
       + extract(epoch FROM (t_enforce - t_approve))) * 1000)::int AS system_response_ms,
  round((last_seen_ns - first_seen_ns) / 1e6)::int              AS scan_span_ms
FROM q
WHERE t_enforce IS NOT NULL AND t_alert IS NOT NULL
ORDER BY id DESC
LIMIT 50;

\echo ''
\echo '== Aggregate over executed quarantines (system response excludes human review) =='
WITH q AS (
  SELECT a.created_at AS t_draft, a.approved_at AS t_approve, a.executed_at AS t_enforce,
         (SELECT max(al.received_at) FROM alerts al
          WHERE al.incident_id = i.id AND al.received_at <= a.created_at) AS t_alert
  FROM incidents i
  JOIN actions a ON a.incident_id = i.id AND a.type = 'quarantine_pod'
  WHERE a.executed_at IS NOT NULL
), d AS (
  SELECT extract(epoch FROM (t_draft - t_alert)) * 1000 AS alert_to_draft_ms,
         (extract(epoch FROM (t_draft - t_alert))
        + extract(epoch FROM (t_enforce - COALESCE(t_approve, t_draft)))) * 1000 AS system_response_ms
  FROM q
  WHERE t_alert IS NOT NULL
)
SELECT
  count(*)                                                              AS n,
  round(avg(alert_to_draft_ms))::int                                   AS avg_alert_to_draft_ms,
  round(avg(system_response_ms))::int                                  AS avg_system_response_ms,
  round(percentile_cont(0.5)  WITHIN GROUP (ORDER BY system_response_ms))::int AS p50_system_response_ms,
  round(percentile_cont(0.95) WITHIN GROUP (ORDER BY system_response_ms))::int AS p95_system_response_ms,
  round(max(system_response_ms))::int                                  AS max_system_response_ms
FROM d;
