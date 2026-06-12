// PortScanDetector — bọc một Estimator (CMS / Misra-Gries / HashMap)
// với logic cửa sổ tumbling + dedup alert per-window + ghi trace.
#pragma once
#include "event.h"
#include "dsa/frequency/estimator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace portscan {

struct Alert {
    std::string  algo;
    uint32_t     src_ip;
    uint64_t     freq;        // tần suất ước lượng tại thời điểm vượt ngưỡng
    uint64_t     timestamp_ns;
    uint64_t     window_id;
    uint64_t     event_index; // số thứ tự sự kiện (1-based) khi alert phát ra
};

struct DetectorConfig {
    std::string algo;            // "cms" | "mg" | "hashmap"
    uint32_t    cms_width   = 2048;
    uint32_t    cms_depth   = 5;
    uint32_t    mg_k        = 64;
    uint64_t    threshold   = 40;
    uint64_t    window_ns   = 60ULL * 1'000'000'000ULL;  // 60 s tumbling
};

class PortScanDetector {
public:
    explicit PortScanDetector(DetectorConfig cfg);

    // Đăng ký callback nhận snapshot trace tại các sự kiện cụ thể (1-based index).
    // Snapshot writer in trạng thái nội tại của estimator ra ostream `out`.
    using TraceWriter = std::function<void(std::ostream& out,
                                           uint64_t event_index,
                                           const dsa::frequency::Estimator& est)>;
    void set_trace_writer(TraceWriter w) { trace_writer_ = std::move(w); }
    void add_trace_breakpoint(uint64_t event_index) {
        trace_at_.insert(event_index);
    }
    void set_trace_output(std::ostream* out) { trace_out_ = out; }

    // Xử lý một sự kiện. Trả về true nếu sự kiện vừa kích hoạt một alert mới.
    bool process(const NetworkEvent& evt);

    // Tài nguyên/thông tin để ghi vào comparison.csv.
    const std::vector<Alert>& alerts() const { return alerts_; }
    uint64_t total_events() const { return total_events_; }
    size_t   peak_memory_bytes() const { return peak_memory_; }
    const dsa::frequency::Estimator& estimator() const { return *estimator_; }
    const std::string& algo() const { return cfg_.algo; }

private:
    void maybe_reset_window(uint64_t now_ns);
    void maybe_emit_trace(const NetworkEvent& evt);

    DetectorConfig                           cfg_;
    std::unique_ptr<dsa::frequency::Estimator> estimator_;
    std::unordered_set<uint32_t>             alerted_in_window_;
    std::vector<Alert>                       alerts_;
    uint64_t total_events_  = 0;
    uint64_t window_start_ns_ = 0;
    uint64_t window_id_      = 0;
    size_t   peak_memory_   = 0;

    std::unordered_set<uint64_t> trace_at_;
    TraceWriter                  trace_writer_;
    std::ostream*                trace_out_ = nullptr;
};

// Các writer in trạng thái nội tại tương ứng với từng thuật toán.
void write_cms_snapshot(std::ostream& out,
                        uint64_t event_index,
                        const dsa::frequency::Estimator& est);

void write_mg_snapshot(std::ostream& out,
                       uint64_t event_index,
                       const dsa::frequency::Estimator& est);

void write_hashmap_snapshot(std::ostream& out,
                            uint64_t event_index,
                            const dsa::frequency::Estimator& est);

} // namespace portscan
