#!/usr/bin/env bash
# Run the closed-loop latency report against the SOC Postgres.
#   ./scripts/latency-report.sh
# Env overrides: SOC_NS (default soc), PG_POD (default postgres-0).
set -euo pipefail
NS="${SOC_NS:-soc}"
POD="${PG_POD:-postgres-0}"
DIR="$(cd "$(dirname "$0")" && pwd)"
kubectl -n "$NS" exec -i "$POD" -- psql -U soc -d soc -f - < "$DIR/latency-report.sql"
