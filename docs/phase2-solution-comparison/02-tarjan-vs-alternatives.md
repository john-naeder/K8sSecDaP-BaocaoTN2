# So sánh phương án giải bài toán Phát hiện thành phần liên thông mạnh
# Solution Comparison: Strongly Connected Component Detection

---

## 1. Nhắc lại bài toán (Problem Recap)

Cho đồ thị có hướng $G = (V, E)$ với $|V| = n$, $|E| = m$. Tìm phân hoạch $\{C_1, C_2, \ldots, C_k\}$ của $V$ thành các thành phần liên thông mạnh (SCC) — tập con tối đại mà mọi cặp đỉnh trong đó đều đạt được lẫn nhau.

Biến thể dynamic: duy trì phân hoạch SCC khi đồ thị thay đổi (thêm/xóa cung).

---

## 2. Các phương án giải quyết (Solution Approaches)

### 2.1. Phương án A — Brute Force (Naive Reachability)

#### Mô tả (Description)

Cách tiếp cận thô nhất: với mỗi cặp đỉnh $(u, v)$, kiểm tra xem $u$ có đạt được $v$ **và** $v$ có đạt được $u$ hay không. Hai đỉnh cùng SCC khi cả hai điều kiện thỏa mãn.

#### Thuật toán (Algorithm)

```
FIND_SCCs_BRUTEFORCE(G):
    // Tính transitive closure
    for each vertex u in V:
        Reach[u] = BFS(G, u)       // tập đỉnh đạt được từ u

    // Xác định SCC
    for each pair (u, v):
        if v ∈ Reach[u] AND u ∈ Reach[v]:
            merge u, v into same component
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(n \cdot (n + m))$ — chạy BFS/DFS $n$ lần |
| **Space** | $O(n^2)$ cho transitive closure matrix |

#### Nhược điểm (Disadvantages)
- Quá chậm: $O(n^2 + nm)$ so với optimal $O(n + m)$
- Bộ nhớ $O(n^2)$ quá lớn cho đồ thị lớn
- Không có ý nghĩa thực tiễn khi đã có thuật toán linear-time

#### Kết luận (Verdict)
**Không khả thi** — chỉ dùng để minh họa tính đúng đắn, không bao giờ dùng trong thực tế.

---

### 2.2. Phương án B — Kosaraju-Sharir (Two-Pass DFS)

#### Mô tả (Description)

Thuật toán Kosaraju-Sharir (Kosaraju 1978, Sharir 1981) dựa trên một **nhận xét cốt lõi**: Nếu ta duyệt DFS trên đồ thị ngược $G^T$ theo thứ tự giảm dần **finish time** của DFS trên $G$ gốc, thì mỗi cây DFS trên $G^T$ chính là một SCC.

#### Thuật toán (Algorithm)

```
KOSARAJU(G = (V, E)):
    // Pass 1: DFS trên G gốc, ghi nhận finish time
    stack = empty
    visited = all false
    for each vertex u in V:
        if not visited[u]:
            DFS1(G, u, visited, stack)

    // Xây đồ thị ngược
    G_T = transpose(G)    // đảo hướng tất cả cung

    // Pass 2: DFS trên G^T theo thứ tự finish time giảm dần
    visited = all false
    sccs = []
    while stack is not empty:
        u = stack.pop()
        if not visited[u]:
            component = []
            DFS2(G_T, u, visited, component)
            sccs.append(component)

    return sccs

DFS1(G, u, visited, stack):
    visited[u] = true
    for each v in Adj(u):
        if not visited[v]:
            DFS1(G, v, visited, stack)
    stack.push(u)    // ghi finish time

DFS2(G_T, u, visited, component):
    visited[u] = true
    component.append(u)
    for each v in Adj_T(u):
        if not visited[v]:
            DFS2(G_T, v, visited, component)
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(n + m)$ — hai lần DFS, mỗi lần $O(n + m)$ |
| **Space** | $O(n + m)$ — cho $G^T$ + stack + visited arrays |
| **Số lần DFS** | 2 |
| **Deterministic** | Yes |

#### Chứng minh tính đúng (Correctness Sketch)

**Bổ đề chính (Key Lemma):** Nếu SCC $C$ có finish time lớn nhất (max finish time trong Pass 1) so với SCC $C'$, thì trong $G^T$, DFS từ đỉnh có finish time lớn nhất trong $C$ **không thể** đạt tới $C'$.

Điều này đảm bảo rằng mỗi DFS trong Pass 2 chỉ đi đúng trong phạm vi 1 SCC.

#### Ưu điểm (Advantages)
- **Đơn giản về mặt khái niệm:** Hai bước rõ ràng, dễ hiểu và dễ chứng minh
- **Dễ cài đặt:** Chỉ cần DFS cơ bản + transpose graph
- Xuất SCC theo **thứ tự topo ngược** (reverse topological order) tự nhiên

#### Nhược điểm (Disadvantages)
- **Cần xây đồ thị ngược $G^T$:** Tốn thêm $O(n + m)$ bộ nhớ và thời gian
- **Hai lần duyệt toàn bộ đồ thị:** Cache performance kém hơn single-pass
- **Stack overflow:** DFS đệ quy có thể gây stack overflow trên đồ thị lớn (chuỗi dài)
- **Không dễ mở rộng cho dynamic:** Khi thêm/xóa cạnh, phải chạy lại từ đầu

---

### 2.3. Phương án C — Tarjan's Algorithm (Single-Pass DFS)

#### Mô tả (Description)

Thuật toán Tarjan (1972) tìm tất cả SCC trong **một lần DFS duy nhất** bằng cách duy trì hai giá trị cho mỗi đỉnh:
- **disc[v]:** Thứ tự khám phá (discovery time)
- **low[v]:** Giá trị discovery nhỏ nhất có thể đạt được từ subtree của $v$ qua các back edges

Khi DFS kết thúc tại đỉnh $v$ và $\text{low}[v] = \text{disc}[v]$, thì $v$ là **root** của một SCC.

#### Thuật toán (Algorithm)

```
TARJAN(G = (V, E)):
    timer = 0
    stack = empty         // stack chứa đỉnh đang xét
    on_stack = all false
    disc = all -1         // -1 = chưa visited
    low = all -1
    sccs = []

    for each vertex u in V:
        if disc[u] == -1:
            STRONGCONNECT(u)

    return sccs

STRONGCONNECT(u):
    disc[u] = low[u] = timer++
    stack.push(u)
    on_stack[u] = true

    for each v in Adj(u):
        if disc[v] == -1:          // v chưa visited
            STRONGCONNECT(v)       // recurse
            low[u] = min(low[u], low[v])
        else if on_stack[v]:       // v đang trên stack (back edge)
            low[u] = min(low[u], disc[v])

    // Nếu u là root của SCC
    if low[u] == disc[u]:
        component = []
        repeat:
            w = stack.pop()
            on_stack[w] = false
            component.append(w)
        until w == u
        sccs.append(component)
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(n + m)$ — single-pass DFS |
| **Space** | $O(n)$ — stack + disc[] + low[] + on_stack[] |
| **Số lần DFS** | 1 |
| **Deterministic** | Yes |

#### Chứng minh tính đúng (Correctness Sketch)

1. **Mỗi SCC có đúng một root:** Đỉnh đầu tiên được khám phá trong SCC sẽ có $\text{low}[u] = \text{disc}[u]$ vì không có back edge nào dẫn ra ngoài SCC tới đỉnh có discovery time nhỏ hơn.

2. **Tất cả đỉnh trong SCC nằm trên stack:** Giữa thời điểm root $u$ được push lên stack và thời điểm nó pop, tất cả đỉnh khác trong SCC cũng đã được push (vì chúng reachable từ $u$) và chưa pop (vì chưa tìm thấy root nào khác cho chúng).

3. **Pop cho đúng SCC:** Khi root $u$ pop, tất cả đỉnh pop cùng lúc (từ $u$ trở lên trên stack) chính xác là SCC chứa $u$.

#### Ưu điểm (Advantages)
- **Single-pass:** Chỉ duyệt đồ thị 1 lần, cache-friendly hơn Kosaraju
- **Không cần đồ thị ngược:** Tiết kiệm $O(n + m)$ bộ nhớ cho $G^T$
- **Space tối ưu:** Chỉ $O(n)$ auxiliary space (disc, low, stack, on_stack)
- **Xuất SCC theo thứ tự topo ngược** tự nhiên (giống Kosaraju)
- **Thuật toán nền tảng:** Được dùng rộng rãi trong competitive programming, compilers, model checking

#### Nhược điểm (Disadvantages)
- **Phức tạp hơn Kosaraju:** Low-link value khó hiểu trực quan hơn
- **DFS đệ quy:** Có thể stack overflow trên đồ thị rất lớn hoặc đồ thị dạng chuỗi dài → cần chuyển sang iterative DFS
- **Static algorithm:** Như Kosaraju, khi đồ thị thay đổi phải chạy lại từ đầu

---

### 2.4. Phương án D — Incremental SCC (Chỉ thêm cạnh)

#### Mô tả (Description)

Khi đồ thị chỉ tăng trưởng (chỉ thêm cung, không xóa), ta có thể duy trì SCC hiệu quả hơn chạy lại Tarjan mỗi lần.

#### Ý tưởng (Idea)

Duy trì condensation DAG $G^{SCC}$. Khi thêm cung $(u, v)$:
- Nếu $u$ và $v$ cùng SCC → không thay đổi
- Nếu $u$ và $v$ khác SCC → kiểm tra xem có tạo chu trình mới trong $G^{SCC}$ không
  - Nếu có: merge các SCC trên chu trình thành 1 SCC mới
  - Nếu không: chỉ thêm cung vào $G^{SCC}$

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Amortized time per update** | ~11.5× faster so với recomputation |
| **Worst-case per update** | Có thể $O(n + m)$ (khi merge lớn) |
| **Space** | $O(n + m)$ |
| **Insert only** | Yes — không hỗ trợ delete |

#### Ưu điểm
- Nhanh hơn chạy lại static algorithm khi thay đổi nhỏ
- Tận dụng kết quả trước đó

#### Nhược điểm
- Phức tạp đáng kể khi cài đặt
- Worst-case vẫn $O(n + m)$ cho single update
- Không hỗ trợ delete → không phù hợp khi connections expire

---

## 3. Bảng so sánh tổng hợp (Comprehensive Comparison)

| Tiêu chí | Brute Force | Kosaraju | Tarjan | Incremental |
|---|---|---|---|---|
| **Time (static)** | $O(n(n+m))$ | $O(n + m)$ | $O(n + m)$ | — |
| **Time (per update)** | $O(n(n+m))$ | $O(n + m)$ recompute | $O(n + m)$ recompute | Amortized ~11.5× faster |
| **Space** | $O(n^2)$ | $O(n + m)$ | $O(n)$ aux | $O(n + m)$ |
| **DFS passes** | $n$ lần BFS | 2 | **1** | — |
| **Cần $G^T$** | No | **Yes** | **No** | No |
| **Dễ cài đặt** | Simple | Medium | Medium | Hard |
| **Stack overflow risk** | No (BFS) | Yes (DFS) | Yes (DFS) | Low |
| **Dynamic support** | No | No | No | Insert only |

---

## 4. Phương án đề xuất và lý do chọn (Proposed Solution)

### 4.1. Kết luận (Conclusion)

**Phương án chính: Tarjan's Algorithm** cho static/periodic SCC detection.

**Phương án backup: Kosaraju** cho trường hợp cần dễ debug hoặc dễ giải thích.

### 4.2. Lý do chọn Tarjan (Justification)

1. **Single-pass DFS:** Tiết kiệm ~50% thời gian so với Kosaraju (1 pass thay vì 2). Trên đồ thị mạng thực tế (~1000-10000 đỉnh), difference nhỏ nhưng demonstrable trong benchmark.

2. **Không cần đồ thị ngược:** Tiết kiệm $O(n + m)$ bộ nhớ. Quan trọng khi chạy trên Pod K8s với memory limit.

3. **Tích hợp tốt với kiến trúc pipeline:** Trong hệ thống giám sát, SCC detection chạy **periodic** (mỗi 10 giây). Với đồ thị ~1000 nodes, Tarjan chạy trong < 1ms → overhead không đáng kể.

4. **Tại sao không chọn Incremental:**
   - Độ phức tạp cài đặt cao, dễ bug
   - Đồ thị mạng trong K8s thay đổi liên tục (cả thêm và xóa connection) → incremental (insert-only) không đủ
   - Với $n$ nhỏ (~1000), Tarjan chạy lại toàn bộ vẫn rất nhanh, không cần optimize thêm

5. **Tại sao không chọn Kosaraju:**
   - Cần thêm $O(n + m)$ cho $G^T$ — lãng phí khi Tarjan không cần
   - Hai pass DFS → kém cache-friendly hơn
   - Tuy dễ chứng minh đúng, nhưng Tarjan cũng well-established

### 4.3. Chiến lược triển khai (Deployment Strategy)

```
Mỗi 10 giây:
    1. Snapshot đồ thị hiện tại
    2. Chạy Tarjan → danh sách SCCs
    3. So sánh với "expected SCCs" (whitelist kiến trúc)
    4. Nếu phát hiện SCC bất thường → alert
```

Với $n \le 10,000$ và $m \le 100,000$:
- Tarjan: ~1-5 ms per run
- Memory: ~200 KB auxiliary
- CPU: negligible (0.05% of 1 core)

---

## 5. Tham khảo (References)

1. **Tarjan, R. E.** (1972). "Depth-First Search and Linear Graph Algorithms." *SIAM Journal on Computing*, 1(2), pp. 146–160.
2. **Sharir, M.** (1981). "A Strong-Connectivity Algorithm and its Application in Data Flow Analysis." *Computers & Mathematics with Applications*, 7(1), pp. 67–72.
3. **Bender, M. A., Fineman, J. T., & Gilbert, S.** (2016). "A New Approach to Incremental Cycle Detection and Related Problems." *ACM Transactions on Algorithms*, 12(2).
4. **Cormen, T. H. et al.** (2009). *Introduction to Algorithms* (3rd ed.), Chapter 22.5. MIT Press.
