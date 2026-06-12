#!/usr/bin/env bash
# =============================================================================
#  K8sSecDaP — RUNBOOK tong hop moi lenh van hanh / kiem chung / demo
# =============================================================================
#  Tong hop tat ca lenh da dung: inventory cum, minh chung eBPF (ssh+bpftool),
#  chan doan + sua dashboard Grafana (ServiceMonitor), mo phong tan cong,
#  kiem chung phat hien, lam dashboard bien dong, truy cap MinIO.
#
#  Cach dung:
#    ./runbook.sh            # in menu
#    ./runbook.sh 1          # chay muc 1 (inventory)
#    ./runbook.sh 2 ...      # v.v.
#  Cac muc CHI doc (1,3-verify,5,7) an toan chay bat ky luc nao.
#  Cac muc CO TAC DONG: 3-apply (ServiceMonitor), 4/6 (tan cong) — can chu y.
# -----------------------------------------------------------------------------
set -uo pipefail

# ===================== CAU HINH (gia tri THAT tren cum) ======================
NS_MAP="zt-mapper"; NS_SOC="soc"; NS_TGT="zt-targets"; NS_MON="monitoring"
ATTACKER="attacker"
ATTACKER_NODE="userver-worker-1"
SSH_USER="johnnaeder"                 # user SSH tren node (xem prompt bpftool)
TAU=40                                # nguong phat hien quet cong
PROM_SVC="kube-prometheus-kube-prome-prometheus"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"   # K8sSecDaP-soc
SM_MANIFEST="$REPO_DIR/manifests/26-soc-servicemonitor.yaml"

# Topology (Tailscale/CGNAT IP):
#   userver-master       100.120.16.113   control-plane   pipeline pod: zt-pipeline-ebpf-qztgt
#   userver-worker-1     100.95.126.102   worker          pipeline pod: zt-pipeline-ebpf-tx94l   (attacker o day)
#   userver-worker-home  100.119.246.28   worker (chap chon vi VPN)  pipeline pod: zt-pipeline-ebpf-nbrfz
# Pod DaemonSet: 3 container/pod = collector | pipeline | alert-bridge
# Bo nho: container 'collector' KHONG co bpftool -> chay bpftool TREN NODE.

banner(){ echo; echo "############################################################"; echo "#  $*"; echo "############################################################"; }
pipe_pod(){ kubectl -n "$NS_MAP" get pod -l app.kubernetes.io/name=zt-pipeline-ebpf \
            --field-selector "spec.nodeName=$ATTACKER_NODE" -o jsonpath='{.items[0].metadata.name}' 2>/dev/null; }
psql_soc(){ kubectl -n "$NS_SOC" exec postgres-0 -- sh -c \
            "psql -U \"\${POSTGRES_USER:-postgres}\" -d \"\${POSTGRES_DB:-soc}\" -tAc \"$1\"" 2>/dev/null; }

# =============================================================================
m1_inventory() {
  banner "1) INVENTORY CUM (node / pods / container)"
  echo "\$ kubectl config current-context"; kubectl config current-context
  echo; echo "\$ kubectl get nodes -o wide"; kubectl get nodes -o wide
  echo; echo "\$ kubectl get ns"; kubectl get ns
  echo; echo "\$ kubectl -n $NS_MAP get pods -o wide   # collector/pipeline/alert-bridge"
  kubectl -n "$NS_MAP" get pods -o wide
  echo; echo "\$ kubectl -n $NS_SOC get pods -o wide   # nats/postgres/aggregator/incident/console/minio"
  kubectl -n "$NS_SOC" get pods -o wide
  echo; echo "\$ kubectl -n $NS_TGT get pods -o wide   # nan nhan (web/api) + attacker"
  kubectl -n "$NS_TGT" get pods -o wide
  echo; echo "\$ container trong 1 pod pipeline:"
  kubectl -n "$NS_MAP" get pod "$(pipe_pod)" -o jsonpath='{range .spec.containers[*]}{.name}{" -> "}{.image}{"\n"}{end}'
  echo; echo "# Vao container collector (KHONG ssh duoc vao pod):"
  echo "  kubectl exec -it -n $NS_MAP $(pipe_pod) -c collector -- bash"
}

# =============================================================================
m2_ebpf() {
  banner "2) MINH CHUNG eBPF O TANG NHAN (ssh node + bpftool)"
  local NODE="$ATTACKER_NODE"
  echo "# bpftool KHONG co trong container -> chay tren NODE qua SSH:"
  echo "\$ ssh $SSH_USER@$NODE"
  echo "  sudo bpftool prog show | grep -A4 -i 'kprobe\\|tcp_v4_connect'   # kprobe+kretprobe, da JIT, run_cnt"
  echo "  sudo bpftool map show  | grep -i 'cidr_map\\|events\\|sock_store' # lpm_trie / ringbuf / hash"
  echo "  sudo bpftool map dump name cidr_map | head                        # CIDR pod-network da nap"
  echo
  echo "# (Khong co SSH? debug container co bpftool tren node:)"
  echo "  kubectl debug node/$NODE -it --image=quay.io/cilium/cilium-bpftool -- bpftool prog show"
  echo
  echo "# Tracing thoi gian thuc (tu trong container, KHONG can bpftool):"
  echo "\$ kubectl -n $NS_MAP logs $(pipe_pod) -c collector -f   # moi connect() -> 1 su kien JSON"
  echo "# (chay thu 8s:)"
  timeout 8 kubectl -n "$NS_MAP" logs "$(pipe_pod)" -c collector -f 2>/dev/null | tail -n 8 || true
}

# =============================================================================
m3_grafana_diag() {
  banner "3a) CHAN DOAN: vi sao dashboard SOC trong ma trang Reports day"
  echo "# Reports doc PostgreSQL (ben vung); Grafana doc Loki(cua so 30') / Prometheus(counter)."
  echo "\$ kubectl get servicemonitor -A | grep -i soc   # truoc khi sua: KHONG co"
  kubectl get servicemonitor -A 2>/dev/null | grep -i 'soc\|zt-' || echo "  (chua co ServiceMonitor cho SOC)"
  echo; echo "\$ Prometheus serviceMonitorSelector (Operator chon theo nhan nay):"
  kubectl -n "$NS_MON" get prometheus -o jsonpath='{.items[0].spec.serviceMonitorSelector}{"\n"}' 2>/dev/null
  echo "\$ Service SOC + nhan:"
  kubectl -n "$NS_SOC" get svc zt-aggregator zt-incident-service \
    -o jsonpath='{range .items[*]}{.metadata.name}{" labels="}{.metadata.labels}{"\n"}{end}' 2>/dev/null
}

m3_grafana_fix() {
  banner "3b) SUA: them ServiceMonitor + nhan -> Prometheus scrape SOC  (CO TAC DONG)"
  echo "\$ kubectl -n $NS_SOC label svc zt-incident-service app.kubernetes.io/name=zt-incident-service --overwrite"
  kubectl -n "$NS_SOC" label svc zt-incident-service app.kubernetes.io/name=zt-incident-service --overwrite
  echo "\$ kubectl apply -f $SM_MANIFEST"
  kubectl apply -f "$SM_MANIFEST"
  echo; echo "# Verify (Operator reload ~30-60s): xem target + query 1 metric"
  ( kubectl -n "$NS_MON" port-forward svc/$PROM_SVC 9090:9090 >/tmp/pf-rb.log 2>&1 & echo $! >/tmp/pf-rb.pid )
  curl -s --retry 12 --retry-delay 5 --retry-all-errors --max-time 6 \
    'http://localhost:9090/api/v1/targets?state=active' 2>/dev/null \
    | tr ',' '\n' | grep -i 'zt-aggregator.soc\|zt-incident-service.soc' | grep scrapeUrl | head
  echo "  zt_incidents_created_total ="
  curl -s --max-time 6 'http://localhost:9090/api/v1/query?query=zt_incidents_created_total' 2>/dev/null | head -c 300; echo
  [ -f /tmp/pf-rb.pid ] && kill "$(cat /tmp/pf-rb.pid)" 2>/dev/null; rm -f /tmp/pf-rb.pid
}

# =============================================================================
m4_attack() {
  banner "4) MO PHONG TAN CONG tu pod attacker  (CO TAC DONG)"
  local AIP; AIP=$(kubectl -n "$NS_TGT" get pod "$ATTACKER" -o jsonpath='{.status.podIP}' 2>/dev/null)
  echo "# IP nguon (attacker): $AIP   nguong tau=$TAU"
  echo "\$ kubectl -n $NS_TGT exec $ATTACKER -- nmap -sT -p 1-200 --max-rate 200 10.244.204.128/26"
  kubectl -n "$NS_TGT" exec "$ATTACKER" -- nmap -sT -p 1-200 --max-rate 200 10.244.204.128/26 2>/dev/null | tail -n 12
  echo "\$ blast radius: cham nhieu dich"
  kubectl -n "$NS_TGT" exec "$ATTACKER" -- sh -c \
    'for ip in 10.244.204.{128..160}; do ncat -w1 -z $ip 80 2>/dev/null; done; echo "quet xong"'
}

# =============================================================================
m5_detect() {
  banner "5) KIEM CHUNG PHAT HIEN (alerts / incidents / DB / Prometheus)"
  echo "\$ Pipeline ALERT (node $ATTACKER_NODE):"
  kubectl -n "$NS_MAP" logs "$(pipe_pod)" -c pipeline --since=8m 2>/dev/null | grep -i 'ALERT' | tail -n 8
  echo; echo "\$ incident-service created/action:"
  kubectl -n "$NS_SOC" logs deploy/zt-incident-service --since=8m 2>/dev/null | grep -iE 'created|action' | tail -n 8
  echo; echo "\$ DB: tong incident + theo type:"
  psql_soc "select count(*) total, count(*) filter (where severity in ('high','critical')) high_crit from incidents;" | sed 's/^/  total|high_crit = /'
  psql_soc "select type,severity,count(*) from incidents group by type,severity order by count desc limit 8;"
  echo; echo "\$ DB: incident cua attacker (source_ip):"
  local AIP; AIP=$(kubectl -n "$NS_TGT" get pod "$ATTACKER" -o jsonpath='{.status.podIP}' 2>/dev/null)
  echo "  (attacker podIP = $AIP)"
  psql_soc "select id,type,severity,occurrence_count,status from incidents where source_ip='$AIP' order by id;"
  echo; echo "\$ Prometheus counters (quyet dinh dashboard):"
  ( kubectl -n "$NS_MON" port-forward svc/$PROM_SVC 9090:9090 >/tmp/pf-rb.log 2>&1 & echo $! >/tmp/pf-rb.pid )
  for q in zt_aggregator_alerts_received_total zt_aggregator_alerts_emitted_total \
           zt_aggregator_active_incidents zt_incidents_created_total zt_incidents_updated_total; do
    v=$(curl -s --retry 6 --retry-delay 3 --retry-all-errors --max-time 6 \
        "http://localhost:9090/api/v1/query?query=$q" 2>/dev/null \
        | python3 -c 'import sys,json;d=json.load(sys.stdin);r=d["data"]["result"];print(sum(float(x["value"][1]) for x in r) if r else "NO DATA")' 2>/dev/null)
    printf "  %-40s = %s\n" "$q" "$v"
  done
  [ -f /tmp/pf-rb.pid ] && kill "$(cat /tmp/pf-rb.pid)" 2>/dev/null; rm -f /tmp/pf-rb.pid
}

# =============================================================================
m6_dashboard_live() {
  banner "6) LAM DASHBOARD GRAFANA BIEN DONG (de chup man hinh)"
  echo "# Grafana: dat time range 'Last 15 minutes', auto-refresh 5s, roi:"
  echo
  echo "# (a) Quet LIEN TUC de gauge giu >0 du lau ma chup (Ctrl-C de dung):"
  echo "  kubectl -n $NS_TGT exec $ATTACKER -- \\"
  echo "    sh -c 'while true; do nmap -sT -p1-200 --max-rate 300 10.244.204.128/26 >/dev/null 2>&1; done'"
  echo
  echo "# (b) De 'Incidents Created' tang RO -> dung nguon MOI (incident_key moi):"
  echo "  kubectl -n $NS_TGT delete pod $ATTACKER --wait=false   # tao lai -> IP moi"
  echo "  # hoac quet tu 1 pod web:"
  echo "  kubectl -n $NS_TGT exec web-756df59f8-7vpnt -- \\"
  echo "    sh -c 'for ip in 10.244.204.{128..170}; do nc -w1 -z \$ip 80 2>/dev/null; done'"
  echo
  echo "# Panel chac chan nho khi dang quet: Alerts Received Rate / Alerts Emitted by Reason / Active Incidents"
}

# =============================================================================
m7_minio() {
  banner "7) TRUY CAP MinIO (kho doi tuong: bao cao tuan / backup / netpol)"
  echo "# Console (:9001) qua port-forward:"
  echo "  kubectl -n $NS_SOC port-forward svc/minio 9001:9001    # -> http://localhost:9001 (user: zt-soc-admin)"
  echo "# API (:9000) + mc:"
  echo "  kubectl -n $NS_SOC port-forward svc/minio 9000:9000 &"
  echo "  mc alias set zt http://localhost:9000 zt-soc-admin '<minio-password>'"
  echo "  mc ls zt        # zt-postgres-backups zt-reports zt-pcap-dumps zt-netpol-archives"
  echo "  mc ls zt/zt-reports   # bao cao tuan day tu nut 'Generate & upload to MinIO' tren console"
}

# =============================================================================
usage() {
  cat <<EOF
RUNBOOK K8sSecDaP — chay: ./runbook.sh <muc>
  1   Inventory cum (nodes / pods / container)
  2   Minh chung eBPF o tang nhan (ssh node + bpftool)        [in huong dan]
  3   Chan doan dashboard SOC (vi sao trong)                  [chi doc]
  3a  = 3 (alias chan doan)
  3f  SUA: them ServiceMonitor + nhan -> Prometheus scrape    [CO TAC DONG]
  4   Mo phong tan cong (nmap tu attacker)                    [CO TAC DONG]
  5   Kiem chung phat hien (alerts/incidents/DB/Prometheus)   [chi doc]
  6   Lam dashboard Grafana bien dong                         [in huong dan]
  7   Truy cap MinIO                                          [in huong dan]
EOF
}

case "${1:-}" in
  1)        m1_inventory ;;
  2)        m2_ebpf ;;
  3|3a)     m3_grafana_diag ;;
  3f)       m3_grafana_fix ;;
  4)        m4_attack ;;
  5)        m5_detect ;;
  6)        m6_dashboard_live ;;
  7)        m7_minio ;;
  *)        usage ;;
esac
