#pragma once
#include "dsa/frequency/estimator.h"
#include <cstdint>
#include <vector>

namespace dsa::frequency {

class CountMinSketch : public Estimator {
public:
    // width = number of columns (~ e/epsilon), depth = number of rows (~ ln(1/delta))
    CountMinSketch(uint32_t width = 2048, uint32_t depth = 5);

    void record(const void* key, size_t key_len) override;
    uint64_t estimate(const void* key, size_t key_len) const override;
    void reset() override;
    size_t memory_usage() const override;
    std::string name() const override;

    uint32_t width() const { return width_; }
    uint32_t depth() const { return depth_; }

private:
    uint32_t hash(const void* key, size_t key_len, uint32_t seed) const;

    uint32_t width_;
    uint32_t depth_;
    std::vector<std::vector<uint64_t>> table_;
    std::vector<uint32_t> seeds_;
};

} // namespace dsa::frequency
