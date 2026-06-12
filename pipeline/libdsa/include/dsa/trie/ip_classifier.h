#pragma once
#include <cstdint>
#include <string>

namespace dsa::trie {

// Abstract interface for IP classification via Longest Prefix Match.
// Implementations: LPMTrie (Patricia Trie based)
class IPClassifier {
public:
    virtual ~IPClassifier() = default;

    // Insert a CIDR prefix with a label.
    // prefix: network address (host byte order, e.g., 0x0AF40000 for 10.244.0.0)
    // prefix_len: number of significant bits (e.g., 16 for /16)
    // label: classification result (e.g., 1 = pod network, 2 = service network)
    virtual void insert(uint32_t prefix, uint8_t prefix_len, int32_t label) = 0;

    // Remove a CIDR prefix.
    virtual bool remove(uint32_t prefix, uint8_t prefix_len) = 0;

    // Lookup: return label for longest matching prefix.
    // Returns -1 if no match.
    virtual int32_t lookup(uint32_t ip) const = 0;

    // Number of prefixes stored.
    virtual size_t size() const = 0;

    // Memory footprint in bytes.
    virtual size_t memory_usage() const = 0;

    virtual std::string name() const = 0;
};

} // namespace dsa::trie
