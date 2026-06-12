#pragma once
#include "dsa/frequency/estimator.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace dsa::frequency {

// Exact frequency counter using hash map. Baseline for benchmarks.
class HashMapExact : public Estimator {
public:
    void record(const void* key, size_t key_len) override;
    uint64_t estimate(const void* key, size_t key_len) const override;
    void reset() override;
    size_t memory_usage() const override;
    std::string name() const override;

private:
    std::unordered_map<std::string, uint64_t> counts_;
};

} // namespace dsa::frequency
