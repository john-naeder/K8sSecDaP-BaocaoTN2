-- Pod context for incidents: which Pod (name/namespace) carried the offending
-- source IP at detection time. Resolved best-effort by the subscriber when an
-- incident is first created. Nullable — the pod may already be gone, or the
-- service may run without cluster access (dryrun).
ALTER TABLE incidents ADD COLUMN IF NOT EXISTS pod_name      TEXT;
ALTER TABLE incidents ADD COLUMN IF NOT EXISTS pod_namespace TEXT;
