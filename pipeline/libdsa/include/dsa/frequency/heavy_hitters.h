#pragma once
#include "dsa/frequency/estimator.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace dsa::frequency {

// Misra-Gries (1982) bounded-counter algorithm.
// Maintains at most k-1 counters. For any key x:
//   f_hat(x) <= f(x)                          (underestimate)
//   f(x) - f_hat(x) <= m / k                  (bounded error)
// where m is the length of the stream seen so far.
class HeavyHitters : public Estimator {
public:
    explicit HeavyHitters(uint32_t k = 64);

    void record(const void* key, size_t key_len) override;
    uint64_t estimate(const void* key, size_t key_len) const override;
    void reset() override;
    size_t memory_usage() const override;
    std::string name() const override;

    uint32_t k() const { return k_; }
    size_t active_counters() const { return counters_.size(); }
    uint64_t stream_length() const { return n_; }

    // Exposed for tracing / reporting. Returns a copy of the current counter map.
    std::unordered_map<std::string, uint64_t> snapshot() const { return counters_; }

private:
    uint32_t k_;
    uint64_t n_;
    std::unordered_map<std::string, uint64_t> counters_;
};

} // namespace dsa::frequency
