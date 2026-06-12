# Bài toán 2: Phát hiện thành phần liên thông mạnh trên đồ thị động
# Problem 2: Strongly Connected Component Detection on Dynamic Graphs

---

## 1. Đặt vấn đề (Problem Statement)

### 1.1. Bối cảnh tổng quát (General Context)

Trong nhiều hệ thống, các thực thể (entities) tương tác với nhau theo kiểu **có hướng** (directed): A gọi B, nhưng B không nhất thiết gọi lại A. Khi các tương tác này thay đổi theo thời gian — kết nối mới xuất hiện, kết nối cũ biến mất — ta cần liên tục phân tích **cấu trúc liên thông** của hệ thống để phát hiện các **cụm tương tác khép kín** (closed interaction clusters).

In many systems, entities interact in a **directed** manner: A calls B, but B does not necessarily call A back. As these interactions change over time — new connections appear, old ones disappear — we need to continuously analyze the **connectivity structure** of the system to detect **closed interaction clusters**.

**Câu hỏi cốt lõi (Core Question):** Cho một đồ thị có hướng mà cạnh được thêm/xóa theo thời gian, làm thế nào để duy trì hiệu quả thông tin về các **thành phần liên thông mạnh** (Strongly Connected Components — SCCs)?

### 1.2. Ví dụ trực quan (Intuitive Examples)

- **Mạng xã hội:** Người dùng A follow B, B follow C, C follow A → tạo thành một "nhóm kín" (SCC). Phát hiện các nhóm kín giúp đề xuất bạn bè và phát hiện bot networks.
- **Phụ thuộc phần mềm (Dependency graph):** Module A import B, B import C, C import A → phụ thuộc vòng tròn (circular dependency), thường là dấu hiệu thiết kế xấu.
- **Giao tiếp giữa dịch vụ:** Service Frontend gọi Backend, Backend gọi Database, nhưng nếu Database bất ngờ gọi Frontend → hình thành SCC bất thường, có thể là dấu hiệu vi phạm kiến trúc hoặc xâm nhập.

---

## 2. Định nghĩa hình thức (Formal Definition)

### 2.1. Đồ thị có hướng (Directed Graph)

**Định nghĩa (Definition):** Một **đồ thị có hướng** (directed graph) là bộ $G = (V, E)$ trong đó:
- $V = \{v_1, v_2, \ldots, v_n\}$ là tập **đỉnh** (vertices), $|V| = n$
- $E \subseteq V \times V$ là tập **cung** (directed edges / arcs), $|E| = m$
- Mỗi cung $(u, v) \in E$ biểu diễn kết nối **từ** $u$ **đến** $v$ (hướng quan trọng)

**Biểu diễn (Representation):** Sử dụng **danh sách kề** (Adjacency List):

$$\text{Adj}(u) = \{v \in V : (u, v) \in E\}$$

- Space: $O(n + m)$
- Kiểm tra cung $(u,v)$: $O(\deg^+(u))$ hoặc $O(1)$ nếu dùng hash set

### 2.2. Tính đạt được và liên thông mạnh (Reachability and Strong Connectivity)

**Tính đạt được (Reachability):** Đỉnh $v$ **đạt được** (reachable) từ $u$ nếu tồn tại đường đi có hướng từ $u$ đến $v$:

$$u \rightsquigarrow v \iff \exists \text{ path } u = w_0 \to w_1 \to \cdots \to w_k = v$$

**Liên thông mạnh (Strong Connectivity):** Hai đỉnh $u, v \in V$ được gọi là **liên thông mạnh** (strongly connected) nếu và chỉ nếu:

$$u \rightsquigarrow v \quad \text{và} \quad v \rightsquigarrow u$$

Đây là một **quan hệ tương đương** (equivalence relation) trên $V$:
- **Phản xạ (Reflexive):** $u \rightsquigarrow u$ (đường đi rỗng)
- **Đối xứng (Symmetric):** Nếu $u$ liên thông mạnh với $v$ thì $v$ liên thông mạnh với $u$
- **Bắc cầu (Transitive):** Nếu $u$ liên thông mạnh với $v$ và $v$ liên thông mạnh với $w$ thì $u$ liên thông mạnh với $w$

### 2.3. Thành phần liên thông mạnh (Strongly Connected Component — SCC)

**Định nghĩa (Definition):** Một **thành phần liên thông mạnh** (SCC) của đồ thị $G$ là một tập con **tối đại** (maximal) $C \subseteq V$ sao cho mọi cặp đỉnh trong $C$ đều liên thông mạnh:

$$\forall u, v \in C: \; u \rightsquigarrow v \quad \text{và} \quad v \rightsquigarrow u$$

**Tính chất quan trọng (Key Properties):**

1. **Phân hoạch (Partition):** Các SCC tạo thành một phân hoạch của $V$:
$$V = C_1 \sqcup C_2 \sqcup \cdots \sqcup C_k, \quad C_i \cap C_j = \emptyset \; \forall i \ne j$$

2. **DAG cô đọng (Condensation DAG):** Thu gọn mỗi SCC thành một đỉnh, đồ thị kết quả $G^{SCC}$ là một **DAG** (Directed Acyclic Graph). Nếu $G^{SCC}$ không phải DAG thì tồn tại chu trình giữa các SCC, mâu thuẫn với tính tối đại.

3. **Số lượng SCC:** Tối thiểu 1 (toàn bộ đồ thị liên thông mạnh) và tối đa $n$ (mỗi đỉnh là một SCC riêng, xảy ra khi đồ thị là DAG).

### 2.4. Bài toán SCC (SCC Problem — Static)

**Bài toán tĩnh (Static Problem):** Cho đồ thị có hướng $G = (V, E)$, tìm tất cả các SCC.

**Input:** $G = (V, E)$ với $|V| = n, |E| = m$
**Output:** Phân hoạch $\{C_1, C_2, \ldots, C_k\}$ của $V$ thành các SCC

### 2.5. Bài toán SCC động (Dynamic SCC Problem)

**Bài toán (Problem):** Duy trì phân hoạch SCC khi đồ thị thay đổi theo thời gian. Hỗ trợ các thao tác:

1. **INSERT($u$, $v$):** Thêm cung $(u, v)$ vào $E$
2. **DELETE($u$, $v$):** Xóa cung $(u, v)$ khỏi $E$
3. **QUERY($u$, $v$):** Hai đỉnh $u, v$ có thuộc cùng SCC không?
4. **GET_SCCS():** Trả về phân hoạch SCC hiện tại

**Các biến thể theo mức độ động (Variants by Dynamism):**

| Biến thể | INSERT | DELETE | Ứng dụng |
|---|---|---|---|
| **Static** | — | — | Phân tích một lần (snapshot) |
| **Incremental** | ✓ | — | Đồ thị chỉ tăng trưởng (connections accumulate) |
| **Decremental** | — | ✓ | Phân tích khi xóa kết nối (node failure) |
| **Fully Dynamic** | ✓ | ✓ | Mạng thay đổi liên tục |

---

## 3. Phân tích lý thuyết phức tạp (Theoretical Complexity Analysis)

### 3.1. Bài toán tĩnh — Giới hạn (Static Problem — Bounds)

**Giới hạn dưới (Lower Bound):** Tìm SCC cần ít nhất $\Omega(n + m)$ thời gian vì phải đọc toàn bộ input.

**Giới hạn trên (Upper Bound):** Thuật toán Tarjan (1972) và Kosaraju (1978) đều đạt $O(n + m)$ — **tối ưu**.

| Thuật toán | Time | Space | Số lần DFS | Đặc điểm |
|---|---|---|---|---|
| **Tarjan (1972)** | $O(n + m)$ | $O(n)$ | 1 | Single-pass, dùng low-link values |
| **Kosaraju (1978)** | $O(n + m)$ | $O(n)$ | 2 | Hai lần DFS (forward + reverse graph) |

### 3.2. Bài toán động — Giới hạn (Dynamic Problem — Bounds)

**Incremental (chỉ thêm cạnh):**
- **Naive:** Chạy lại Tarjan mỗi lần thêm cạnh → $O(m \cdot (n + m))$ tổng
- **Tốt nhất đã biết (2025):** Amortized speedup ~11.5× so với recomputation tĩnh cho single-update; ~5.0× cho batch-update

> *Tham khảo:* "Incremental Detection of Strongly Connected Components." *Journal of Computer Science and Technology*, 2025.

**Fully Dynamic (thêm + xóa cạnh):**
- **Worst-case:** $O(n^{6/7})$ per update (thông qua duy trì thứ tự topo)
- **Conditional lower bounds:** Có bằng chứng lý thuyết rằng thuật toán near-optimal cho fully dynamic SCC có thể không tồn tại (trừ khi các giả thuyết complexity theory bị phá vỡ)

### 3.3. Mô hình tính toán (Computational Model)

Các thuật toán SCC hoạt động trên **RAM model** với:
- Word size $w = O(\log n)$
- Pointer/index operations trong $O(1)$
- Biểu diễn đồ thị bằng adjacency list: space $O(n + m)$

---

## 4. Khái niệm liên quan (Related Concepts)

### 4.1. DAG cô đọng (Condensation DAG)

Khi thu gọn mỗi SCC thành một siêu đỉnh (super-vertex), đồ thị kết quả là DAG:

$$G^{SCC} = (V^{SCC}, E^{SCC})$$

trong đó:
- $V^{SCC} = \{C_1, C_2, \ldots, C_k\}$ (mỗi SCC là một đỉnh)
- $(C_i, C_j) \in E^{SCC} \iff \exists u \in C_i, v \in C_j: (u, v) \in E$

**Ứng dụng:** Thứ tự topo trên $G^{SCC}$ cho biết "hướng phụ thuộc" giữa các cụm dịch vụ.

### 4.2. DFS Tree và Classification of Edges

Trong DFS trên đồ thị có hướng, cạnh được phân loại thành:
- **Tree edges:** Cạnh trên cây DFS
- **Back edges:** Cạnh ngược về ancestor → **tạo thành chu trình** → đóng vai trò quan trọng trong SCC
- **Forward edges:** Cạnh tới descendant (không phải tree edge)
- **Cross edges:** Cạnh tới node đã xử lý xong ở nhánh khác

**Nhận xét quan trọng (Key Insight):** Một SCC chứa $\ge 2$ đỉnh khi và chỉ khi tồn tại ít nhất một **back edge** trong DFS traversal bên trong nó.

### 4.3. Low-link Value (Giá trị low-link)

**Định nghĩa (Definition — Tarjan):** Với mỗi đỉnh $v$, **low-link value** $\text{low}(v)$ là chỉ số discovery nhỏ nhất có thể đạt được từ $v$ thông qua các cạnh tree và back:

$$\text{low}(v) = \min\left(\text{disc}(v), \; \min_{(v, w) \in E} \begin{cases} \text{low}(w) & \text{if } w \text{ chưa assigned SCC} \\ \text{disc}(w) & \text{if } w \text{ trên stack} \end{cases}\right)$$

Nếu $\text{low}(v) = \text{disc}(v)$, thì $v$ là **root** (gốc) của một SCC.

---

## 5. Tham khảo chính (Key References)

1. **Tarjan, R. E.** (1972). "Depth-First Search and Linear Graph Algorithms." *SIAM Journal on Computing*, 1(2), pp. 146–160. — Thuật toán SCC tối ưu, nền tảng của lý thuyết đồ thị thuật toán.

2. **Sharir, M.** (1981). "A Strong-Connectivity Algorithm and its Application in Data Flow Analysis." *Computers & Mathematics with Applications*, 7(1), pp. 67–72. — Thuật toán Kosaraju-Sharir (hai lần DFS).

3. **Cormen, T. H., Leiserson, C. E., Rivest, R. L., & Stein, C.** (2009). *Introduction to Algorithms* (3rd edition), Chapter 22.5: "Strongly Connected Components." MIT Press.

4. **Bender, M. A., Fineman, J. T., & Gilbert, S.** (2016). "A New Approach to Incremental Cycle Detection and Related Problems." *ACM Transactions on Algorithms*, 12(2), Article 14. — Incremental SCC detection.

5. **Bernstein, A., Probst, M., & Wulff-Nilsen, C.** (2019). "Decremental Strongly-Connected Components and Single-Source Reachability in Near-Linear Time." *Proceedings of the 51st ACM STOC*, pp. 365–376.

6. **Karczmarz, A. & Łącki, J.** (2023). "Fully Dynamic Strongly Connected Components in Planar Digraphs." *Proceedings of the 34th ACM-SIAM SODA*.

---

## 6. Tóm tắt (Summary)

| Khía cạnh | Chi tiết |
|---|---|
| **Bài toán** | Phân hoạch đồ thị có hướng thành các SCC |
| **Input** | Đồ thị $G = (V, E)$, có thể thay đổi (dynamic) |
| **Output** | Phân hoạch $\{C_1, \ldots, C_k\}$ sao cho mỗi $C_i$ liên thông mạnh tối đại |
| **Static optimal** | $\Theta(n + m)$ — đạt bởi Tarjan và Kosaraju |
| **Dynamic (incremental)** | Amortized ~11.5× faster than recomputation |
| **Dynamic (fully)** | $O(n^{6/7})$ per update (best known worst-case) |
| **Ứng dụng** | Phát hiện phụ thuộc vòng tròn, cụm tương tác bất thường, phân tích kiến trúc |
| **Cấu trúc phụ trợ** | Condensation DAG, DFS tree, low-link values |
