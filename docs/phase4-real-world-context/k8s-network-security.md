# Bảo mật mạng trong Kubernetes
# Network Security in Kubernetes

---

## 1. Mô hình mạng Kubernetes (Kubernetes Network Model)

### 1.1. Nguyên tắc cơ bản (Fundamental Principles)

Kubernetes áp dụng một mô hình mạng **flat** (phẳng) với 3 nguyên tắc:

1. **Mọi Pod có thể giao tiếp với mọi Pod khác** trên bất kỳ node nào, **không cần NAT** (Network Address Translation)
2. **Agents trên node** (kubelet, kube-proxy) có thể giao tiếp với tất cả Pods trên node đó
3. **Mỗi Pod có IP riêng** — Pods không chia sẻ IP (mỗi Pod có network namespace riêng)

Mô hình này đơn giản hóa phát triển ứng dụng nhưng tạo ra **bề mặt tấn công rộng** (large attack surface) — mặc định, mọi Pod đều có thể kết nối tới mọi Pod khác.

### 1.2. Các dải mạng trong K8s (Network Ranges)

| Dải mạng | CIDR mặc định (ví dụ) | Mô tả |
|---|---|---|
| **Pod Network** | `10.244.0.0/16` | IP của các Pods — mỗi Pod nhận 1 IP từ dải này |
| **Service Network** | `10.96.0.0/12` | ClusterIP của Services — virtual IPs, NAT bởi kube-proxy |
| **Node Network** | `192.168.1.0/24` | IP của các worker nodes |
| **External** | Mọi thứ khác | Traffic ra/vào cluster |

**Ý nghĩa cho LPM Trie:** Cấu hình LPM Trie trong eBPF với các CIDR ranges trên để phân loại nhanh: IP đích thuộc pod network, service network, hay external.

### 1.3. CNI — Container Network Interface

**CNI** là plugin cung cấp networking cho Pods. Mỗi CNI implementation có cách triển khai flat network khác nhau:

| CNI | Cơ chế | eBPF support |
|---|---|---|
| **Flannel** | VXLAN overlay / host-gw | Không (iptables) |
| **Calico** | BGP routing / VXLAN | Có (eBPF dataplane) |
| **Cilium** | **Thuần eBPF** | Native — thay thế kube-proxy |
| **Weave** | VXLAN overlay | Không |

**Lưu ý:** Dự án này hoạt động ở tầng **trên CNI** — hook vào `tcp_v4_connect` bắt được mọi TCP connection bất kể CNI nào được sử dụng.

---

## 2. Các mối đe dọa bảo mật (Security Threats)

### 2.1. Lateral Movement — Di chuyển ngang

**Định nghĩa:** Sau khi xâm nhập được một Pod (qua vulnerability, misconfiguration, supply chain attack), attacker cố gắng **mở rộng phạm vi kiểm soát** bằng cách kết nối tới các Pods/Services khác trong cluster.

**Cơ chế:**

```
Attacker → Internet → Compromised Pod (CVE exploit)
                            │
                            ▼
                      ┌─────────────┐
                      │ Lateral     │
                      │ Movement    │
                      ├─────────────┤
                      │ 1. Scan internal network (port scan)
                      │ 2. Connect to database Pods
                      │ 3. Access secrets/configmaps
                      │ 4. Pivot to other namespaces
                      │ 5. Escalate to node-level access
                      └─────────────┘
```

**Tại sao K8s dễ bị lateral movement:**
- **Flat network mặc định:** Mọi Pod giao tiếp được với nhau → attacker có full network access
- **Pod IP thay đổi liên tục:** Pods được tạo/xóa liên tục → firewall rules theo IP không hiệu quả
- **Service discovery tự động:** DNS nội bộ (`*.svc.cluster.local`) cho phép attacker dễ dàng khám phá topology

### 2.2. Port Scanning — Quét cổng

**Định nghĩa:** Attacker gửi TCP SYN packets tới nhiều IP:port combinations để phát hiện services đang chạy.

**Đặc điểm trong K8s:**
- **Horizontal scan:** Quét cùng port trên nhiều IPs (tìm tất cả instances của một service)
- **Vertical scan:** Quét nhiều ports trên cùng IP (tìm tất cả services trên một Pod)
- **Tốc độ cao:** Trong internal network (low latency), scan có thể đạt hàng nghìn connections/giây

**Phát hiện:** Một Pod bình thường chỉ kết nối tới vài services cố định. Nếu một Pod kết nối tới hàng trăm IP:port khác nhau trong thời gian ngắn → **dấu hiệu port scan**.

→ **Count-Min Sketch** ước lượng tần suất kết nối per-source, phát hiện khi vượt ngưỡng.

### 2.3. Architecture Violation — Vi phạm kiến trúc

**Định nghĩa:** Trong kiến trúc microservices, communication pattern thường tuân theo quy tắc:

```
Client → Frontend → Backend → Database
                             → Cache
```

Nếu bất ngờ xuất hiện:
- Frontend → Database (bypass Backend) → **vi phạm**
- Database → Frontend (reverse flow) → **bất thường**

Các kết nối bất thường này tạo thành **chu trình** (cycle) hoặc **SCC bất thường** trong đồ thị giao tiếp.

→ **Tarjan's Algorithm** phát hiện SCCs, so sánh với expected architecture.

### 2.4. Data Exfiltration — Trích xuất dữ liệu

**Định nghĩa:** Attacker lấy dữ liệu nhạy cảm từ cluster ra ngoài, thường qua:
- DNS tunneling
- HTTP/HTTPS requests tới external endpoints
- Encrypted channels tới C2 (Command & Control) servers

**Phát hiện:** Monitoring external connections — nếu một Pod backend (thường chỉ nhận requests, không gửi external) bất ngờ tạo outbound connections → cảnh báo.

→ **LPM Trie** phân loại traffic: internal vs external ngay tại kernel level.

---

## 3. Mô hình Zero-Trust trong K8s (Zero-Trust Model)

### 3.1. Nguyên tắc Zero-Trust (Zero-Trust Principles)

**"Never trust, always verify"** — Không tin tưởng bất kỳ kết nối nào mặc định, kể cả internal.

| Nguyên tắc | Áp dụng trong K8s |
|---|---|
| **Verify explicitly** | Mọi connection phải được kiểm tra, kể cả Pod-to-Pod |
| **Least privilege** | Pod chỉ được phép kết nối tới services cần thiết |
| **Assume breach** | Giả định rằng đã có Pod bị compromise → monitor mọi thứ |
| **Continuous validation** | Liên tục kiểm tra, không chỉ lúc thiết lập connection |

### 3.2. Network Policies — Chính sách mạng

Kubernetes hỗ trợ **NetworkPolicy** resource để giới hạn traffic:

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: backend-policy
spec:
  podSelector:
    matchLabels:
      app: backend
  policyTypes:
    - Ingress
    - Egress
  ingress:
    - from:
        - podSelector:
            matchLabels:
              app: frontend
  egress:
    - to:
        - podSelector:
            matchLabels:
              app: database
      ports:
        - port: 5432
```

**Vấn đề:** NetworkPolicy chỉ **ngăn chặn** (prevent) — không **phát hiện** (detect) hay **giám sát** (monitor). Nếu attacker exploit vulnerability trong allowed path (frontend → backend), NetworkPolicy không giúp gì.

### 3.3. Gap mà dự án này giải quyết (Gap This Project Addresses)

```
┌────────────────────────────────────────────────────────────┐
│                    Security Layers                          │
├─────────────────────┬──────────────────────────────────────┤
│ Prevention          │ NetworkPolicy, Service Mesh (Istio)  │
│ (Ngăn chặn)        │ → Chặn connections không được phép    │
├─────────────────────┼──────────────────────────────────────┤
│ Detection           │ ★ DỰ ÁN NÀY ★                      │
│ (Phát hiện)         │ → Giám sát real-time, phát hiện      │
│                     │   anomaly ngay cả trong allowed paths │
├─────────────────────┼──────────────────────────────────────┤
│ Response            │ Automated isolation, alerting         │
│ (Phản hồi)         │ → BFS blast radius → quarantine       │
└─────────────────────┴──────────────────────────────────────┘
```

**Dự án bổ sung tầng Detection:**
- **CMS** → phát hiện scan patterns (tần suất bất thường)
- **Tarjan SCC** → phát hiện architecture violations (chu trình bất thường)
- **BFS** → đánh giá blast radius (phạm vi ảnh hưởng)
- **LPM Trie** → phân loại traffic real-time (internal vs external)

---

## 4. Triển khai trên K8s (K8s Deployment)

### 4.1. DaemonSet — Chạy trên mọi node

eBPF program cần chạy ở **kernel level** của mỗi node. Cách triển khai: **DaemonSet** — đảm bảo mỗi node có đúng 1 Pod giám sát.

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: zero-trust-mapper
  namespace: monitoring
spec:
  selector:
    matchLabels:
      app: zt-mapper
  template:
    metadata:
      labels:
        app: zt-mapper
    spec:
      hostPID: true
      hostNetwork: true
      containers:
        - name: collector
          image: zt-mapper:latest
          securityContext:
            privileged: true        # Cần cho eBPF
          volumeMounts:
            - name: sys-kernel
              mountPath: /sys/kernel/debug
            - name: lib-modules
              mountPath: /lib/modules
              readOnly: true
      volumes:
        - name: sys-kernel
          hostPath:
            path: /sys/kernel/debug
        - name: lib-modules
          hostPath:
            path: /lib/modules
```

### 4.2. Luồng dữ liệu trên K8s (Data Flow on K8s)

```
Node 1                    Node 2                    Node 3
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│ eBPF Collector  │      │ eBPF Collector  │      │ eBPF Collector  │
│ Pod (DaemonSet) │      │ Pod (DaemonSet) │      │ Pod (DaemonSet) │
│                 │      │                 │      │                 │
│ kprobe hook     │      │ kprobe hook     │      │ kprobe hook     │
│ → LPM filter   │      │ → LPM filter   │      │ → LPM filter   │
│ → Ring Buffer   │      │ → Ring Buffer   │      │ → Ring Buffer   │
│ → Pipeline      │      │ → Pipeline      │      │ → Pipeline      │
│   (CMS+Graph)   │      │   (CMS+Graph)   │      │   (CMS+Graph)   │
└────────┬────────┘      └────────┬────────┘      └────────┬────────┘
         │                        │                        │
         └────────────┬───────────┴────────────────────────┘
                      │ alerts.json / graph.json
                      ▼
              ┌───────────────┐
              │ Visualization │
              │ (Central Pod) │
              │ Python + Dash │
              └───────────────┘
```

Mỗi node chạy pipeline **độc lập** (local CMS, local graph). Kết quả (alerts, graph snapshots) có thể aggregate bởi central visualization pod.

---

## 5. Mô phỏng tấn công (Attack Simulation)

### 5.1. Kịch bản test (Test Scenarios)

**Scenario 1: Port Scan Detection**

```bash
# Trên "attacker" pod:
kubectl run attacker --image=alpine --restart=Never -- sh
kubectl exec -it attacker -- apk add nmap
kubectl exec -it attacker -- nmap -sT -p 1-1000 10.244.1.5

# Expected: CMS phát hiện attacker IP vượt ngưỡng → alert
```

**Scenario 2: Lateral Movement**

```bash
# Tạo topology: frontend → backend → database
# Attacker compromise frontend, thử kết nối trực tiếp tới database
kubectl exec -it frontend -- curl http://database-service:5432

# Expected: Tarjan phát hiện SCC mới (frontend ↔ database)
# BFS từ frontend → blast radius bao gồm database
```

**Scenario 3: Normal Traffic (Baseline)**

```bash
# Traffic hợp lệ: frontend → backend → database
# Không trigger alert nào → validate false positive rate
```

---

## 6. Tham khảo (References)

1. **Kubernetes Documentation.** "Cluster Networking." https://kubernetes.io/docs/concepts/cluster-administration/networking/
2. **Kubernetes Documentation.** "Network Policies." https://kubernetes.io/docs/concepts/services-networking/network-policies/
3. **NIST SP 800-207.** (2020). "Zero Trust Architecture." National Institute of Standards and Technology.
4. **Gilman, E. & Barth, D.** (2017). *Zero Trust Networks*. O'Reilly Media.
5. **Rice, L.** (2022). *Security Observability with eBPF*. O'Reilly Media.
6. **Isovalent/Cilium.** "eBPF-based Networking, Security, and Observability." https://cilium.io
7. **MITRE ATT&CK.** "Lateral Movement." https://attack.mitre.org/tactics/TA0008/
