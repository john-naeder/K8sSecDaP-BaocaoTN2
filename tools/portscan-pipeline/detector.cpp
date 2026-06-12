#include "detector.h"

#include "dsa/frequency/count_min_sketch.h"
#include "dsa/frequency/hash_map_exact.h"
#include "dsa/frequency/heavy_hitters.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace portscan {

namespace {

// Watchlist IPs in lại trong mỗi snapshot — gồm attacker (10.0.5.99) và các
// nguồn lưu lượng nền tiêu biểu để người đọc đối chiếu.
const std::vector<std::pair<std::string, std::string>> kWatchlist = {
    {"10.0.5.99",   "attacker        "},
    {"10.0.1.10",   "frontend-1      "},
    {"10.0.1.11",   "frontend-2      "},
    {"10.0.2.10",   "backend-1       "},
    {"10.0.2.11",   "backend-2       "},
    {"10.0.3.10",   "postgres        "},
    {"10.0.3.11",   "redis           "},
    {"10.0.4.10",   "prometheus      "},
};

uint32_t parse_watch_ip(const std::string& s) {
    uint32_t o[4] = {0,0,0,0};
    int i = 0;
    std::string cur;
    for (char c : s) {
        if (c == '.') { o[i++] = static_cast<uint32_t>(std::stoul(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty() && i < 4) o[i++] = static_cast<uint32_t>(std::stoul(cur));
    return (o[0] << 24) | (o[1] << 16) | (o[2] << 8) | o[3];
}

std::unique_ptr<dsa::frequency::Estimator> make_estimator(const DetectorConfig& cfg) {
    if (cfg.algo == "cms") {
        return std::make_unique<dsa::frequency::CountMinSketch>(cfg.cms_width, cfg.cms_depth);
    } else if (cfg.algo == "mg") {
        return std::make_unique<dsa::frequency::HeavyHitters>(cfg.mg_k);
    } else if (cfg.algo == "hashmap") {
        return std::make_unique<dsa::frequency::HashMapExact>();
    }
    throw std::runtime_error("unknown algo: " + cfg.algo);
}

void write_watchlist(std::ostream& out, const dsa::frequency::Estimator& est) {
    out << "  watchlist (estimated frequency from current estimator):\n";
    for (const auto& [ip, label] : kWatchlist) {
        const uint32_t key = parse_watch_ip(ip);
        const uint64_t f = est.estimate_u32(key);
        out << "    " << ip << "  " << label << "  est=" << f << "\n";
    }
}

} // namespace

PortScanDetector::PortScanDetector(DetectorConfig cfg)
    : cfg_(std::move(cfg)),
      estimator_(make_estimator(cfg_)) {
    peak_memory_ = estimator_->memory_usage();
}

void PortScanDetector::maybe_reset_window(uint64_t now_ns) {
    if (window_start_ns_ == 0) {
        window_start_ns_ = now_ns;
        return;
    }
    if (now_ns - window_start_ns_ > cfg_.window_ns) {
        estimator_->reset();
        alerted_in_window_.clear();
        window_start_ns_ = now_ns;
        ++window_id_;
    }
}

bool PortScanDetector::process(const NetworkEvent& evt) {
    maybe_reset_window(evt.timestamp_ns);

    estimator_->record_u32(evt.src_ip);
    const uint64_t freq = estimator_->estimate_u32(evt.src_ip);
    ++total_events_;

    peak_memory_ = std::max(peak_memory_, estimator_->memory_usage());

    bool emitted = false;
    if (freq > cfg_.threshold &&
        alerted_in_window_.find(evt.src_ip) == alerted_in_window_.end()) {
        alerted_in_window_.insert(evt.src_ip);
        alerts_.push_back(Alert{
            cfg_.algo,
            evt.src_ip,
            freq,
            evt.timestamp_ns,
            window_id_,
            total_events_,
        });
        emitted = true;
    }

    maybe_emit_trace(evt);
    return emitted;
}

void PortScanDetector::maybe_emit_trace(const NetworkEvent& evt) {
    if (!trace_writer_ || !trace_out_) return;
    if (trace_at_.find(total_events_) == trace_at_.end()) return;

    *trace_out_ << "=== Snapshot at event #" << total_events_
                << " (window " << window_id_ << ", t="
                << evt.timestamp_ns << " ns) ===\n";
    *trace_out_ << "  algorithm     : " << estimator_->name() << "\n";
    *trace_out_ << "  memory_usage  : " << estimator_->memory_usage() << " bytes\n";
    *trace_out_ << "  total_events  : " << total_events_ << "\n";
    *trace_out_ << "  alerted_in_w  : " << alerted_in_window_.size() << "\n";
    trace_writer_(*trace_out_, total_events_, *estimator_);
    *trace_out_ << "\n";
}

// ─── Snapshot writers ──────────────────────────────────────────────────────

void write_cms_snapshot(std::ostream& out,
                        uint64_t /*event_index*/,
                        const dsa::frequency::Estimator& est) {
    const auto* cms = dynamic_cast<const dsa::frequency::CountMinSketch*>(&est);
    if (cms) {
        out << "  cms_width     : " << cms->width() << "\n";
        out << "  cms_depth     : " << cms->depth() << "\n";
    }
    write_watchlist(out, est);
}

void write_mg_snapshot(std::ostream& out,
                       uint64_t /*event_index*/,
                       const dsa::frequency::Estimator& est) {
    const auto* mg = dynamic_cast<const dsa::frequency::HeavyHitters*>(&est);
    if (mg) {
        out << "  mg_k          : " << mg->k() << "\n";
        out << "  active_slots  : " << mg->active_counters() << "\n";
        out << "  stream_length : " << mg->stream_length() << "\n";
        // In top slots theo thứ tự giảm dần.
        const auto snap = mg->snapshot();
        std::vector<std::pair<std::string, uint64_t>> sorted(snap.begin(), snap.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        out << "  top slots (decoded as IPv4 little-endian uint32):\n";
        const size_t shown = std::min<size_t>(sorted.size(), 8);
        for (size_t i = 0; i < shown; ++i) {
            const auto& [k, v] = sorted[i];
            // Khoá MG là chuỗi 4 byte raw — giải mã ngược thành a.b.c.d
            // (record_u32 ghi key = uint32 little-endian).
            std::string ip_str = "?";
            if (k.size() == 4) {
                ip_str = std::to_string(static_cast<uint8_t>(k[3])) + "." +
                         std::to_string(static_cast<uint8_t>(k[2])) + "." +
                         std::to_string(static_cast<uint8_t>(k[1])) + "." +
                         std::to_string(static_cast<uint8_t>(k[0]));
            }
            out << "    " << std::setw(15) << std::left << ip_str
                << "  count=" << v << "\n";
        }
    }
    write_watchlist(out, est);
}

void write_hashmap_snapshot(std::ostream& out,
                            uint64_t /*event_index*/,
                            const dsa::frequency::Estimator& est) {
    write_watchlist(out, est);
}

} // namespace portscan
