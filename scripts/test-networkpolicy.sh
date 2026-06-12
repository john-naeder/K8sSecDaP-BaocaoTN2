#!/usr/bin/env bash
# test-networkpolicy.sh — verify the active CNI both routes pod traffic
# AND enforces NetworkPolicy. The test is the cheapest possible signal
# that the migration to a policy-aware CNI succeeded.
#
# Modes:
#   --connectivity-only   Skip the NetworkPolicy assertion (used by the
#                         migration script before Calico is up).
#   default               Connectivity + NetworkPolicy enforcement.
#
# Layout:
#   ns: zt-cni-test
#   pod victim   (label app=victim,    role=server) — runs nc listener :8080
#   pod attacker (label app=attacker,  role=client) — runs nc client
#
# Steps:
#   1. Create ns + pods + service
#   2. Assert attacker → victim succeeds (baseline connectivity)
#   3. Apply default-deny NetworkPolicy
#   4. Assert attacker → victim fails (Calico drops traffic)
#   5. Cleanup
set -euo pipefail

NS="${NS:-zt-cni-test}"
CONNECTIVITY_ONLY=0
[[ "${1:-}" == "--connectivity-only" ]] && CONNECTIVITY_ONLY=1

err()  { printf "\e[31m[err]\e[0m  %s\n" "$*" >&2; exit 1; }
log()  { printf "\e[34m[step]\e[0m %s\n" "$*"; }
ok()   { printf "\e[32m[ok]\e[0m   %s\n" "$*"; }
warn() { printf "\e[33m[warn]\e[0m %s\n" "$*"; }

cleanup() { kubectl delete ns "$NS" --wait=false >/dev/null 2>&1 || true; }
trap cleanup EXIT

command -v kubectl >/dev/null || err "kubectl not in PATH"

log "creating namespace $NS"
kubectl create ns "$NS" >/dev/null
ok "ns created"

log "deploying victim + attacker pods"
kubectl -n "$NS" apply -f - <<'YAML' >/dev/null
apiVersion: v1
kind: Pod
metadata:
  name: victim
  labels: { app: victim, role: server }
spec:
  containers:
    - name: server
      image: alpine:3.20
      command: ["sh","-c","apk add --no-cache busybox-extras >/dev/null 2>&1; nc -lk -p 8080 -e echo OK"]
      ports: [{ containerPort: 8080 }]
---
apiVersion: v1
kind: Service
metadata: { name: victim }
spec:
  selector: { app: victim }
  ports: [{ port: 8080, targetPort: 8080 }]
---
apiVersion: v1
kind: Pod
metadata:
  name: attacker
  labels: { app: attacker, role: client }
spec:
  containers:
    - name: client
      image: alpine:3.20
      command: ["sh","-c","apk add --no-cache busybox-extras >/dev/null 2>&1; sleep 3600"]
YAML

log "waiting for both pods Ready (≤2m)"
kubectl -n "$NS" wait --for=condition=Ready pod/victim   --timeout=120s
kubectl -n "$NS" wait --for=condition=Ready pod/attacker --timeout=120s
ok "pods Ready"

probe() {
    kubectl -n "$NS" exec attacker -- sh -c \
        'nc -z -w 3 victim 8080 && echo REACHABLE || echo BLOCKED' \
        2>/dev/null | tail -1
}

log "[1/2] baseline: attacker → victim should be REACHABLE"
sleep 2
result="$(probe)"
[[ "$result" == "REACHABLE" ]] \
    || err "baseline failed (got $result) — pod-to-pod routing broken"
ok "baseline reachable"

if (( CONNECTIVITY_ONLY )); then
    ok "connectivity-only mode: skipping NetworkPolicy assertion"
    exit 0
fi

log "applying default-deny NetworkPolicy on label app=victim"
kubectl -n "$NS" apply -f - <<'YAML' >/dev/null
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata: { name: default-deny-victim }
spec:
  podSelector: { matchLabels: { app: victim } }
  policyTypes: [Ingress]
  ingress: []          # deny everything
YAML

log "[2/2] enforced: attacker → victim should be BLOCKED"
sleep 5  # give the data-plane a moment to install rules
result="$(probe)"
if [[ "$result" == "BLOCKED" ]]; then
    ok "NetworkPolicy enforced — CNI is policy-aware"
elif [[ "$result" == "REACHABLE" ]]; then
    err "NetworkPolicy NOT enforced — current CNI ignores policy (still Flannel?)"
else
    err "unexpected probe output: $result"
fi
