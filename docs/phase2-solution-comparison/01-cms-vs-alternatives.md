# So sánh phương án giải bài toán Ước lượng tần suất trên luồng dữ liệu
# Solution Comparison: Stream Frequency Estimation

---

## 1. Nhắc lại bài toán (Problem Recap)

Cho luồng dữ liệu $S = (a_1, a_2, \ldots, a_m)$ với phần tử từ universe $U$, $|U| = n$, bộ nhớ $M \ll m$. Cần ước lượng tần suất $f_x$ của phần tử $x$ bất kỳ sao cho:

$$\Pr\left[|\hat{f}_x - f_x| \le \varepsilon \cdot m\right] \ge 1 - \delta$$

Bài toán yêu cầu single-pass, online processing, bounded memory.

---

## 2. Các phương án giải quyết (Solution Approaches)

### 2.1. Phương án A — Exact Hash Map (Bảng băm chính xác)

#### Mô tả (Description)

Cách tiếp cận trực tiếp nhất: sử dụng một hash map (bảng băm) $H: U \to \mathbb{N}$ lưu trữ đếm chính xác cho **mỗi phần tử phân biệt** đã xuất hiện trong luồng.

#### Thuật toán (Algorithm)

```
INITIALIZE:
    H = empty hash map

UPDATE(a_j):
    H[a_j] += 1

QUERY(x):
    return H[x]    // trả về 0 nếu x chưa xuất hiện
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Space** | $O(d \cdot (\log n + \log m))$ bits, với $d$ = số phần tử phân biệt, $d \le \min(n, m)$ |
| **Update time** | $O(1)$ amortized (hash map) |
| **Query time** | $O(1)$ amortized |
| **Accuracy** | **Chính xác tuyệt đối** — $\hat{f}_x = f_x$ luôn |
| **Loại lỗi** | Không có lỗi |

#### Ưu điểm (Advantages)
- Kết quả chính xác 100%, không cần điều chỉnh tham số
- Triển khai đơn giản, dễ debug
- Có thể liệt kê tất cả phần tử và tần suất (full enumeration)

#### Nhược điểm (Disadvantages)
- **Bộ nhớ không bounded:** Nếu attacker gửi gói tin từ hàng triệu IP khác nhau (IP spoofing), hash map phình to vô hạn → **bộ nhớ bùng nổ** (memory explosion)
- Trong worst-case: $d = n$ (mọi phần tử trong universe đều xuất hiện) → space = $O(n \cdot \log m)$, không khả thi khi $n$ lớn (ví dụ $n = 2^{32}$ cho IPv4)
- **Không phù hợp cho adversarial input:** Hacker có thể cố tình tạo ra $d$ rất lớn để gây denial-of-service trên chính hệ thống giám sát

#### Kết luận (Verdict)
Phù hợp làm **baseline benchmark** (so sánh độ chính xác), nhưng **không khả thi cho production** khi input có thể adversarial.

---

### 2.2. Phương án B — Misra-Gries / Heavy Hitters (Thuật toán đếm tần suất cao)

#### Mô tả (Description)

Thuật toán Misra-Gries (1982) duy trì tối đa $k$ cặp (phần tử, counter) tại mọi thời điểm. Ý tưởng cốt lõi: khi bộ nhớ đầy, **giảm đồng đều** tất cả counters và loại bỏ những phần tử có counter = 0.

#### Thuật toán (Algorithm)

```
INITIALIZE:
    T = empty associative array    // tối đa k entries
    k = ⌈1/ε⌉

UPDATE(a_j):
    if a_j ∈ T:
        T[a_j] += 1
    else if |T| < k:
        T[a_j] = 1
    else:
        // Bộ nhớ đầy: giảm tất cả counters
        for each (element, count) in T:
            count -= 1
            if count == 0:
                remove element from T

QUERY(x):
    if x ∈ T:
        return T[x]
    else:
        return 0
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Space** | $O(1/\varepsilon)$ entries = $O\!\left(\frac{1}{\varepsilon} \cdot (\log n + \log m)\right)$ bits |
| **Update time** | $O(1)$ amortized (với hash map cho $T$); $O(k)$ worst-case khi cần giảm tất cả |
| **Query time** | $O(1)$ |
| **Accuracy** | $f_x - \frac{m}{k+1} \le \hat{f}_x \le f_x$ — **underestimate only** |
| **Loại lỗi** | One-sided: luôn **thấp hơn hoặc bằng** giá trị thật |
| **Deterministic** | Có — không cần randomization |

#### Đảm bảo chính xác (Accuracy Guarantee)

**Định lý (Theorem — Misra & Gries, 1982):** Với $k = \lceil 1/\varepsilon \rceil$:

$$f_x - \varepsilon \cdot m \le \hat{f}_x \le f_x$$

**Chứng minh sketch:** Tổng lượng "mất mát" qua tất cả các lần giảm counters ≤ $m/(k+1)$ vì mỗi lần giảm loại bỏ ít nhất $k+1$ đơn vị khỏi tổng. Do đó counter của $x$ bị giảm tối đa $m/(k+1) \le \varepsilon \cdot m$.

#### Ưu điểm (Advantages)
- **Deterministic:** Không cần random, đảm bảo worst-case
- **Space tối ưu:** $O(1/\varepsilon)$ entries, match lower bound
- Giải trực tiếp bài toán Heavy Hitters: phần tử có $f_x \ge \varepsilon \cdot m$ **chắc chắn** nằm trong $T$

#### Nhược điểm (Disadvantages)
- **Underestimate:** Tần suất ước lượng luôn ≤ giá trị thật → khi dùng để phát hiện bất thường (ngưỡng cảnh báo), có thể **bỏ sót** (false negative) các phần tử đáng ngờ
- **Worst-case update $O(k)$:** Khi cần giảm tất cả counters, latency spike xảy ra → không phù hợp cho hệ thống real-time cần latency ổn định
- **Không hỗ trợ deletion tốt:** Khó mở rộng cho sliding window (phần tử cũ expire)
- Với $\varepsilon$ nhỏ (yêu cầu chính xác cao), $k$ lớn → tốn bộ nhớ hơn CMS

---

### 2.3. Phương án C — Count-Min Sketch (CMS)

#### Mô tả (Description)

Count-Min Sketch (Cormode & Muthukrishnan, 2005) sử dụng một **ma trận 2D** kích thước $d \times w$ cùng $d$ hàm hash **pairwise independent** $h_1, h_2, \ldots, h_d : U \to \{1, 2, \ldots, w\}$.

Mỗi phần tử được hash qua **tất cả $d$ hàm hash** và cập nhật $d$ counters. Khi truy vấn, lấy **giá trị nhỏ nhất** từ $d$ counters.

#### Thuật toán (Algorithm)

```
INITIALIZE:
    w = ⌈e/ε⌉           // e = 2.718... (Euler's number)
    d = ⌈ln(1/δ)⌉
    M[1..d][1..w] = all zeros
    Choose d pairwise independent hash functions h_1, ..., h_d

UPDATE(a_j):
    for i = 1 to d:
        M[i][h_i(a_j)] += 1

QUERY(x):
    return min_{i=1..d} M[i][h_i(x)]
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Space** | $d \times w = \lceil\ln(1/\delta)\rceil \times \lceil e/\varepsilon \rceil$ counters |
| | = $O\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$ counters, mỗi counter $O(\log m)$ bits |
| **Update time** | $O(d) = O\!\left(\log \frac{1}{\delta}\right)$ — cố định, deterministic |
| **Query time** | $O(d) = O\!\left(\log \frac{1}{\delta}\right)$ |
| **Accuracy** | $f_x \le \hat{f}_x \le f_x + \varepsilon \cdot m$ với xác suất $\ge 1 - \delta$ |
| **Loại lỗi** | One-sided: luôn **cao hơn hoặc bằng** giá trị thật (**overestimate**) |
| **Randomized** | Có — phụ thuộc vào chọn hash functions |

#### Đảm bảo chính xác (Accuracy Guarantee)

**Định lý (Theorem — Cormode & Muthukrishnan, 2005):**

Với $w = \lceil e/\varepsilon \rceil$ và $d = \lceil \ln(1/\delta) \rceil$:

$$\Pr\left[\hat{f}_x \le f_x + \varepsilon \cdot m\right] \ge 1 - \delta$$

và $\hat{f}_x \ge f_x$ luôn đúng (deterministic lower bound).

**Chứng minh sketch (Proof Sketch):**

1. **Overestimate luôn đúng:** $M[i][h_i(x)] \ge f_x$ vì counter này đếm $x$ và có thể đếm thêm phần tử khác hash cùng bucket.

2. **Bound cho một hàng:** Với hàng $i$, gọi $Y_i = M[i][h_i(x)] - f_x$ là lượng overcounting. Bằng tính chất pairwise independent:
$$E[Y_i] = \sum_{y \ne x} f_y \cdot \Pr[h_i(y) = h_i(x)] = \frac{\sum_{y \ne x} f_y}{w} \le \frac{m}{w}$$

3. **Áp dụng Markov:** $\Pr[Y_i \ge \varepsilon \cdot m] \le \frac{E[Y_i]}{\varepsilon \cdot m} \le \frac{m/w}{\varepsilon \cdot m} = \frac{1}{\varepsilon \cdot w} \le \frac{1}{e}$ (với $w = \lceil e/\varepsilon \rceil$).

4. **Lấy min $d$ hàng:** $\Pr[\hat{f}_x > f_x + \varepsilon \cdot m] = \Pr[\forall i: Y_i \ge \varepsilon \cdot m] \le (1/e)^d \le \delta$ (với $d = \lceil \ln(1/\delta) \rceil$).

#### Ưu điểm (Advantages)
- **Bộ nhớ cố định và bounded:** Không phụ thuộc $n$ hay $m$, chỉ phụ thuộc $\varepsilon$ và $\delta$. Attacker không thể gây memory explosion.
- **Update time hằng số:** $O(d)$ với $d$ thường rất nhỏ (3-7), không có worst-case spike
- **Overestimate (one-sided error):** Trong bối cảnh phát hiện bất thường, overestimate là **an toàn hơn** underestimate — có thể có false positive (báo nhầm) nhưng **không có false negative** (không bỏ sót) đối với phần tử thực sự vượt ngưỡng
- **Mergeable:** Hai CMS có thể cộng element-wise → phù hợp cho hệ thống phân tán
- **Hỗ trợ sliding window:** Kết hợp với time-bucketed scheme (chia thành nhiều CMS theo khoảng thời gian)

#### Nhược điểm (Disadvantages)
- **Randomized:** Cần hash functions tốt; kết quả phụ thuộc vào chọn hash
- **Overestimate bias:** Với dữ liệu skewed (phân bố lệch), overcounting có thể đáng kể cho phần tử tần suất thấp
- **Không thể liệt kê:** Không biết phần tử nào đã xuất hiện — chỉ trả lời point query
- **Trade-off $\varepsilon$-memory:** Muốn chính xác hơn ($\varepsilon$ nhỏ hơn) thì cần nhiều bộ nhớ hơn tuyến tính theo $1/\varepsilon$

#### Biến thể cải tiến: Count-Min Sketch with Conservative Update

```
UPDATE_CONSERVATIVE(a_j):
    current_estimate = min_{i=1..d} M[i][h_i(a_j)]
    for i = 1 to d:
        M[i][h_i(a_j)] = max(M[i][h_i(a_j)], current_estimate + 1)
```

Conservative update chỉ tăng counter lên mức `current_estimate + 1` thay vì tăng vô điều kiện. Điều này **giảm overcounting** đáng kể trong thực tế (giảm false positive rate 2-5x) mà không thay đổi worst-case bounds.

---

### 2.4. Phương án D — Count Sketch

#### Mô tả (Description)

Count Sketch (Charikar et al., 2004) tương tự CMS nhưng sử dụng thêm hàm sign $s_i: U \to \{-1, +1\}$ để **triệt tiêu noise** thay vì chỉ lấy min.

#### Thuật toán (Algorithm)

```
INITIALIZE:
    w = O(1/ε²)
    d = O(log(1/δ))
    M[1..d][1..w] = all zeros
    Choose d hash functions h_i and d sign functions s_i

UPDATE(a_j):
    for i = 1 to d:
        M[i][h_i(a_j)] += s_i(a_j)

QUERY(x):
    return median_{i=1..d}( s_i(x) · M[i][h_i(x)] )
```

#### Phân tích (Analysis)

| Tiêu chí | Giá trị |
|---|---|
| **Space** | $O\!\left(\frac{1}{\varepsilon^2} \cdot \log \frac{1}{\delta}\right)$ counters |
| **Accuracy** | $|\hat{f}_x - f_x| \le \varepsilon \cdot \sqrt{F_2}$ với xác suất $\ge 1 - \delta$ |
| **Loại lỗi** | Two-sided: có thể overestimate hoặc underestimate |

#### So sánh với CMS
- **Lỗi tốt hơn cho heavy hitters:** Bound theo $\sqrt{F_2}$ thay vì $F_1 = m$. Khi phân bố skewed (ít phần tử tần suất cao), $\sqrt{F_2} \ll m$ → chính xác hơn
- **Nhưng space lớn hơn:** $O(1/\varepsilon^2)$ so với $O(1/\varepsilon)$ → tốn bộ nhớ hơn đáng kể
- **Và lỗi two-sided:** Có thể underestimate → false negative trong anomaly detection

---

## 3. Bảng so sánh tổng hợp (Comprehensive Comparison)

### 3.1. Complexity Comparison

| Tiêu chí | Hash Map (Exact) | Misra-Gries | Count-Min Sketch | Count Sketch |
|---|---|---|---|---|
| **Space** | $O(d \cdot \log n)$ — unbounded | $O(1/\varepsilon)$ | $O\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$ | $O\!\left(\frac{1}{\varepsilon^2} \cdot \log \frac{1}{\delta}\right)$ |
| **Update** | $O(1)$ amortized | $O(1)$ amortized, $O(k)$ worst | $O\!\left(\log \frac{1}{\delta}\right)$ deterministic | $O\!\left(\log \frac{1}{\delta}\right)$ |
| **Query** | $O(1)$ | $O(1)$ | $O\!\left(\log \frac{1}{\delta}\right)$ | $O\!\left(\log \frac{1}{\delta}\right)$ |
| **Error type** | None | Underestimate | Overestimate | Two-sided |
| **Error bound** | 0 | $\le \varepsilon \cdot m$ | $\le \varepsilon \cdot m$ | $\le \varepsilon \cdot \sqrt{F_2}$ |
| **Deterministic** | Yes | Yes | No | No |
| **Adversary-safe** | **No** | Yes | Yes | Yes |
| **Mergeable** | Yes (union) | Complex | **Yes (add)** | Yes (add) |
| **Sliding window** | Complex | Hard | **Easy (bucketed)** | Easy (bucketed) |

### 3.2. Ví dụ số liệu thực tế (Practical Numbers)

Với $\varepsilon = 0.001$ (sai số 0.1%) và $\delta = 0.01$ (xác suất thất bại 1%):

| Phương án | Số counters | Bộ nhớ (4 bytes/counter) |
|---|---|---|
| Hash Map | $d$ entries (có thể hàng triệu) | **Không bounded** |
| Misra-Gries | 1,000 | ~4 KB |
| Count-Min Sketch | $\lceil e/0.001 \rceil \times \lceil \ln(100) \rceil = 2,719 \times 5 = 13,595$ | ~53 KB |
| Count Sketch | $O(1/0.001^2) \times 5 = 5,000,000$ | ~19 MB |

→ CMS tốn ~53 KB cho accuracy 0.1% — **cực kỳ hiệu quả**.

### 3.3. Phân tích theo kịch bản ứng dụng (Scenario Analysis)

#### Kịch bản 1: Traffic monitoring bình thường
- Số IP phân biệt: ~10,000
- Hash Map: ~80 KB — OK
- CMS: ~53 KB — OK, gần tương đương
- **Winner:** Hash Map (chính xác hơn, chi phí tương đương)

#### Kịch bản 2: Đang bị tấn công DDoS/port scan
- Attacker spoof hàng triệu IP → $d$ bùng nổ
- Hash Map: hàng GB → **crash / OOM kill**
- CMS: vẫn 53 KB — **không thay đổi**
- **Winner:** CMS (bounded memory, adversary-resistant)

#### Kịch bản 3: Hệ thống phân tán (multi-node K8s)
- Cần merge kết quả từ 3 nodes
- Hash Map: merge bằng union → bandwidth lớn
- CMS: merge bằng element-wise addition → bandwidth = size of sketch (~53 KB)
- **Winner:** CMS (mergeable, fixed-size transfer)

---

## 4. Phương án đề xuất và lý do chọn (Proposed Solution)

### 4.1. Kết luận (Conclusion)

**Phương án tối ưu cho bài toán phát hiện bất thường trên luồng dữ liệu mạng: Count-Min Sketch (CMS) với Conservative Update.**

### 4.2. Lý do chọn CMS (Justification)

1. **Bounded memory dưới adversarial input:** Đây là yếu tố quyết định. Hệ thống bảo mật **phải** hoạt động ổn định ngay cả khi đang bị tấn công. Hash Map thất bại ở tiêu chí này.

2. **One-sided overestimate phù hợp cho anomaly detection:** Trong phát hiện bất thường, false positive (báo nhầm, cần kiểm tra thêm) ít nghiêm trọng hơn false negative (bỏ sót tấn công thật). CMS overestimate → có thể false positive nhưng không false negative cho heavy hitters.

3. **Deterministic update time:** $O(d)$ per element, không có latency spike. So với Misra-Gries có worst-case $O(k)$.

4. **Native sliding window support:** Kết hợp CMS với time-bucketed scheme (mỗi bucket 1 phút → 5 CMS cho window 5 phút, xoay vòng) → tự nhiên và hiệu quả.

5. **Mergeability cho distributed deployment:** Trên K8s cluster 3 nodes, mỗi node duy trì CMS riêng. Merge bằng element-wise max/add để có global view.

### 4.3. Tham số đề xuất (Recommended Parameters)

Cho ứng dụng phát hiện port scanning trên K8s cluster:

| Tham số | Giá trị | Lý do |
|---|---|---|
| $\varepsilon$ | 0.001 (0.1%) | Sai số đủ nhỏ cho anomaly detection |
| $\delta$ | 0.01 (1%) | Xác suất thất bại thấp |
| $w$ (width) | 2,048 (power of 2 cho fast modulo) | $\ge \lceil e/0.001 \rceil = 2,719$ → làm tròn xuống power of 2 gần nhất đủ lớn |
| $d$ (depth) | 5 | $\ge \lceil \ln(100) \rceil = 5$ |
| Window | 5 phút, chia 5 buckets × 1 phút | Phát hiện scan trong khoảng ngắn |
| Threshold | 100 connections/window | IP kết nối >100 lần trong 5 phút → cảnh báo |

**Bộ nhớ thực tế:** $5 \times 2048 \times 4$ bytes = **40 KB** per node. Cực kỳ nhẹ.

---

## 5. Tham khảo (References)

1. **Cormode, G. & Muthukrishnan, S.** (2005). "An Improved Data Stream Summary: The Count-Min Sketch and its Applications." *Journal of Algorithms*, 55(1), pp. 58–75.
2. **Misra, J. & Gries, D.** (1982). "Finding Repeated Elements." *Science of Computer Programming*, 2(2), pp. 143–152.
3. **Charikar, M., Chen, K., & Farach-Colton, M.** (2004). "Finding Frequent Items in Data Streams." *Theoretical Computer Science*, 312(1), pp. 3–15.
4. **Estan, C. & Varghese, G.** (2003). "New Directions in Traffic Measurement and Accounting: Focusing on the Elephants, Ignoring the Mice." *ACM Transactions on Computer Systems*, 21(3), pp. 270–313. — Conservative update variant.
5. **Cormode, G. & Hadjieleftheriou, M.** (2008). "Finding Frequent Items in Data Streams." *Proceedings of the VLDB Endowment*, 1(2), pp. 1530–1541. — Experimental comparison.
