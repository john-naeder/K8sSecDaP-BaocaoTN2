# CNI migration runbook: Flannel → Calico

This runbook documents the in-place CNI swap for the existing Kubernetes
cluster running Flannel. Calico is required because Flannel does not
enforce `networking.k8s.io/v1` NetworkPolicy, which the SOC Layer-1
auto-quarantine in `soc/incident-service` depends on.

The migration is destructive to in-flight pod traffic for ~5–10 minutes.
Schedule a maintenance window.

## Why we have to do this

Flannel ships only the data-plane bits (VXLAN encapsulation, route
distribution). It does not install netfilter/eBPF rules for
NetworkPolicy. Applying a `NetworkPolicy` resource on a Flannel cluster
silently has zero effect — `kubectl describe networkpolicy` shows the
spec, but pod traffic is unrestricted.

`soc/incident-service` (commit 2e6a99e) calls
`NetworkingV1().NetworkPolicies(ns).Create(...)` when the operator
approves a quarantine action. On Flannel that call succeeds at the API
server but the data-plane never enforces it — the attack continues. We
need a policy-aware CNI.

Calico v3.28 was chosen over Cilium for setup simplicity, lower memory
footprint on a 3-node cluster, and better compatibility with the
existing eBPF kprobes in `ebpf-collector/` (Cilium also installs eBPF
hooks, which can conflict).

## Pre-flight checklist

- [ ] All workloads can tolerate a 5–10 minute pod-network outage.
- [ ] You hold root SSH on every node (Step 4 needs `sudo`).
- [ ] `kubectl get nodes` returns Ready for every node.
- [ ] Cluster pod CIDR matches `deploy/cni/calico/calico-custom.yaml`
      (`10.244.0.0/16` by default; check `kubectl cluster-info dump |
      grep -m1 cluster-cidr`).
- [ ] Free disk space on every node ≥ 1 GiB for the new CNI binaries.
- [ ] `etcd` snapshot taken (defence in depth):
      `sudo ETCDCTL_API=3 etcdctl --endpoints=... snapshot save /backup/pre-cni-migration.db`.
- [ ] Every node that has a stable network path to the API server is
      labeled `zt-stable=true` (see *Node labeling* below). Without
      this, `calico-apiserver` / `calico-kube-controllers` / `typha`
      stay Pending because `controlPlaneNodeSelector` matches nothing.

## Node labeling (zt-stable)

`calico-custom.yaml` sets `controlPlaneNodeSelector: { zt-stable: "true" }`.
Reason: nodes that can only reach the kube-apiserver via a flaky
Tailscale DERP relay (no IPv6 / CGNAT symmetric NAT) make these
operator-managed control-plane pods CrashLoop with TLS handshake
timeouts (incident 2026-05-30).

Apply the label to every node that **can** host these pods:

```bash
# Required on at least one node, ideally two for HA.
kubectl label node <stable-node> zt-stable=true --overwrite

# Remove on flaky nodes (or simply leave unlabeled):
kubectl label node <flaky-node> zt-stable-
```

Verify:

```bash
kubectl get nodes -L zt-stable
# NAME                 STATUS   ...   ZT-STABLE
# userver-master       Ready    ...   true
# userver-worker-1     Ready    ...   true
# userver-worker-home  Ready    ...   (empty)
```

`calico-node` (the per-node DaemonSet) is **not** affected by this
selector — it still runs on every node, by design.

## Forward migration

```bash
# From the repo root.
bash scripts/migrate-flannel-to-calico.sh
```

The script walks 7 steps, prompting for confirmation at each
destructive boundary. The interactive Step 4 prints commands the
operator must run on every node (the script cannot SSH for you):

```bash
sudo rm -rf /etc/cni/net.d/10-flannel.conflist /etc/cni/net.d/10-flannel.conf
sudo rm -rf /run/flannel
sudo rm -rf /var/lib/cni/networks/cbr0
sudo ip link delete cni0     2>/dev/null || true
sudo ip link delete flannel.1 2>/dev/null || true
sudo systemctl restart kubelet
```

After all nodes restart their kubelet, type `yes` at the prompt and
the script will install Calico via `tigera-operator` and run the
`scripts/test-networkpolicy.sh --connectivity-only` smoke check.

A timestamped backup of pre-migration state lands in
`/tmp/zt-cni-migration-<date>/`.

## Verifying enforcement

After the migration succeeds, run the full smoke test:

```bash
bash scripts/test-networkpolicy.sh
```

Expected: `[ok] NetworkPolicy enforced — CNI is policy-aware`. If you
see `[err] NetworkPolicy NOT enforced`, the migration is incomplete —
do not redeploy `incident-service` with `APPLY_MODE=apply` until this
test passes.

## Rollback

If Calico fails to stabilise (calico-node CrashLoopBackOff on multiple
nodes for >5 minutes), roll back:

```bash
bash scripts/rollback-calico-to-flannel.sh
```

Same shape as the forward script; Step 4 also requires manual node
cleanup (paths differ — Calico writes `/etc/cni/net.d/10-calico.conflist`
and `/var/lib/calico`).

## Common failures

| Symptom                                              | Fix                                                                                         |
|------------------------------------------------------|---------------------------------------------------------------------------------------------|
| `tigera-operator` Deployment never becomes Available | Check operator pod logs: `kubectl -n tigera-operator logs deploy/tigera-operator`. Usually image-pull failure on air-gapped nodes — pre-pull the image or set `imagePullPolicy: Never`. |
| `calico-node` CrashLoopBackOff                       | `kubectl -n calico-system logs ds/calico-node -c calico-node`. Often kernel module missing (`xt_set`, `nf_conntrack`) — install `iptables-nft` and `conntrack-tools` on the node. |
| Pod cross-node ping fails after Step 6               | VXLAN UDP/4789 blocked by host firewall. `sudo iptables -L -n -v` and look for DROP. Add `iptables -I INPUT -p udp --dport 4789 -j ACCEPT` (or your firewall equivalent). |
| `kubectl --dry-run=client` rejects Installation      | tigera-operator not yet up. Wait for `kubectl -n tigera-operator wait deploy/tigera-operator --for=condition=Available --timeout=60s` then re-apply. |

## Post-migration

Once the smoke test passes:

1. Apply the SOC manifests in order:
   ```bash
   kubectl apply -f deploy/soc/00-namespace.yaml
   kubectl apply -f deploy/soc/05-rbac-incident-service.yaml
   kubectl apply -f deploy/soc/15-mailhog.yaml
   kubectl apply -f deploy/soc/35-incident-service.yaml
   ```
2. Trigger an attack scenario (`bash scripts/attack_scenarios.sh`) and
   confirm a NetworkPolicy named `zt-quarantine-incident-<id>` appears
   in `zt-targets` after admin approval.
3. Update the BC2 chapter `report/stage2/chapters/ch-deploy.tex` with
   the actual screenshot and timing measurements.
