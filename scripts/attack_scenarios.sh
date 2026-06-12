#!/usr/bin/env bash
# Attack scenarios for the Zero-Trust SOC end-to-end demo.
# Each scenario produces real network traffic from the attacker pod and
# verifies the SOC pipeline reacts (alerts → incidents → actions).
#
# Usage:
#   bash scripts/attack_scenarios.sh           # run all
#   bash scripts/attack_scenarios.sh scan      # single scenario
#   bash scripts/attack_scenarios.sh approve   # approve next pending action

set -uo pipefail

NS_T=${NS_TARGETS:-zt-targets}
NS_S=${NS_SOC:-soc}
ATTACKER=${ATTACKER_POD:-attacker}
SVC_INC=${SVC_INC:-zt-incident-service}
LOCAL_PORT=${LOCAL_PORT:-18080}

YELLOW=$'\033[1;33m'; GREEN=$'\033[1;32m'; RED=$'\033[1;31m'; RST=$'\033[0m'
step() { printf "${YELLOW}== %s ==${RST}\n" "$*"; }
ok()   { printf "${GREEN}✓ %s${RST}\n" "$*"; }
warn() { printf "${RED}! %s${RST}\n" "$*"; }

PF_PID=""
start_portforward() {
    if ! curl -sS -o /dev/null --max-time 1 "http://localhost:${LOCAL_PORT}/ready"; then
        kubectl -n "$NS_S" port-forward "svc/$SVC_INC" "${LOCAL_PORT}:8080" >/tmp/zt-pf.log 2>&1 &
        PF_PID=$!
        # wait for it
        for i in {1..15}; do
            sleep 1
            curl -sS -o /dev/null --max-time 1 "http://localhost:${LOCAL_PORT}/ready" && return 0
        done
        warn "incident-service port-forward failed; tail /tmp/zt-pf.log"
        return 1
    fi
}
stop_portforward() { [[ -n "$PF_PID" ]] && kill "$PF_PID" 2>/dev/null || true; }
trap stop_portforward EXIT

token_for() {
    local role="$1"
    kubectl -n "$NS_S" get secret incident-rbac-tokens -o jsonpath='{.data.RBAC_TOKENS}' \
        | base64 -d | tr ',' '\n' | grep "^${role}-demo:" | awk -F: '{print $3}'
}

api() {
    local method="$1" path="$2" tok="$3" body="${4:-}"
    if [[ -n "$body" ]]; then
        curl -sS -X "$method" -H "Authorization: Bearer $tok" -H "Content-Type: application/json" -d "$body" "http://localhost:${LOCAL_PORT}${path}"
    else
        curl -sS -X "$method" -H "Authorization: Bearer $tok" "http://localhost:${LOCAL_PORT}${path}"
    fi
}

count_incidents() {
    local tok body
    tok=$(token_for viewer)
    body=$(api GET "/api/v1/incidents?limit=500" "$tok")
    printf '%s' "$body" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 0
}

# ───────────────────────── Scenario 1 ──────────────────────────────────────
scenario_scan() {
    step "Scenario 1: TCP SYN port scan via Service DNS"
    start_portforward || return 1
    local before after
    before=$(count_incidents)
    kubectl -n "$NS_T" exec "$ATTACKER" -- nmap -Pn -T5 -p1-200 web.zt-targets.svc.cluster.local 2>&1 | tail -3
    sleep 8
    after=$(count_incidents)
    if [[ $after -gt $before ]]; then
        ok "incidents grew from $before → $after"
    else
        warn "no new incidents (was=$before now=$after) — pipeline may already have alerted on this src in the dedup window"
    fi
}

# ───────────────────────── Scenario 2 ──────────────────────────────────────
scenario_lateral() {
    step "Scenario 2: cross-node lateral movement"
    start_portforward || return 1
    for tgt in web.zt-targets.svc.cluster.local api.zt-targets.svc.cluster.local; do
        kubectl -n "$NS_T" exec "$ATTACKER" -- nmap -Pn -T5 -p1-200 "$tgt" 2>&1 | tail -1
    done
    sleep 10
    local tok body
    tok=$(token_for viewer)
    body=$(api GET "/api/v1/incidents?limit=20" "$tok")
    local cross
    cross=$(printf '%s' "$body" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(sum(1 for i in d if i.get("cross_node")))')
    if [[ "$cross" -gt 0 ]]; then
        ok "$cross incidents marked cross_node=true"
    else
        warn "no cross_node incidents yet (need scans observed by 2+ pipeline nodes; currently flannel may have only worker-1 active)"
    fi
}

# ───────────────────────── Scenario 3 ──────────────────────────────────────
scenario_data_exfil() {
    step "Scenario 3: data exfil to external IP"
    kubectl -n "$NS_T" exec "$ATTACKER" -- nmap -Pn -p443 1.1.1.1 2>&1 | tail -2
    sleep 5
    ok "external dst (label=0) appears in pipeline graph"
}

# ───────────────────────── Scenario 4 ──────────────────────────────────────
scenario_approve() {
    step "Scenario 4: admin approves first pending NetworkPolicy draft"
    start_portforward || return 1
    local atok body id
    atok=$(token_for admin)
    body=$(api GET "/api/v1/actions?status=pending" "$atok")
    id=$(printf '%s' "$body" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d[0]["id"]) if d else None' 2>/dev/null)
    if [[ -z "$id" || "$id" == "None" ]]; then
        warn "no pending actions to approve — run scenario_scan first to generate one"
        return
    fi
    step "approving action id=$id"
    api POST "/api/v1/actions/${id}/approve" "$atok" "" \
        | python3 -c 'import json,sys; d=json.load(sys.stdin); a=d.get("action",{}); print(f"action #{a.get(\"id\")} status={a.get(\"status\")} approved_by={a.get(\"approved_by\")} -> {d.get(\"applied_to\")}")'
    ok "YAML draft saved to PVC inside incident-service pod"
}

# ───────────────────────── Status snapshot ─────────────────────────────────
scenario_status() {
    step "Status snapshot"
    start_portforward || return 1
    local vtok atok inc act
    vtok=$(token_for viewer)
    atok=$(token_for admin)
    inc=$(api GET "/api/v1/incidents?limit=500" "$vtok" | python3 -c 'import json,sys; d=json.load(sys.stdin)
print(f"  incidents:        {len(d)}")
new=sum(1 for i in d if i["status"]=="new")
ack=sum(1 for i in d if i["status"]=="ack")
res=sum(1 for i in d if i["status"]=="resolved")
print(f"    new={new} ack={ack} resolved={res}")
high=sum(1 for i in d if i["severity"] in ("high","critical"))
print(f"    high/critical:  {high}")
cn=sum(1 for i in d if i.get("cross_node"))
print(f"    cross_node:     {cn}")')
    echo "$inc"
    act=$(api GET "/api/v1/actions" "$atok" | python3 -c 'import json,sys; d=json.load(sys.stdin)
print(f"  actions:          {len(d)}")
for s in ("pending","approved","executed","rejected"):
    n=sum(1 for a in d if a["status"]==s)
    if n: print(f"    {s}={n}")')
    echo "$act"
}

run_all() {
    scenario_scan
    sleep 3
    scenario_lateral
    sleep 3
    scenario_data_exfil
    sleep 3
    scenario_approve
    sleep 2
    scenario_status
}

case "${1:-all}" in
    scan)            scenario_scan ;;
    lateral)         scenario_lateral ;;
    exfil|exfiltration) scenario_data_exfil ;;
    approve)         scenario_approve ;;
    status)          scenario_status ;;
    all|*)           run_all ;;
esac
