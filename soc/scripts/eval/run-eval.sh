#!/usr/bin/env bash
# Quantitative evaluation harness for the eBPF detect→prevent pipeline (M3).
#
# IMPORTANT — actual detector semantics (pipeline/src/engine.cpp):
#   port_scan    ⟺ a source IP makes > THRESHOLD (40) *connections* in the 60 s
#                  tumbling window. The CMS is keyed on src_ip and counts
#                  connection VOLUME, not distinct destination ports.
#   blast_radius ⟺ computed only for a source that already tripped port_scan,
#                  reporting how many distinct pods it reached (lateral spread).
#
# So detection is connection-rate based: real scans (many connects) are caught,
# but a high-volume *benign* client also trips it — a limitation this harness
# measures explicitly (the L-chatty row) rather than hiding.
#
#   ./scripts/eval/run-eval.sh
#
# Env: EVAL_NODE (userver-worker-1), NS_TARGETS (zt-targets), NS_MAPPER (zt-mapper),
#      NMAP_IMG (instrumentisto/nmap:7.95).
set -uo pipefail

NODE="${EVAL_NODE:-userver-worker-1}"
NST="${NS_TARGETS:-zt-targets}"
NSM="${NS_MAPPER:-zt-mapper}"
IMG="${NMAP_IMG:-instrumentisto/nmap:7.95}"
THRESHOLD=40
OUTDIR="$(cd "$(dirname "$0")" && pwd)/results"
mkdir -p "$OUTDIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$OUTDIR/eval-$STAMP.md"
CSV="$OUTDIR/eval-$STAMP.csv"

say() { echo "[eval] $*" >&2; }

EBPF_POD="$(kubectl -n "$NSM" get pods -l app.kubernetes.io/name=zt-pipeline-ebpf \
  -o jsonpath="{range .items[*]}{.metadata.name}{' '}{.spec.nodeName}{'\n'}{end}" \
  | awk -v n="$NODE" '$2==n{print $1; exit}')"
[ -n "$EBPF_POD" ] || { say "no eBPF pod on $NODE"; exit 1; }
say "eBPF detector pod on $NODE = $EBPF_POD"

mkpod() {  # $1=name → echoes pod IP
  local name="$1"
  kubectl -n "$NST" delete pod "$name" --grace-period=0 --force >/dev/null 2>&1
  kubectl -n "$NST" run "$name" --image="$IMG" --restart=Never \
    --overrides="{\"spec\":{\"nodeSelector\":{\"kubernetes.io/hostname\":\"$NODE\"}}}" \
    --command -- sleep infinity >/dev/null 2>&1
  kubectl -n "$NST" wait --for=condition=Ready "pod/$name" --timeout=60s >/dev/null 2>&1
  kubectl -n "$NST" get pod "$name" -o jsonpath='{.status.podIP}'
}

# Targets for scans (web + api pods); a web pod (:80 open) for benign clients.
mapfile -t TARGETS < <(kubectl -n "$NST" get pods -l 'app in (web,api)' \
  -o jsonpath='{range .items[*]}{.status.podIP}{"\n"}{end}' 2>/dev/null | grep -E '^[0-9]' | head -6)
[ "${#TARGETS[@]}" -gt 0 ] || { say "no target pods in $NST"; exit 1; }
T0="${TARGETS[0]}"
WEB="$(kubectl -n "$NST" get pods -l app=web \
  -o jsonpath='{.items[0].status.podIP}' 2>/dev/null)"; WEB="${WEB:-$T0}"
say "scan targets: ${TARGETS[*]}  benign web target: $WEB:80"

# detected SRC TYPE [ITERS] → 0 if an alert of TYPE for SRC appears in the eBPF
# pipeline logs within ITERS×2 s (default 9 → 18 s), else 1. blast_radius rides
# the slower graph/SCC cadence, so it polls longer.
detected() {
  local src="$1" type="$2" iters="${3:-9}" i
  for i in $(seq 1 "$iters"); do
    if kubectl -n "$NSM" logs "$EBPF_POD" -c pipeline --since=140s 2>/dev/null \
        | grep -q "\"type\":\"$type\",\"source\":\"$src\""; then
      return 0
    fi
    sleep 2
  done
  return 1
}

# N benign connects to the open web port (fast, deterministic count).
benign_connects() { kubectl -n "$NST" exec "$1" -- sh -c \
  "for i in \$(seq 1 $2); do nc -w1 -z $WEB 80 2>/dev/null; done" >/dev/null 2>&1; }

declare -a ROWS CREATED
record() { ROWS+=("$1"); say "RESULT: $1"; }
track()  { CREATED+=("$1"); }

# Benign FIRST on a drained window: the detector keys on source IP and pod IPs
# recycle fast, so a benign pod created right after a scanner could inherit the
# scanner's connection count within the 60 s window (a test artifact). Draining
# + benign-first removes that.
say "draining the 60 s window before benign sampling (75 s)..."
sleep 75

# ════════════════════ Benign — realistic legit rates (< threshold) ══════════
# 5 independent benign clients at 15/20/25/30/35 connects (all < 40) → expect OK.
say "B1..B5 benign clients at increasing-but-sub-threshold connection rates"
i=0
for cnt in 15 20 25 30 35; do
  i=$((i+1))
  IP="$(mkpod "eval-ben-$i")"; track "eval-ben-$i"
  benign_connects "eval-ben-$i" "$cnt"
  detected "$IP" port_scan && record "B$i benign-${cnt}conn|benign|(none)|FALSE-POSITIVE" \
                           || record "B$i benign-${cnt}conn|benign|(none)|OK-no-alert"
done

# Boundary just UNDER threshold (38 connects) must NOT alert.
say "B-edge boundary-under (38 connects, just under threshold $THRESHOLD)"
IP="$(mkpod eval-ben-edge)"; track eval-ben-edge
benign_connects eval-ben-edge 38
detected "$IP" port_scan && record "B-edge boundary-under-38|benign|(none)|FALSE-POSITIVE" \
                         || record "B-edge boundary-under-38|benign|(none)|OK-no-alert"

# ════════════════════ Attack scenarios (expect DETECTED) ════════════════════
say "S1 vertical scan (1 src → 1 target, 200 ports → 200 connects)"
IP="$(mkpod eval-s1)"; track eval-s1
kubectl -n "$NST" exec eval-s1 -- nmap -sT -Pn -T4 -p1-200 "$T0" >/dev/null 2>&1
detected "$IP" port_scan && record "S1 vertical-scan|attack|port_scan|DETECTED" \
                         || record "S1 vertical-scan|attack|port_scan|MISSED"

say "S2 lateral scan (1 src → ${#TARGETS[@]} pods, 30 ports each → fan-out + volume)"
IP="$(mkpod eval-s2)"; track eval-s2
kubectl -n "$NST" exec eval-s2 -- sh -c "nmap -sT -Pn -T4 -p1-30 ${TARGETS[*]}" >/dev/null 2>&1
detected "$IP" blast_radius 25 && record "S2 lateral-scan|attack|blast_radius|DETECTED" \
                               || record "S2 lateral-scan|attack|blast_radius|MISSED"

say "S3 burst scan (1 src → 1 target, 1000 ports → 1000 connects)"
IP="$(mkpod eval-s3)"; track eval-s3
kubectl -n "$NST" exec eval-s3 -- nmap -sT -Pn -T5 -p1-1000 "$T0" >/dev/null 2>&1
detected "$IP" port_scan && record "S3 burst-scan|attack|port_scan|DETECTED" \
                         || record "S3 burst-scan|attack|port_scan|MISSED"

say "S4 boundary-over (45 connects, just over threshold $THRESHOLD)"
IP="$(mkpod eval-s4)"; track eval-s4
kubectl -n "$NST" exec eval-s4 -- nmap -sT -Pn -T4 -p1-45 "$T0" >/dev/null 2>&1
detected "$IP" port_scan && record "S4 boundary-over-45|attack|port_scan|DETECTED" \
                         || record "S4 boundary-over-45|attack|port_scan|MISSED"

# ════════════════════ Limitation demonstrations (off headline metrics) ══════
# L-slow: 50 connects spread at 2 s/port (~100 s) so each 60 s window sees ≈30
# (< 40) → MISSED. Documents the tumbling-window blind spot (→ sliding-window CMS).
say "L-slow slow-scan evasion (50 ports @ 2 s, ~30 connects/60 s window)"
IP="$(mkpod eval-lslow)"; track eval-lslow
kubectl -n "$NST" exec eval-lslow -- nmap -sT -Pn -T3 --scan-delay 2000ms -p1-50 "$T0" >/dev/null 2>&1 &
PID=$!; SLOW=MISSED
while kill -0 "$PID" 2>/dev/null; do
  kubectl -n "$NSM" logs "$EBPF_POD" -c pipeline --since=140s 2>/dev/null \
    | grep -q "\"type\":\"port_scan\",\"source\":\"$IP\"" && { SLOW=DETECTED; kill "$PID" 2>/dev/null; break; }
  sleep 5
done
wait "$PID" 2>/dev/null
record "L-slow slow-scan-evasion|limit-FN|port_scan|$SLOW"

# L-chatty: a *benign* client making 60 connects to a single port (busy conn
# pool / load test) → FALSE-POSITIVE. Documents that detection is connection-
# count, not distinct-port, based (→ key the CMS on distinct dst ports).
say "L-chatty high-volume benign (60 connects to one port → expected FP)"
IP="$(mkpod eval-lchatty)"; track eval-lchatty
benign_connects eval-lchatty 60
detected "$IP" port_scan && record "L-chatty busy-benign-60conn|limit-FP|(none)|FALSE-POSITIVE" \
                         || record "L-chatty busy-benign-60conn|limit-FP|(none)|OK-no-alert"

# ════════════════════ Collector userspace footprint ════════════════════════
top_collector() {
  local c=0 m=0 k cpu mem
  for k in 1 2 3; do
    read -r cpu mem < <(kubectl -n "$NSM" top pod "$EBPF_POD" --containers 2>/dev/null \
      | awk '$2=="collector"{gsub(/m/,"",$3);gsub(/Mi/,"",$4);print $3" "$4}')
    [ -n "${cpu:-}" ] && c=$((c+cpu)) && m=$((m+mem)); sleep 2
  done
  echo "CPU=$((c/3))m MEM=$((m/3))Mi"
}
say "collector footprint: idle"
OVER_IDLE="$(top_collector)"
say "collector footprint: under sustained scan load"
IP="$(mkpod eval-load)"; track eval-load
kubectl -n "$NST" exec eval-load -- sh -c \
  "for r in \$(seq 1 6); do nmap -sT -Pn -T5 -p1-1000 $T0 >/dev/null 2>&1; done" >/dev/null 2>&1 &
LPID=$!; sleep 4
OVER_LOAD="$(top_collector)"
wait "$LPID" 2>/dev/null

# ════════════════════ Reports (md + csv) ════════════════════════════════════
det=$(printf '%s\n' "${ROWS[@]}" | awk -F'|' '$2=="attack" && $4=="DETECTED"' | wc -l | tr -d ' ')
atk=$(printf '%s\n' "${ROWS[@]}" | awk -F'|' '$2=="attack"' | wc -l | tr -d ' ')
fp=$(printf  '%s\n' "${ROWS[@]}" | awk -F'|' '$2=="benign" && $4=="FALSE-POSITIVE"' | wc -l | tr -d ' ')
ben=$(printf '%s\n' "${ROWS[@]}" | awk -F'|' '$2=="benign"' | wc -l | tr -d ' ')

{ echo "scenario,class,expected_alert,result"
  for r in "${ROWS[@]}"; do echo "$r" | awk -F'|' '{print $1","$2","$3","$4}'; done; } > "$CSV"

{
  echo "# eBPF detect→prevent evaluation — $STAMP"
  echo
  echo "- Node: \`$NODE\`  detector pod: \`$EBPF_POD\`"
  echo "- Scan targets: \`${TARGETS[*]}\`  benign web target: \`$WEB:80\`"
  echo "- Detector rule: **port_scan ⟺ > $THRESHOLD connections / source / 60 s tumbling window** (CMS on src_ip)."
  echo "- Collector userspace footprint (1 node): idle **$OVER_IDLE**, under load **$OVER_LOAD**"
  echo "  (\`kubectl top\` resolution is 1 mCPU; precise kernel per-connect cost → \`overhead-ebpf.sh\`)."
  echo
  echo "| Scenario | Class | Expected alert | Result |"
  echo "|---|---|---|---|"
  for r in "${ROWS[@]}"; do echo "$r" | awk -F'|' '{printf "| %s | %s | %s | %s |\n",$1,$2,$3,$4}'; done
  echo
  echo "**Detection rate (true scans):** $det/$atk attack scenarios detected."
  echo
  echo "**False-positive rate (realistic benign):** $fp/$ben → FPR = $fp/$ben."
  echo
  echo "> Two rows are labelled limitations, excluded from the headline metrics:"
  echo "> \`L-slow\` (limit-FN) — a slow scan kept under 40 conn/window evades the"
  echo "> tumbling window (→ sliding-window CMS). \`L-chatty\` (limit-FP) — a benign"
  echo "> client exceeding 40 conn/window is flagged because detection counts"
  echo "> connection volume, not distinct ports (→ key the CMS on distinct dst ports)."
} > "$OUT"

say "cleaning up eval pods"
kubectl -n "$NST" delete pod "${CREATED[@]}" --grace-period=0 --force >/dev/null 2>&1
echo; cat "$OUT"
say "report: $OUT"; say "csv: $CSV"
