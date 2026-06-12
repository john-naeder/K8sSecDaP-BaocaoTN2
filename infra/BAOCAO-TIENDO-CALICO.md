# Báo cáo tiến độ: Dựng lại cluster với Calico CNI

**Ngày:** 2026-05-28
**Phạm vi:** Dựng mới hoàn toàn K8s cluster (Calico thay Flannel) + dọn config app demo + tái cấu trúc thư mục.

---

## 1. Lý do đổi công nghệ: Flannel → Calico

Flannel là một CNI plugin tối giản, chỉ tập trung vào lớp kết nối mạng overlay (pod-to-pod
connectivity) và **không hỗ trợ Kubernetes NetworkPolicy** một cách native — bản thân
Flannel không có thành phần policy engine để thực thi (enforce) các luật cô lập lưu lượng
giữa các pod. Trong khi đó, đề tài hướng tới mô hình **Zero-Trust** cho mạng nội bộ cluster,
yêu cầu khả năng định nghĩa và thực thi chính sách mạng chi tiết (ai được nói chuyện với ai,
trên cổng nào) ở tầng pod.

Do đó tech stack được chuyển sang **Calico** vì:

- **Hỗ trợ NetworkPolicy đầy đủ** — Calico có Felix dataplane thực thi cả Kubernetes
  `NetworkPolicy` chuẩn lẫn `CalicoNetworkPolicy`/`GlobalNetworkPolicy` mở rộng (L3–L4,
  và L5–L7 nếu cần), phù hợp trực tiếp với yêu cầu Zero-Trust của đề tài.
- **Kiểm soát chi tiết hơn** — hỗ trợ rule theo namespace, label selector, CIDR, port,
  hướng ingress/egress; cho phép mặc định "deny-all" rồi mở dần (least-privilege).
- **Tương thích hạ tầng hiện có** — vẫn dùng VXLAN overlay qua Tailscale như Flannel,
  nên không phá vỡ mô hình mạng inter-node sẵn có.

Tóm lại: Flannel đủ cho "cho pod nói chuyện được với nhau", nhưng **không đáp ứng được
yêu cầu thực thi network policy** của mô hình Zero-Trust — đây là lý do bắt buộc đổi sang Calico.

---

## 2. Các công việc đã thực hiện

### 2.1. Tái cấu trúc thư mục
- Di chuyển `old_cluster_cleared_out_config/infra-iac/{ansible,bootstrap}` → `deploy/infra/`.
- Xóa toàn bộ `old_cluster_cleared_out_config/` (phần `gitops-manifests/`,
  `platform-automation/`, `helmfile/` cũ đã được thay thế bằng `deploy/platform/` hiện hành).

### 2.2. Chuyển Flannel → Calico (Ansible)
- Tạo role mới `deploy/infra/ansible/roles/cni_calico/` — gọi `deploy/cni/calico/install-calico.sh`
  (Tigera Operator + Installation CR), chờ rollout, verify MTU và node IP.
- Xóa role `cni_flannel/`.
- Sửa playbook `master.yml`, `site.yml`, `reset.yml`: thay `cni_flannel` → `cni_calico`,
  cập nhật iptables cleanup từ chain `FLANNEL-` sang `cali-/CALI-`.
- `inventory/group_vars/all/network.yml`: đổi cổng firewall VXLAN `8472/udp` (Flannel) →
  `4789/udp` (Calico).
- `inventory/group_vars/all/versions.yml`: bỏ `flannel_manifest_url`, thêm `calico_version: v3.28.2`.
- Cập nhật `README.md` của ansible.

### 2.3. Dọn config app demo
- Bỏ 2 trigger `github-push-demo-api` và `github-push-demo-worker` trong
  `deploy/platform/tekton/triggers/eventlistener.yaml`.
- Xóa `deploy/platform/tekton/triggers/trigger-template.yaml` (template riêng cho demo-api).
- (Các app `scholarhub-*` đã không còn trong `deploy/platform/argocd/apps/` từ trước.)

### 2.4. Apply lên cluster
- Chạy `make setup` (ansible-playbook `site.yml`) lên 3 node qua Tailscale.
- Kết quả: cluster dựng thành công với Calico.

---

## 3. Sự cố gặp phải và cách xử lý

| # | Sự cố | Nguyên nhân | Cách xử lý |
|---|-------|-------------|------------|
| 1 | Verify MTU thất bại (`computedMTU` rỗng) | Sai tên field trong status của Installation CR Calico v3.28 | Đổi jsonpath sang `.status.mtu`, thêm `retries/until` chờ operator điền giá trị |
| 2 | `install-calico.sh` phá hủy cluster khi chạy lại | Script dùng `kubectl create \|\| replace --force` → khi re-run sẽ **xóa toàn bộ namespace + CRD** rồi treo 5 phút | Sửa script sang `kubectl apply --server-side --force-conflicts` (idempotent, an toàn, xử lý được CRD lớn) |
| 3 | Verify node IP thất bại (`unrecognized identifier InternalIP`) | Ansible `command` module dùng `shlex.split` nuốt mất dấu nháy kép trong jsonpath | Bọc biểu thức jsonpath bằng dấu nháy đơn `'...'` |
| 4 | Pod kẹt `ContainerCreating` — lỗi `cannot find a qualified ippool` | Installation CR bị treo trạng thái `Terminating` (di chứng từ lần `replace --force` ở sự cố #2), operator bỏ qua việc tạo IPPool | Gỡ finalizer trên Installation CR, force-finalize các namespace `calico-system`/`calico-apiserver` kẹt Terminating, chạy lại `install-calico.sh`, restart pod operator để reconcile lại |

**Bài học rút ra:** không bao giờ dùng `kubectl replace --force` cho manifest tigera-operator
trong quy trình tự động — luôn dùng `apply --server-side`. Lỗi gốc (#2) đã được sửa trực tiếp
trong `deploy/cni/calico/install-calico.sh` nên các lần dựng cluster sau sẽ không tái diễn
chuỗi sự cố #2 → #4.

---

## 4. Trạng thái cluster hiện tại

```
NAME                  STATUS     ROLES           VERSION    CNI
userver-master        Ready      control-plane   v1.32.13   ✓ Calico
userver-worker-1      Ready      <none>          v1.32.13   ✓ Calico
userver-worker-home   NotReady   <none>          v1.32.13   (node đã được shutdown chủ động)
```

- **2/3 node Ready** với Calico hoạt động đầy đủ.
- MTU computed = **1230** (khớp Tailscale overlay 1280 − 50 byte VXLAN header).
- IPPool `10.244.0.0/16` encapsulation VXLAN, NAT outgoing — hoạt động (pod được cấp IP,
  ví dụ `pingtest-1` nhận `10.244.204.132`).
- Không còn namespace `kube-flannel` — cluster sạch Flannel.
- `userver-worker-home`: đã được shutdown chủ động, không cần xử lý.

---

## 5. Việc cần làm tiếp theo

1. **Commit các thay đổi** (hiện đang dirty trong git):
   - `deploy/infra/` (mới)
   - `deploy/cni/calico/{calico-custom.yaml, install-calico.sh}` (đã sửa)
   - `deploy/platform/tekton/triggers/{eventlistener.yaml, trigger-template.yaml}` (sửa/xóa)
2. **Bootstrap lớp platform** — chạy helmfile bootstrap rồi apply ArgoCD app-of-apps
   (`deploy/platform/helmfile/helmfile-bootstrap.yaml` → `deploy/platform/argocd/app-of-apps.yaml`).
3. **Kiểm chứng NetworkPolicy** — đây là mục tiêu chính của việc đổi sang Calico: viết và
   apply một vài `NetworkPolicy` mẫu (default deny-all + allow có chọn lọc) để xác nhận Calico
   thực thi đúng (có thể dùng lại ý tưởng `scripts/test-networkpolicy.sh` nếu còn).
4. **Triển khai app mới** (`pipeline/`, `ebpf-collector/`) — phase riêng, tạo manifest deploy
   (đã có sẵn một phần trong `deploy/soc/`) và wire vào ArgoCD/Tekton sau khi platform ổn định.
5. **(Tùy chọn) Khôi phục `userver-worker-home`** khi cần — chỉ cần bật node lên rồi chạy
   `make worker NODE=userver-worker-home`, role `worker_join` đã idempotent.
