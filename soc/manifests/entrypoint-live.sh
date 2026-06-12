#!/usr/bin/env bash
# Live mode — capture real TCP SYNs via tcpdump and pipe into zt-pipeline.
# Requires hostNetwork + NET_RAW + NET_ADMIN.
#
# Pipeline config must NOT use event_source.type=file in this mode; the
# zt-pipeline-config-live ConfigMap overrides type so the pipeline reads stdin.

set -uo pipefail

: "${PIPELINE_CONFIG:=/etc/zt/pipeline.yaml}"
: "${LIVE_IFACE:=any}"

echo "[live] node=${NODE_NAME:-?} iface=${LIVE_IFACE}" >&2
echo "[live] tcpdump version: $(tcpdump --version 2>&1 | head -1)" >&2
ip -br link show 2>&1 | head -10 >&2 || true

while true; do
    echo "[live] starting capture pipeline ..." >&2

    # tcpdump filter: SYN set, ACK clear → connection initiation only.
    # -U for packet-buffered output (line-buffered for our parser).
    # -Z root: skip Ubuntu's automatic privilege drop to "tcpdump" user.
    tcpdump -nn -l -U -Z root -i "${LIVE_IFACE}" \
        'tcp[tcpflags] & tcp-syn != 0 and tcp[tcpflags] & tcp-ack == 0' \
      | /usr/local/bin/tcpdump_to_events.py \
      | /usr/local/bin/zt-pipeline --config "${PIPELINE_CONFIG}" \
      || echo "[live] pipeline exited code=$? — restarting in 5s" >&2

    sleep 5
done
