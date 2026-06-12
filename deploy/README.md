# K8sSecDaP-deploy

GitOps & platform manifests cho **K8sSecDaP** (Port-scan K8s Detect-and-Prevent).

## Layout
- `platform/` — ArgoCD (app-of-apps, apps, appsets, clusters, policies), Tekton CI, Traefik
  ingress, Cloudflare tunnel, helmfile bootstrap (cert-manager/traefik/sealed-secrets/argocd).
- `cni/` — Calico CNI (Installation CR + `install-calico.sh`), bản canonical cho GitOps
  (ArgoCD app `zt-cni-calico`). Bản vendor để bootstrap nằm ở repo **K8sSecDaP-infra**.

## ArgoCD repos
- App-of-apps + hầu hết app → repo này (`K8sSecDaP-deploy`).
- `zt-soc-core`/`zt-soc-data` → repo **K8sSecDaP-soc** (path `manifests`/`data`).
- Repo private → cần đăng ký **repo credential** trong ArgoCD (SSH deploy key / PAT).
