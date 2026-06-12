#!/usr/bin/env bash
# Patch kubeadm control-plane static pods + kube-proxy to expose metrics
# on 0.0.0.0 instead of 127.0.0.1. Required for kube-prometheus-stack
# ServiceMonitors (controller-manager, scheduler, etcd, kube-proxy) to
# reach the metrics ports across the pod network.
#
# Idempotent: safe to re-run. kubelet auto-restarts the static pods on
# manifest change; kube-proxy needs an explicit rollout because its
# bind address comes from a ConfigMap.

set -euo pipefail

MANIFEST_DIR=/etc/kubernetes/manifests
# Use the root admin.conf so the script works under `sudo` without
# inheriting the invoking user's KUBECONFIG (which is what made the
# previous run fall back to localhost:8080 and fail).
KUBECONFIG=/etc/kubernetes/admin.conf
export KUBECONFIG

if [[ $EUID -ne 0 ]]; then
  echo "ERROR: must run as root (kubelet writes /etc/kubernetes/manifests as root)" >&2
  exit 1
fi

if [[ ! -r $KUBECONFIG ]]; then
  echo "ERROR: $KUBECONFIG not readable — are you on the control-plane node?" >&2
  exit 1
fi

# --- kube-controller-manager ---
if grep -q -- '--bind-address=127.0.0.1' "$MANIFEST_DIR/kube-controller-manager.yaml"; then
  sed -i 's|--bind-address=127.0.0.1|--bind-address=0.0.0.0|' \
    "$MANIFEST_DIR/kube-controller-manager.yaml"
  echo "patched kube-controller-manager: bind 0.0.0.0"
else
  echo "kube-controller-manager: already 0.0.0.0 or no bind-address flag"
fi

# --- kube-scheduler ---
if grep -q -- '--bind-address=127.0.0.1' "$MANIFEST_DIR/kube-scheduler.yaml"; then
  sed -i 's|--bind-address=127.0.0.1|--bind-address=0.0.0.0|' \
    "$MANIFEST_DIR/kube-scheduler.yaml"
  echo "patched kube-scheduler: bind 0.0.0.0"
else
  echo "kube-scheduler: already 0.0.0.0 or no bind-address flag"
fi

# --- etcd ---
# kubeadm does not set --listen-metrics-urls by default; metrics serve
# alongside peer/client traffic which is mTLS. Expose a plaintext
# metrics endpoint on 2381 so Prometheus can scrape.
#
# Note: kubeadm v1.32+ does add --listen-metrics-urls but binds it to
# 127.0.0.1, which fails Prometheus scrapes from the pod network. So we
# check the *value*, not just existence.
if grep -q -- '--listen-metrics-urls=http://0.0.0.0:2381' "$MANIFEST_DIR/etcd.yaml"; then
  echo "etcd: --listen-metrics-urls=http://0.0.0.0:2381 already set"
elif grep -q -- '--listen-metrics-urls=' "$MANIFEST_DIR/etcd.yaml"; then
  sed -i -E 's|--listen-metrics-urls=http://[^[:space:]]+|--listen-metrics-urls=http://0.0.0.0:2381|' \
    "$MANIFEST_DIR/etcd.yaml"
  echo "patched etcd: rewrote --listen-metrics-urls to 0.0.0.0:2381"
else
  sed -i '/- etcd$/a\    - --listen-metrics-urls=http://0.0.0.0:2381' \
    "$MANIFEST_DIR/etcd.yaml"
  echo "patched etcd: added --listen-metrics-urls=http://0.0.0.0:2381"
fi

# --- kube-proxy ---
# metricsBindAddress lives in a ConfigMap, not a static pod manifest.
# Use a strategic-merge JSON patch so only data["config.conf"] is
# rewritten — the rest of the ConfigMap stays untouched.
CURRENT=$(kubectl -n kube-system get cm kube-proxy -o jsonpath='{.data.config\.conf}' \
  | awk '/^metricsBindAddress:/ {gsub(/"/, "", $2); print $2}')
if [[ "$CURRENT" != "0.0.0.0:10249" ]]; then
  NEW_CONF=$(kubectl -n kube-system get cm kube-proxy -o jsonpath='{.data.config\.conf}' \
    | sed -E 's|^metricsBindAddress:.*|metricsBindAddress: "0.0.0.0:10249"|')
  if ! grep -q '^metricsBindAddress:' <<<"$NEW_CONF"; then
    NEW_CONF="${NEW_CONF}"$'\n'"metricsBindAddress: \"0.0.0.0:10249\""
  fi
  # Build a JSON patch document — base64 isn't needed because we use
  # a strategic-merge body and let kubectl serialise the string.
  kubectl -n kube-system patch configmap kube-proxy --type=merge \
    -p "$(jq -n --arg c "$NEW_CONF" '{data: {"config.conf": $c}}')"
  kubectl -n kube-system rollout restart daemonset kube-proxy
  echo "patched kube-proxy CM + rolled out DS"
else
  echo "kube-proxy: metricsBindAddress already 0.0.0.0:10249"
fi

echo
echo "Done. kubelet will pick up static-pod changes within ~30s."
echo "Watch with:"
echo "  watch 'kubectl get pods -n kube-system | grep -E \"controller-manager|scheduler|etcd|kube-proxy\"'"
