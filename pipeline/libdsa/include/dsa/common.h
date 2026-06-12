#pragma once
#include <cstdint>

namespace dsa {

// Shared event type between eBPF collector and DSA library.
// Pure C struct — no eBPF headers required.
struct NetworkEvent {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint32_t pid;
    uint64_t timestamp_ns;
};

// Helper: pack IPv4 from octets
inline uint32_t ip_from_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8)  |
           static_cast<uint32_t>(d);
}

} // namespace dsa
