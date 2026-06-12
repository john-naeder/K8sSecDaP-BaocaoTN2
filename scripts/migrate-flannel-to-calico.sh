#!/usr/bin/env bash
# migrate-flannel-to-calico.sh — orchestrate the in-place CNI swap.
#
# Expected downtime: ~5–10 minutes (workload pods lose connectivity
# during the kubelet restart on each node, then come back as Calico
# attaches new veth pairs).
#
# This script does NOT SSH into the nodes itself — Step 4 prints the
# commands the operator must run on each node manually (or via a
# Distributed Shell tool like Ansible). Acknowledging this step is the
# safety gate; everything else is automated through kubectl.
#
# Pre-flight checks:
#   - kubectl reachable
#   - cluster currently uses Flannel (kube-flannel-ds DaemonSet present)
#   - install-calico.sh exists alongside this repo's deploy/cni/calico/
#
# Steps:
#   1. Backup pod inventory
#   2. Cordon every node
#   3. Delete Flannel DaemonSet + namespace
#   4. PROMPT: clean /etc/cni + restart kubelet on every node
#   5. Apply Calico
#   6. Uncordon every node
#   7. Verify pod connectivity
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_SCRIPT="$REPO_ROOT/deploy/cni/calico/install-calico.sh"
BACKUP_DIR="${BACKUP_DIR:-/tmp/zt-cni-migration-$(date +%Y%m%d-%H%M%S)}"

err()  { printf "\e[31m[err]\e[0m  %s\n" "$*" >&2; exit 1; }
log()  { printf "\e[34m[step]\e[0m %s\n" "$*"; }
ok()   { printf "\e[32m[ok]\e[0m   %s\n" "$*"; }
warn() { printf "\e[33m[warn]\e[0m %s\n" "$*"; }

confirm() {
    read -r -p "$1 [yes/NO]: " ans
    [[ "${ans,,}" == "yes" ]] || err "aborted by operator"
}

[[ -x "$INSTALL_SCRIPT" ]] || err "missing $INSTALL_SCRIPT (chmod +x?)"
command -v kubectl >/dev/null || err "kubectl not in PATH"
mkdir -p "$BACKUP_DIR"

ctx="$(kubectl config current-context 2>/dev/null || true)"
[[ -n "$ctx" ]] || err "no kubectl context"
log "context: $ctx"
log "backup dir: $BACKUP_DIR"

# ─── Step 1: Backup ────────────────────────────────────────────────────────

log "[1/7] backing up cluster state"
kubectl get nodes -o wide                                > "$BACKUP_DIR/nodes.txt"
kubectl get pods -A -o wide                              > "$BACKUP_DIR/pods.txt"
kubectl -n kube-system get ds                            > "$BACKUP_DIR/kube-system-ds.txt" 2>&1 || true
kubectl get networkpolicies -A                           > "$BACKUP_DIR/networkpolicies.txt" 2>&1 || true
kubectl get cm -n kube-system kube-flannel-cfg -o yaml   > "$BACKUP_DIR/flannel-cm.yaml" 2>&1 || true
kubectl get ds -n kube-system kube-flannel-ds -o yaml    > "$BACKUP_DIR/flannel-ds.yaml" 2>&1 || true
ok "saved to $BACKUP_DIR"

# ─── Pre-flight: detect Flannel ────────────────────────────────────────────

if ! kubectl -n kube-system get ds kube-flannel-ds >/dev/null 2>&1 \
   && ! kubectl -n kube-flannel get ds kube-flannel-ds >/dev/null 2>&1; then
    warn "no kube-flannel-ds DaemonSet found — proceed anyway?"
    confirm "Continue migration without confirmed Flannel?"
fi

# ─── Step 2: Cordon ────────────────────────────────────────────────────────

log "[2/7] cordoning every node (workloads stay; new pods are blocked)"
mapfile -t NODES < <(kubectl get nodes -o name)
for n in "${NODES[@]}"; do
    kubectl cordon "${n#node/}"
done
ok "${#NODES[@]} node(s) cordoned"

# ─── Step 3: Delete Flannel ────────────────────────────────────────────────

log "[3/7] deleting Flannel DaemonSet + ConfigMap"
confirm "About to delete Flannel — workloads will lose pod-network briefly. Proceed?"
kubectl -n kube-system delete --ignore-not-found ds kube-flannel-ds
kubectl -n kube-system delete --ignore-not-found cm kube-flannel-cfg
kubectl -n kube-system delete --ignore-not-found sa flannel
kubectl delete --ignore-not-found clusterrole flannel
kubectl delete --ignore-not-found clusterrolebinding flannel
kubectl delete --ignore-not-found ns kube-flannel
ok "Flannel resources removed from API"

# ─── Step 4: Manual node-side cleanup ──────────────────────────────────────

log "[4/7] node-side cleanup — operator action required"
cat <<'EOF'

  Run the following on EACH node (root or sudo):

    sudo rm -rf /etc/cni/net.d/10-flannel.conflist \
                /etc/cni/net.d/10-flannel.conf
    sudo rm -rf /run/flannel
    sudo rm -rf /var/lib/cni/networks/cbr0
    sudo ip link delete cni0 2>/dev/null || true
    sudo ip link delete flannel.1 2>/dev/null || true
    sudo systemctl restart kubelet

  Confirm the restart finished on every node before answering 'yes' below.
EOF
confirm "Has node-side cleanup completed on every node?"

# ─── Step 5: Apply Calico ──────────────────────────────────────────────────

log "[5/7] installing Calico (this can take 2–5 minutes)"
"$INSTALL_SCRIPT"
ok "Calico installed"

log "waiting for every kubelet to report Ready (≤5m)"
kubectl wait --for=condition=Ready nodes --all --timeout=300s

# ─── Step 6: Uncordon ──────────────────────────────────────────────────────

log "[6/7] uncordoning nodes — scheduler can place pods again"
for n in "${NODES[@]}"; do
    kubectl uncordon "${n#node/}"
done
ok "all nodes schedulable"

# ─── Step 7: Verify ────────────────────────────────────────────────────────

log "[7/7] sanity check — pod cross-node ping"
"$REPO_ROOT/scripts/test-networkpolicy.sh" --connectivity-only \
    || warn "connectivity smoke test failed — investigate before declaring success"

ok "Migration complete. Pre-migration backup at $BACKUP_DIR."
