// portscan-pipeline — chương trình demo phát hiện port-scan trên luồng IP thật.
//
// Đọc các sự kiện mạng (JSON Lines hoặc tcpdump SYN) từ stdin/file, áp dụng
// một trong ba thuật toán ước lượng tần suất (Count-Min Sketch / Misra-Gries /
// Hash Map chính xác) lên trường src_ip, phát alert khi tần suất ước lượng
// trong cửa sổ trượt vượt ngưỡng. Tinh giản từ stage-2 pipeline để chỉ tập
// trung vào logic frequency estimation — không có graph/trie/NATS/PostgreSQL.
#include "detector.h"
#include "event.h"
#include "parser.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    std::string algo       = "all";   // cms | mg | hashmap | all
    std::string input;                 // path; rỗng = stdin
    std::string trace_dir;             // nếu set, ghi trace_<algo>.txt vào đây
    std::string alerts_path;           // nếu set, ghi alerts.json
    std::string comparison_path;       // nếu set, ghi comparison.csv
    uint32_t    cms_width  = 2048;
    uint32_t    cms_depth  = 5;
    uint32_t    mg_k       = 64;
    uint64_t    threshold  = 40;
    uint64_t    window_s   = 60;
    std::vector<uint64_t> trace_at = {50, 100, 110, 120, 130, 140};
};

void usage(const char* argv0) {
    std::cerr <<
"Usage: " << argv0 << " [options]\n"
"\n"
"Options:\n"
"  --algo {cms|mg|hashmap|all}  Thuật toán dùng (mặc định: all)\n"
"  --input PATH                 File JSON Lines, mặc định đọc stdin\n"
"  --trace-dir DIR              Ghi trace_<algo>.txt vào DIR\n"
"  --alerts PATH                Ghi alerts.json vào PATH\n"
"  --comparison PATH            Ghi comparison.csv (cần --algo all)\n"
"  --cms-width N                CMS width  (mặc định 2048)\n"
"  --cms-depth N                CMS depth  (mặc định 5)\n"
"  --mg-k N                     MG slots   (mặc định 64)\n"
"  --threshold N                Ngưỡng alert (mặc định 40)\n"
"  --window-seconds N           Cửa sổ tumbling (mặc định 60)\n"
"  --trace-at LIST              CSV các event_index sinh snapshot\n"
"                               (mặc định 50,100,110,120,130,140)\n";
}

bool parse_uint(const char* s, uint64_t& out) {
    try { out = std::stoull(s); return true; } catch (...) { return false; }
}

std::vector<uint64_t> parse_csv_uints(const std::string& s) {
    std::vector<uint64_t> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        out.push_back(std::stoull(tok));
    }
    return out;
}

bool parse_args(int argc, char** argv, CliArgs& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Thiếu giá trị cho " << name << "\n"; return nullptr;
            }
            return argv[++i];
        };
        if      (flag == "--algo")            { auto v = next("--algo");            if (!v) return false; a.algo = v; }
        else if (flag == "--input")           { auto v = next("--input");           if (!v) return false; a.input = v; }
        else if (flag == "--trace-dir")       { auto v = next("--trace-dir");       if (!v) return false; a.trace_dir = v; }
        else if (flag == "--alerts")          { auto v = next("--alerts");          if (!v) return false; a.alerts_path = v; }
        else if (flag == "--comparison")      { auto v = next("--comparison");      if (!v) return false; a.comparison_path = v; }
        else if (flag == "--cms-width")       { auto v = next("--cms-width");       if (!v) return false; uint64_t x=0; parse_uint(v,x); a.cms_width = static_cast<uint32_t>(x); }
        else if (flag == "--cms-depth")       { auto v = next("--cms-depth");       if (!v) return false; uint64_t x=0; parse_uint(v,x); a.cms_depth = static_cast<uint32_t>(x); }
        else if (flag == "--mg-k")            { auto v = next("--mg-k");            if (!v) return false; uint64_t x=0; parse_uint(v,x); a.mg_k      = static_cast<uint32_t>(x); }
        else if (flag == "--threshold")       { auto v = next("--threshold");       if (!v) return false; parse_uint(v, a.threshold); }
        else if (flag == "--window-seconds")  { auto v = next("--window-seconds");  if (!v) return false; parse_uint(v, a.window_s); }
        else if (flag == "--trace-at")        { auto v = next("--trace-at");        if (!v) return false; a.trace_at = parse_csv_uints(v); }
        else if (flag == "-h" || flag == "--help") { usage(argv[0]); return false; }
        else { std::cerr << "Tham số không hỗ trợ: " << flag << "\n"; usage(argv[0]); return false; }
    }
    return true;
}

portscan::DetectorConfig make_cfg(const CliArgs& a, const std::string& algo) {
    portscan::DetectorConfig cfg;
    cfg.algo       = algo;
    cfg.cms_width  = a.cms_width;
    cfg.cms_depth  = a.cms_depth;
    cfg.mg_k       = a.mg_k;
    cfg.threshold  = a.threshold;
    cfg.window_ns  = a.window_s * 1'000'000'000ULL;
    return cfg;
}

void register_trace(portscan::PortScanDetector& det,
                    const std::string& algo,
                    const std::string& trace_dir,
                    std::ofstream& trace_file,
                    const std::vector<uint64_t>& trace_at) {
    if (trace_dir.empty()) return;
    trace_file.open(trace_dir + "/trace_" + algo + ".txt");
    if (!trace_file) {
        std::cerr << "Không mở được file trace cho " << algo << "\n";
        return;
    }
    det.set_trace_output(&trace_file);
    if      (algo == "cms")     det.set_trace_writer(portscan::write_cms_snapshot);
    else if (algo == "mg")      det.set_trace_writer(portscan::write_mg_snapshot);
    else                         det.set_trace_writer(portscan::write_hashmap_snapshot);
    for (auto idx : trace_at) det.add_trace_breakpoint(idx);

    trace_file << "# trace generated by portscan-pipeline; algo=" << algo << "\n";
    trace_file << "# event indices captured: ";
    for (size_t i = 0; i < trace_at.size(); ++i) {
        trace_file << trace_at[i] << (i + 1 == trace_at.size() ? "" : ",");
    }
    trace_file << "\n\n";
}

void write_alerts_json(const std::string& path,
                       const std::vector<portscan::PortScanDetector*>& dets) {
    std::ofstream out(path);
    if (!out) { std::cerr << "Không ghi được " << path << "\n"; return; }
    out << "[\n";
    bool first = true;
    for (const auto* d : dets) {
        for (const auto& a : d->alerts()) {
            if (!first) out << ",\n";
            out << "  {\"algo\":\"" << a.algo << "\""
                << ",\"src_ip\":\"" << portscan::ipv4_to_string(a.src_ip) << "\""
                << ",\"freq\":" << a.freq
                << ",\"timestamp_ns\":" << a.timestamp_ns
                << ",\"window_id\":" << a.window_id
                << ",\"event_index\":" << a.event_index << "}";
            first = false;
        }
    }
    out << "\n]\n";
}

void write_comparison_csv(const std::string& path,
                          const std::vector<portscan::PortScanDetector*>& dets,
                          const std::vector<double>& throughputs_eps) {
    std::ofstream out(path);
    if (!out) { std::cerr << "Không ghi được " << path << "\n"; return; }
    out << "algo,total_events,alerts,true_positives,false_positives,peak_memory_kb,throughput_eps\n";
    const uint32_t attacker = (10u << 24) | (0u << 16) | (5u << 8) | 99u;
    for (size_t i = 0; i < dets.size(); ++i) {
        const auto* d = dets[i];
        size_t tp = 0, fp = 0;
        for (const auto& a : d->alerts()) {
            if (a.src_ip == attacker) ++tp; else ++fp;
        }
        out << d->algo() << "," << d->total_events() << ","
            << d->alerts().size() << "," << tp << "," << fp << ","
            << (d->peak_memory_bytes() / 1024.0) << ","
            << static_cast<long long>(throughputs_eps[i]) << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    CliArgs a;
    if (!parse_args(argc, argv, a)) return 2;

    std::vector<std::string> algos;
    if (a.algo == "all") algos = {"cms", "mg", "hashmap"};
    else                  algos = {a.algo};

    // Đọc toàn bộ input vào RAM một lần để 3 detector chạy trên cùng dữ liệu.
    std::vector<std::string> lines;
    {
        std::istream* in = &std::cin;
        std::ifstream fin;
        if (!a.input.empty()) {
            fin.open(a.input);
            if (!fin) { std::cerr << "Không mở được " << a.input << "\n"; return 1; }
            in = &fin;
        }
        std::string line;
        while (std::getline(*in, line)) {
            if (!line.empty()) lines.push_back(std::move(line));
        }
    }
    std::cerr << "[portscan-pipeline] Đã đọc " << lines.size() << " dòng đầu vào\n";

    std::vector<std::unique_ptr<portscan::PortScanDetector>> dets;
    std::vector<std::ofstream> trace_files(algos.size());
    std::vector<double> throughputs_eps(algos.size(), 0.0);

    for (size_t i = 0; i < algos.size(); ++i) {
        const auto& algo = algos[i];
        auto det = std::make_unique<portscan::PortScanDetector>(make_cfg(a, algo));
        register_trace(*det, algo, a.trace_dir, trace_files[i], a.trace_at);

        const auto t0 = std::chrono::steady_clock::now();
        for (const auto& line : lines) {
            portscan::NetworkEvent evt;
            if (!portscan::parse_event(line, evt)) continue;
            det->process(evt);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        throughputs_eps[i] = sec > 0.0 ? det->total_events() / sec : 0.0;

        std::cerr << "[" << algo << "] processed=" << det->total_events()
                  << " alerts=" << det->alerts().size()
                  << " peak_memory=" << det->peak_memory_bytes() << " bytes"
                  << " throughput=" << static_cast<long long>(throughputs_eps[i]) << " eps\n";
        for (const auto& al : det->alerts()) {
            std::cerr << "  [ALERT " << algo << "] "
                      << portscan::ipv4_to_string(al.src_ip)
                      << " freq=" << al.freq
                      << " event#" << al.event_index << "\n";
        }
        dets.push_back(std::move(det));
    }

    std::vector<portscan::PortScanDetector*> raw;
    for (auto& d : dets) raw.push_back(d.get());

    if (!a.alerts_path.empty())     write_alerts_json(a.alerts_path, raw);
    if (!a.comparison_path.empty()) write_comparison_csv(a.comparison_path, raw, throughputs_eps);

    return 0;
}
