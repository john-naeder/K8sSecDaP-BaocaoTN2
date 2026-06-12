// Mini network-event struct used by portscan-pipeline.
//
// Bản tinh giản của cấu trúc trong stage-2 (zt-mapper) — chỉ giữ
// các trường cần thiết cho việc đếm tần suất theo địa chỉ IP nguồn.
#pragma once
#include <cstdint>
#include <string>

namespace portscan {

struct NetworkEvent {
    uint32_t src_ip      = 0;   // IPv4 packed: 192.168.1.10 -> 0xC0A8010A
    uint32_t dst_ip      = 0;
    uint16_t dst_port    = 0;
    uint64_t timestamp_ns = 0;
};

// "10.0.5.99" -> packed uint32_t big-endian (octet 0 ở 8 bit cao nhất).
uint32_t parse_ipv4(const std::string& s);

// Định dạng ngược lại để in alert / trace.
std::string ipv4_to_string(uint32_t ip);

} // namespace portscan
