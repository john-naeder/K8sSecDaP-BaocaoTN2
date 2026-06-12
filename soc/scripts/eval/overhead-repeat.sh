#!/usr/bin/env bash
# Repeated eBPF kernel-space overhead measurement → mean ± 95% CI (M3 polish).
#
# Runs N back-to-back measurement cycles of the per-connect() cost of the
# collector's eBPF programs (bpf_stats delta via host bpftool / nsenter), then
# aggregates with a Student-t 95% confidence interval. Emits:
#   results/overhead-samples-<stamp>.csv   one row per cycle
#   results/overhead-agg-<stamp>.csv       mean/std/CI per metric
#   results/overhead-stats-<stamp>.md      human summary
# and report-ready LaTeX into the report repo (if present):
#   K8sSecDaP-report/shared/figures/overhead-ebpf.tex   pgfplots bar + error bars
#
#   ./scripts/eval/overhead-repeat.sh           # N=10 cycles
#   N=15 ./scripts/eval/overhead-repeat.sh
#
# Env: EVAL_NODE, NS_MAPPER, NS_TARGETS, NMAP_IMG, N (cycles), ROUNDS (scans/cycle).
set -uo pipefail

NODE="${EVAL_NODE:-userver-worker-1}"
NSM="${NS_MAPPER:-zt-mapper}"
NST="${NS_TARGETS:-zt-targets}"
IMG="${NMAP_IMG:-instrumentisto/nmap:7.95}"
N="${N:-10}"
ROUNDS="${ROUNDS:-3}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUTDIR="$HERE/results"; mkdir -p "$OUTDIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
SAMPLES="$OUTDIR/overhead-samples-$STAMP.csv"
AGG="$OUTDIR/overhead-agg-$STAMP.csv"
MD="$OUTDIR/overhead-stats-$STAMP.md"
FIGDIR="$(cd "$HERE/../../../K8sSecDaP-report/shared/figures" 2>/dev/null && pwd || true)"

say() { echo "[ohr] $*" >&2; }

POD="$(kubectl -n "$NSM" get pods -l app.kubernetes.io/name=zt-pipeline-ebpf \
  -o jsonpath="{range .items[*]}{.metadata.name}{' '}{.spec.nodeName}{'\n'}{end}" \
  | awk -v n="$NODE" '$2==n{print $1; exit}')"
[ -n "$POD" ] || { say "no collector pod on $NODE"; exit 1; }
say "collector pod on $NODE = $POD"

bpftool_host() { kubectl -n "$NSM" exec "$POD" -c collector -- \
  nsenter -t 1 -m -u -i -n -p -- /usr/sbin/bpftool "$@" 2>/dev/null; }
set_stats() { kubectl -n "$NSM" exec "$POD" -c collector -- \
  sh -c "echo $1 > /proc/sys/kernel/bpf_stats_enabled" >/dev/null 2>&1; }
# Read one prog's (run_time_ns, run_cnt) by id — small JSON, never truncates.
prog_field() { bpftool_host -j prog show id "$1" | jq -r '"\(.run_time_ns) \(.run_cnt)"'; }
read_stats() {  # echoes "entry_ns entry_cnt return_ns return_cnt"
  echo "$(prog_field "$ENTRY_ID") $(prog_field "$RETURN_ID")"
}
cleanup() { set_stats 0; kubectl -n "$NST" delete pod ohr-load --grace-period=0 --force >/dev/null 2>&1; }
trap cleanup EXIT

set_stats 1
# Resolve the two prog ids once via plain-text listing (2 lines, robust).
read -r ENTRY_ID RETURN_ID < <(bpftool_host prog show \
  | awk '/name tcp_v4_connect_entry/{e=$1} /name tcp_v4_connect_return/{r=$1} END{gsub(/:/,"",e);gsub(/:/,"",r);print e, r}')
[ -n "$ENTRY_ID" ] && [ -n "$RETURN_ID" ] || { say "could not resolve prog ids"; exit 1; }
say "prog ids: entry=$ENTRY_ID return=$RETURN_ID"
T0="$(kubectl -n "$NST" get pods -l 'app in (web,api)' -o jsonpath='{.items[0].status.podIP}' 2>/dev/null)"
[ -n "$T0" ] || T0="$(kubectl -n "$NST" get pods -o jsonpath='{.items[0].status.podIP}')"
kubectl -n "$NST" delete pod ohr-load --grace-period=0 --force >/dev/null 2>&1
kubectl -n "$NST" run ohr-load --image="$IMG" --restart=Never \
  --overrides="{\"spec\":{\"nodeSelector\":{\"kubernetes.io/hostname\":\"$NODE\"}}}" \
  --command -- sleep infinity >/dev/null 2>&1
kubectl -n "$NST" wait --for=condition=Ready pod/ohr-load --timeout=60s >/dev/null 2>&1
say "load target=$T0  cycles=$N  rounds/cycle=$ROUNDS"

echo "cycle,window_s,connects,ns_entry,ns_return,ns_per_connect" > "$SAMPLES"
for k in $(seq 1 "$N"); do
  read -r E0 EC0 R0 RC0 < <(read_stats)
  S="$(date +%s.%N)"
  kubectl -n "$NST" exec ohr-load -- sh -c \
    "for r in \$(seq 1 $ROUNDS); do nmap -sT -Pn -T5 -p1-1000 $T0 >/dev/null 2>&1; done" >/dev/null 2>&1
  EN="$(date +%s.%N)"
  read -r E1 EC1 R1 RC1 < <(read_stats)
  # Skip a cycle whose reads were incomplete (no new connects → bad sample).
  if [ -z "${E1:-}" ] || [ -z "${EC1:-}" ] || [ "$EC1" -le "$EC0" ] 2>/dev/null; then
    say "cycle $k skipped (incomplete read)"; continue
  fi
  awk -v k="$k" -v e0="$E0" -v ec0="$EC0" -v r0="$R0" \
      -v e1="$E1" -v ec1="$EC1" -v r1="$R1" -v s="$S" -v en="$EN" 'BEGIN{
    dec=ec1-ec0; de=e1-e0; dr=r1-r0; win=en-s;
    ave=(dec>0)?de/dec:0; avr=(dec>0)?dr/dec:0; per=ave+avr;
    printf "%d,%.2f,%d,%.1f,%.1f,%.1f\n",k,win,dec,ave,avr,per;
  }' >> "$SAMPLES"
  say "cycle $k done ($(tail -1 "$SAMPLES"))"
done

# ── aggregate (mean ± 95% CI, Student-t) ────────────────────────────────────
python3 - "$SAMPLES" "$AGG" "$MD" "${FIGDIR:-}" "$N" <<'PY'
import csv, sys, math, statistics as st
samples, agg, md, figdir, n = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
# two-sided t critical, alpha=0.05, by degrees of freedom (n-1)
TCRIT={1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,
       10:2.228,11:2.201,12:2.179,13:2.160,14:2.145,15:2.131,16:2.120,17:2.110,
       18:2.101,19:2.093,20:2.086,24:2.064,29:2.045}
def tcrit(df):
    if df in TCRIT: return TCRIT[df]
    keys=sorted(TCRIT);
    for kk in keys:
        if df<=kk: return TCRIT[kk]
    return 1.96
rows=list(csv.DictReader(open(samples)))
cols=["ns_entry","ns_return","ns_per_connect","connects"]
df=len(rows)-1; t=tcrit(df)
out=[("metric","mean","std","ci95_halfwidth","ci_lo","ci_hi","n")]
res={}
for c in cols:
    xs=[float(r[c]) for r in rows]
    m=st.mean(xs); s=st.stdev(xs) if len(xs)>1 else 0.0
    hw=t*s/math.sqrt(len(xs)) if len(xs)>1 else 0.0
    out.append((c,f"{m:.2f}",f"{s:.2f}",f"{hw:.2f}",f"{m-hw:.2f}",f"{m+hw:.2f}",str(len(xs))))
    res[c]=(m,hw)
with open(agg,"w",newline="") as f: csv.writer(f).writerows(out)
with open(md,"w") as f:
    f.write(f"# eBPF per-connect overhead — {n} cycles, mean ± 95% CI (Student-t, df={df})\n\n")
    f.write("| Metric | Mean | 95% CI |\n|---|---|---|\n")
    lbl={"ns_entry":"entry kprobe (ns/connect)","ns_return":"return kretprobe (ns/connect)",
         "ns_per_connect":"combined (ns/connect)",
         "connects":"connects measured / cycle"}
    for c in cols:
        m,hw=res[c]; f.write(f"| {lbl[c]} | {m:.1f} | ±{hw:.1f} |\n")
# report-ready pgfplots figure (entry/return/combined with 95% CI error bars)
if figdir:
    e=res["ns_entry"]; r=res["ns_return"]; cmb=res["ns_per_connect"]
    fig=figdir+"/overhead-ebpf.tex"
    with open(fig,"w") as f:
        f.write("%% Auto-generated by scripts/eval/overhead-repeat.sh -- eBPF per-connect overhead.\n")
        f.write("%% Chi phi CPU kernel cho moi connect() (mean +/- 95%% CI, n=%d).\n"%n)
        f.write("\\begin{tikzpicture}\n\\begin{axis}[\n")
        f.write("    ybar, bar width=18pt, width=0.85\\linewidth, height=6cm,\n")
        f.write("    ylabel={ns / connect()}, ymin=0,\n")
        f.write("    symbolic x coords={entry kprobe, return kretprobe, combined},\n")
        f.write("    xtick=data, x tick label style={font=\\small},\n")
        f.write("    nodes near coords, every node near coord/.append style={font=\\footnotesize},\n")
        f.write("    error bars/y dir=both, error bars/y explicit,\n")
        f.write("    enlarge x limits=0.25,\n]\n")
        f.write("\\addplot+[fill=blue!25] coordinates {\n")
        f.write("    (entry kprobe,%.0f) +- (0,%.0f)\n"%(e[0],e[1]))
        f.write("    (return kretprobe,%.0f) +- (0,%.0f)\n"%(r[0],r[1]))
        f.write("    (combined,%.0f) +- (0,%.0f)\n"%(cmb[0],cmb[1]))
        f.write("};\n\\end{axis}\n\\end{tikzpicture}\n")
    print("FIG "+fig)
print("AGG "+agg); print("MD "+md)
PY

echo; cat "$MD"
say "samples: $SAMPLES"; say "agg: $AGG"
