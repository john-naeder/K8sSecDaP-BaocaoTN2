#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace dsa::frequency {

// Abstract interface for stream frequency estimation.
// Implementations: CountMinSketch, HashMapExact
class Estimator {
public:
    virtual ~Estimator() = default;

    // Record an observation of a key.
    virtual void record(const void* key, size_t key_len) = 0;

    // Query estimated frequency of a key.
    virtual uint64_t estimate(const void* key, size_t key_len) const = 0;

    // Reset all counters.
    virtual void reset() = 0;

    // Memory footprint in bytes.
    virtual size_t memory_usage() const = 0;

    // Human-readable name.
    virtual std::string name() const = 0;

    // Convenience overloads for uint32_t keys (e.g., IP addresses)
    void record_u32(uint32_t key) { record(&key, sizeof(key)); }
    uint64_t estimate_u32(uint32_t key) const { return estimate(&key, sizeof(key)); }

    // Convenience for (src_ip, dst_ip) pair
    void record_pair(uint32_t src, uint32_t dst) {
        uint64_t pair = (static_cast<uint64_t>(src) << 32) | dst;
        record(&pair, sizeof(pair));
    }
    uint64_t estimate_pair(uint32_t src, uint32_t dst) const {
        uint64_t pair = (static_cast<uint64_t>(src) << 32) | dst;
        return estimate(&pair, sizeof(pair));
    }
};

} // namespace dsa::frequency
