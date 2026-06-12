# So sánh phương án giải bài toán Tìm tiền tố dài nhất khớp
# Solution Comparison: Longest Prefix Match (LPM)

---

## 1. Nhắc lại bài toán (Problem Recap)

Cho tập tiền tố $P = \{(p_1, \ell_1), \ldots, (p_n, \ell_n)\}$ trong đó $p_i \in \{0,1\}^*$ là tiền tố (variable-length bit string) và $\ell_i$ là nhãn liên kết. Cho khóa truy vấn $x \in \{0,1\}^L$ (với $L$ cố định, ví dụ $L = 32$ cho IPv4):

$$\text{LPM}(x, P) = \arg\max_{p_i \in P, \; p_i \sqsubseteq x} |p_i|$$

Trả về nhãn $\ell_j$ của tiền tố dài nhất khớp với $x$.

Trong ngữ cảnh hệ thống: phân loại IP address thuộc subnet nào — K8s pod network, service network, hay external.

---

## 2. Các phương án giải quyết (Solution Approaches)

### 2.1. Phương án A — Linear Scan (Duyệt tuần tự)

#### Mô tả (Description)

Cách đơn giản nhất: với mỗi truy vấn, duyệt qua toàn bộ $n$ tiền tố, kiểm tra từng tiền tố có khớp với key không, giữ lại tiền tố dài nhất.

#### Thuật toán (Algorithm)

```
LINEAR_SCAN_LPM(P, x):
    best_match = ⊥
    best_length = -1

    for each (p_i, ℓ_i, len_i) in P:
        if x[0..len_i-1] == p_i AND len_i > best_length:
            best_match = ℓ_i
            best_length = len_i

    return best_match
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Lookup time** | $O(n \cdot L)$ — duyệt $n$ entries, mỗi entry so sánh $O(L)$ bits |
| **Insert/Delete** | $O(1)$ (append/remove from list) |
| **Space** | $O(n \cdot L)$ bits |

#### Nhược điểm (Disadvantages)
- $O(n \cdot L)$ per lookup quá chậm cho high-throughput applications
- Với bảng routing 1000 entries, mỗi lookup cần ~32,000 bit comparisons
- Không có cấu trúc → không thể tận dụng prefix overlap

#### Kết luận (Verdict)
**Không khả thi** cho production — chỉ phù hợp làm baseline correctness test.

---

### 2.2. Phương án B — Hash Table per Prefix Length (Bảng băm theo độ dài)

#### Mô tả (Description)

Tạo $L$ bảng băm riêng biệt, mỗi bảng cho một prefix length. Khi lookup, thử tất cả $L$ prefix lengths từ dài nhất đến ngắn nhất, trả về match đầu tiên.

#### Thuật toán (Algorithm)

```
INITIALIZE:
    tables[0..L] = L+1 empty hash tables

INSERT(prefix, length, label):
    tables[length][prefix] = label

LOOKUP(x):
    for len = L downto 0:
        key = x[0..len-1]
        if key ∈ tables[len]:
            return tables[len][key]
    return ⊥
```

#### Tối ưu: Binary Search on Prefix Length

Thay vì duyệt tuần tự $L$ bảng, dùng **binary search** trên prefix length:

```
LOOKUP_BINARY(x):
    lo = 0, hi = L
    result = ⊥

    while lo ≤ hi:
        mid = (lo + hi) / 2
        key = x[0..mid-1]
        if key ∈ tables[mid]:
            result = tables[mid][key]
            lo = mid + 1           // thử prefix dài hơn
        else:
            hi = mid - 1           // thử prefix ngắn hơn

    return result
```

**Lưu ý:** Binary search trên prefix length **không hoàn toàn đúng** nếu có "gaps" (prefix length 16 match nhưng length 24 không match, trong khi length 20 match). Cần marker bits để đánh dấu nơi có tiền tố "chuyển hướng" → phức tạp.

#### Phân tích (Analysis)

| Tiêu chí | Phiên bản | Giá trị |
|---|---|---|
| **Lookup time** | Linear scan | $O(L)$ hash lookups |
| **Lookup time** | Binary search | $O(\log L)$ hash lookups (average), nhưng cần markers |
| **Insert** | — | $O(1)$ per hash table |
| **Space** | — | $O(n)$ entries across all tables, nhưng $L$ tables overhead |

#### Ưu điểm (Advantages)
- $O(1)$ per hash lookup → tổng $O(L)$ hoặc $O(\log L)$
- Đơn giản về mặt khái niệm
- Mỗi bảng có thể dùng hash table tiêu chuẩn

#### Nhược điểm (Disadvantages)
- **$L$ bảng hash riêng biệt:** Overhead quản lý, memory fragmentation
- **Binary search phức tạp:** Cần marker bits, trường hợp biên khó xử lý, dễ bug
- **Không tận dụng prefix overlap:** Hai prefix `10.0.0.0/8` và `10.244.0.0/16` lưu hoàn toàn riêng biệt, dù chia sẻ 8 bits đầu
- **Memory overhead cho hash tables:** Mỗi hash table cần capacity riêng, load factor management
- **Cache-unfriendly:** $L$ hash tables nằm ở vùng nhớ khác nhau → nhiều cache misses

#### Kết luận (Verdict)
Khả thi nhưng **không tối ưu** — trie-based approaches tận dụng cấu trúc prefix tốt hơn.

---

### 2.3. Phương án C — Binary Trie (Cây nhị phân tiền tố)

#### Mô tả (Description)

Cây nhị phân trong đó mỗi nhánh trái/phải ứng với bit 0/1. Đường đi từ root đến một node biểu diễn một prefix. Nodes có nhãn tại các vị trí có prefix trong $P$.

#### Thuật toán (Algorithm)

```
struct TrieNode:
    children[2]     // left (0) and right (1)
    label           // ⊥ if no prefix ends here
    has_prefix      // true if a prefix ends at this node

INSERT(root, prefix, length, label):
    node = root
    for i = 0 to length - 1:
        bit = prefix[i]
        if node.children[bit] == null:
            node.children[bit] = new TrieNode()
        node = node.children[bit]
    node.label = label
    node.has_prefix = true

LOOKUP(root, key):
    node = root
    best_label = ⊥
    for i = 0 to L - 1:
        if node.has_prefix:
            best_label = node.label    // ghi nhận match dài nhất hiện tại
        bit = key[i]
        if node.children[bit] == null:
            break                       // không có nhánh tiếp
        node = node.children[bit]
    // Kiểm tra node cuối
    if node != null AND node.has_prefix:
        best_label = node.label
    return best_label
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Lookup time** | $O(L)$ — duyệt tối đa $L$ mức |
| **Insert time** | $O(L)$ |
| **Delete time** | $O(L)$ |
| **Space** | $O(n \cdot L)$ nodes worst-case |

#### Ưu điểm (Advantages)
- **Đơn giản, dễ hiểu:** Cấu trúc trực quan, dễ cài đặt
- **Lookup luôn đúng:** Theo đường đi key, ghi nhận prefix cuối cùng gặp → đảm bảo longest match
- **$O(L)$ lookup deterministic:** Với IPv4, $L = 32$ → constant time $O(32)$

#### Nhược điểm (Disadvantages)
- **Nhiều node trung gian lãng phí:** Nodes chỉ có 1 con chiếm bộ nhớ mà không mang thông tin. Ví dụ: prefix `10.244.1.0/24` tạo 24 nodes, nhưng chỉ node cuối có nhãn
- **Space $O(n \cdot L)$:** Tệ hơn cần thiết — Patricia Trie giải quyết vấn đề này
- **Cache-unfriendly:** Mỗi mức trie là một pointer dereference → nhiều cache misses

#### Kết luận (Verdict)
Đúng đắn nhưng **lãng phí bộ nhớ** — nên dùng Patricia Trie (nén) thay thế.

---

### 2.4. Phương án D — Patricia Trie / Radix Trie (Cây tiền tố nén)

#### Mô tả (Description)

Patricia Trie (Morrison, 1968) — viết tắt của **Practical Algorithm To Retrieve Information Coded In Alphanumeric** — là phiên bản nén của Binary Trie. Các chuỗi nodes chỉ có 1 con được hợp nhất (collapsed) thành 1 node, lưu "skip count" (số bits bỏ qua).

#### Ý tưởng nén (Compression Idea)

Binary Trie cho 3 prefixes `000*`, `0010*`, `0011*`:
```
Binary Trie:              Patricia Trie (compressed):
     root                       root
     /                          /
    0                      [skip 2 bits: "00"]
   / \                        / \
  0   (none)                 0   1
 / \                         |   |
0   1                       0*  / \
|  / \                        0*  1*
*  0   1
   |   |
   *   *
```

Nodes với 1 con bị loại bỏ → ít nodes hơn, compact hơn.

#### Thuật toán (Algorithm)

```
struct PatriciaNode:
    bit_position    // vị trí bit cần kiểm tra (skip count)
    prefix_data     // bits chia sẻ chung
    prefix_length   // độ dài prefix nếu có
    label           // nhãn (⊥ nếu không phải endpoint)
    children[2]     // left (0) and right (1)

LOOKUP(root, key):
    node = root
    best_label = ⊥

    while node != null:
        if node.label != ⊥:
            // Verify prefix match (may need bit comparison)
            if key matches node.prefix_data for node.prefix_length bits:
                best_label = node.label

        if node.bit_position >= L:
            break

        bit = key[node.bit_position]
        node = node.children[bit]

    return best_label
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Lookup time** | $O(L)$ worst-case, nhưng thực tế nhanh hơn (fewer pointer dereferences) |
| **Insert time** | $O(L)$ |
| **Space** | $O(n)$ **nodes** — mỗi prefix tạo tối đa 1 internal node |
| **Memory** | Nhỏ hơn Binary Trie 3-10x trong thực tế |

#### Ưu điểm (Advantages)
- **Space $O(n)$ nodes:** Tối ưu — tỷ lệ thuận với số prefixes, không phụ thuộc $L$
- **Nhanh hơn Binary Trie thực tế:** Ít pointer dereferences hơn (skip qua nodes 1-con)
- **Well-established:** Sử dụng trong Linux kernel (IPv4 routing), nhiều implementations ổn định
- **Hỗ trợ dynamic:** Insert/Delete $O(L)$

#### Nhược điểm (Disadvantages)
- **Vẫn $O(L)$ worst-case lookup:** Khi trie không nén được nhiều (many branching points)
- **Cài đặt phức tạp hơn Binary Trie:** Cần xử lý skip counts, prefix verification
- **Cache performance:** Vẫn pointer-chasing, dù ít hơn Binary Trie

---

### 2.5. Phương án E — LPM Trie (eBPF-native)

#### Mô tả (Description)

`BPF_MAP_TYPE_LPM_TRIE` là cấu trúc dữ liệu LPM được tích hợp sẵn trong Linux kernel cho eBPF programs. Nó là một **unbalanced trie** tối ưu cho IP prefix matching.

#### Đặc điểm kỹ thuật (Technical Characteristics)

```c
// Kernel API
struct bpf_lpm_trie_key {
    __u32 prefixlen;        // prefix length in bits
    __u8  data[0];          // flexible array for key data
};

// Tạo map
struct bpf_map_def SEC("maps") lpm_map = {
    .type = BPF_MAP_TYPE_LPM_TRIE,
    .key_size = sizeof(struct bpf_lpm_trie_key) + 4,  // +4 for IPv4
    .value_size = sizeof(__u32),                        // label
    .max_entries = 1024,
    .map_flags = BPF_F_NO_PREALLOC,  // BẮT BUỘC
};
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Lookup time** | $O(L)$ — trie traversal, $L = 32$ cho IPv4 |
| **Insert time** | $O(L)$ |
| **Space** | Dynamic allocation (flag `BPF_F_NO_PREALLOC` bắt buộc) |
| **Chạy trong** | **Kernel space** — zero context switch |

#### Ưu điểm (Advantages)
- **Native kernel support:** Không cần copy data lên user-space để classify IP
- **Zero context switch:** LPM lookup xảy ra ngay trong eBPF program context → latency cực thấp (nanoseconds)
- **Atomic operations:** Kernel đảm bảo thread-safety cho concurrent access
- **API đơn giản:** `bpf_map_lookup_elem()` trả về kết quả LPM ngay lập tức
- **Tích hợp với eBPF ecosystem:** Kết hợp tự nhiên với kprobe hooks, ring buffer, etc.

#### Nhược điểm (Disadvantages)
- **Chỉ chạy trong kernel:** Không dùng được cho user-space testing/benchmarking
- **BPF verifier constraints:** Key size, map size bị giới hạn bởi eBPF verifier
- **`BPF_F_NO_PREALLOC` bắt buộc:** Dynamic allocation có thể ảnh hưởng performance dưới memory pressure
- **Debugging khó:** eBPF programs khó debug hơn user-space code
- **Kernel version dependent:** Cần kernel ≥ 4.11 cho LPM Trie

---

## 3. Bảng so sánh tổng hợp (Comprehensive Comparison)

### 3.1. Complexity Comparison

| Tiêu chí | Linear Scan | Hash Tables | Binary Trie | Patricia Trie | LPM Trie (eBPF) |
|---|---|---|---|---|---|
| **Lookup** | $O(nL)$ | $O(L)$ or $O(\log L)$ | $O(L)$ | $O(L)$ | $O(L)$ |
| **Insert** | $O(1)$ | $O(1)$ | $O(L)$ | $O(L)$ | $O(L)$ |
| **Space (nodes)** | $O(n)$ | $O(n)$ + $L$ tables | $O(nL)$ | **$O(n)$** | $O(n)$ dynamic |
| **Cache-friendly** | Yes (linear) | No ($L$ tables) | No (pointers) | **Moderate** | N/A (kernel) |
| **Correctness** | Trivial | Complex (markers) | Simple | Medium | Kernel-verified |
| **User-space** | Yes | Yes | Yes | **Yes** | **No** |
| **Kernel-space** | No | No | No | No | **Yes** |

### 3.2. Ví dụ số liệu thực tế cho IPv4 (Practical Numbers, $L = 32$)

Với $n = 100$ CIDR prefixes (realistic K8s cluster):

| Phương án | Lookup ops | Memory | Thời gian ước tính |
|---|---|---|---|
| Linear Scan | $100 \times 32 = 3,200$ comparisons | ~3 KB | ~10 μs |
| Hash Tables | $32$ hash lookups (worst) | ~5 KB + overhead | ~2 μs |
| Binary Trie | $32$ pointer dereferences | ~10 KB (many empty nodes) | ~1 μs |
| Patricia Trie | $\le 32$ pointer dereferences | ~4 KB (compressed) | ~0.5 μs |
| LPM Trie (kernel) | $32$ trie steps | Kernel-managed | ~0.1 μs |

---

## 4. Phương án đề xuất và lý do chọn (Proposed Solution)

### 4.1. Kết luận (Conclusion)

**Hai phương án, hai tầng:**

1. **Kernel-space (eBPF program):** Sử dụng `BPF_MAP_TYPE_LPM_TRIE` — native, zero-overhead
2. **User-space (libdsa library):** Implement **Patricia Trie (LPM Trie)** — cho testing, benchmarking, và dùng độc lập

### 4.2. Lý do chọn (Justification)

#### Tại sao LPM Trie (eBPF) cho kernel-space:

1. **Performance tối ưu:** Lookup ngay trong kernel context, không cần copy data lên user-space → latency ~100ns, phù hợp cho line-rate processing
2. **Đã được kernel verify:** Correctness đảm bảo bởi eBPF verifier → không cần debug logic LPM
3. **Tích hợp tự nhiên:** Pipeline eBPF: `tcp_v4_connect hook → LPM lookup → Ring Buffer`
4. **Không có alternative tốt hơn:** Trong kernel eBPF, `BPF_MAP_TYPE_LPM_TRIE` là lựa chọn duy nhất cho LPM

#### Tại sao Patricia Trie cho user-space:

1. **Space $O(n)$:** Tối ưu — mỗi prefix chỉ cần constant memory, không lãng phí như Binary Trie $O(nL)$
2. **Lookup $O(L) = O(32)$:** Constant cho IPv4, nhanh và deterministic
3. **Testable standalone:** Chạy được trên mọi máy, không cần kernel/eBPF
4. **Benchmarkable:** So sánh với Hash Tables, Binary Trie cho work paper
5. **Tại sao không Hash Tables:** Phức tạp (binary search on prefix length cần markers, nhiều edge cases), cache-unfriendly ($L$ tables riêng biệt)

### 4.3. Chiến lược triển khai hai tầng (Two-Tier Strategy)

```
Kernel-space (eBPF):
    BPF_MAP_TYPE_LPM_TRIE ← populated từ K8s API (CIDR ranges)
    Mỗi tcp_v4_connect:
        result = bpf_map_lookup_elem(&lpm_map, &dst_ip)
        if result == K8S_INTERNAL:
            bpf_ringbuf_output(&rb, &event, sizeof(event), 0)
        else:
            // external traffic, skip

User-space (libdsa, cho testing):
    PatriciaTrie trie;
    trie.insert("10.244.0.0", 16, LABEL_POD_NET);
    trie.insert("10.96.0.0", 12, LABEL_SVC_NET);

    // Test
    assert(trie.lookup("10.244.1.5") == LABEL_POD_NET);
    assert(trie.lookup("10.96.0.1") == LABEL_SVC_NET);
    assert(trie.lookup("8.8.8.8") == LABEL_NONE);
```

---

## 5. Tham khảo (References)

1. **Morrison, D. R.** (1968). "PATRICIA — Practical Algorithm To Retrieve Information Coded in Alphanumeric." *Journal of the ACM*, 15(4), pp. 514–534.
2. **Nilsson, S. & Karlsson, G.** (1999). "IP-Address Lookup Using LC-Tries." *IEEE JSAC*, 17(6), pp. 1083–1092.
3. **Waldvogel, M. et al.** (2001). "Scalable High Speed IP Routing Lookups." *ACM SIGCOMM*.
4. **Varghese, G.** (2005). *Network Algorithmics*. Morgan Kaufmann.
5. **Linux Kernel Documentation.** "BPF Map Type: LPM Trie." https://docs.kernel.org/bpf/map_lpm_trie.html
6. **Cloudflare Engineering.** (2024). "A Deep Dive into BPF LPM Trie Performance and Optimization."
