#include "dsa/frequency/heavy_hitters.h"
#include <stdexcept>

namespace dsa::frequency {

HeavyHitters::HeavyHitters(uint32_t k) : k_(k), n_(0) {
    if (k_ < 2) {
        throw std::invalid_argument("HeavyHitters: k must be >= 2");
    }
    counters_.reserve(k_);
}

void HeavyHitters::record(const void* key, size_t key_len) {
    std::string s(static_cast<const char*>(key), key_len);
    ++n_;

    auto it = counters_.find(s);
    if (it != counters_.end()) {
        ++it->second;
        return;
    }
    if (counters_.size() + 1 < k_) {
        counters_.emplace(std::move(s), 1);
        return;
    }
    // Decrement-all phase: subtract 1 from every counter; drop those reaching 0.
    for (auto cur = counters_.begin(); cur != counters_.end();) {
        if (--cur->second == 0) {
            cur = counters_.erase(cur);
        } else {
            ++cur;
        }
    }
}

uint64_t HeavyHitters::estimate(const void* key, size_t key_len) const {
    std::string s(static_cast<const char*>(key), key_len);
    auto it = counters_.find(s);
    return (it != counters_.end()) ? it->second : 0;
}

void HeavyHitters::reset() {
    counters_.clear();
    n_ = 0;
}

size_t HeavyHitters::memory_usage() const {
    size_t total = sizeof(*this);
    for (const auto& [k, v] : counters_) {
        total += k.capacity() + sizeof(v) + sizeof(void*) * 2;
    }
    return total;
}

std::string HeavyHitters::name() const {
    return "heavy_hitters";
}

} // namespace dsa::frequency
