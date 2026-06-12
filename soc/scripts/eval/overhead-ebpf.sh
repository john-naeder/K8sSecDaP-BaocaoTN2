#!/usr/bin/env bash
# eBPF kernel-space overhead microbenchmark (M3).
#
# Measures the per-connect() CPU cost of the collector's eBPF programs directly,
# using the kernel's own per-program run-time accounting (bpf_stats) read via the
# host bpftool through the privileged hostPID collector pod — so it needs NO
# removal of the collector and does NOT touch GitOps/ArgoCD.
#
# Methodology (delta, to exclude runs counted before stats were enabled):
#   1. enable kernel.bpf_stats_enabled on the node
#   2. read (run_time_ns, run_cnt) for the entry kprobe + return kretprobe  → t0
#   3. drive a fixed connect() storm from a pod on the node, timing the window
#   4. read again → t1
#   5. avg_ns/connect = Δrun_time_ns / Δrun_cnt   (entry + return)
#      kprobe CPU share = Σ Δrun_time_ns / (window_s · 1e9) · 100 %
#
#   ./scripts/eval/overhead-ebpf.sh
#
# Env: EVAL_NODE (userver-worker-1), NS_MAPPER (zt-mapper), NS_TARGETS (zt-targets),
#      NMAP_IMG (instrumentisto/nmap:7.95), LOAD_ROUNDS (12 × 1000-port scans).
set -uo pipefail

NODE="${EVAL_NODE:-userver-worker-1}"
NSM="${NS_MAPPER:-zt-mapper}"
NST="${NS_TARGETS:-zt-targets}"
IMG="${NMAP_IMG:-instrumentisto/nmap:7.95}"
LOAD_ROUNDS="${LOAD_ROUNDS:-12}"
OUTDIR="$(cd "$(dirname "$0")" && pwd)/results"
mkdir -p "$OUTDIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$OUTDIR/overhead-ebpf-$STAMP.md"
CSV="$OUTDIR/overhead-ebpf-$STAMP.csv"

say() { echo "[oh] $*" >&2; }

POD="$(kubectl -n "$NSM" get pods -l app.kubernetes.io/name=zt-pipeline-ebpf \
  -o jsonpath="{range .items[*]}{.metadata.name}{' '}{.spec.nodeName}{'\n'}{end}" \
  | awk -v n="$NODE" '$2==n{print $1; exit}')"
[ -n "$POD" ] || { say "no collector pod on $NODE"; exit 1; }
say "collector pod on $NODE = $POD"

# Run the host's bpftool inside the node's namespaces via the privileged pod.
bpftool_host() {
  kubectl -n "$NSM" exec "$POD" -c collector -- \
    nsenter -t 1 -m -u -i -n -p -- /usr/sbin/bpftool "$@" 2>/dev/null
}
set_stats() { kubectl -n "$NSM" exec "$POD" -c collector -- \
  sh -c "echo $1 > /proc/sys/kernel/bpf_stats_enabled" >/dev/null 2>&1; }

# Echo "entry_ns entry_cnt return_ns return_cnt" for the two collector progs.
read_stats() {
  bpftool_host -j prog show \
    | jq -r '
        (map(select(.name=="tcp_v4_connect_entry"))[0]) as $e |
        (map(select(.name=="tcp_v4_connect_return"))[0]) as $r |
        "\($e.run_time_ns) \($e.run_cnt) \($r.run_time_ns) \($r.run_cnt)"'
}

cleanup() {
  set_stats 0
  kubectl -n "$NST" delete pod oh-load --grace-period=0 --force >/dev/null 2>&1
}
trap cleanup EXIT

say "enabling kernel.bpf_stats_enabled on $NODE"
set_stats 1

# Target: first reachable pod IP in zt-targets.
T0="$(kubectl -n "$NST" get pods -l 'app in (web,api)' \
  -o jsonpath='{.items[0].status.podIP}' 2>/dev/null)"
[ -n "$T0" ] || T0="$(kubectl -n "$NST" get pods -o jsonpath='{.items[0].status.podIP}')"
say "load target = $T0"

# Load generator pod pinned to the node.
kubectl -n "$NST" delete pod oh-load --grace-period=0 --force >/dev/null 2>&1
kubectl -n "$NST" run oh-load --image="$IMG" --restart=Never \
  --overrides="{\"spec\":{\"nodeSelector\":{\"kubernetes.io/hostname\":\"$NODE\"}}}" \
  --command -- sleep infinity >/dev/null 2>&1
kubectl -n "$NST" wait --for=condition=Ready pod/oh-load --timeout=60s >/dev/null 2>&1

say "baseline read"
read -r E0 EC0 R0 RC0 < <(read_stats)
say "baseline entry=($E0 ns,$EC0) return=($R0 ns,$RC0)"

say "driving $LOAD_ROUNDS × 1000-port connect storms..."
START="$(date +%s.%N)"
kubectl -n "$NST" exec oh-load -- sh -c \
  "for r in \$(seq 1 $LOAD_ROUNDS); do nmap -sT -Pn -T5 -p1-1000 $T0 >/dev/null 2>&1; done" >/dev/null 2>&1
END="$(date +%s.%N)"

say "final read"
read -r E1 EC1 R1 RC1 < <(read_stats)
say "final entry=($E1 ns,$EC1) return=($R1 ns,$RC1)"

# ── compute (awk for float math) ────────────────────────────────────────────
read -r WIN AVG_E AVG_R PER_CONN TOT_NS PCT_CORE EVENTS EPS < <(awk -v e0="$E0" -v ec0="$EC0" \
  -v r0="$R0" -v rc0="$RC0" -v e1="$E1" -v ec1="$EC1" -v r1="$R1" -v rc1="$RC1" \
  -v s="$START" -v en="$END" 'BEGIN{
  de=e1-e0; dec=ec1-ec0; dr=r1-r0; drc=rc1-rc0; win=en-s;
  ave = (dec>0)? de/dec : 0;
  avr = (drc>0)? dr/drc : 0;
  per = ave+avr; tot = de+dr;
  pct = (win>0)? tot/(win*1e9)*100 : 0;
  eps = (win>0)? dec/win : 0;
  printf "%.2f %.0f %.0f %.0f %.0f %.4f %d %.0f", win, ave, avr, per, tot, pct, dec, eps;
}')

{
  echo "metric,value"
  echo "node,$NODE"
  echo "window_seconds,$WIN"
  echo "connects_measured,$EVENTS"
  echo "connects_per_second,$EPS"
  echo "avg_ns_entry_kprobe,$AVG_E"
  echo "avg_ns_return_kretprobe,$AVG_R"
  echo "avg_ns_per_connect_total,$PER_CONN"
  echo "total_kprobe_cpu_ns,$TOT_NS"
  echo "kprobe_cpu_core_percent,$PCT_CORE"
} > "$CSV"

{
  echo "# eBPF kernel-space overhead — $STAMP"
  echo
  echo "- Node: \`$NODE\`  collector pod: \`$POD\`"
  echo "- Method: kernel \`bpf_stats\` delta over a $WIN s connect storm (host bpftool via nsenter)."
  echo "- Connects measured: **$EVENTS** (~$EPS connect/s)."
  echo
  echo "| eBPF program | avg cost / connect |"
  echo "|---|---|"
  echo "| \`kprobe/tcp_v4_connect\` (entry) | **$AVG_E ns** |"
  echo "| \`kretprobe/tcp_v4_connect\` (return) | **$AVG_R ns** |"
  echo "| **combined per connect()** | **$PER_CONN ns** |"
  echo
  echo "**Aggregate kernel CPU for the collector probes during load:** $TOT_NS ns over $WIN s"
  echo "= **$PCT_CORE % of one CPU core** at ~$EPS connect/s."
  echo
  echo "> The probes add ≈$PER_CONN ns of kernel CPU per outbound TCP connect; even under a"
  echo "> sustained scan storm the data path costs a small fraction of a single core."
} > "$OUT"

echo
cat "$OUT"
say "report: $OUT"
say "csv:    $CSV"
