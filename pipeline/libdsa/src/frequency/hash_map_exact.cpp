#include "dsa/frequency/hash_map_exact.h"

namespace dsa::frequency {

void HashMapExact::record(const void* key, size_t key_len) {
    std::string k(static_cast<const char*>(key), key_len);
    counts_[k]++;
}

uint64_t HashMapExact::estimate(const void* key, size_t key_len) const {
    std::string k(static_cast<const char*>(key), key_len);
    auto it = counts_.find(k);
    return (it != counts_.end()) ? it->second : 0;
}

void HashMapExact::reset() {
    counts_.clear();
}

size_t HashMapExact::memory_usage() const {
    size_t total = sizeof(counts_);
    for (const auto& [k, v] : counts_) {
        total += k.capacity() + sizeof(v) + sizeof(void*) * 2; // key + value + bucket overhead
    }
    return total;
}

std::string HashMapExact::name() const {
    return "hash_map_exact";
}

} // namespace dsa::frequency
