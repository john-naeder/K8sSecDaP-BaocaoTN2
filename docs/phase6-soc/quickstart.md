# Zero-Trust SOC — Quickstart

End-to-end SOC stack deployed in ~5 manifests + 1 attack script.

## Cluster footprint

| Namespace  | Workload                | Image |
|------------|--------------------------|-------|
| `zt-mapper`  | DaemonSet `zt-pipeline-live` (pipeline + tcpdump + alert-bridge sidecar) | `johnnaeder/ebpf-portscan-k8s-check:pipeline-soc-s2-r4` + `:alert-bridge-s2` |
| `zt-targets` | Deployments `web` & `api`, Pod `attacker` (nmap) | `nginx:1.27` / `ealen/echo-server` / `instrumentisto/nmap` |
| `soc`        | StatefulSet `nats`, StatefulSet `postgres`, Deployments `zt-aggregator` & `zt-incident-service` | `nats:2.10` / `postgres:16` / `:aggregator-s2-r2` / `:incident-service-s3-r3` |
| `monitoring` | `loki`, `loki-grafana` (with `prometheus` datasource), `prometheus` | helm + `prom/prometheus` |

## Data path

```
nmap (attacker pod)
   └► TCP SYN packets on cni0
        └► tcpdump (DaemonSet) → tcpdump_to_events.py → zt-pipeline (CMS+Tarjan+BFS)
              └► alerts.json + stderr
                   └► alert-bridge sidecar → NATS zt.alerts.raw
                          └► zt-aggregator (dedup + cross-node) → NATS zt.alerts.enriched
                                 └► zt-incident-service (Postgres + auto NetworkPolicy draft)
                                        └► REST API + Grafana dashboards
```

## Apply order (clean cluster)

```bash
# 0. local-path provisioner (only once per cluster)
kubectl apply -f https://raw.githubusercontent.com/rancher/local-path-provisioner/v0.0.30/deploy/local-path-storage.yaml
kubectl patch storageclass local-path -p '{"metadata":{"annotations":{"storageclass.kubernetes.io/is-default-class":"true"}}}'

# 1. Namespaces + NATS + Postgres + Prometheus + monitoring
kubectl apply -f deploy/soc/00-namespace.yaml \
              -f deploy/soc/10-nats.yaml \
              -f deploy/soc/30-postgres.yaml \
              -f deploy/soc/60-prometheus.yaml
kubectl label ns zt-mapper pod-security.kubernetes.io/enforce=privileged --overwrite
kubectl label ns soc       pod-security.kubernetes.io/enforce=baseline   --overwrite

# 2. SOC core
kubectl apply -f deploy/soc/25-aggregator.yaml \
              -f deploy/soc/35-incident-service.yaml

# 3. Detection plane (real traffic)
kubectl apply -f deploy/soc/22-pipeline-live.yaml

# 4. Attacker + targets
kubectl apply -f deploy/soc/40-attack-targets.yaml \
              -f deploy/soc/41-attacker.yaml
```

## Tokens (RBAC demo)

```bash
kubectl -n soc get secret incident-rbac-tokens -o jsonpath='{.data.RBAC_TOKENS}' | base64 -d
# Format: viewer-demo:viewer:viewer-demo-token,editor-demo:editor:editor-demo-token,admin-demo:admin:admin-demo-token
```

| Role   | Can do                                          |
|--------|-------------------------------------------------|
| viewer | GET /incidents, /actions, /audit                |
| editor | + PATCH /incidents/{id}/status (ack/resolve)    |
| admin  | + POST /actions/{id}/{approve,reject}           |

## Demo run

```bash
# 1. tail alerts in one terminal
kubectl -n zt-mapper logs -f -l app.kubernetes.io/name=zt-pipeline-live -c pipeline | grep ALERT

# 2. attack from another
kubectl -n zt-targets exec attacker -- /attack/burst-scan.sh 10.244.1.40

# 3. inspect through API (port-forward first)
kubectl -n soc port-forward svc/zt-incident-service 18080:8080 &
TOK=admin-demo-token
curl -sS -H "Authorization: Bearer $TOK" http://localhost:18080/api/v1/incidents | jq '.[].source_ip'
curl -sS -H "Authorization: Bearer $TOK" 'http://localhost:18080/api/v1/actions?status=pending' | jq '.[].id'

# 4. approve a quarantine action (writes YAML to PVC, dryrun mode)
curl -sS -X POST -H "Authorization: Bearer $TOK" http://localhost:18080/api/v1/actions/1/approve | jq

# 5. or use the wrapper
bash scripts/attack_scenarios.sh
```

## Grafana dashboards

```bash
kubectl -n monitoring port-forward svc/loki-grafana 3000:80 &
PASS=$(kubectl -n monitoring get secret loki-grafana -o jsonpath='{.data.admin-password}' | base64 -d)
echo "admin / $PASS"
```

- `/d/zt-soc-overview`   — alert stream (Loki)
- `/d/zt-soc-incidents`  — incident lifecycle + actions queue + audit (Loki + Prometheus)

## Cleanup

```bash
kubectl delete -f deploy/soc/41-attacker.yaml -f deploy/soc/40-attack-targets.yaml
kubectl delete -f deploy/soc/22-pipeline-live.yaml
kubectl delete -f deploy/soc/35-incident-service.yaml -f deploy/soc/25-aggregator.yaml
kubectl delete -f deploy/soc/30-postgres.yaml -f deploy/soc/10-nats.yaml -f deploy/soc/60-prometheus.yaml
kubectl delete ns zt-mapper zt-targets soc
```

## What's NOT in this build (deferred)

- **APPLY_MODE=apply** — applying the NetworkPolicy via K8s API server is a stub; today only `dryrun` (write YAML to PVC) is implemented. Set `APPLY_MODE=apply` env on incident-service + complete the SSA call in `internal/response/apply.go`.
- **Helm umbrella chart** — current install is loose manifests. Plan said S6 ships `helm/zt-soc/`; left as future work.
- **Global graph in aggregator** — current correlator only does dedup + cross-node. The plan envisions reusing libdsa Tarjan/BFS over a cluster-wide graph rebuilt from raw events; would require pipeline to also publish `zt.events.*`.
- **OIDC** — RBAC is shared-token only; production would front the API with OIDC + K8s TokenReview.
