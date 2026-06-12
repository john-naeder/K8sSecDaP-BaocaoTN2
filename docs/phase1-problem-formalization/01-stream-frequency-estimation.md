# Bài toán 1: Ước lượng tần suất trên luồng dữ liệu
# Problem 1: Stream Frequency Estimation

---

## 1. Đặt vấn đề (Problem Statement)

### 1.1. Bối cảnh tổng quát (General Context)

Trong nhiều hệ thống xử lý dữ liệu, ta đối mặt với một **luồng dữ liệu liên tục** (data stream) — một chuỗi các phần tử đến theo thời gian thực mà ta chỉ được quan sát **một lần duy nhất** (single-pass). Luồng dữ liệu này có thể rất lớn, thậm chí vô hạn, trong khi bộ nhớ khả dụng bị giới hạn nghiêm ngặt.

In many data processing systems, we face a **continuous data stream** — a sequence of elements arriving in real-time that can only be observed **once** (single-pass). This stream can be extremely large, even unbounded, while available memory is strictly limited.

**Câu hỏi cốt lõi (Core Question):** Làm thế nào để ước lượng tần suất xuất hiện của một phần tử cụ thể trong luồng dữ liệu, khi ta không có đủ bộ nhớ để lưu trữ toàn bộ lịch sử?

### 1.2. Ví dụ trực quan (Intuitive Examples)

- **Đếm từ khóa tìm kiếm:** Google nhận hàng tỷ truy vấn mỗi ngày. Để biết từ khóa nào "trending", cần đếm tần suất mỗi truy vấn — nhưng không thể lưu bảng đếm cho mọi chuỗi truy vấn có thể có.
- **Phát hiện bất thường mạng:** Một router xử lý hàng triệu gói tin mỗi giây. Để phát hiện IP nào đang gửi quá nhiều gói tin (dấu hiệu tấn công), cần đếm tần suất mỗi IP — nhưng không gian địa chỉ IPv4 đã là 2³² ≈ 4.3 tỷ.
- **Phân tích nhật ký (log):** Hệ thống phân tán sinh ra hàng triệu dòng log mỗi phút. Cần xác định mã lỗi nào xuất hiện nhiều nhất để ưu tiên xử lý.

---

## 2. Định nghĩa hình thức (Formal Definition)

### 2.1. Mô hình luồng dữ liệu (Data Stream Model)

**Định nghĩa (Definition):** Cho một **universe** (không gian phần tử) $U = \{1, 2, \ldots, n\}$ với $n$ phần tử phân biệt. Một **luồng dữ liệu** (data stream) là một chuỗi hữu hạn:

$$S = (a_1, a_2, \ldots, a_m)$$

trong đó mỗi $a_j \in U$ và $m$ là tổng số phần tử trong luồng (có thể rất lớn).

**Ràng buộc mô hình (Model Constraints):**
- **Single-pass:** Mỗi phần tử $a_j$ chỉ được đọc đúng một lần theo thứ tự đến.
- **Bounded memory:** Bộ nhớ khả dụng $M \ll m$ (thường $M = O(\text{polylog}(m, n))$).
- **Online processing:** Kết quả phải có sẵn tại mọi thời điểm, không cần đợi kết thúc luồng.

### 2.2. Tần suất và moment (Frequency and Moments)

**Định nghĩa tần suất (Frequency Definition):** Với mỗi phần tử $i \in U$, **tần suất** (frequency) của $i$ trong luồng $S$ là:

$$f_i = |\{j : a_j = i, \; 1 \le j \le m\}|$$

tức là số lần phần tử $i$ xuất hiện trong luồng. Vector tần suất là $\mathbf{f} = (f_1, f_2, \ldots, f_n)$.

**Frequency Moment bậc $k$ (Frequency Moment of order $k$):**

$$F_k = \sum_{i=1}^{n} f_i^k$$

Các trường hợp đặc biệt:
- $F_0$ = số phần tử phân biệt (distinct elements) trong luồng
- $F_1 = m$ = tổng số phần tử (length of stream)
- $F_2 = \sum f_i^2$ = "repeat rate" hoặc "self-join size"

**Chuẩn $L_1$ của vector tần suất ($L_1$ norm):**

$$\|\mathbf{f}\|_1 = \sum_{i=1}^{n} f_i = F_1 = m$$

### 2.3. Bài toán Point Query (Point Query Problem)

**Bài toán (Problem):** Thiết kế một cấu trúc dữ liệu $\mathcal{D}$ hỗ trợ hai thao tác:

1. **UPDATE($\mathcal{D}$, $a_j$):** Cập nhật cấu trúc dữ liệu khi phần tử $a_j$ đến.
2. **QUERY($\mathcal{D}$, $x$):** Trả về giá trị ước lượng $\hat{f}_x$ cho tần suất $f_x$ của phần tử $x$.

**Yêu cầu chính xác (Accuracy Requirement):** Với các tham số lỗi $\varepsilon > 0$ và $\delta > 0$ được cho trước:

$$\Pr\left[|\hat{f}_x - f_x| \le \varepsilon \cdot \|\mathbf{f}\|_1\right] \ge 1 - \delta$$

tức là ước lượng sai lệch tối đa $\varepsilon \cdot m$ so với giá trị thật, với xác suất ít nhất $1 - \delta$.

**Ràng buộc tài nguyên (Resource Constraints):**
- **Không gian (Space):** $O\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$ counters (mỗi counter $O(\log m)$ bits)
- **Thời gian cập nhật (Update time):** $O\!\left(\log \frac{1}{\delta}\right)$ per element
- **Thời gian truy vấn (Query time):** $O\!\left(\log \frac{1}{\delta}\right)$ per query

---

## 3. Phân tích lý thuyết phức tạp (Theoretical Complexity Analysis)

### 3.1. Giới hạn dưới (Lower Bounds)

**Định lý (Theorem — Alon, Matias, Szegedy, 1996):** Bất kỳ thuật toán streaming randomized nào ước lượng $F_0$ (số phần tử phân biệt) với hệ số xấp xỉ hằng số đều cần $\Omega(\log n)$ bits bộ nhớ.

> *Tham khảo:* Alon, N., Matias, Y., & Szegedy, M. (1996). "The Space Complexity of Approximating the Frequency Moments." *Proceedings of the 28th ACM STOC*, pp. 20–29. **(Giải thưởng Gödel Prize 2005)**

**Hệ quả cho Point Query (Corollary for Point Query):** Bất kỳ cấu trúc dữ liệu nào giải bài toán Point Query với sai số $\varepsilon \cdot m$ đều cần ít nhất $\Omega(1/\varepsilon)$ bits bộ nhớ (không tính log factors).

> *Tham khảo:* Ganguly, S. (2012). "Lower bounds on frequency estimation of data streams." *CSR 2012, Lecture Notes in Computer Science*, vol. 7353.

### 3.2. Giới hạn trên đã biết (Known Upper Bounds)

| Cấu trúc dữ liệu | Space | Update Time | Query Time | Loại lỗi |
|---|---|---|---|---|
| Exact Hash Map | $O(n \cdot \log m)$ | $O(1)$ amortized | $O(1)$ | Chính xác |
| Count-Min Sketch | $O\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$ | $O\!\left(\log \frac{1}{\delta}\right)$ | $O\!\left(\log \frac{1}{\delta}\right)$ | One-sided (overestimate) |
| Count Sketch | $O\!\left(\frac{1}{\varepsilon^2} \cdot \log \frac{1}{\delta}\right)$ | $O\!\left(\log \frac{1}{\delta}\right)$ | $O\!\left(\log \frac{1}{\delta}\right)$ | Two-sided |
| Misra-Gries / Heavy Hitters | $O\!\left(\frac{1}{\varepsilon}\right)$ | $O(1)$ amortized | $O(1)$ | One-sided (underestimate) |

### 3.3. Khoảng cách tới tối ưu (Gap to Optimality)

Count-Min Sketch đạt space $O\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$, trong khi lower bound là $\Omega(1/\varepsilon)$. Khoảng cách là yếu tố $\log(1/\delta)$, liên quan đến số hàm hash cần thiết để đảm bảo xác suất thất bại $\delta$. Với $\delta$ cố định (ví dụ $\delta = 0.01$), Count-Min Sketch gần như tối ưu.

---

## 4. Các biến thể và mở rộng (Variants and Extensions)

### 4.1. Sliding Window Model (Mô hình cửa sổ trượt)

Thay vì đếm trên toàn bộ luồng, chỉ quan tâm đến $W$ phần tử gần nhất:

$$f_i^{(W)} = |\{j : a_j = i, \; m - W < j \le m\}|$$

**Ý nghĩa:** Trong thực tế, hành vi bất thường chỉ có ý nghĩa trong một khoảng thời gian nhất định. Ví dụ: "IP X gửi 1000 SYN packets trong 5 phút qua" quan trọng hơn "IP X gửi 1000 SYN packets trong suốt 24 giờ qua".

**Độ phức tạp bổ sung:** Cần cơ chế "quên" (expiration), thường tăng space thêm yếu tố $O(\log W)$.

### 4.2. Weighted Streams (Luồng có trọng số)

Mỗi phần tử đến kèm theo trọng số: $(a_j, w_j)$ với $w_j \in \mathbb{R}^+$. Tần suất trở thành:

$$f_i = \sum_{j: a_j = i} w_j$$

### 4.3. Heavy Hitters Problem (Bài toán phần tử tần suất cao)

Tìm tất cả phần tử $i$ sao cho $f_i \ge \phi \cdot m$ với ngưỡng $\phi \in (0, 1)$. Đây là bài toán liên quan chặt chẽ — giải Point Query tốt sẽ giải được Heavy Hitters.

---

## 5. Tham khảo chính (Key References)

1. **Alon, N., Matias, Y., & Szegedy, M.** (1996). "The Space Complexity of Approximating the Frequency Moments." *Proceedings of the 28th ACM Symposium on Theory of Computing (STOC)*, pp. 20–29. — Bài báo nền tảng cho streaming algorithms, giải Gödel Prize 2005.

2. **Cormode, G. & Muthukrishnan, S.** (2005). "An Improved Data Stream Summary: The Count-Min Sketch and its Applications." *Journal of Algorithms*, 55(1), pp. 58–75. — Giới thiệu Count-Min Sketch.

3. **Misra, J. & Gries, D.** (1982). "Finding Repeated Elements." *Science of Computer Programming*, 2(2), pp. 143–152. — Thuật toán deterministic cho Heavy Hitters.

4. **Charikar, M., Chen, K., & Farach-Colton, M.** (2004). "Finding Frequent Items in Data Streams." *Theoretical Computer Science*, 312(1), pp. 3–15. — Giới thiệu Count Sketch.

5. **Chakrabarti, A.** (2019). "Data Stream Algorithms." *Lecture Notes, Dartmouth College*. — Tài liệu tổng hợp toàn diện về streaming algorithms.

6. **Ganguly, S.** (2012). "Lower Bounds on Frequency Estimation of Data Streams." *CSR 2012, LNCS* vol. 7353. — Chứng minh lower bounds cho frequency estimation.

---

## 6. Tóm tắt (Summary)

| Khía cạnh | Chi tiết |
|---|---|
| **Bài toán** | Point Frequency Estimation trên data stream |
| **Input** | Luồng $S = (a_1, \ldots, a_m)$, phần tử từ universe $U$ kích thước $n$ |
| **Output** | Ước lượng $\hat{f}_x$ cho tần suất $f_x$ của phần tử $x$ bất kỳ |
| **Ràng buộc** | Single-pass, bộ nhớ $M \ll m$, online |
| **Accuracy** | $\|\hat{f}_x - f_x\| \le \varepsilon \cdot m$ với xác suất $\ge 1 - \delta$ |
| **Optimal Space** | $\Theta\!\left(\frac{1}{\varepsilon} \cdot \log \frac{1}{\delta}\right)$ counters |
| **Best Known** | Count-Min Sketch — đạt optimal (up to constants) |
| **Mở rộng** | Sliding window, weighted streams, Heavy Hitters |
