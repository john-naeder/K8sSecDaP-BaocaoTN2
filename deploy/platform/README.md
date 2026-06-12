# `deploy/platform/` — GitOps platform layer for BC2

This tree carries the manifests that turn a bare Kubernetes cluster into the
BC2 production-style environment. **Nothing here is applied automatically**
— the user runs the bootstrap helmfile once, then Argo CD takes over.

## Layout

```
deploy/platform/
├── helmfile/                  Bootstrap layer (run by helmfile CLI)
│   ├── helmfile-bootstrap.yaml         cert-manager → traefik → sealed-secrets → argocd
│   └── values/                         per-release values overrides
├── argocd/
│   ├── app-of-apps.yaml                Single-cluster root (apply once after helmfile)
│   ├── appsets/cluster-roots.yaml      Multi-cluster root (ApplicationSet — preferred when ≥2 clusters)
│   ├── clusters/
│   │   ├── onprem/config.yaml          ACTIVE — BC2 on-prem cluster
│   │   └── gke/config.yaml.example     TEMPLATE — copy to config.yaml when adding GKE
│   ├── apps/                           One Application CRD per platform component
│   └── policies/                       Kyverno ClusterPolicy ruleset
├── cloudflared/                        Outbound tunnel + ingress route docs
└── tekton/                             CI tasks/pipelines/triggers (sub-session H)
```

## Component matrix

| Application      | Chart                                              | Purpose                                                                  |
|------------------|----------------------------------------------------|--------------------------------------------------------------------------|
| `harbor`         | helm.goharbor.io/harbor 1.16.2                     | OCI registry. **Trivy disabled** (resource budget).                      |
| `nexus`          | oteemo/sonatype-nexus 5.5.0                        | Generic artifact repo (Maven, npm, Helm, docker-proxy).                  |
| `monitoring`     | prometheus-community/kube-prometheus-stack 68.4.5  | Prometheus + Grafana + Alertmanager. zt-soc scrape configs + alert rules. |
| `logging`        | grafana/loki-stack 2.10.2                          | Loki + Promtail. Loki datasource auto-wired into Grafana.                |
| `tekton-*`       | tektoncd-operator 0.74.x + repo-shipped resources  | CI engine + this repo's tasks/pipelines/triggers.                        |
| `rancher`        | rancher/rancher 2.9.4                              | Multi-cluster UI (BC2 + future GKE).                                     |
| `kyverno`        | kyverno/kyverno 3.3.7 + repo-shipped policies      | Admission engine + slim policy set (`policies/baseline.yaml`).           |
| `cloudflared`    | repo-shipped Deployment                            | Outbound tunnel exposing the apps below to the internet.                 |
| `ingress-routing`| repo-shipped Traefik IngressRoutes                 | Per-app routes consumed by cloudflared.                                  |
| `zt-soc`         | repo-shipped (`deploy/soc/`, `deploy/data/`, `deploy/cni/calico/`) | BC2 application itself. Sync waves keep CNI → core → data ordering. |

## Bootstrap sequence (manual, one-time)

```bash
# 0. Pre-requisites: kubectl reachable, helm/helmfile installed, helm-diff plugin.
#    Cluster CNI must already be Calico (see deploy/cni/calico/RUNBOOK.md).

# 1. Bootstrap the four base helm releases (cert-manager, traefik, sealed-secrets, argocd).
helmfile -f deploy/platform/helmfile/helmfile-bootstrap.yaml diff
helmfile -f deploy/platform/helmfile/helmfile-bootstrap.yaml apply

# 2. Hand off to Argo CD. Single-cluster path:
kubectl apply -f deploy/platform/argocd/app-of-apps.yaml
#    Multi-cluster path (preferred for BC2 + GKE):
kubectl apply -f deploy/platform/argocd/appsets/cluster-roots.yaml

# 3. (Optional) Bootstrap the Cloudflare tunnel token secret:
kubectl create secret generic cloudflared-token -n cloudflare \
    --from-literal=tunnel-token="$CLOUDFLARE_TUNNEL_TOKEN"
```

After step 2 every subsequent change is a Git commit — Argo CD auto-syncs.

## Adding a new cluster (e.g. GKE)

```bash
# 1. Register cluster with Argo CD (requires kubeconfig).
argocd cluster add gke-bc2 --kubeconfig ~/.kube/gke-bc2.yaml

# 2. Activate the cluster in this repo.
cp deploy/platform/argocd/clusters/gke/config.yaml.example \
   deploy/platform/argocd/clusters/gke/config.yaml
# Edit cluster.server to the GKE API endpoint:
#   gcloud container clusters describe <name> --format='value(endpoint)'

# 3. Commit + push. ApplicationSet `cluster-roots` generates the new root.
git add deploy/platform/argocd/clusters/gke/config.yaml
git commit -m "feat(platform): activate GKE cluster"
git push
```

## What we keep / what we drop vs. `old_cluster_cleared_out_config`

**Keep** — Traefik (ingress), Cloudflare Tunnel (exposure), Kyverno (engine
with a slim policy set), ArgoCD app-of-apps + ApplicationSet, Harbor,
Nexus (new), kube-prometheus-stack, loki-stack, Rancher (new), Tekton.

**Drop** — Trivy (Harbor `trivy.enabled: false`; resource budget),
cosign / SLSA-L3 verification policies (outside luận-văn scope), Kafka /
Strimzi, scholarhub demo apps, Flannel CNI role (BC2 = Calico).

## Verification (no cluster required)

```bash
# YAML parse + K8s schema (CRDs not on the local cluster, so --validate=false)
find deploy/platform -name '*.yaml' -print0 \
    | xargs -0 -I{} kubectl apply --dry-run=client --validate=false -f {}

# Lint
yamllint -s deploy/platform/

# Helmfile lint (optional; needs helmfile CLI)
helmfile -f deploy/platform/helmfile/helmfile-bootstrap.yaml lint
```
