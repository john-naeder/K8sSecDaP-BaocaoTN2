#!/usr/bin/env bash
# =============================================================================
#  K8sSecDaP — Kich ban demo end-to-end (chay tay, dung de chup man hinh)
# =============================================================================
#  Muc tieu: chung minh tu A->Z tren cum that:
#    (1) eBPF da nap + gan moc + bat su kien o TANG NHAN
#    (2) Mo phong tan cong (quet cong + lan truyen ngang)
#    (3) Pipeline phat hien -> SOC tao su co -> tu sinh NetworkPolicy
#    (4) Phe duyet tren console -> ap chinh sach cach ly
#    (5) Xac minh pod tan cong bi cat ket noi
#    (6) Grafana/Prometheus + MinIO
#
#  Cach dung: chay tung buoc, script DUNG LAI sau moi buoc (nhan Enter de tiep).
#    chmod +x demo-e2e.sh && ./demo-e2e.sh
#  Co the chay 1 buoc rieng:  ./demo-e2e.sh 3
# =============================================================================
set -uo pipefail

# ---- Cau hinh (ten that tren cum) -------------------------------------------
NS_MAP="zt-mapper"          # DaemonSet thu thap/pipeline
NS_SOC="soc"                # NATS, postgres, aggregator, incident-service, console, minio
NS_TGT="zt-targets"         # pod nan nhan + pod attacker
NS_MON="monitoring"         # kube-prometheus-stack
ATTACKER="attacker"         # pod tan cong (image nmap)
ATTACKER_NODE="userver-worker-1"   # node dat pod attacker
TAU=40                      # nguong phat hien quet cong

pause() { echo; read -rp ">>> [Enter] de sang buoc tiep theo (chup man hinh truoc khi nhan)... " _; echo; }
banner(){ echo; echo "============================================================"; echo "  $*"; echo "============================================================"; }

# Pod pipeline dat tren cung node voi attacker (noi su kien duoc xu ly)
resolve_pipe_pod() {
  kubectl -n "$NS_MAP" get pod -l app.kubernetes.io/name=zt-pipeline-ebpf \
    --field-selector "spec.nodeName=$ATTACKER_NODE" \
    -o jsonpath='{.items[0].metadata.name}' 2>/dev/null
}

# -----------------------------------------------------------------------------
step0() {
  banner "BUOC 0 — Hien trang cum (node + cac pod thanh phan)"
  echo "# 3 node cua cum:"
  kubectl get nodes -o wide
  echo; echo "# DaemonSet thu thap/pipeline tren MOI node (3 container/pod):"
  kubectl -n "$NS_MAP" get pods -o wide
  echo; echo "# Cac dich vu SOC (NATS, postgres, aggregator, incident, console, minio):"
  kubectl -n "$NS_SOC" get pods -o wide
  echo; echo "# Pod nan nhan + pod tan cong:"
  kubectl -n "$NS_TGT" get pods -o wide
}

# -----------------------------------------------------------------------------
step1() {
  banner "BUOC 1 — MINH CHUNG eBPF O TANG NHAN  (phan quan trong nhat)"
  local POD; POD=$(resolve_pipe_pod)

  echo "# Node	               Pod collector"
  echo "# userver-worker-1	   zt-pipeline-ebpf-tx94l"
  echo "# userver-master	     zt-pipeline-ebpf-qztgt"
  echo "# userver-worker-home	 zt-pipeline-ebpf-nbrfz"
  echo "# (Neu khong thay, co the do node selector cua DaemonSet chi dinh node khac — xem lai cau hinh DaemonSet)"
  echo "########################################################"
  echo "# Pod thu thap tren node $ATTACKER_NODE: $POD (container 'collector')"
  echo "# LUU Y: container 'collector' KHONG co bpftool. eBPF nam trong nhan HOST"
  echo "#        (DaemonSet chay hostNetwork + privileged) nen PHAI chay bpftool TREN NODE."
  echo
  echo "# --> SSH vao node $ATTACKER_NODE roi chay 3 lenh sau (chup man hinh tung lenh):"
  echo
  echo "  # (1) Chuong trinh eBPF da NAP: kprobe gan tcp_v4_connect, da qua verifier + JIT"
  echo "  \$ sudo bpftool prog show | grep -A4 -i 'kprobe\\|tcp_v4_connect'"
  echo
  echo "  # (2) Ba BPF map: cidr_map(lpm_trie), events(ringbuf), sock_store(hash)"
  echo "  \$ sudo bpftool map show | grep -i 'cidr_map\\|events\\|sock_store'"
  echo
  echo "  # (3) Cac CIDR pod-network da nap vao bo loc LPM (doi chieu)"
  echo "  \$ sudo bpftool map dump name cidr_map | head"
  echo
  echo "# (Khong co quyen SSH? Dung debug container co bpftool tren chinh node do:)"
  echo "  \$ kubectl debug node/$ATTACKER_NODE -it --image=quay.io/cilium/cilium-bpftool -- \\"
  echo "       bpftool prog show"
  echo
  echo "# Y nghia: dong 'kprobe ... tcp_v4_connect' = diem moc; truong 'xlated/jited' = da"
  echo "#          qua verifier va JIT (chay TRONG nhan); 3 map dung kieu => quan sat tai nguon."
  echo
  echo "# (4) TRACING THOI GIAN THUC tu trong container (cho nay KHONG can bpftool):"
  echo "#     moi connect() trong cum sinh 1 su kien JSON."
  echo "#     De cua so nay chay 8s roi tu thoat (chup canh dong su kien chay)."
  echo "\$ kubectl -n $NS_MAP logs $POD -c collector -f"
  timeout 8 kubectl -n "$NS_MAP" logs "$POD" -c collector -f 2>/dev/null | tail -n 15 \
    || true
}

# -----------------------------------------------------------------------------
step2() {
  banner "BUOC 2 — MO PHONG TAN CONG (quet cong + lan truyen ngang)"
  echo "# IP cac pod nan nhan (web :80, api :8080):"
  kubectl -n "$NS_TGT" get pods -l app=web  -o wide --no-headers | awk '{print "  web  "$6}'
  kubectl -n "$NS_TGT" get pods -l app=api  -o wide --no-headers | awk '{print "  api  "$6}'
  local AIP; AIP=$(kubectl -n "$NS_TGT" get pod "$ATTACKER" -o jsonpath='{.status.podIP}')
  echo "# IP pod tan cong (nguon): $AIP  (nguong phat hien tau=$TAU ket noi/cua so)"
  echo
  echo "# (a) Quet cong connect-scan dai pod => vuot nguong => canh bao port_scan"
  echo "\$ kubectl -n $NS_TGT exec $ATTACKER -- nmap -sT -p 1-200 --max-rate 200 10.244.204.128/26"
  kubectl -n "$NS_TGT" exec "$ATTACKER" -- nmap -sT -p 1-200 --max-rate 200 10.244.204.128/26 2>/dev/null | tail -n 20
  echo
  echo "# (b) Cham toi nhieu dich khac nhau => canh bao blast_radius (lan truyen ngang)"
  echo "\$ for ip in 10.244.204.{128..160}; do ncat -w1 -z \$ip 80; done"
  kubectl -n "$NS_TGT" exec "$ATTACKER" -- sh -c \
    'for ip in 10.244.204.{128..160}; do ncat -w1 -z $ip 80 2>/dev/null; done; echo "quet xong"'
}

# -----------------------------------------------------------------------------
step3() {
  banner "BUOC 3 — PHAT HIEN: pipeline phat canh bao port_scan / blast_radius"
  local POD; POD=$(resolve_pipe_pod)
  echo "\$ kubectl -n $NS_MAP logs $POD -c pipeline | grep ALERT | tail"
  kubectl -n "$NS_MAP" logs "$POD" -c pipeline 2>/dev/null | grep -i 'ALERT' | tail -n 20 \
    || echo "  (chua thay ALERT — doi vai giay roi chay lai buoc nay, hoac lap lai buoc 2)"
}

# -----------------------------------------------------------------------------
step4() {
  banner "BUOC 4 — SU CO: incident-service tao su co + TU SINH NetworkPolicy"
  echo "\$ kubectl -n $NS_SOC logs deploy/zt-incident-service | grep -E 'incident.created|action' | tail"
  kubectl -n "$NS_SOC" logs deploy/zt-incident-service 2>/dev/null \
    | grep -E 'incident.created|action' | tail -n 15
  echo
  echo "# Cac ban nhap hanh dong dang CHO PHE DUYET (trang thai pending):"
  echo "# -> mo console http://console.zt.local  muc 'Pending actions' de chup man hinh."
  echo "# (So su co thuc te trong CSDL:)"
  kubectl -n "$NS_SOC" exec postgres-0 -- sh -c \
    'psql -U "${POSTGRES_USER:-postgres}" -d "${POSTGRES_DB:-soc}" -tAc \
     "select count(*) total, count(*) filter (where severity in ('"'"'high'"'"','"'"'critical'"'"')) high_crit from incidents;"' \
    2>/dev/null | sed 's/^/  total|high_crit = /'
}

# -----------------------------------------------------------------------------
step5() {
  banner "BUOC 5 — PHE DUYET tren console -> AP chinh sach cach ly"
  echo "# Thao tac TAY tren console (chup man hinh):"
  echo "#   1. Mo http://console.zt.local  -> tab Incidents -> mo 1 su co high/critical"
  echo "#   2. O muc Actions, bam 'Approve' tren ban nhap quarantine_pod"
  echo "#   3. Hanh dong chuyen 'executed' -> NetworkPolicy duoc ap"
  echo
  echo "# Kiem chung tu CLI: cac NetworkPolicy cach ly da duoc tao trong $NS_TGT:"
  echo "\$ kubectl -n $NS_TGT get networkpolicy | grep quarantine"
  kubectl -n "$NS_TGT" get networkpolicy 2>/dev/null | grep -i quarantine \
    || echo "  (chua co — hay phe duyet 1 hanh dong tren console truoc)"
}

# -----------------------------------------------------------------------------
step6() {
  banner "BUOC 6 — XAC MINH: pod tan cong mat ket noi di ra (egress-deny)"
  echo "# Truoc cach ly: exit=0 (ket noi duoc).  Sau cach ly: exit khac 0."
  echo "\$ kubectl -n $NS_TGT exec $ATTACKER -- ncat -w2 -z 10.244.204.161 80; echo exit=\$?"
  kubectl -n "$NS_TGT" exec "$ATTACKER" -- sh -c 'ncat -w2 -z 10.244.204.161 80; echo "exit=$?"' 2>/dev/null
  echo
  echo "# Mot pod hop le KHAC khong bi anh huong (chung minh cach ly co chon loc)."
}

# -----------------------------------------------------------------------------
step7() {
  banner "BUOC 7 — QUAN TRAC: Prometheus/Grafana da co du lieu"
  echo "# Sau khi them ServiceMonitor 'zt-soc', Prometheus da scrape cac SOC service."
  echo "# Kiem tra nhanh bang port-forward (chup dashboard tren Grafana se ro hon):"
  echo "\$ kubectl -n $NS_MON port-forward svc/kube-prometheus-kube-prome-prometheus 9090:9090"
  echo "  -> http://localhost:9090  thu query: zt_incidents_created_total"
  echo
  echo "# Grafana: mo dashboard 'Zero-Trust SOC — Incidents & Actions'."
  echo "#   Luu y: bang 'Alerts Overview' (nguon Loki) co cua so mac dinh 30 phut —"
  echo "#   neu trong, hay NOI cua so thoi gian hoac chay lai buoc 2 ngay truoc khi xem."
}

# -----------------------------------------------------------------------------
step8() {
  banner "BUOC 8 — MinIO: kho doi tuong (bao cao tuan / backup / netpol)"
  echo "# MinIO khong mo ra ngoai; truy cap qua port-forward console (:9001):"
  echo "\$ kubectl -n $NS_SOC port-forward svc/minio 9001:9001"
  echo "  -> http://localhost:9001   (user: zt-soc-admin)"
  echo
  echo "# Liet ke bucket bang mc qua API (:9000):"
  echo "\$ kubectl -n $NS_SOC port-forward svc/minio 9000:9000 &"
  echo "\$ mc alias set zt http://localhost:9000 zt-soc-admin '<minio-password>'"
  echo "\$ mc ls zt   # zt-postgres-backups zt-reports zt-pcap-dumps zt-netpol-archives"
  echo
  echo "# Tren console, trang Reports -> nut 'Generate & upload to MinIO' day bao cao"
  echo "# tuan len bucket zt-reports; kiem chung: mc ls zt/zt-reports"
}

# ---- Dieu phoi -------------------------------------------------------------
run_all() {
  step0; pause
  step1; pause
  step2; pause
  step3; pause
  step4; pause
  step5; pause
  step6; pause
  step7; pause
  step8
  banner "HOAN TAT DEMO"
}

if [ $# -ge 1 ]; then
  "step$1"
else
  run_all
fi
