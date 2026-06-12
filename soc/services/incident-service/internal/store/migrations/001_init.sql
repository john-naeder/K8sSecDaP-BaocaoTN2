-- Initial schema for the SOC incident service.
-- Applied automatically on startup (idempotent).

-- Severity ordering helper used by ON CONFLICT updates.
CREATE OR REPLACE FUNCTION severityRank(s TEXT) RETURNS INT AS $$
SELECT CASE COALESCE(s,'')
    WHEN 'critical' THEN 4
    WHEN 'high'     THEN 3
    WHEN 'medium'   THEN 2
    WHEN 'low'      THEN 1
    ELSE 0
END
$$ LANGUAGE SQL IMMUTABLE;

CREATE TABLE IF NOT EXISTS incidents (
    id              BIGSERIAL PRIMARY KEY,
    incident_key    TEXT NOT NULL,                -- type|source dedup key
    type            TEXT NOT NULL,
    source_ip       TEXT NOT NULL,
    severity        TEXT NOT NULL DEFAULT 'medium',
    status          TEXT NOT NULL DEFAULT 'new',  -- new|ack|resolved|suppressed
    nodes_observed  TEXT[] NOT NULL DEFAULT '{}',
    first_seen_ns   BIGINT NOT NULL,
    last_seen_ns    BIGINT NOT NULL,
    occurrence_count INT NOT NULL DEFAULT 1,
    blast_radius    JSONB,
    tags            TEXT[] NOT NULL DEFAULT '{}',
    cross_node      BOOLEAN NOT NULL DEFAULT FALSE,
    ack_at          TIMESTAMPTZ,
    ack_by          TEXT,
    resolved_at     TIMESTAMPTZ,
    resolved_by     TEXT,
    notes           TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (incident_key)
);

CREATE INDEX IF NOT EXISTS idx_incidents_status      ON incidents (status);
CREATE INDEX IF NOT EXISTS idx_incidents_severity    ON incidents (severity);
CREATE INDEX IF NOT EXISTS idx_incidents_last_seen   ON incidents (last_seen_ns DESC);

CREATE TABLE IF NOT EXISTS alerts (
    id              BIGSERIAL PRIMARY KEY,
    incident_id     BIGINT REFERENCES incidents(id) ON DELETE CASCADE,
    alert_json      JSONB NOT NULL,
    received_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_alerts_incident ON alerts (incident_id);

CREATE TABLE IF NOT EXISTS actions (
    id              BIGSERIAL PRIMARY KEY,
    incident_id     BIGINT NOT NULL REFERENCES incidents(id) ON DELETE CASCADE,
    type            TEXT NOT NULL,                -- e.g. quarantine_pod
    status          TEXT NOT NULL DEFAULT 'pending', -- pending|approved|rejected|executed|expired|failed
    yaml_draft      TEXT NOT NULL,
    target_namespace TEXT,
    target_pod_label TEXT,
    approved_by     TEXT,
    approved_at     TIMESTAMPTZ,
    rejected_by     TEXT,
    rejected_at     TIMESTAMPTZ,
    executed_at     TIMESTAMPTZ,
    error           TEXT,
    expires_at      TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '24 hours'),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_actions_status     ON actions (status);
CREATE INDEX IF NOT EXISTS idx_actions_incident   ON actions (incident_id);

CREATE TABLE IF NOT EXISTS audit_log (
    id              BIGSERIAL PRIMARY KEY,
    actor           TEXT NOT NULL,                -- username or system token
    actor_role      TEXT,
    action          TEXT NOT NULL,                -- ack, resolve, approve, reject, ...
    target_type     TEXT NOT NULL,                -- incident, action
    target_id       BIGINT NOT NULL,
    diff            JSONB,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS idx_audit_target ON audit_log (target_type, target_id);
CREATE INDEX IF NOT EXISTS idx_audit_actor  ON audit_log (actor);
