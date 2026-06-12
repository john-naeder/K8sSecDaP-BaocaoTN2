# Control-plane patches

kubeadm static pod manifests live on the master node at
`/etc/kubernetes/manifests/`. They are not GitOps-reconciled (kubelet
itself watches that directory and reloads on file change). We keep the
patches here as the canonical source so any reinstall starts from the
same configuration.

## Files

- `bind-0.0.0.0.patch.sh` — idempotent script that:
  - kube-controller-manager: `--bind-address=127.0.0.1` → `0.0.0.0`
  - kube-scheduler: `--bind-address=127.0.0.1` → `0.0.0.0`
  - etcd: adds `--listen-metrics-urls=http://0.0.0.0:2381`
  - kube-proxy ConfigMap: `metricsBindAddress: ""` → `0.0.0.0:10249`
  - then restarts the kube-proxy DaemonSet so the new metricsBindAddress
    applies.

Apply (must be run as root on the master node):

```bash
sudo bash control-plane/bind-0.0.0.0.patch.sh
```

After apply, Prometheus targets for kube-controller-manager, kube-scheduler,
kube-etcd, and kube-proxy flip to `up` within ~30s.

## Why not GitOps?

kubelet static-pod manifests are managed by the kubelet on the host, not
the K8s API. kubeadm intentionally writes them to local disk because
they boot before the cluster API is reachable. The script + reference
manifests here are what we'd re-apply on a fresh control-plane.
