-- 002_action_types.sql — extend actions.type vocabulary for Layer-2 ops.
--
-- BC2 introduces two follow-up action types on top of the original
-- 'quarantine_pod' (Layer-1 NetworkPolicy):
--   - delete_pod   : remove the offending pod immediately. If owned by a
--                    Deployment / DaemonSet, the controller recreates it.
--   - shutdown_pod : label with zt-soc/shutdown=true and delete with a
--                    300s grace period for log inspection.
--
-- The actions.yaml_draft column stays NOT NULL; for pod ops we store a
-- short JSON fragment ({"namespace":..., "pod_name":...}) instead of a
-- full NetworkPolicy YAML so the existing schema does not need to change.

-- Idempotent. We deliberately keep status/type as TEXT (no CHECK
-- constraint) to allow callers to register new types without DDL changes.

CREATE INDEX IF NOT EXISTS idx_actions_type ON actions (type);
