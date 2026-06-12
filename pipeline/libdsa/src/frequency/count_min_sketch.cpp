#include "dsa/frequency/count_min_sketch.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace dsa::frequency {

CountMinSketch::CountMinSketch(uint32_t width, uint32_t depth)
    : width_(width), depth_(depth),
      table_(depth, std::vector<uint64_t>(width, 0)) {
    // Generate deterministic seeds for reproducibility
    seeds_.reserve(depth);
    for (uint32_t i = 0; i < depth; ++i) {
        seeds_.push_back(0x9e3779b9 + i * 0x517cc1b7);
    }
}

// MurmurHash3-inspired hash for good distribution
uint32_t CountMinSketch::hash(const void* key, size_t key_len, uint32_t seed) const {
    const uint8_t* data = static_cast<const uint8_t*>(key);
    uint32_t h = seed;

    // Process 4-byte chunks
    size_t nblocks = key_len / 4;
    for (size_t i = 0; i < nblocks; ++i) {
        uint32_t k;
        std::memcpy(&k, data + i * 4, 4);

        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;

        h ^= k;
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }

    // Process remaining bytes
    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (key_len & 3) {
        case 3: k1 ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
        case 2: k1 ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
        case 1: k1 ^= static_cast<uint32_t>(tail[0]);
                k1 *= 0xcc9e2d51;
                k1 = (k1 << 15) | (k1 >> 17);
                k1 *= 0x1b873593;
                h ^= k1;
    }

    // Finalization
    h ^= static_cast<uint32_t>(key_len);
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

void CountMinSketch::record(const void* key, size_t key_len) {
    for (uint32_t i = 0; i < depth_; ++i) {
        uint32_t col = hash(key, key_len, seeds_[i]) % width_;
        table_[i][col]++;
    }
}

uint64_t CountMinSketch::estimate(const void* key, size_t key_len) const {
    uint64_t min_val = std::numeric_limits<uint64_t>::max();
    for (uint32_t i = 0; i < depth_; ++i) {
        uint32_t col = hash(key, key_len, seeds_[i]) % width_;
        min_val = std::min(min_val, table_[i][col]);
    }
    return min_val;
}

void CountMinSketch::reset() {
    for (auto& row : table_) {
        std::fill(row.begin(), row.end(), 0);
    }
}

size_t CountMinSketch::memory_usage() const {
    return depth_ * width_ * sizeof(uint64_t) + seeds_.size() * sizeof(uint32_t);
}

std::string CountMinSketch::name() const {
    return "count_min_sketch";
}

} // namespace dsa::frequency
