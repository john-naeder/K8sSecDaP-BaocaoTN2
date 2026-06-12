-- 003_retention_indices.sql — add composite indices that BC2 Phase D
-- export endpoints lean on (audit range scans and severity-keyed
-- incident summaries). Idempotent — re-running is safe.
--
-- audit_log queries hit (target_type, occurred_at DESC) for per-incident
-- timelines and (actor, occurred_at DESC) for forensic dump by user.
-- incidents queries hit (severity, last_seen_ns DESC) for "top recent
-- high-severity" summary rows.

CREATE INDEX IF NOT EXISTS idx_audit_target_time
    ON audit_log (target_type, occurred_at DESC);

CREATE INDEX IF NOT EXISTS idx_audit_actor_time
    ON audit_log (actor, occurred_at DESC);

CREATE INDEX IF NOT EXISTS idx_incidents_severity_lastseen
    ON incidents (severity, last_seen_ns DESC);
