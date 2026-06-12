#include "dsa/trie/lpm_trie.h"
#include "dsa/common.h"
#include <gtest/gtest.h>

using namespace dsa::trie;
using dsa::ip_from_octets;

TEST(LPMTrie, BasicLookup) {
    LPMTrie trie;

    // 10.0.0.0/8 -> label 1
    trie.insert(ip_from_octets(10, 0, 0, 0), 8, 1);
    // 192.168.0.0/16 -> label 2
    trie.insert(ip_from_octets(192, 168, 0, 0), 16, 2);

    EXPECT_EQ(trie.lookup(ip_from_octets(10, 1, 2, 3)), 1);
    EXPECT_EQ(trie.lookup(ip_from_octets(192, 168, 1, 1)), 2);
    EXPECT_EQ(trie.lookup(ip_from_octets(8, 8, 8, 8)), -1); // no match
}

TEST(LPMTrie, LongestMatch) {
    LPMTrie trie;

    // /8 -> label 1, /16 -> label 2, /24 -> label 3
    trie.insert(ip_from_octets(10, 0, 0, 0), 8, 1);
    trie.insert(ip_from_octets(10, 244, 0, 0), 16, 2);
    trie.insert(ip_from_octets(10, 244, 1, 0), 24, 3);

    // 10.244.1.5 should match /24 (longest)
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 1, 5)), 3);

    // 10.244.2.5 should match /16
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 2, 5)), 2);

    // 10.1.2.3 should match /8
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 1, 2, 3)), 1);
}

TEST(LPMTrie, K8sNetworks) {
    LPMTrie trie;

    // Typical K8s CIDR ranges
    trie.insert(ip_from_octets(10, 244, 0, 0), 16, 1);  // Pod network
    trie.insert(ip_from_octets(10, 96, 0, 0), 12, 2);   // Service network

    // Pod IPs
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 1, 5)), 1);
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 255, 1)), 1);

    // Service IPs
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 96, 0, 1)), 2);
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 100, 5, 3)), 2);

    // External
    EXPECT_EQ(trie.lookup(ip_from_octets(8, 8, 8, 8)), -1);
    EXPECT_EQ(trie.lookup(ip_from_octets(172, 16, 0, 1)), -1);
}

TEST(LPMTrie, DefaultRoute) {
    LPMTrie trie;

    // 0.0.0.0/0 -> default route (matches everything)
    trie.insert(0, 0, 99);
    trie.insert(ip_from_octets(10, 0, 0, 0), 8, 1);

    EXPECT_EQ(trie.lookup(ip_from_octets(10, 1, 2, 3)), 1);     // /8 wins
    EXPECT_EQ(trie.lookup(ip_from_octets(8, 8, 8, 8)), 99);     // default
    EXPECT_EQ(trie.lookup(ip_from_octets(192, 168, 0, 1)), 99); // default
}

TEST(LPMTrie, Remove) {
    LPMTrie trie;
    trie.insert(ip_from_octets(10, 0, 0, 0), 8, 1);
    trie.insert(ip_from_octets(10, 244, 0, 0), 16, 2);

    EXPECT_EQ(trie.size(), 2u);
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 1, 5)), 2);

    // Remove /16, should fall back to /8
    EXPECT_TRUE(trie.remove(ip_from_octets(10, 244, 0, 0), 16));
    EXPECT_EQ(trie.size(), 1u);
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 244, 1, 5)), 1); // fallback to /8

    // Remove non-existent
    EXPECT_FALSE(trie.remove(ip_from_octets(192, 168, 0, 0), 16));
}

TEST(LPMTrie, EmptyTrie) {
    LPMTrie trie;
    EXPECT_EQ(trie.lookup(ip_from_octets(10, 0, 0, 1)), -1);
    EXPECT_EQ(trie.size(), 0u);
}

TEST(LPMTrie, Name) {
    LPMTrie trie;
    EXPECT_EQ(trie.name(), "lpm_trie");
}
