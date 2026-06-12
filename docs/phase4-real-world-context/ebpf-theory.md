# Lý thuyết eBPF — Extended Berkeley Packet Filter
# eBPF Theory — From Packet Filtering to Kernel Programmability

---

## 1. Tổng quan (Overview)

### 1.1. eBPF là gì? (What is eBPF?)

**eBPF (extended Berkeley Packet Filter)** là một công nghệ cho phép chạy các chương trình **sandboxed** trực tiếp bên trong **Linux kernel** mà không cần sửa đổi mã nguồn kernel hay nạp kernel module. Các chương trình eBPF được **verify** (kiểm tra tính an toàn) trước khi chạy, đảm bảo không gây crash hay vòng lặp vô hạn trong kernel.

eBPF biến Linux kernel từ một hệ thống **cố định** (fixed) thành một nền tảng **lập trình được** (programmable) — cho phép thêm logic xử lý tùy chỉnh tại các điểm chiến lược trong kernel mà không cần reboot hay nạp module mới.

### 1.2. Lịch sử phát triển (History)

| Năm | Sự kiện |
|---|---|
| 1992 | **BPF** (classic) ra đời tại Lawrence Berkeley Lab — chỉ lọc gói tin (`tcpdump`) |
| 2014 | **eBPF** (extended) merged vào Linux 3.18 — mở rộng ra ngoài packet filtering |
| 2016 | **XDP** (eXpress Data Path) — eBPF cho high-performance networking |
| 2017 | **BCC** (BPF Compiler Collection) — toolchain Python/C cho eBPF |
| 2018 | **libbpf** + **BTF** (BPF Type Format) — CO-RE (Compile Once, Run Everywhere) |
| 2020 | **Ring Buffer** (`BPF_MAP_TYPE_RINGBUF`) — Linux 5.8, thay thế perf buffer |
| 2022+ | eBPF trở thành nền tảng cho Cilium (K8s CNI), Falco (security), Pixie (observability) |

### 1.3. Kiến trúc tổng quan (Architecture Overview)

```
User Space                              Kernel Space
┌─────────────────────┐                ┌──────────────────────────────┐
│  User Application   │                │  Linux Kernel                │
│  (C++/Python/Go)    │                │                              │
│                     │   load BPF     │  ┌──────────────┐            │
│  libbpf / BCC  ─────┼───────────────>│  │ BPF Verifier │            │
│                     │                │  └──────┬───────┘            │
│                     │                │         │ verified           │
│                     │                │         ▼                    │
│                     │                │  ┌──────────────┐            │
│                     │                │  │ JIT Compiler │            │
│                     │                │  └──────┬───────┘            │
│                     │                │         │ native code        │
│                     │   read maps    │         ▼                    │
│  Ring Buffer  <─────┼────────────────│  ┌──────────────┐            │
│  Poll/Read         │                │  │ BPF Program  │            │
│                     │                │  │ (attached to │            │
│                     │                │  │  hook point) │            │
│                     │                │  └──────────────┘            │
│                     │                │         │                    │
│                     │                │         ▼                    │
│                     │                │  ┌──────────────┐            │
│                     │                │  │  BPF Maps    │            │
│                     │                │  │ (shared data)│            │
│                     │                │  └──────────────┘            │
└─────────────────────┘                └──────────────────────────────┘
```

---

## 2. Thành phần cốt lõi (Core Components)

### 2.1. BPF Program — Chương trình eBPF

Chương trình eBPF là một tập hợp **BPF instructions** (bytecode) được viết bằng C (restricted subset), biên dịch thành BPF bytecode bởi Clang/LLVM, sau đó được **JIT-compiled** thành native machine code bởi kernel.

**Đặc điểm:**
- **Event-driven:** Chương trình chạy khi một sự kiện xảy ra (syscall, network packet, function call, etc.)
- **Bounded execution:** Verifier đảm bảo chương trình luôn kết thúc (no infinite loops)
- **Limited stack:** Stack tối đa 512 bytes
- **No dynamic allocation:** Không có `malloc()` — sử dụng BPF Maps để lưu trữ

**Ví dụ chương trình eBPF đơn giản:**

```c
SEC("kprobe/tcp_v4_connect")
int BPF_KPROBE(trace_connect, struct sock *sk) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    u32 dst_ip = BPF_CORE_READ(sk, __sk_common.skc_daddr);
    u16 dst_port = BPF_CORE_READ(sk, __sk_common.skc_dport);

    struct event_t evt = {
        .pid = pid,
        .dst_ip = dst_ip,
        .dst_port = bpf_ntohs(dst_port),
    };

    bpf_ringbuf_output(&events, &evt, sizeof(evt), 0);
    return 0;
}
```

### 2.2. BPF Verifier — Bộ kiểm tra an toàn

Trước khi chương trình eBPF được phép chạy trong kernel, nó phải vượt qua **BPF Verifier** — một static analyzer trong kernel kiểm tra:

1. **Termination (Kết thúc):** Không có vòng lặp vô hạn — verifier giới hạn tổng số instructions (hiện tại: 1 triệu verified instructions)
2. **Memory safety (An toàn bộ nhớ):** Mọi truy cập bộ nhớ phải hợp lệ — không out-of-bounds, không null pointer dereference
3. **Type safety (An toàn kiểu):** Sử dụng BTF (BPF Type Format) để kiểm tra kiểu dữ liệu
4. **No unreachable code:** Không có code path nào không thể đạt được
5. **Stack bounds:** Stack usage ≤ 512 bytes

**Nếu verifier từ chối:** Chương trình không được nạp vào kernel → không bao giờ gây crash kernel.

### 2.3. BPF Maps — Cấu trúc dữ liệu chia sẻ

BPF Maps là cơ chế **lưu trữ và chia sẻ dữ liệu** giữa:
- BPF program ↔ BPF program (kernel-to-kernel)
- BPF program ↔ User-space application (kernel-to-user)

**Các loại BPF Map quan trọng cho dự án:**

| Map Type | Mô tả | Complexity | Ứng dụng |
|---|---|---|---|
| `BPF_MAP_TYPE_HASH` | Hash table key-value | O(1) lookup | Lưu trạng thái per-connection |
| `BPF_MAP_TYPE_ARRAY` | Fixed-size array | O(1) by index | Counters, configuration |
| `BPF_MAP_TYPE_LPM_TRIE` | Longest Prefix Match trie | O(key_len) | **IP classification** — phân loại IP thuộc subnet nào |
| `BPF_MAP_TYPE_RINGBUF` | Ring buffer (MPSC queue) | O(1) push | **Event delivery** — gửi events lên user-space |
| `BPF_MAP_TYPE_PERCPU_HASH` | Per-CPU hash table | O(1), no lock | High-frequency counters without contention |

### 2.4. Hook Points — Điểm gắn chương trình

eBPF programs được gắn vào các **hook points** khác nhau trong kernel:

| Hook Type | Mô tả | Ổn định API | Use Case |
|---|---|---|---|
| **Kprobe** | Hook vào bất kỳ kernel function nào | Thấp (internal API) | Debug, tracing, monitoring |
| **Kretprobe** | Hook vào return của kernel function | Thấp | Đo latency, capture return values |
| **Tracepoint** | Hook vào predefined stable points | **Cao** (stable ABI) | Production monitoring |
| **XDP** | Hook ngay khi NIC nhận packet | Cao | DDoS mitigation, load balancing |
| **TC (Traffic Control)** | Hook ở network queueing layer | Cao | Packet manipulation, policy |
| **LSM (Linux Security Module)** | Hook ở security checkpoints | Cao | Access control, audit |

**Cho dự án này:** Sử dụng **Kprobe** trên hàm `tcp_v4_connect` — hook vào mỗi lần một TCP connection được thiết lập.

---

## 3. Chi tiết kỹ thuật cho dự án (Technical Details for This Project)

### 3.1. Hook: `tcp_v4_connect`

**Hàm kernel:** `int tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)`

Hàm này được gọi mỗi khi một process thực hiện TCP `connect()` syscall trên IPv4. Tại đây ta có thể trích xuất:

| Thông tin | Cách lấy | Ý nghĩa |
|---|---|---|
| Source IP | `sk->__sk_common.skc_rcv_saddr` | IP nguồn (Pod IP) |
| Dest IP | `sk->__sk_common.skc_daddr` | IP đích (target Pod/Service) |
| Dest Port | `sk->__sk_common.skc_dport` | Port đích (service port) |
| PID | `bpf_get_current_pid_tgid() >> 32` | Process ID (identify container) |
| Timestamp | `bpf_ktime_get_ns()` | Thời điểm kết nối (nanoseconds) |

**Kprobe vs Tracepoint trade-off:**

| Tiêu chí | Kprobe (`kprobe/tcp_v4_connect`) | Tracepoint (`tcp:tcp_connect`) |
|---|---|---|
| **Availability** | Mọi kernel function | Chỉ predefined points |
| **Stability** | Có thể break khi kernel upgrade | **Stable ABI** — guaranteed |
| **Performance** | Slightly slower (int3 breakpoint) | Faster (nop sled) |
| **Information** | Full function arguments | Limited predefined fields |
| **Recommendation** | Dev/testing | Production |

**Cho dự án:** Dùng **Kprobe** vì cần truy cập trực tiếp `struct sock` để lấy IP/port — tracepoint `tcp:tcp_connect` có thể không expose đầy đủ thông tin.

### 3.2. LPM Trie trong eBPF — Phân loại IP tại Kernel

**Mục đích:** Ngay tại kernel, xác định IP đích thuộc K8s cluster (pod network, service network) hay external. Chỉ gửi events lên user-space cho internal traffic → giảm tải.

**Cấu trúc key:**

```c
struct lpm_key {
    __u32 prefixlen;    // prefix length in bits (e.g., 16 for /16)
    __u32 data;         // IP address in network byte order
};
```

**Tạo map:**

```c
struct {
    __uint(type, BPF_MAP_TYPE_LPM_TRIE);
    __type(key, struct lpm_key);
    __type(value, __u32);           // label: 1=pod, 2=service, 0=unknown
    __uint(max_entries, 1024);
    __uint(map_flags, BPF_F_NO_PREALLOC);   // BẮT BUỘC cho LPM Trie
} lpm_map SEC(".maps");
```

**Lookup trong eBPF program:**

```c
struct lpm_key lookup_key = {
    .prefixlen = 32,            // lookup full IP
    .data = dst_ip,             // network byte order
};

__u32 *label = bpf_map_lookup_elem(&lpm_map, &lookup_key);
if (label && *label > 0) {
    // Internal K8s traffic → emit event
    // ...
} else {
    // External traffic → skip
    return 0;
}
```

**Populate từ user-space (Python/C++):**

```python
# Using BCC
lpm = b["lpm_map"]
# 10.244.0.0/16 → label 1 (pod network)
key = lpm.Key(prefixlen=16, data=struct.pack("I", socket.htonl(0x0AF40000)))
lpm[key] = ct.c_uint32(1)
```

**Lưu ý quan trọng:**
- Flag `BPF_F_NO_PREALLOC` là **bắt buộc** khi tạo LPM Trie map — nếu thiếu, `bpf()` syscall trả về `-EINVAL`
- Key data phải ở **network byte order** (big-endian)
- `max_entries` bị **bỏ qua** (dynamic allocation) nhưng vẫn phải set > 0

### 3.3. Ring Buffer — Giao tiếp Kernel → User-space

**Ring Buffer** (`BPF_MAP_TYPE_RINGBUF`, Linux ≥ 5.8) là cơ chế ưu việt để gửi events từ kernel lên user-space, thay thế **Perf Buffer** cũ.

**So sánh Ring Buffer vs Perf Buffer:**

| Tiêu chí | Perf Buffer | Ring Buffer |
|---|---|---|
| **Throughput** | 1.6-9.0M events/s | **21.5M events/s** |
| **Memory** | Per-CPU buffers (waste) | Single shared buffer (efficient) |
| **Event ordering** | **Không đảm bảo** (per-CPU) | **Đảm bảo** (global FIFO) |
| **API** | `bpf_perf_event_output()` | `bpf_ringbuf_output()` hoặc `bpf_ringbuf_reserve()` + `bpf_ringbuf_submit()` |
| **Wakeup control** | Luôn wakeup | `BPF_RB_NO_WAKEUP`, `BPF_RB_FORCE_WAKEUP` |

**Tạo ring buffer:**

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);    // 256 KB buffer
} events SEC(".maps");
```

**Gửi event từ eBPF:**

```c
// Cách 1: Copy (đơn giản)
bpf_ringbuf_output(&events, &evt, sizeof(evt), 0);

// Cách 2: Reserve + Submit (zero-copy, hiệu quả hơn)
struct event_t *evt = bpf_ringbuf_reserve(&events, sizeof(*evt), 0);
if (!evt) return 0;    // buffer full
evt->src_ip = src_ip;
evt->dst_ip = dst_ip;
evt->dst_port = dst_port;
evt->pid = pid;
evt->timestamp_ns = bpf_ktime_get_ns();
bpf_ringbuf_submit(evt, 0);
```

**Đọc event từ user-space (libbpf):**

```c
// Callback function
int handle_event(void *ctx, void *data, size_t data_sz) {
    struct event_t *evt = (struct event_t *)data;
    // Process event: feed to Count-Min Sketch, update graph, etc.
    return 0;
}

// Polling loop
struct ring_buffer *rb = ring_buffer__new(map_fd, handle_event, NULL, NULL);
while (!exiting) {
    ring_buffer__poll(rb, 100 /* timeout_ms */);
}
```

### 3.4. Lifecycle của eBPF Program

```
1. WRITE     → Viết eBPF program bằng C (restricted subset)
2. COMPILE   → Clang/LLVM biên dịch thành BPF bytecode (.o file)
3. LOAD      → User-space app gọi bpf() syscall để nạp bytecode
4. VERIFY    → Kernel verifier kiểm tra an toàn
5. JIT       → JIT compiler biên dịch bytecode → native machine code
6. ATTACH    → Gắn program vào hook point (kprobe, tracepoint, etc.)
7. RUN       → Program chạy mỗi khi event xảy ra
8. COMMUNICATE → Maps + Ring Buffer để trao đổi dữ liệu
9. DETACH    → Gỡ program khi không cần nữa
```

---

## 4. Công cụ phát triển (Development Tools)

### 4.1. BCC (BPF Compiler Collection)

- **Ngôn ngữ:** eBPF program bằng C inline, user-space bằng Python hoặc C++
- **Ưu điểm:** Đơn giản, nhanh prototype, nhiều ví dụ
- **Nhược điểm:** Compile eBPF code at runtime (cần kernel headers trên target)
- **Phù hợp:** Development, prototyping

### 4.2. libbpf + CO-RE (Compile Once, Run Everywhere)

- **Ngôn ngữ:** eBPF program bằng C, user-space bằng C/C++/Go/Rust
- **Ưu điểm:** Pre-compiled BPF, portable across kernel versions, production-ready
- **Nhược điểm:** Setup phức tạp hơn BCC
- **Phù hợp:** Production deployment

### 4.3. Cilium ebpf (Go library)

- **Ngôn ngữ:** eBPF program bằng C, user-space bằng Go
- **Ưu điểm:** Type-safe Go bindings, well-maintained
- **Phù hợp:** Go-based infrastructure tools

**Cho dự án:** Sử dụng **BCC** cho phase prototype (dễ iterate), sau đó có thể migrate sang **libbpf** cho production deployment trên K8s.

---

## 5. Yêu cầu hệ thống (System Requirements)

| Yêu cầu | Chi tiết |
|---|---|
| **Kernel** | ≥ 5.8 (cho Ring Buffer), khuyến nghị ≥ 5.15 |
| **Quyền** | `CAP_BPF` + `CAP_PERFMON` (hoặc `privileged: true` trong K8s) |
| **Headers** | Kernel headers (cho BCC) hoặc BTF support (cho CO-RE) |
| **Tools** | Clang/LLVM ≥ 10, bpftool, BCC hoặc libbpf |
| **K8s Pod** | `privileged: true`, mount `/sys/kernel/debug` |

**Kiểm tra hỗ trợ:**

```bash
# Kiểm tra kernel version
uname -r

# Kiểm tra BTF support
ls /sys/kernel/btf/vmlinux

# Kiểm tra BPF syscall
bpftool feature probe
```

---

## 6. Tham khảo (References)

1. **Gregg, B.** (2019). *BPF Performance Tools*. Addison-Wesley. — Sách tham khảo toàn diện nhất về eBPF.
2. **Vieira, L. & Rice, D.** (2023). *Learning eBPF*. O'Reilly Media. — Hướng dẫn thực hành.
3. **Nakryiko, A.** (2020). "BPF Ring Buffer." Blog post. — Tác giả Ring Buffer trong kernel.
4. **eBPF Foundation.** https://ebpf.io — Trang chủ chính thức.
5. **BCC Reference Guide.** https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md
6. **Linux Kernel Documentation.** "BPF Design Q&A." https://docs.kernel.org/bpf/
7. **Cloudflare Blog.** (2024). "A Deep Dive into BPF LPM Trie Performance."
