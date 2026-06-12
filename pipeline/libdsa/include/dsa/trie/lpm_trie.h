#pragma once
#include "dsa/trie/ip_classifier.h"
#include <memory>

namespace dsa::trie {

// Patricia Trie (Radix Trie) based LPM for IPv4 addresses.
class LPMTrie : public IPClassifier {
public:
    LPMTrie();
    ~LPMTrie();

    void insert(uint32_t prefix, uint8_t prefix_len, int32_t label) override;
    bool remove(uint32_t prefix, uint8_t prefix_len) override;
    int32_t lookup(uint32_t ip) const override;
    size_t size() const override;
    size_t memory_usage() const override;
    std::string name() const override { return "lpm_trie"; }

private:
    struct Node {
        std::unique_ptr<Node> children[2]; // 0 and 1
        int32_t label = -1;                // -1 = no prefix here
        bool has_prefix = false;
    };

    // Get bit at position pos (0 = MSB) from a 32-bit value
    static int get_bit(uint32_t val, int pos) {
        return (val >> (31 - pos)) & 1;
    }

    std::unique_ptr<Node> root_;
    size_t count_ = 0;
    size_t node_count_ = 0;
};

} // namespace dsa::trie
