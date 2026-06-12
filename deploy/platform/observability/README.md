# Observability layer

Bổ sung 3 thành phần quan sát hệ thống bên cạnh kube-prometheus-stack
(`platform/argocd/apps/monitoring.yaml`) đã có sẵn (Prometheus + Grafana +
Alertmanager). Tất cả đều là workload nhẹ, phù hợp cụm 2 node.

| Thành phần | ArgoCD App | Namespace | Vai trò |
|---|---|---|---|
| metrics-server | `apps/metrics-server.yaml` | `kube-system` | Resource Metrics API cho `kubectl top` + HPA |
| Grafana dashboards (zt-soc) | `apps/grafana-dashboards.yaml` | `monitoring` | Dashboard ZT-SOC, import qua Grafana sidecar |
| Uptime Kuma | `apps/uptime-kuma.yaml` | `monitoring` | Status page / blackbox uptime |

ArgoCD root (`platform/argocd/app-of-apps.yaml`) đọc cả thư mục
`platform/argocd/apps/` → 3 App mới tự được tạo, không cần sửa app-of-apps.

## Thứ tự sync

- `metrics-server` độc lập, sync bất kỳ lúc nào.
- `grafana-dashboards` và `uptime-kuma` cần namespace `monitoring` (do
  kube-prometheus-stack tạo) → sync `monitoring` trước.

## Cách verify (sau khi node lên + đã sync)

```sh
# Resource Metrics API hoạt động
kubectl top nodes
kubectl top pods -n soc

# Dashboard: mở Grafana qua Cloudflare Tunnel (grafana.zt.local),
# tìm dashboard "ZT-SOC Overview" + "ZT-SOC Incidents".

# Uptime Kuma: mở uptime.zt.local, tạo monitor cho web-console / argocd qua UI.
```

## Ghi chú

- metrics-server bật `--kubelet-insecure-tls` vì kubelet trên cụm kubeadm dùng
  cert self-signed; thiếu cờ này sẽ lỗi `x509`.
- Dashboard JSON dùng cùng metric mà incident-service (`:9101`) và aggregator
  (`:9100`) thực sự export (xem `K8sSecDaP-soc/manifests/grafana/`).
- Monitor của Uptime Kuma cấu hình thủ công qua UI; state lưu trên PVC 1Gi
  (`local-path`) nên giữ được sau restart.
- Route `uptime.zt.local` đã thêm vào `platform/cloudflared/config.example.yaml`.
