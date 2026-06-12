# Bài toán 3: Phân tích khả năng đạt được theo k bước trên đồ thị
# Problem 3: K-Hop Reachability Analysis on Directed Graphs

---

## 1. Đặt vấn đề (Problem Statement)

### 1.1. Bối cảnh tổng quát (General Context)

Trong nhiều hệ thống phức tạp được mô hình hóa bằng đồ thị, khi một đỉnh được đánh dấu là "đặc biệt" (nguồn lây nhiễm, điểm khởi phát lan truyền, hoặc nguồn tín hiệu), ta cần xác định **tập hợp tất cả các đỉnh** có thể bị ảnh hưởng trong **phạm vi giới hạn** — tức là chỉ xét các đường đi có độ dài tối đa $k$ bước.

In many complex systems modeled as graphs, when a vertex is marked as "special" (infection source, propagation origin, or signal source), we need to determine the **set of all vertices** that can be affected within a **bounded range** — considering only paths of length at most $k$ hops.

**Câu hỏi cốt lõi (Core Question):** Cho một đồ thị có hướng $G = (V, E)$, một đỉnh nguồn $s$, và giới hạn bước $k$, tìm tất cả các đỉnh mà $s$ có thể đạt tới trong tối đa $k$ bước.

### 1.2. Ví dụ trực quan (Intuitive Examples)

- **Lan truyền dịch bệnh:** Một người nhiễm bệnh (đỉnh nguồn) có thể lây cho người tiếp xúc trực tiếp (1-hop), người tiếp xúc gián tiếp (2-hop), v.v. Với $k = 3$, ta cần biết ai có nguy cơ.
- **Bán kính ảnh hưởng trong mạng (Blast Radius):** Khi một máy chủ bị tấn công, hacker có thể "nhảy" sang các máy chủ khác qua các kết nối mạng. $k$ hops xác định phạm vi thiệt hại tối đa.
- **Lan truyền thông tin:** Trong mạng xã hội, một tin nhắn được chia sẻ qua $k$ lần forward sẽ đạt tới tập người dùng nào?
- **Failure propagation:** Trong hệ thống phân tán, lỗi ở service A có thể cascade sang các services phụ thuộc trong $k$ bước.

---

## 2. Định nghĩa hình thức (Formal Definition)

### 2.1. Đường đi và khoảng cách (Path and Distance)

**Đường đi có hướng (Directed Path):** Một đường đi từ $u$ đến $v$ trong đồ thị $G = (V, E)$ là một chuỗi đỉnh:

$$P = (u = w_0, w_1, w_2, \ldots, w_\ell = v)$$

sao cho $(w_{i-1}, w_i) \in E$ với mọi $1 \le i \le \ell$. **Độ dài** (length) của đường đi là $\ell$ (số cung).

**Khoảng cách (Distance):** Khoảng cách từ $u$ đến $v$ là độ dài đường đi ngắn nhất:

$$d(u, v) = \min\{\ell : \exists \text{ path from } u \text{ to } v \text{ of length } \ell\}$$

Nếu không tồn tại đường đi, $d(u, v) = \infty$.

### 2.2. Tập đạt được theo k bước (K-Hop Reachable Set)

**Định nghĩa (Definition):** Cho đồ thị $G = (V, E)$, đỉnh nguồn $s \in V$, và số nguyên $k \ge 0$. **Tập đạt được theo $k$ bước** (k-hop reachable set) từ $s$ là:

$$R_k(s) = \{v \in V : d(s, v) \le k\}$$

**Tính chất (Properties):**
- $R_0(s) = \{s\}$ (chỉ có đỉnh nguồn)
- $R_1(s) = \{s\} \cup \text{Adj}(s)$ (đỉnh nguồn và hàng xóm trực tiếp)
- $R_k(s) \subseteq R_{k+1}(s)$ (tính đơn điệu — monotonicity)
- $R_\infty(s) = \{v \in V : s \rightsquigarrow v\}$ (transitive closure từ $s$)

**Phân tầng (Layering):** Ta có thể phân $R_k(s)$ thành các tầng theo khoảng cách:

$$L_i(s) = \{v \in V : d(s, v) = i\}, \quad 0 \le i \le k$$

$$R_k(s) = \bigsqcup_{i=0}^{k} L_i(s)$$

### 2.3. Bài toán K-Hop Reachability

**Bài toán chính (Main Problem):** Cho đồ thị $G = (V, E)$, đỉnh nguồn $s$, và giới hạn $k$:

**Input:**
- Đồ thị $G = (V, E)$ với $|V| = n$, $|E| = m$
- Đỉnh nguồn $s \in V$
- Giới hạn bước $k \ge 0$

**Output:**
- Tập $R_k(s)$ cùng với khoảng cách $d(s, v)$ cho mỗi $v \in R_k(s)$

**Biến thể truy vấn (Query Variant):** Cho trước $s$ và $t$, trả lời: $d(s, t) \le k$?

### 2.4. Bài toán mở rộng — Đồ thị có trọng số (Weighted Extension)

Trong trường hợp mỗi cung $(u, v)$ có trọng số $w(u, v) \ge 0$ (ví dụ: chi phí tấn công, xác suất thành công, độ trễ), khoảng cách trở thành:

$$d_w(u, v) = \min_{P: u \leadsto v} \sum_{(a, b) \in P} w(a, b)$$

Và bài toán trở thành: Tìm $R_k^w(s) = \{v : d_w(s, v) \le k\}$ (đạt được với tổng trọng số $\le k$).

---

## 3. Phân tích lý thuyết phức tạp (Theoretical Complexity Analysis)

### 3.1. Giới hạn dưới (Lower Bounds)

**Giới hạn dưới cho single-source reachability (Lower Bound):**

Bài toán k-hop reachability cần đọc ít nhất một phần đáng kể của đồ thị. Trong worst-case:

$$\Omega(n + \min(m, n \cdot k))$$

vì cần duyệt tất cả cung từ các đỉnh trong $R_k(s)$.

**Đối với trường hợp không giới hạn ($k = \infty$):** Single-source reachability yêu cầu $\Omega(n + m)$ trong mô hình comparison-based.

### 3.2. Giới hạn trên — Thuật toán đã biết (Known Upper Bounds)

| Thuật toán | Time | Space | Đồ thị | Đặc điểm |
|---|---|---|---|---|
| **BFS** | $O(n + m)$ | $O(n)$ | Unweighted | Tối ưu, natural k-hop layering |
| **DFS (depth-limited)** | $O(n + m)$ | $O(k)$ stack | Unweighted | Tốn stack ít hơn nhưng không tìm shortest path |
| **Dijkstra** | $O((n + m) \log n)$ | $O(n)$ | Non-negative weights | Cần cho weighted graphs |
| **Bellman-Ford** | $O(n \cdot m)$ | $O(n)$ | General weights | Cho phép trọng số âm |
| **Bidirectional BFS** | $O(n + m)$ worst, faster average | $O(n)$ | Unweighted, point query | Hiệu quả cho single-pair query |

### 3.3. Tại sao BFS tối ưu cho unweighted k-hop (Why BFS is Optimal)

**Tính chất cốt lõi (Core Property):** BFS khám phá đồ thị theo **tầng** (layer-by-layer). Sau khi xử lý tầng $i$, tất cả đỉnh trong $L_i(s)$ đã được tìm thấy. Điều này khớp hoàn hảo với cấu trúc k-hop:

$$\text{BFS dừng tự nhiên khi tầng hiện tại} > k$$

**So sánh chi tiết (Detailed Comparison):**

| Tiêu chí | BFS | DFS (depth-limited) | Dijkstra |
|---|---|---|---|
| **Tìm shortest path** | ✓ (đảm bảo) | ✗ (không đảm bảo) | ✓ (đảm bảo) |
| **Dừng tại k-hop** | ✓ (natural) | ✓ (explicit check) | ✓ (early termination) |
| **Time complexity** | $O(n + m)$ | $O(n + m)$ | $O((n + m) \log n)$ |
| **Space cho queue/stack** | $O(\|L_{\max}\|)$ | $O(k)$ | $O(n)$ priority queue |
| **Cache-friendly** | ✓ (sequential access) | ✗ (recursive, scattered) | ✗ (heap operations) |
| **Kết quả theo tầng** | ✓ (tự nhiên) | ✗ (cần post-processing) | ✓ (nhưng chậm hơn) |

---

## 4. Các biến thể và mở rộng (Variants and Extensions)

### 4.1. Reverse Reachability (Đạt được ngược)

Thay vì "đỉnh nào $s$ có thể đạt tới", hỏi "đỉnh nào có thể đạt tới $s$":

$$R_k^{-1}(s) = \{u \in V : d(u, s) \le k\}$$

Tương đương với BFS trên đồ thị đảo $G^T = (V, E^T)$ trong đó $E^T = \{(v, u) : (u, v) \in E\}$.

**Ứng dụng:** Sau khi phát hiện node bị tấn công, tìm các node mà attacker **có thể đã đi qua** để tới đó (truy vết nguồn gốc).

### 4.2. Multi-Source Reachability (Đạt được từ nhiều nguồn)

Cho tập nguồn $S = \{s_1, s_2, \ldots, s_q\}$, tìm:

$$R_k(S) = \bigcup_{i=1}^{q} R_k(s_i)$$

Giải bằng multi-source BFS: khởi tạo queue với tất cả $s_i$ ở tầng 0.

**Ứng dụng:** Khi nhiều node bị compromise cùng lúc (coordinated attack), blast radius là hợp của các tập đạt được.

### 4.3. Probabilistic Reachability (Đạt được xác suất)

Mỗi cung $(u, v)$ có xác suất "thành công" $p(u,v) \in [0, 1]$. Xác suất đạt được qua đường đi $P$:

$$\Pr[P] = \prod_{(a, b) \in P} p(a, b)$$

Tìm $\{v : \max_{P: s \leadsto v} \Pr[P] \ge \theta\}$ (các đỉnh có xác suất bị ảnh hưởng $\ge \theta$).

**Chuyển đổi (Reduction):** Lấy $-\log$ trọng số, bài toán trở thành shortest path trên đồ thị có trọng số dương → dùng Dijkstra.

### 4.4. Time-Bounded Reachability on Dynamic Graphs

Đồ thị thay đổi theo thời gian, và ta muốn tìm reachability chỉ sử dụng các cạnh "active" trong khoảng thời gian $[t_1, t_2]$.

---

## 5. Tham khảo chính (Key References)

1. **Cormen, T. H., Leiserson, C. E., Rivest, R. L., & Stein, C.** (2009). *Introduction to Algorithms* (3rd edition), Chapter 22.2: "Breadth-First Search." MIT Press. — BFS chuẩn mực.

2. **Cheng, J., Shang, Z., Cheng, H., Wang, H., & Yu, J. X.** (2014). "K-Reach: Who is in Your Small World." *The VLDB Journal*, 23(5), pp. 697–715. — K-hop reachability index cho large graphs.

3. **Yildirim, H., Chaoji, V., & Zaki, M. J.** (2010). "GRAIL: Scalable Reachability Index for Large Graphs." *Proceedings of the VLDB Endowment*, 3(1-2), pp. 276–284. — Reachability indexing.

4. **Jin, R., Xiang, Y., Ruan, N., & Fuhry, D.** (2009). "3-HOP: A High-Compression Indexing Scheme for Reachability Query." *Proceedings of the ACM SIGMOD*, pp. 813–826.

5. **Dijkstra, E. W.** (1959). "A Note on Two Problems in Connexion with Graphs." *Numerische Mathematik*, 1(1), pp. 269–271. — Thuật toán Dijkstra cho shortest path.

6. **Potamias, M., Bonchi, F., Castillo, C., & Gionis, A.** (2009). "Fast Shortest Path Distance Estimation in Large Networks." *Proceedings of the 18th ACM CIKM*, pp. 867–876.

---

## 6. Tóm tắt (Summary)

| Khía cạnh | Chi tiết |
|---|---|
| **Bài toán** | K-Hop Reachability — tìm tất cả đỉnh đạt được từ nguồn trong $k$ bước |
| **Input** | Đồ thị $G = (V, E)$, đỉnh nguồn $s$, giới hạn $k$ |
| **Output** | Tập $R_k(s) = \{v : d(s, v) \le k\}$ cùng khoảng cách |
| **Optimal (unweighted)** | $\Theta(n + m)$ — đạt bởi BFS |
| **Optimal (weighted)** | $\Theta((n + m) \log n)$ — đạt bởi Dijkstra |
| **BFS advantages** | Natural layering, early termination tại $k$, cache-friendly |
| **Mở rộng** | Reverse, multi-source, probabilistic, time-bounded |
| **Ứng dụng** | Blast radius, failure propagation, influence spread, truy vết tấn công |
