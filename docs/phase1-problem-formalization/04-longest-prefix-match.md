# Bài toán 4: Tìm tiền tố dài nhất khớp
# Problem 4: Longest Prefix Match (LPM)

---

## 1. Đặt vấn đề (Problem Statement)

### 1.1. Bối cảnh tổng quát (General Context)

Trong nhiều hệ thống, ta cần **phân loại** (classify) một khóa (key) dựa trên **tiền tố** (prefix) dài nhất của nó khớp với một trong các mẫu (patterns) đã được định nghĩa trước. Các mẫu có **độ dài khác nhau** và có thể **chồng lấp** (overlap) — nghĩa là một khóa có thể khớp với nhiều mẫu, và ta cần chọn mẫu **cụ thể nhất** (most specific), tức mẫu dài nhất.

In many systems, we need to **classify** a key based on the **longest prefix** that matches one of the predefined patterns. Patterns have **variable lengths** and can **overlap** — meaning a key may match multiple patterns, and we need the **most specific** one, i.e., the longest match.

**Câu hỏi cốt lõi (Core Question):** Cho một tập mẫu tiền tố với độ dài khác nhau và một khóa truy vấn, làm thế nào để tìm nhanh nhất mẫu dài nhất khớp với khóa?

### 1.2. Ví dụ trực quan (Intuitive Examples)

- **Định tuyến IP (IP Routing):** Bảng định tuyến chứa các CIDR blocks: `10.0.0.0/8`, `10.244.0.0/16`, `10.244.1.0/24`. Khi gói tin đến IP `10.244.1.5`, cần tìm route cụ thể nhất → `/24` (dài nhất).
- **Tự động hoàn thành (Autocomplete):** Kho từ điển chứa: "app", "apple", "application". Khi người dùng gõ "appli", cần tìm tiền tố dài nhất khớp → "app" (vì "appli" chưa hoàn chỉnh nhưng bắt đầu bằng "app").
- **Phân loại URL:** Quy tắc: `/api/*` → backend, `/api/v2/*` → backend-v2, `/` → frontend. URL `/api/v2/users` khớp cả ba, chọn dài nhất → `/api/v2/`.
- **Phân loại số điện thoại:** Mã vùng `+84` (Vietnam), `+841` (Hanoi), `+8428` (Ho Chi Minh). Số `+84283456789` khớp dài nhất với `+8428`.

---

## 2. Định nghĩa hình thức (Formal Definition)

### 2.1. Tiền tố và khớp tiền tố (Prefix and Prefix Matching)

**Bảng chữ cái (Alphabet):** $\Sigma = \{0, 1\}$ (binary alphabet cho trường hợp tổng quát nhất — IP addresses, bit strings).

**Chuỗi bit (Bit String):** Một chuỗi $x \in \Sigma^*$ có độ dài $|x|$ ký tự.

**Tiền tố (Prefix):** Chuỗi $p$ là **tiền tố** của $x$ (ký hiệu $p \sqsubseteq x$) nếu $x$ bắt đầu bằng $p$:

$$p \sqsubseteq x \iff \exists y \in \Sigma^* : x = p \cdot y$$

trong đó $\cdot$ là phép nối chuỗi (concatenation).

**Quan hệ tiền tố (Prefix Ordering):** Quan hệ $\sqsubseteq$ là một **thứ tự bộ phận** (partial order) trên $\Sigma^*$:
- Phản xạ: $x \sqsubseteq x$
- Phản đối xứng: $p \sqsubseteq q$ và $q \sqsubseteq p$ $\implies$ $p = q$
- Bắc cầu: $p \sqsubseteq q$ và $q \sqsubseteq r$ $\implies$ $p \sqsubseteq r$

### 2.2. Bài toán Longest Prefix Match

**Bài toán (Problem):** Cho:
- Tập tiền tố $P = \{(p_1, \ell_1), (p_2, \ell_2), \ldots, (p_n, \ell_n)\}$ trong đó $p_i \in \Sigma^*$ là tiền tố và $\ell_i$ là nhãn (label/value) liên kết.
- Khóa truy vấn $x \in \Sigma^L$ (chuỗi bit có độ dài cố định $L$, ví dụ $L = 32$ cho IPv4).

**Output:** Tìm tiền tố $p_j$ dài nhất là tiền tố của $x$:

$$\text{LPM}(x, P) = \arg\max_{p_i \in P, \; p_i \sqsubseteq x} |p_i|$$

và trả về nhãn $\ell_j$ tương ứng. Nếu không có tiền tố nào khớp, trả về $\bot$ (undefined).

**Yêu cầu (Requirements):** Hỗ trợ các thao tác:

1. **INSERT($p$, $\ell$, $|p|$):** Thêm tiền tố $p$ với độ dài $|p|$ và nhãn $\ell$
2. **DELETE($p$, $|p|$):** Xóa tiền tố $p$
3. **LOOKUP($x$):** Trả về $\ell_j$ cho tiền tố dài nhất $p_j \sqsubseteq x$

### 2.3. Trường hợp đặc biệt: IP Address Classification

Trong ngữ cảnh IP networking, bài toán LPM có dạng cụ thể:

- $\Sigma = \{0, 1\}$, $L = 32$ (IPv4) hoặc $L = 128$ (IPv6)
- Mỗi tiền tố là một **CIDR block**: $\text{prefix}/\text{length}$
  - Ví dụ: `10.244.0.0/16` → $p$ = 16 bit đầu của `10.244.0.0` = `00001010.11110100`
- Nhãn $\ell$ là routing decision (next hop, interface, action)

**Tính chất đặc biệt của IP-LPM:**
- Độ dài khóa $L$ cố định và nhỏ (32 hoặc 128)
- Số lượng tiền tố $n$ có thể rất lớn (full BGP table > 900K entries)
- Tần suất lookup cực cao (line-rate: triệu lookups/giây)
- INSERT/DELETE ít thường xuyên hơn nhiều so với LOOKUP

---

## 3. Phân tích lý thuyết phức tạp (Theoretical Complexity Analysis)

### 3.1. Giới hạn dưới (Lower Bounds)

**Trong mô hình comparison-based (Lower Bound):**

Bài toán LPM trên $n$ tiền tố với khóa dài $L$ bits có giới hạn dưới:

$$\Omega(\min(L, \log n))$$

cho mỗi lookup, vì cần phân biệt ít nhất $n$ trường hợp (hoặc đọc $L$ bits của khóa).

**Trong mô hình word-RAM:** Với word size $w \ge L$, một số operations trên toàn bộ khóa có thể thực hiện trong $O(1)$, cho phép cải thiện bounds.

### 3.2. Giới hạn trên — Các cấu trúc dữ liệu (Known Upper Bounds)

| Cấu trúc dữ liệu | Lookup | Insert/Delete | Space | Đặc điểm |
|---|---|---|---|---|
| **Binary Trie** | $O(L)$ | $O(L)$ | $O(n \cdot L)$ | Đơn giản, nhiều node trung gian lãng phí |
| **Patricia Trie (Radix Tree)** | $O(L)$ | $O(L)$ | $O(n)$ nodes | Nén node 1-con, tiết kiệm bộ nhớ |
| **Level-Compressed Trie (LC-Trie)** | $O(L / \log d)$ | $O(L)$ | $O(n)$ | Multi-way branching, cache-friendly |
| **Hash-based LPM** | $O(L)$ worst, $O(\log L)$ expected | $O(L)$ | $O(n \cdot L)$ | Binary search on prefix length |
| **TCAM (Hardware)** | $O(1)$ | $O(n)$ reorganize | $O(n \cdot L)$ | Phần cứng chuyên dụng, đắt đỏ |

### 3.3. Phân tích chi tiết từng cấu trúc (Detailed Analysis)

#### Binary Trie

Cây nhị phân đầy đủ, mỗi nhánh ứng với bit 0 hoặc 1:

```
Root
├── 0: ...
│   ├── 0: ...
│   └── 1: ...
└── 1: ...
    ├── 0: ...
    └── 1: ...
```

- **Lookup:** Đi từ root, theo bit thứ 0, 1, 2, ... của key. Tại mỗi node, nếu có prefix kết thúc tại đây, ghi nhận. Trả về prefix dài nhất ghi nhận.
- **Nhược điểm:** Với IPv4 ($L = 32$), cây có thể lên tới $2^{32}$ lá — nhưng thực tế chỉ $n$ node có ý nghĩa, phần lớn node trung gian trống.

#### Patricia Trie (Radix Trie)

Tối ưu hóa Binary Trie bằng cách **nén** các chuỗi node chỉ có 1 con thành 1 node:

- Node lưu: bit position cần kiểm tra (skip count) + prefix data
- Giảm số node từ $O(n \cdot L)$ xuống $O(n)$
- **Đặc biệt phù hợp** khi tiền tố phân bố thưa (sparse) trong không gian $\{0,1\}^L$

#### Hash-based LPM (Binary Search on Prefix Length)

Ý tưởng: Vì $L$ nhỏ (32 cho IPv4), ta có thể thử tất cả $L$ độ dài prefix:

1. Tạo $L$ hash tables, mỗi table cho 1 prefix length
2. Với key $x$, thử lookup $x[0..L-1]$, $x[0..L-2]$, ..., $x[0..0]$
3. Trả về match đầu tiên (dài nhất)

**Tối ưu:** Binary search trên prefix length → $O(\log L)$ hash lookups trung bình.

---

## 4. Các biến thể và mở rộng (Variants and Extensions)

### 4.1. Multi-Field Classification (Phân loại đa trường)

Thay vì khớp trên 1 trường (IP), khớp trên nhiều trường đồng thời:

$$(src\_ip, dst\_ip, src\_port, dst\_port, protocol)$$

Mỗi trường có prefix rule riêng. Bài toán trở thành **multi-dimensional LPM** — NP-hard trong trường hợp tổng quát.

### 4.2. Ternary Matching (Khớp ba giá trị)

Mở rộng $\Sigma = \{0, 1, *\}$ trong đó $*$ khớp cả 0 và 1. Đây là cơ sở của TCAM (Ternary Content-Addressable Memory).

### 4.3. Weighted LPM (LPM có trọng số)

Thay vì chọn prefix dài nhất, chọn prefix có **ưu tiên cao nhất** (priority). Prefix dài hơn thường có priority cao hơn, nhưng có thể override.

### 4.4. Batch Lookup (Tra cứu hàng loạt)

Cho tập keys $\{x_1, x_2, \ldots, x_q\}$, thực hiện LPM cho tất cả. Tận dụng:
- **Cache locality:** Sắp xếp keys theo prefix chung để tái sử dụng trie traversal
- **SIMD:** Xử lý nhiều keys song song trên cùng một trie path

---

## 5. Mối liên hệ với các bài toán khác (Connections to Other Problems)

### 5.1. LPM và String Matching

LPM là trường hợp đặc biệt của **dictionary matching** khi chỉ quan tâm tiền tố. Liên quan đến Aho-Corasick automaton nhưng đơn giản hơn vì:
- Chỉ match prefix (không cần match ở giữa chuỗi)
- Key có độ dài cố định

### 5.2. LPM và Interval Stabbing

Mỗi prefix $p$ với length $|p|$ định nghĩa một interval:
$$[p \cdot 0^{L-|p|}, \; p \cdot 1^{L-|p|}]$$

LPM tương đương với tìm interval nhỏ nhất chứa điểm $x$ (interval stabbing with minimum enclosing interval).

### 5.3. LPM trong eBPF Context

Linux kernel cung cấp `BPF_MAP_TYPE_LPM_TRIE` — một trie-based LPM map hoạt động trực tiếp trong kernel space:
- Key: `struct bpf_lpm_trie_key { __u32 prefixlen; __u8 data[0]; }`
- Lookup: $O(L)$ với $L$ = chiều dài key tính bằng bits
- Yêu cầu flag `BPF_F_NO_PREALLOC` khi tạo map

---

## 6. Tham khảo chính (Key References)

1. **Morrison, D. R.** (1968). "PATRICIA — Practical Algorithm To Retrieve Information Coded in Alphanumeric." *Journal of the ACM*, 15(4), pp. 514–534. — Bài báo gốc giới thiệu Patricia Trie.

2. **Nilsson, S. & Karlsson, G.** (1999). "IP-Address Lookup Using LC-Tries." *IEEE Journal on Selected Areas in Communications*, 17(6), pp. 1083–1092. — Level-Compressed Trie cho IP routing.

3. **Waldvogel, M., Varghese, G., Turner, J., & Plattner, B.** (2001). "Scalable High Speed IP Routing Lookups." *Proceedings of the ACM SIGCOMM*, pp. 25–36. — Binary search on prefix length.

4. **Gupta, P. & McKeown, N.** (2001). "Algorithms for Packet Classification." *IEEE Network*, 15(2), pp. 24–32. — Survey toàn diện về packet classification.

5. **Varghese, G.** (2005). *Network Algorithmics: An Interdisciplinary Approach to Designing Fast Networked Devices*. Morgan Kaufmann. — Sách tham khảo chính cho network data structures.

6. **Cloudflare Engineering Blog** (2024). "A Deep Dive into BPF LPM Trie Performance and Optimization." — Phân tích thực tế LPM Trie trong eBPF.

---

## 7. Tóm tắt (Summary)

| Khía cạnh | Chi tiết |
|---|---|
| **Bài toán** | Longest Prefix Match — tìm tiền tố dài nhất khớp với key truy vấn |
| **Input** | Tập tiền tố $P = \{(p_i, \ell_i)\}$ và key $x \in \Sigma^L$ |
| **Output** | Nhãn $\ell_j$ của tiền tố dài nhất $p_j \sqsubseteq x$ |
| **Optimal (software)** | $O(L)$ lookup — đạt bởi Binary Trie, Patricia Trie |
| **Fastest (software)** | $O(L / \log d)$ — LC-Trie với multi-way branching |
| **Fastest (hardware)** | $O(1)$ — TCAM parallel lookup |
| **Best space** | $O(n)$ nodes — Patricia Trie (nén node 1-con) |
| **Đặc biệt cho IPv4** | $L = 32$ cố định → tất cả lookup $O(32)$ = $O(1)$ constant |
| **Mở rộng** | Multi-field, ternary, weighted, batch |
| **Ứng dụng** | IP routing, firewall classification, URL routing, phone number lookup |
