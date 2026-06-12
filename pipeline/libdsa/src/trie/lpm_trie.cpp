#include "dsa/trie/lpm_trie.h"

namespace dsa::trie {

LPMTrie::LPMTrie() : root_(std::make_unique<Node>()) {
    node_count_ = 1;
}

LPMTrie::~LPMTrie() = default;

void LPMTrie::insert(uint32_t prefix, uint8_t prefix_len, int32_t label) {
    Node* node = root_.get();

    for (int i = 0; i < prefix_len; ++i) {
        int bit = get_bit(prefix, i);
        if (!node->children[bit]) {
            node->children[bit] = std::make_unique<Node>();
            node_count_++;
        }
        node = node->children[bit].get();
    }

    if (!node->has_prefix) {
        count_++;
    }
    node->has_prefix = true;
    node->label = label;
}

bool LPMTrie::remove(uint32_t prefix, uint8_t prefix_len) {
    Node* node = root_.get();

    for (int i = 0; i < prefix_len; ++i) {
        int bit = get_bit(prefix, i);
        if (!node->children[bit]) return false;
        node = node->children[bit].get();
    }

    if (!node->has_prefix) return false;

    node->has_prefix = false;
    node->label = -1;
    count_--;
    return true;
}

int32_t LPMTrie::lookup(uint32_t ip) const {
    const Node* node = root_.get();
    int32_t best_label = -1;

    // Check root
    if (node->has_prefix) {
        best_label = node->label;
    }

    for (int i = 0; i < 32; ++i) {
        int bit = get_bit(ip, i);
        if (!node->children[bit]) break;
        node = node->children[bit].get();

        if (node->has_prefix) {
            best_label = node->label; // longer prefix found
        }
    }

    return best_label;
}

size_t LPMTrie::size() const {
    return count_;
}

size_t LPMTrie::memory_usage() const {
    return node_count_ * sizeof(Node);
}

} // namespace dsa::trie
