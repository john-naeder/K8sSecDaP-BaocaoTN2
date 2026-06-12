#!/usr/bin/env bash
# Demo loop for the zt-pipeline image when no eBPF collector is wired up.
# Each iteration:
#   1. Regenerates synthetic events into $EVENTS_FILE
#   2. Runs zt-pipeline once against that file (alerts → stderr + file handler)
#   3. Sleeps $DEMO_INTERVAL seconds
#
# Real production: replace this with the eBPF collector piped into zt-pipeline
# (event_source.type=ebpf in the config).

set -euo pipefail

: "${PIPELINE_CONFIG:=/etc/zt/pipeline.yaml}"
: "${EVENTS_FILE:=/tmp/events.json}"
: "${DEMO_INTERVAL:=30}"
: "${DEMO_COUNT:=400}"
: "${DEMO_ATTACK_START:=250}"
: "${DEMO_SCAN_PROBES:=40}"

mkdir -p /var/log/zt /var/lib/zt
cd /var/lib/zt

iter=0
while true; do
    iter=$((iter + 1))
    echo "[demo] ─── iteration ${iter} @ $(date -Iseconds) ───────────────────" >&2

    python3 /usr/local/bin/generate_synthetic_events.py \
        --count        "${DEMO_COUNT}" \
        --attack-start "${DEMO_ATTACK_START}" \
        --scan-probes  "${DEMO_SCAN_PROBES}" \
        > "${EVENTS_FILE}"

    # Pipeline writes ndjson alerts via the configured handler chain.
    # NODE_NAME / POD_NAMESPACE come from the Downward API in the manifest.
    /usr/local/bin/zt-pipeline --config "${PIPELINE_CONFIG}" || {
        echo "[demo] pipeline exited non-zero — continuing" >&2
    }

    sleep "${DEMO_INTERVAL}"
done
