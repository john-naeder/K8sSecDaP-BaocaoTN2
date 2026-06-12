# K8sSecDaP-soc

Zero-Trust SOC application cho **K8sSecDaP** (Port-scan K8s Detect-and-Prevent):
source các micro-service + Kubernetes manifests.

## Layout
- `services/` — source code các service:
  - `aggregator/` (Go) — gom & tương quan alert.
  - `alert-bridge/` (Go) — cầu nối alert → NATS/web.
  - `incident-service/` (Go) — quản lý incident, weekly report.
  - `web-console/` (Python/Flask) — UI duyệt action/incident.
- `manifests/` — K8s manifests deploy SOC core (NATS, MailHog, pipeline, postgres, grafana...).
- `data/` — K8s manifests tầng dữ liệu (MinIO, postgres backup, retention, weekly-report cronjobs).

## Deploy
Quản lý bởi ArgoCD (repo **K8sSecDaP-deploy** → app `zt-soc-core` path `manifests`, `zt-soc-data` path `data`).
Images build từ `services/*` đẩy lên Harbor.
