# Ánh xạ bài toán lý thuyết sang vấn đề thực tế
# Mapping Theoretical Problems to Real-World Network Security

---

## 1. Tổng quan ánh xạ (Mapping Overview)

Giai đoạn 1-2 đã hình thức hóa 4 bài toán DSA trừu tượng. Giai đoạn 3 đã implement và benchmark các giải thuật. Tài liệu này **ánh xạ** (map) từng bài toán lý thuyết sang vấn đề bảo mật mạng cụ thể trong Kubernetes.

```
Lý thuyết (Abstract)                    Thực tế (Concrete)
┌──────────────────────┐                ┌──────────────────────────────┐
│ Stream Frequency     │ ──────────────>│ Port Scan Detection          │
│ Estimation           │                │ (Count-Min Sketch)           │
├──────────────────────┤                ├──────────────────────────────┤
│ Dynamic Graph SCC    │ ──────────────>│ Architecture Violation       │
│ Detection            │                │ Detection (Tarjan)           │
├──────────────────────┤                ├──────────────────────────────┤
│ K-Hop Reachability   │ ──────────────>│ Blast Radius Analysis        │
│ (BFS)                │                │ (Lateral Movement Impact)    │
├──────────────────────┤                ├──────────────────────────────┤
│ Longest Prefix Match │ ──────────────>│ IP Classification            │
│ (LPM Trie)           │                │ (Internal vs External)       │
└──────────────────────┘                └──────────────────────────────┘
```

---

## 2. Ánh xạ chi tiết (Detailed Mapping)

### 2.1. Stream Frequency Estimation → Port Scan Detection

#### Bài toán lý thuyết (Theoretical Problem)

> Cho luồng $S = (a_1, a_2, \ldots, a_m)$ từ universe $U$, ước lượng tần suất $f_x$ trong bộ nhớ bounded sao cho $|\hat{f}_x - f_x| \le \varepsilon \cdot m$ với xác suất $\ge 1 - \delta$.

#### Ánh xạ sang thực tế (Real-World Mapping)

| Khái niệm lý thuyết | Thực tế K8s |
|---|---|
| Stream $S$ | Luồng TCP connection events từ eBPF Ring Buffer |
| Phần tử $a_j$ | Cặp `(source_ip, dest_ip)` hoặc `source_ip` |
| Universe $U$ | Không gian IP addresses ($2^{32}$ cho IPv4) |
| Tần suất $f_x$ | Số connections từ IP $x$ trong time window |
| Ngưỡng anomaly | $f_x > \text{threshold}$ → port scan detected |
| Bộ nhớ bounded | CMS 40-80 KB, không phụ thuộc số IPs |

#### Tại sao CMS phù hợp (Why CMS Fits)

**Vấn đề thực tế:** Một Pod bình thường kết nối tới 3-5 services cố định. Nếu một Pod kết nối tới 100+ IPs khác nhau trong 5 phút → đang scan mạng.

**Tại sao không dùng hash map:**
- Attacker có thể spoof hàng triệu source IPs → hash map bùng nổ bộ nhớ
- Hệ thống monitoring **phải ổn định ngay cả khi đang bị tấn công**

**CMS giải quyết:**
- Bộ nhớ **cố định 40 KB** bất kể số IPs
- Overestimate → **không bỏ sót** attacker (false positive chấp nhận được, false negative không)
- Update $O(d)$ ≈ 72ns per event → xử lý 14M events/s (benchmark GĐ3)

#### Pipeline thực tế (Actual Pipeline)

```
Kernel:
    tcp_v4_connect triggered
    → LPM: dst_ip ∈ K8s? → Yes
    → Ring Buffer: emit {src_ip, dst_ip, dst_port, ts}

User-space:
    Read event from Ring Buffer
    → CMS.record(src_ip)                    // đếm connections per source
    → estimate = CMS.estimate(src_ip)
    → if estimate > THRESHOLD (100):
        → ALERT: "Port scan detected from {src_ip}"
        → Mark src_ip as malicious
        → Trigger BFS blast radius analysis
```

#### Tham số thực tế (Practical Parameters)

| Tham số | Giá trị | Lý do |
|---|---|---|
| CMS width | 2048 | Overcounting trung bình ~41 (từ benchmark) |
| CMS depth | 5 | Xác suất thất bại $\delta \le e^{-5} \approx 0.67\%$ |
| Time window | 5 phút (5 buckets × 1 phút) | Phát hiện scan trong khoảng ngắn |
| Threshold | 100 connections/window | Pod bình thường < 20 connections/5min |
| Memory | 2048 × 5 × 8 = **80 KB** | Negligible |

---

### 2.2. Dynamic Graph SCC → Architecture Violation Detection

#### Bài toán lý thuyết (Theoretical Problem)

> Cho đồ thị có hướng $G = (V, E)$ thay đổi theo thời gian, duy trì phân hoạch SCC. Phát hiện khi SCC mới xuất hiện hoặc SCCs merge.

#### Ánh xạ sang thực tế (Real-World Mapping)

| Khái niệm lý thuyết | Thực tế K8s |
|---|---|
| Đỉnh $v \in V$ | Pod IP hoặc Service endpoint |
| Cung $(u, v) \in E$ | TCP connection từ Pod $u$ đến Pod $v$ |
| SCC $C_i$ | Cụm services giao tiếp vòng tròn (chu trình) |
| SCC bất thường | Vi phạm kiến trúc: frontend ↔ database direct |
| Condensation DAG | Topology mong đợi (hierarchical, no cycles) |

#### Tại sao Tarjan phù hợp (Why Tarjan Fits)

**Vấn đề thực tế:** Kiến trúc microservices thiết kế theo hướng **hierarchical** (phân tầng):

```
Expected (DAG — no SCCs with >1 vertex):
    Frontend → Backend → Database
                      → Cache

Anomalous (SCC detected — cycle):
    Frontend → Backend → Database
       ↑                    │
       └────────────────────┘    ← SCC {Frontend, Backend, Database}
```

Nếu Tarjan phát hiện SCC chứa > 1 đỉnh mà **không nằm trong whitelist** → architecture violation.

**Tại sao dùng Tarjan thay vì Kosaraju:**
- Single-pass: chạy nhanh hơn 1.5-1.7x (benchmark GĐ3)
- Không cần xây đồ thị ngược: tiết kiệm bộ nhớ
- Chạy periodic mỗi 10 giây → Tarjan trên đồ thị ~1000 nodes: < 2ms

#### Pipeline thực tế (Actual Pipeline)

```
User-space (mỗi 10 giây):
    1. graph = snapshot đồ thị hiện tại
    2. sccs = Tarjan.find_sccs(graph)
    3. for each scc in sccs:
         if |scc| > 1 AND scc ∉ WHITELIST:
             → ALERT: "Architecture violation: unexpected SCC {scc}"
             → Log: which pods are in the cycle
```

#### Whitelist mẫu (Example Whitelist)

```yaml
# Các SCC hợp lệ (expected bidirectional communication)
allowed_sccs:
  - [backend-api, backend-worker]      # Workers callback to API
  - [redis-master, redis-sentinel]      # Redis replication
```

---

### 2.3. K-Hop Reachability → Blast Radius Analysis

#### Bài toán lý thuyết (Theoretical Problem)

> Cho $G = (V, E)$, đỉnh nguồn $s$, giới hạn $k$. Tìm $R_k(s) = \{v : d(s, v) \le k\}$.

#### Ánh xạ sang thực tế (Real-World Mapping)

| Khái niệm lý thuyết | Thực tế K8s |
|---|---|
| Đỉnh nguồn $s$ | Pod bị compromise (detected by CMS) |
| Giới hạn $k$ | Số bước lateral movement tối đa |
| $R_k(s)$ | **Blast radius** — tập Pods có nguy cơ |
| Layer $i$ | Mức độ nguy cơ: $i=1$ cao, $i=2$ trung bình, $i=3$ thấp |
| $|R_k(s)|$ | Quy mô thiệt hại tiềm tàng |

#### Tại sao BFS phù hợp (Why BFS Fits)

**Vấn đề thực tế:** Khi CMS phát hiện một Pod đang scan mạng, cần biết ngay:
1. Pod này **đã kết nối tới những Pods nào** (layer 1)?
2. Những Pods đó **tiếp tục kết nối tới đâu** (layer 2)?
3. Phạm vi ảnh hưởng tối đa nếu attacker di chuyển ngang?

**BFS natural layering khớp hoàn hảo:**

```
BFS từ malicious Pod (k=3):

Layer 0: malicious-pod                → SOURCE (isolate immediately)
Layer 1: [pod-A, pod-B, pod-C]       → HIGH RISK (audit + monitor)
Layer 2: [pod-D, pod-E]              → MEDIUM RISK (monitor)
Layer 3: [pod-F, database-pod]       → LOW RISK (alert if database)
```

**Tại sao không Dijkstra:** Đồ thị mạng K8s là unweighted (connection tồn tại hoặc không). BFS nhanh hơn 2x (benchmark GĐ3).

#### Pipeline thực tế (Actual Pipeline)

```
Khi CMS phát hiện malicious_ip:
    1. node = find_pod_by_ip(malicious_ip)
    2. blast = BFS.reachable_within(graph, node, k=3)
    3. Output alert:
       {
         "type": "blast_radius",
         "source": "malicious-pod",
         "timestamp": "2026-04-08T14:30:00Z",
         "layers": {
           "1_high_risk": ["pod-A", "pod-B"],
           "2_medium_risk": ["pod-D"],
           "3_low_risk": ["pod-F"]
         },
         "total_at_risk": 4,
         "contains_critical": true,  // database pod in blast radius
         "recommendation": "Isolate malicious-pod, audit layer-1 pods"
       }
```

#### Kết hợp với NetworkPolicy (Integration with NetworkPolicy)

Blast radius analysis có thể **tự động sinh NetworkPolicy** để cách ly:

```yaml
# Auto-generated: isolate compromised pod
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: quarantine-malicious-pod
spec:
  podSelector:
    matchLabels:
      app: malicious-pod
  policyTypes:
    - Ingress
    - Egress
  # Empty rules = deny all traffic
```

---

### 2.4. Longest Prefix Match → IP Classification at Kernel Level

#### Bài toán lý thuyết (Theoretical Problem)

> Cho tập tiền tố $P$ và key $x$, tìm tiền tố dài nhất $p_j \sqsubseteq x$.

#### Ánh xạ sang thực tế (Real-World Mapping)

| Khái niệm lý thuyết | Thực tế K8s |
|---|---|
| Tập tiền tố $P$ | CIDR ranges của K8s cluster |
| Key $x$ | Destination IP address |
| Nhãn $\ell_j$ | Classification: pod / service / external |
| Lookup time $O(32)$ | ~100ns trong kernel (eBPF LPM Trie) |

#### Tại sao LPM Trie phù hợp (Why LPM Trie Fits)

**Vấn đề thực tế:** Mỗi TCP connection, cần phân loại ngay tại kernel:
- **Internal (pod network):** Emit event lên user-space → phân tích
- **Internal (service network):** Emit event, resolve ClusterIP → actual pod
- **External:** Skip — không cần monitor (hoặc monitor riêng cho data exfiltration)

**Lọc tại kernel giảm tải user-space:**

```
Không dùng LPM (user-space filter):
    ALL tcp_v4_connect events → Ring Buffer → User-space checks each
    = ~50K events/sec trên cluster bận → user-space overloaded

Dùng LPM (kernel filter):
    tcp_v4_connect → LPM lookup (100ns) → chỉ internal → Ring Buffer
    = ~5K events/sec (chỉ K8s internal) → user-space dư sức xử lý
```

→ Giảm 10x khối lượng events cho user-space.

#### Cấu hình LPM cho K8s cluster 3 nodes (Configuration)

```yaml
# pipeline.yaml - LPM prefixes
ip_classifier:
  name: "lpm_trie"
  params:
    prefixes:
      - cidr: "10.244.0.0/16"    # Pod network (Flannel default)
        label: 1                  # LABEL_POD_NETWORK
      - cidr: "10.96.0.0/12"     # Service network
        label: 2                  # LABEL_SERVICE_NETWORK
      - cidr: "192.168.1.0/24"   # Node network
        label: 3                  # LABEL_NODE_NETWORK
```

Cấu hình này được load từ YAML → populate vào `BPF_MAP_TYPE_LPM_TRIE` trong kernel.

---

## 3. Luồng xử lý tổng thể (End-to-End Pipeline)

### 3.1. Pipeline hoàn chỉnh (Complete Pipeline)

```
┌──────────────────────────── KERNEL SPACE ────────────────────────────┐
│                                                                      │
│  tcp_v4_connect()                                                   │
│       │                                                              │
│       ▼                                                              │
│  ┌─────────────────┐     ┌──────────────────────────┐               │
│  │ Extract:        │     │ LPM Trie Lookup          │               │
│  │  src_ip, dst_ip │────>│ dst_ip ∈ K8s internal?   │               │
│  │  dst_port, pid  │     └──────────┬───────────────┘               │
│  │  timestamp      │               │                                │
│  └─────────────────┘          Yes ──┤── No → return 0 (skip)        │
│                                     │                                │
│                                     ▼                                │
│                          ┌──────────────────┐                       │
│                          │ Ring Buffer       │                       │
│                          │ emit event_t      │                       │
│                          └────────┬─────────┘                       │
└───────────────────────────────────┼──────────────────────────────────┘
                                    │
┌───────────────────────────────────┼──────────────────────────────────┐
│                            USER SPACE                                │
│                                    │                                 │
│                                    ▼                                 │
│                          ┌──────────────────┐                       │
│                          │ Event Consumer    │                       │
│                          │ (read ring buf)   │                       │
│                          └────────┬─────────┘                       │
│                                   │                                  │
│              ┌────────────────────┼────────────────────┐            │
│              │                    │                    │             │
│              ▼                    ▼                    ▼             │
│  ┌───────────────────┐  ┌─────────────────┐  ┌──────────────┐     │
│  │ Count-Min Sketch  │  │ Graph Builder   │  │ Alerts       │     │
│  │ record(src_ip)    │  │ add_edge(src,   │  │ Manager      │     │
│  │                   │  │          dst)   │  │              │     │
│  │ if estimate >     │  │                 │  │              │     │
│  │   THRESHOLD:      │  │ Every 10s:      │  │              │     │
│  │   → mark_suspect  │  │  Tarjan.sccs()  │  │              │     │
│  │   → trigger BFS   │  │  → check vs     │  │              │     │
│  │                   │  │    whitelist     │  │              │     │
│  └────────┬──────────┘  └────────┬────────┘  └──────┬───────┘     │
│           │                      │                   │              │
│           │    ┌─────────────────┘                   │              │
│           │    │                                     │              │
│           ▼    ▼                                     ▼              │
│  ┌──────────────────┐                      ┌─────────────────┐    │
│  │ BFS Blast Radius │                      │ Output:         │    │
│  │ reachable_within │─────────────────────>│  graph.json     │    │
│  │ (graph, suspect, │                      │  alerts.json    │    │
│  │  k=3)            │                      └────────┬────────┘    │
│  └──────────────────┘                               │              │
│                                                      │              │
└──────────────────────────────────────────────────────┼──────────────┘
                                                       │
                                              ┌────────▼────────┐
                                              │ Visualization   │
                                              │ Python + NetworkX│
                                              │ + Matplotlib    │
                                              └─────────────────┘
```

### 3.2. Bảng ánh xạ tổng hợp (Complete Mapping Table)

| # | Bài toán lý thuyết | Giải thuật | Vấn đề thực tế | Input thực tế | Output thực tế | Benchmark |
|---|---|---|---|---|---|---|
| 1 | Stream Frequency Estimation | Count-Min Sketch | Port scan detection | TCP connect events | Alert khi IP vượt ngưỡng | 14M ops/s, 80KB |
| 2 | Dynamic Graph SCC | Tarjan's Algorithm | Architecture violation | Connection graph | SCCs bất thường | 1.2ms/1K vertices |
| 3 | K-Hop Reachability | BFS | Blast radius | Malicious node + graph | Layered risk assessment | 0.75μs/query |
| 4 | Longest Prefix Match | LPM Trie (Patricia) | IP classification | Dest IP | Internal/External label | 85-259ns/lookup |

### 3.3. Tương tác giữa các module (Module Interactions)

```
Thời gian thực (per-event, ~microseconds):
    LPM → filter event
    CMS → count frequency
    Graph → add edge

Periodic (mỗi 10 giây):
    Tarjan → detect SCCs

On-demand (khi CMS alert):
    BFS → calculate blast radius

Output (continuous):
    graph.json → visualization
    alerts.json → dashboard / webhook
```

---

## 4. So sánh với giải pháp hiện có (Comparison with Existing Solutions)

| Giải pháp | Approach | Overhead | Detection | Học thuật |
|---|---|---|---|---|
| **Cilium Hubble** | eBPF flow logs | Medium | Chỉ monitoring, không anomaly detect | Không |
| **Falco** | Syscall monitoring | High | Rule-based, không graph analysis | Không |
| **Istio** | Service mesh proxy | **Rất cao** (sidecar per pod) | mTLS, traffic policy | Không |
| **Calico** | NetworkPolicy enforcement | Low | Prevention only, không detection | Không |
| **Dự án này** | eBPF + DSA | **Rất thấp** (kernel-level) | CMS + SCC + BFS (algorithmic) | **Có** — DSA formalized |

**Điểm khác biệt chính:**
1. **DSA-driven detection:** Sử dụng thuật toán đã được chứng minh (proven algorithms) thay vì heuristics
2. **Kernel-level efficiency:** LPM filtering ngay tại kernel, không overhead sidecar
3. **Blast radius analysis:** Tính năng độc đáo — không giải pháp nào ở trên cung cấp
4. **Modular DSA library:** Có thể swap algorithms via config → nghiên cứu so sánh

---

## 5. Tham khảo (References)

1. **MITRE ATT&CK.** "Lateral Movement (TA0008)." https://attack.mitre.org/tactics/TA0008/
2. **NIST SP 800-207.** (2020). "Zero Trust Architecture."
3. **Kubernetes Documentation.** "Network Policies." https://kubernetes.io/docs/concepts/services-networking/network-policies/
4. **Rice, L.** (2022). *Security Observability with eBPF*. O'Reilly.
5. **Cormode, G. & Muthukrishnan, S.** (2005). "An Improved Data Stream Summary: The Count-Min Sketch."
6. **Tarjan, R. E.** (1972). "Depth-First Search and Linear Graph Algorithms."
7. **Cilium Documentation.** "Hubble - Network Observability." https://docs.cilium.io/en/stable/observability/hubble/
