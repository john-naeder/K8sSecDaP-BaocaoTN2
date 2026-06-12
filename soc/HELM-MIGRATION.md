# Helm migration plan (Bug 4 — pending)

The SOC stack today ships as raw manifests under `manifests/*.yaml` that
ArgoCD applies via the `zt-soc-core` Application (directory source). The
note flagged we should:

1. Move zt-soc to a Helm chart (versioned releases, parameterised images,
   values per environment).
2. Push images via Tekton + private Docker Hub registry instead of
   ad-hoc `docker buildx build --push`.

## Why not in this commit

The chart is ~17 templates worth of refactor (postgres, NATS, MinIO,
mailhog, aggregator, incident-service, web-console, pipeline,
pipeline-live, attacker, targets, alert-viewer, RBAC, secrets, services,
PVCs, the entry-point ConfigMaps). Doing it in the same commit as
v0.2 image bumps risks regressing the working stack. Track it as its
own work-stream.

## Proposed shape

```
charts/zt-soc/
├── Chart.yaml                # apiVersion v2, name=zt-soc
├── values.yaml               # defaults: images, replicas, storage
├── values-prod.yaml          # overrides for the on-prem lab
└── templates/
    ├── _helpers.tpl
    ├── namespace.yaml
    ├── postgres.yaml         # StatefulSet + PVC + Svc + Secret
    ├── nats.yaml
    ├── minio.yaml
    ├── aggregator.yaml
    ├── incident-service.yaml
    ├── web-console.yaml
    ├── pipeline.yaml
    ├── pipeline-live.yaml
    ├── targets.yaml          # api + web + attacker
    ├── rbac.yaml             # SA + Role + RoleBinding for incident-service
    └── networkpolicy.yaml    # baseline allow rules for SOC ns
```

ArgoCD Application then switches to a chart source with valuesObject:

```yaml
spec:
  source:
    repoURL: oci://registry-1.docker.io/johnnaeder/charts
    chart: zt-soc
    targetRevision: "0.2.0"
    helm:
      valuesObject:
        incidentService:
          image: docker.io/johnnaeder/zt-incident-service:v0.2
        webConsole:
          image: docker.io/johnnaeder/zt-web-console:v0.2
```

## Tekton private registry — already supported

`platform/tekton/pipelines/build-and-deploy.yaml` accepts any image
reference. To push private:

1. Create a Docker Hub access token at https://hub.docker.com/settings/security
2. Create the secret:

   ```bash
   kubectl create secret -n tekton-pipelines docker-registry docker-credentials \
     --docker-server=https://index.docker.io/v1/ \
     --docker-username=johnnaeder \
     --docker-password='<token>'
   ```

3. Attach to ServiceAccount used by the Pipeline:

   ```bash
   kubectl -n tekton-pipelines patch sa default --type=merge \
     -p '{"secrets":[{"name":"docker-credentials"}]}'
   ```

4. Run:

   ```bash
   tkn pipeline start build-and-deploy \
     --param repo-url=git@github.com:john-naeder/K8sSecDaP-soc.git \
     --param revision=main \
     --param image=docker.io/johnnaeder/zt-incident-service:v0.3 \
     --param context=services/incident-service \
     --param manifest-path=manifests/35-incident-service.yaml \
     --workspace name=shared-workspace,volumeClaimTemplateFile=...
   ```

The Pipeline already calls `update-manifest` to bump the image tag in
the GitOps repo and push the commit back; ArgoCD then rolls the
Deployment forward.
