#!/usr/bin/env bash
# rollback-calico-to-flannel.sh — undo the migration if Calico fails to
# stabilise. Same expected-downtime envelope (~5–10 min) and same
# manual-step gate (Step 4) as the forward migration.
#
# Pre-conditions:
#   - Calico currently active (calico-system namespace exists)
#   - You have the Flannel manifest URL (default: kube-flannel upstream)
#
# Steps:
#   1. Backup current state
#   2. Cordon nodes
#   3. Delete Calico Installation + tigera-operator
#   4. PROMPT: clean Calico CNI configs + restart kubelet on every node
#   5. Apply Flannel manifest
#   6. Uncordon
#   7. Verify
set -euo pipefail

FLANNEL_URL="${FLANNEL_URL:-https://github.com/flannel-io/flannel/releases/latest/download/kube-flannel.yml}"
BACKUP_DIR="${BACKUP_DIR:-/tmp/zt-cni-rollback-$(date +%Y%m%d-%H%M%S)}"

err()  { printf "\e[31m[err]\e[0m  %s\n" "$*" >&2; exit 1; }
log()  { printf "\e[34m[step]\e[0m %s\n" "$*"; }
ok()   { printf "\e[32m[ok]\e[0m   %s\n" "$*"; }

confirm() {
    read -r -p "$1 [yes/NO]: " ans
    [[ "${ans,,}" == "yes" ]] || err "aborted by operator"
}

command -v kubectl >/dev/null || err "kubectl not in PATH"
mkdir -p "$BACKUP_DIR"

# ─── Step 1 ────────────────────────────────────────────────────────────────

log "[1/7] backing up current state to $BACKUP_DIR"
kubectl get nodes -o wide                                > "$BACKUP_DIR/nodes.txt"
kubectl get pods -A -o wide                              > "$BACKUP_DIR/pods.txt"
kubectl get installation default -o yaml 2>/dev/null     > "$BACKUP_DIR/calico-installation.yaml" || true
kubectl get networkpolicies -A                           > "$BACKUP_DIR/networkpolicies.txt" 2>&1 || true

# ─── Step 2 ────────────────────────────────────────────────────────────────

log "[2/7] cordoning nodes"
mapfile -t NODES < <(kubectl get nodes -o name)
for n in "${NODES[@]}"; do kubectl cordon "${n#node/}"; done

# ─── Step 3 ────────────────────────────────────────────────────────────────

log "[3/7] removing Calico"
confirm "About to delete Calico Installation + tigera-operator. Proceed?"
kubectl delete --ignore-not-found installation default
kubectl delete --ignore-not-found apiserver default
kubectl delete --ignore-not-found ns calico-system calico-apiserver
kubectl delete --ignore-not-found -f \
    "https://raw.githubusercontent.com/projectcalico/calico/v3.28.2/manifests/tigera-operator.yaml" \
    || true
ok "Calico API objects removed"

# ─── Step 4 ────────────────────────────────────────────────────────────────

log "[4/7] node-side cleanup — operator action required"
cat <<'EOF'

  Run the following on EACH node (root or sudo):

    sudo rm -rf /etc/cni/net.d/10-calico.conflist \
                /etc/cni/net.d/calico-kubeconfig
    sudo rm -rf /var/lib/calico
    sudo ip link delete vxlan.calico 2>/dev/null || true
    sudo systemctl restart kubelet

  Confirm the restart finished on every node before continuing.
EOF
confirm "Has node-side cleanup completed on every node?"

# ─── Step 5 ────────────────────────────────────────────────────────────────

log "[5/7] reinstalling Flannel from $FLANNEL_URL"
kubectl apply -f "$FLANNEL_URL"
kubectl -n kube-flannel rollout status daemonset/kube-flannel-ds --timeout=300s 2>/dev/null \
    || kubectl -n kube-system rollout status daemonset/kube-flannel-ds --timeout=300s

log "waiting for nodes Ready"
kubectl wait --for=condition=Ready nodes --all --timeout=300s

# ─── Step 6 ────────────────────────────────────────────────────────────────

log "[6/7] uncordoning nodes"
for n in "${NODES[@]}"; do kubectl uncordon "${n#node/}"; done

# ─── Step 7 ────────────────────────────────────────────────────────────────

log "[7/7] sanity check"
kubectl get nodes -o wide
kubectl -n kube-flannel get pods 2>/dev/null \
    || kubectl -n kube-system get pods -l app=flannel

ok "Rollback complete. Backup at $BACKUP_DIR."
