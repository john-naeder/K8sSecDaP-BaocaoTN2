# K8sSecDaP — Port-scan K8s Detect-and-Prevent

Umbrella repo gom các thành phần qua **git submodules**. Clone kèm submodule:

```bash
git clone --recurse-submodules git@github.com:john-naeder/K8sSecDaP-BaocaoTN2.git
# hoặc sau khi clone:
make submodules
```

## Submodules
| Path | Repo | Nội dung |
|------|------|----------|
| `infra/` | K8sSecDaP-infra | Ansible IaC dựng cluster (Calico CNI) |
| `deploy/` | K8sSecDaP-deploy | GitOps: ArgoCD/Tekton/Traefik/Cloudflare + Calico manifests |
| `soc/` | K8sSecDaP-soc | SOC services (Go/Python) + K8s manifests (`manifests/`, `data/`) |
| `pipeline/` | K8sSecDaP-pipeline | Detection engine C++ (`zt-pipeline`) + libdsa |
| `collector/` | K8sSecDaP-collector | eBPF collector (kprobe tcp_connect) |
| `report/` | K8sSecDaP-report | Báo cáo đồ án (LaTeX) |

## Misc (trong umbrella, không phải submodule)
`docs/` `scripts/` `tools/` `config/` `visualization/`

## Build
- C++ (pipeline + tools): `make build` → `build/bin/{zt-pipeline,trace_algorithms}`
- Pipeline container image: `docker build -f Dockerfile.pipeline -t <img> .` (cần submodule đã checkout)
- Thesis: `make report`
