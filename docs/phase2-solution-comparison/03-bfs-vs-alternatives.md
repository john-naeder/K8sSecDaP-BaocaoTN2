# So sánh phương án giải bài toán Phân tích khả năng đạt được theo k bước
# Solution Comparison: K-Hop Reachability (Blast Radius Analysis)

---

## 1. Nhắc lại bài toán (Problem Recap)

Cho đồ thị có hướng $G = (V, E)$ với $|V| = n$, $|E| = m$, đỉnh nguồn $s \in V$, và giới hạn $k \ge 0$. Tìm tập:

$$R_k(s) = \{v \in V : d(s, v) \le k\}$$

cùng với khoảng cách $d(s, v)$ cho mỗi $v \in R_k(s)$.

Trong ngữ cảnh bảo mật: $s$ là node bị compromise, $k$ là giới hạn lateral movement, $R_k(s)$ là **blast radius** — tập các node có nguy cơ bị ảnh hưởng.

---

## 2. Các phương án giải quyết (Solution Approaches)

### 2.1. Phương án A — Brute Force (All-Pairs Shortest Path)

#### Mô tả (Description)

Tính toán khoảng cách từ $s$ đến **tất cả** đỉnh bằng cách xây dựng ma trận khoảng cách toàn bộ (Floyd-Warshall hoặc chạy BFS/Dijkstra từ mọi đỉnh).

#### Thuật toán (Algorithm)

```
FLOYD_WARSHALL(G):
    dist[u][v] = ∞ for all u, v
    dist[u][u] = 0 for all u
    dist[u][v] = 1 for all (u,v) ∈ E

    for k = 1 to n:
        for i = 1 to n:
            for j = 1 to n:
                dist[i][j] = min(dist[i][j], dist[i][k] + dist[k][j])

K_HOP_QUERY(s, k):
    return {v : dist[s][v] ≤ k}
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Preprocessing** | $O(n^3)$ |
| **Space** | $O(n^2)$ |
| **Query time** | $O(n)$ — scan row $s$ |

#### Nhược điểm (Disadvantages)
- $O(n^3)$ preprocessing quá chậm cho đồ thị lớn
- $O(n^2)$ bộ nhớ không khả thi khi $n > 10,000$
- Overkill: ta chỉ cần single-source, không cần all-pairs
- Khi đồ thị thay đổi, phải tính lại toàn bộ

#### Kết luận (Verdict)
**Không phù hợp** — quá lãng phí tài nguyên cho bài toán single-source.

---

### 2.2. Phương án B — Depth-First Search giới hạn độ sâu (Depth-Limited DFS)

#### Mô tả (Description)

Duyệt DFS từ $s$ nhưng dừng khi độ sâu vượt quá $k$. Tìm tất cả đỉnh reachable nhưng **không đảm bảo** tìm đường đi ngắn nhất.

#### Thuật toán (Algorithm)

```
DEPTH_LIMITED_DFS(G, s, k):
    reachable = {}

    DFS(u, depth):
        if depth > k:
            return
        if u ∈ reachable AND reachable[u] ≤ depth:
            return    // đã visited với khoảng cách tốt hơn
        reachable[u] = depth
        for each v in Adj(u):
            DFS(v, depth + 1)

    DFS(s, 0)
    return reachable
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(n + m)$ worst-case, nhưng có thể revisit đỉnh → thực tế chậm hơn BFS |
| **Space** | $O(k)$ stack depth (recursion) + $O(n)$ visited |
| **Shortest path** | **Không đảm bảo** — DFS có thể tìm đường dài trước đường ngắn |

#### Vấn đề quan trọng: Không tìm shortest path

Xét ví dụ:
```
s → A → B → C
s → C           (direct edge, distance 1)
```

DFS có thể duyệt `s → A → B → C` trước, ghi $d(s, C) = 3$. Khi DFS gặp cạnh `s → C` sau, nó cập nhật $d(s, C) = 1$ nhưng **phải revisit** toàn bộ subtree từ $C$.

→ Worst-case: DFS revisit cùng đỉnh nhiều lần, thời gian thực tế có thể $O(n \cdot m)$ cho đồ thị dense.

#### Ưu điểm (Advantages)
- Stack space nhỏ: $O(k)$ nếu chỉ cần reachability (không cần shortest path)
- Có thể dùng iterative deepening (IDA*) để khắc phục thiếu shortest path

#### Nhược điểm (Disadvantages)
- **Không tìm shortest path:** Khoảng cách ghi nhận có thể sai → phân tầng blast radius không chính xác
- **Revisit problem:** Phải revisit đỉnh khi tìm đường ngắn hơn → performance không ổn định
- **Không natural k-hop layering:** Phải post-process để phân tầng kết quả
- **Stack overflow risk:** Trên đồ thị có chuỗi dài, DFS đệ quy có thể crash

#### Kết luận (Verdict)
**Không phù hợp** cho blast radius — cần shortest path chính xác để phân tầng. Chỉ phù hợp khi chỉ cần biết "reachable hay không" mà không cần khoảng cách.

---

### 2.3. Phương án C — Breadth-First Search (BFS)

#### Mô tả (Description)

BFS duyệt đồ thị **theo tầng** (layer-by-layer): tầng 0 chứa $s$, tầng 1 chứa hàng xóm trực tiếp của $s$, tầng 2 chứa hàng xóm của tầng 1 chưa visited, v.v. Thuật toán dừng tự nhiên khi tầng hiện tại vượt $k$.

#### Thuật toán (Algorithm)

```
BFS_K_HOP(G, s, k):
    dist = {s: 0}
    queue = [s]
    layers = [[s]]      // layers[0] = [s]

    while queue is not empty:
        u = queue.dequeue()
        if dist[u] >= k:
            continue     // dừng mở rộng từ tầng k

        for each v in Adj(u):
            if v ∉ dist:                    // chưa visited
                dist[v] = dist[u] + 1
                queue.enqueue(v)

                // Phân tầng
                while len(layers) <= dist[v]:
                    layers.append([])
                layers[dist[v]].append(v)

    return dist, layers
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(|R_k(s)| + |E(R_k(s))|)$ ≤ $O(n + m)$ |
| **Space** | $O(|R_k(s)|)$ cho dist map + queue |
| **Shortest path** | **Đảm bảo** — BFS luôn tìm shortest path trên unweighted graph |
| **Layering** | **Tự nhiên** — kết quả sẵn theo tầng |

#### Chứng minh shortest path (Correctness Proof Sketch)

**Định lý (Theorem):** Trên đồ thị unweighted, BFS tìm shortest path từ $s$ đến mọi đỉnh reachable.

**Chứng minh bằng quy nạp trên khoảng cách $d$:**
- **Base:** $d(s, s) = 0$ — đúng.
- **Inductive step:** Giả sử BFS tìm đúng tất cả đỉnh ở khoảng cách $\le d$. Xét đỉnh $v$ với $d(s, v) = d + 1$. Tồn tại $u$ với $d(s, u) = d$ và $(u, v) \in E$. Theo giả thuyết quy nạp, $u$ đã được xử lý đúng. Khi BFS xử lý $u$, nó enqueue $v$ (nếu chưa visited) với $\text{dist}[v] = d + 1$. Vì BFS xử lý theo thứ tự FIFO, không có đỉnh nào ở tầng $< d$ xử lý sau $u$ → $v$ không thể được gán khoảng cách nhỏ hơn $d + 1$ trước đó. □

#### Tính chất early termination

BFS có **early termination tự nhiên** cho k-hop:
- Khi tầng hiện tại = $k$, ta vẫn enqueue các đỉnh ở tầng $k$ nhưng không mở rộng tiếp
- Tổng số đỉnh duyệt = $|R_k(s)|$, có thể $\ll n$
- Tổng số cung duyệt = số cung từ đỉnh trong $R_{k-1}(s)$ = $|E(R_{k-1}(s))|$

→ Với $k$ nhỏ (1-3) và đồ thị sparse, BFS chạy trong $O(|R_k(s)| \cdot \bar{d})$ với $\bar{d}$ là bậc trung bình — rất nhanh.

#### Ưu điểm (Advantages)
- **Shortest path đảm bảo** trên unweighted graph
- **Natural layering:** Kết quả sẵn theo tầng — layer 1 = immediate risk, layer 2 = secondary risk, etc.
- **Early termination:** Chỉ duyệt đúng phần cần thiết
- **Deterministic:** Không randomization, kết quả luôn đúng
- **Cache-friendly:** Queue truy cập tuần tự, không có random jumps
- **No stack overflow:** Dùng queue (iterative), không đệ quy

#### Nhược điểm (Disadvantages)
- **Chỉ cho unweighted graph:** Nếu cạnh có trọng số (chi phí tấn công khác nhau), BFS không cho kết quả đúng
- **Queue size:** Trong worst-case, queue chứa toàn bộ một tầng. Trên đồ thị có hub nodes (bậc rất cao), tầng 1 có thể rất lớn → memory spike (nhưng vẫn $O(n)$)

---

### 2.4. Phương án D — Dijkstra's Algorithm (Weighted Shortest Path)

#### Mô tả (Description)

Thuật toán Dijkstra (1959) giải bài toán shortest path trên đồ thị **có trọng số không âm**. Sử dụng priority queue (min-heap) để luôn xử lý đỉnh có khoảng cách nhỏ nhất trước.

#### Thuật toán (Algorithm)

```
DIJKSTRA_K_HOP(G, s, k, weight):
    dist = {s: 0}
    pq = min_heap([(0, s)])    // (distance, vertex)

    while pq is not empty:
        (d_u, u) = pq.extract_min()

        if d_u > k:
            break              // early termination

        if d_u > dist.get(u, ∞):
            continue           // stale entry

        for each (u, v, w) in weighted_edges(u):
            d_v = d_u + w
            if d_v < dist.get(v, ∞):
                dist[v] = d_v
                pq.insert((d_v, v))

    return {v: d for v, d in dist.items() if d ≤ k}
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O((n + m) \log n)$ với binary heap |
| **Space** | $O(n)$ cho dist + $O(n + m)$ cho priority queue |
| **Shortest path** | **Đảm bảo** cho non-negative weights |
| **Weighted** | **Có** — hỗ trợ trọng số cạnh |

#### Ưu điểm (Advantages)
- **Hỗ trợ trọng số:** Khi cạnh có "chi phí" khác nhau (ví dụ: xác suất tấn công thành công, latency, risk score), Dijkstra cho kết quả chính xác
- **Early termination:** Khi extract_min() > $k$ → dừng ngay
- **Well-established:** Thuật toán kinh điển, triển khai ổn định

#### Nhược điểm (Disadvantages)
- **Chậm hơn BFS:** $O((n + m) \log n)$ so với $O(n + m)$ — yếu tố $\log n$ do heap operations
- **Overkill cho unweighted:** Khi tất cả trọng số = 1, Dijkstra degenerate thành BFS chậm hơn
- **Priority queue overhead:** Mỗi insert/extract_min tốn $O(\log n)$ — cache-unfriendly do heap structure
- **Nhiều bộ nhớ hơn:** Priority queue có thể chứa $O(m)$ entries (nếu dùng lazy deletion)

---

### 2.5. Phương án E — Bidirectional BFS (BFS hai chiều)

#### Mô tả (Description)

Dành cho bài toán **point query**: kiểm tra $d(s, t) \le k$ cho một cặp $(s, t)$ cụ thể. Chạy BFS đồng thời từ $s$ (forward) và từ $t$ trên $G^T$ (backward). Khi hai "sóng" gặp nhau → tìm được path.

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Time** | $O(b^{k/2})$ trung bình với $b$ = branching factor, so với $O(b^k)$ cho one-directional |
| **Space** | $O(b^{k/2})$ |
| **Áp dụng** | Chỉ cho single-pair query |

#### Nhược điểm cho bài toán blast radius
- Cần đồ thị ngược $G^T$
- Chỉ kiểm tra một cặp, không tìm toàn bộ $R_k(s)$
- Không phù hợp khi cần **tất cả** đỉnh trong blast radius

#### Kết luận (Verdict)
**Không phù hợp** cho blast radius (cần toàn bộ $R_k(s)$, không chỉ single-pair query).

---

## 3. Bảng so sánh tổng hợp (Comprehensive Comparison)

### 3.1. Complexity Comparison

| Tiêu chí | Floyd-Warshall | DFS (depth-limited) | BFS | Dijkstra | Bidirectional BFS |
|---|---|---|---|---|---|
| **Time** | $O(n^3)$ | $O(n + m)$* | $O(n + m)$ | $O((n+m)\log n)$ | $O(b^{k/2})$ avg |
| **Space** | $O(n^2)$ | $O(k + n)$ | $O(n)$ | $O(n + m)$ | $O(b^{k/2})$ |
| **Shortest path** | Yes | **No** | **Yes** | Yes | Yes |
| **Weighted** | Yes | No | **No** | **Yes** | No |
| **Layering** | Post-process | Post-process | **Natural** | Post-process | N/A |
| **Early termination** | No | At depth $k$ | At layer $k$ | At dist $k$ | At meeting |
| **Full $R_k(s)$** | Yes | Yes* | **Yes** | Yes | No |
| **Cache-friendly** | No | No | **Yes** | No | Moderate |

*DFS tìm reachable set nhưng không đảm bảo shortest path, nên phân tầng có thể sai.

### 3.2. Phân tích theo kịch bản (Scenario Analysis)

#### Kịch bản 1: Đồ thị unweighted, cần blast radius đầy đủ (Primary use case)
- Node bị compromise, cần biết tất cả nodes at risk + phân theo tầng
- **Winner: BFS** — optimal $O(n + m)$, natural layering, shortest path đảm bảo

#### Kịch bản 2: Đồ thị có weighted edges (risk scores)
- Mỗi connection có "risk score" khác nhau, muốn tìm nodes với total risk ≤ threshold
- **Winner: Dijkstra** — cần weighted shortest path

#### Kịch bản 3: Chỉ cần kiểm tra "node X có nằm trong blast radius không?"
- Single-pair reachability query
- **Winner: Bidirectional BFS** — nhanh hơn one-directional BFS cho sparse graphs

#### Kịch bản 4: Đồ thị rất lớn, bộ nhớ hạn chế
- $n > 100,000$ nhưng chỉ quan tâm $k = 2$
- **Winner: BFS** — chỉ duyệt $|R_2(s)|$ đỉnh, early termination mạnh

---

## 4. Phương án đề xuất và lý do chọn (Proposed Solution)

### 4.1. Kết luận (Conclusion)

**Phương án chính: BFS** cho k-hop blast radius analysis trên đồ thị unweighted.

**Phương án phụ: Dijkstra** — implement sẵn trong library cho trường hợp weighted graph trong tương lai.

### 4.2. Lý do chọn BFS (Justification)

1. **Optimal time complexity:** $O(n + m)$ — không thể nhanh hơn vì phải đọc ít nhất tất cả cung từ đỉnh trong $R_k(s)$.

2. **Natural layering khớp hoàn hảo với blast radius:**
   - Layer 0: Node bị compromise (nguồn)
   - Layer 1: Direct connections — **nguy cơ cao nhất**, cần isolate ngay
   - Layer 2: Secondary connections — **nguy cơ trung bình**, cần monitor
   - Layer 3+: Tertiary — **nguy cơ thấp**, nên audit

   BFS cho kết quả này **tự nhiên**, không cần post-processing.

3. **Đồ thị mạng trong K8s là unweighted:** Mỗi TCP connection hoặc tồn tại hoặc không — không có trọng số tự nhiên. Do đó BFS là công cụ chính xác nhất.

4. **Early termination hiệu quả:** Trong thực tế, blast radius thường giới hạn $k = 2$ hoặc $k = 3$. Với đồ thị K8s (~100-1000 pods, sparse connections), BFS chỉ duyệt vài chục đỉnh → thời gian < 1ms.

5. **No stack overflow:** BFS dùng queue (iterative) — an toàn cho mọi kích thước đồ thị. DFS đệ quy có thể crash trên đồ thị dạng chuỗi dài.

6. **Cache-friendly memory access:** Queue truy cập tuần tự → tận dụng CPU cache tốt hơn DFS (random stack jumps) và Dijkstra (heap operations).

### 4.3. Tại sao vẫn implement Dijkstra (Why Also Implement Dijkstra)

- **Extensibility:** Trong tương lai, có thể muốn gán weight cho connections (ví dụ: risk score dựa trên protocol — SSH riskier than HTTP)
- **Benchmark comparison:** So sánh BFS vs Dijkstra trên cùng dataset cho thấy overhead của priority queue
- **Academic completeness:** Trong work paper, cần implement cả hai để chứng minh BFS optimal cho unweighted case

### 4.4. Chiến lược triển khai (Deployment Strategy)

```
Khi Count-Min Sketch phát hiện malicious IP:
    1. Xác định node (Pod) tương ứng trong đồ thị
    2. Chạy BFS(graph, malicious_node, k=3)
    3. Output:
       - Layer 1: [pod-A, pod-B]         → ALERT: HIGH RISK
       - Layer 2: [pod-C, pod-D, pod-E]  → ALERT: MEDIUM RISK
       - Layer 3: [pod-F]                → ALERT: LOW RISK
    4. Ghi vào alerts.json cho visualization
```

---

## 5. Tham khảo (References)

1. **Cormen, T. H. et al.** (2009). *Introduction to Algorithms* (3rd ed.), Chapter 22.2: "Breadth-First Search." MIT Press.
2. **Dijkstra, E. W.** (1959). "A Note on Two Problems in Connexion with Graphs." *Numerische Mathematik*, 1(1), pp. 269–271.
3. **Cheng, J. et al.** (2014). "K-Reach: Who is in Your Small World." *The VLDB Journal*, 23(5), pp. 697–715.
4. **Potamias, M. et al.** (2009). "Fast Shortest Path Distance Estimation in Large Networks." *Proceedings of the 18th ACM CIKM*, pp. 867–876.
5. **Even, S.** (2011). *Graph Algorithms* (2nd ed.), Chapter 3. Cambridge University Press.
