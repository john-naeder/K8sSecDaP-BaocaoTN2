#include "parser.h"

#include <chrono>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace portscan {

uint32_t parse_ipv4(const std::string& s) {
    uint32_t octets[4] = {0, 0, 0, 0};
    int idx = 0;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.') && idx < 4) {
        octets[idx++] = static_cast<uint32_t>(std::stoul(part));
    }
    if (idx != 4) return 0;
    return (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
}

std::string ipv4_to_string(uint32_t ip) {
    return std::to_string((ip >> 24) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8)  & 0xFF) + "." +
           std::to_string( ip        & 0xFF);
}

namespace {

// Trích chuỗi giá trị của một trường trong JSON một dòng (string hoặc number).
// Đủ dùng cho định dạng do generate_portscan_stream.py phát ra; không phải
// một JSON parser tổng quát.
std::string json_field(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t", pos + 1);
    if (pos == std::string::npos) return "";
    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    auto end = json.find_first_of(",} \t\n", pos);
    return json.substr(pos, end - pos);
}

bool parse_json(const std::string& line, NetworkEvent& evt) {
    if (line.empty() || line.front() != '{') return false;
    const auto src = json_field(line, "src_ip");
    const auto dst = json_field(line, "dst_ip");
    const auto port = json_field(line, "dst_port");
    const auto ts   = json_field(line, "timestamp_ns");
    if (src.empty() || dst.empty()) return false;
    evt.src_ip       = parse_ipv4(src);
    evt.dst_ip       = parse_ipv4(dst);
    evt.dst_port     = port.empty() ? 0 : static_cast<uint16_t>(std::stoul(port));
    evt.timestamp_ns = ts.empty()
        ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count())
        : std::stoull(ts);
    return true;
}

bool parse_tcpdump(const std::string& line, NetworkEvent& evt) {
    // 21:30:42.123 IP 10.0.5.99.34522 > 10.0.0.42.80: Flags [S], seq ...
    static const std::regex re(
        R"(\bIP\s+(\d+\.\d+\.\d+\.\d+)\.(\d+)\s+>\s+(\d+\.\d+\.\d+\.\d+)\.(\d+):\s+Flags\s+\[S\])");
    std::smatch m;
    if (!std::regex_search(line, m, re)) return false;
    evt.src_ip       = parse_ipv4(m[1].str());
    evt.dst_ip       = parse_ipv4(m[3].str());
    evt.dst_port     = static_cast<uint16_t>(std::stoul(m[4].str()));
    evt.timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return true;
}

} // namespace

bool parse_event(const std::string& line, NetworkEvent& evt) {
    if (parse_json(line, evt)) return true;
    return parse_tcpdump(line, evt);
}

} // namespace portscan
