// ============================================================================
// trace_algorithms — Human-readable step-by-step traces of streaming
// frequency estimation algorithms, used by the graduation report appendix.
//
// Subcommands:
//   cms      : Count-Min Sketch trace (INSERT and QUERY steps)
//   misra    : Misra-Gries heavy-hitters trace
//   compare  : Run both on the same stream and compare against exact counts
//
// Output is plain ASCII, line-oriented, intended for \lstinputlisting in LaTeX.
// ============================================================================
#include "dsa/frequency/count_min_sketch.h"
#include "dsa/frequency/heavy_hitters.h"
#include "dsa/frequency/hash_map_exact.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using dsa::frequency::CountMinSketch;
using dsa::frequency::HashMapExact;
using dsa::frequency::HeavyHitters;

namespace {

constexpr int kKeyColumnWidth = 8;

// Fixed demo stream. Kept short so traces are pedagogic, with a dominant
// "heavy" element (a), a secondary element (b), and long-tail items.
const std::vector<std::string> kDemoStream = {
    "a", "b", "a", "c", "a", "b", "d", "a",
};

const std::vector<std::string> kDemoQueries = {"a", "b", "c", "d", "e"};

std::map<std::string, uint64_t> exact_counts(const std::vector<std::string>& stream) {
    std::map<std::string, uint64_t> out;
    for (const auto& x : stream) ++out[x];
    return out;
}

void print_header(const std::string& title) {
    std::cout << "===============================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "===============================================================\n";
}

void print_stream(const std::vector<std::string>& stream) {
    std::cout << "Stream S = [ ";
    for (size_t i = 0; i < stream.size(); ++i) {
        std::cout << stream[i];
        if (i + 1 < stream.size()) std::cout << ", ";
    }
    std::cout << " ]\n";
    std::cout << "Length m = " << stream.size() << "\n\n";
}

// ------------------------- Count-Min Sketch trace ---------------------------

// Mirrors libdsa CountMinSketch exactly (same MurmurHash3-inspired hash, same
// seed schedule), but exposes hash positions and the matrix for pedagogy.
class CMSDirect {
public:
    CMSDirect(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
        seeds_.reserve(depth);
        for (uint32_t i = 0; i < depth; ++i)
            seeds_.push_back(0x9e3779b9u + i * 0x517cc1b7u);
        table_.assign(depth_, std::vector<uint64_t>(width_, 0));
    }

    std::vector<uint32_t> columns_for(const std::string& key) const {
        std::vector<uint32_t> cols(depth_);
        for (uint32_t r = 0; r < depth_; ++r) {
            cols[r] = murmur3_32(key.data(), key.size(), seeds_[r]) % width_;
        }
        return cols;
    }

    void insert(const std::string& key) {
        auto cols = columns_for(key);
        for (uint32_t r = 0; r < depth_; ++r) ++table_[r][cols[r]];
    }
    uint64_t query(const std::string& key) const {
        auto cols = columns_for(key);
        uint64_t m = UINT64_MAX;
        for (uint32_t r = 0; r < depth_; ++r)
            m = std::min(m, table_[r][cols[r]]);
        return m;
    }

    void print_matrix() const {
        for (uint32_t r = 0; r < depth_; ++r) {
            std::cout << "    Row " << r << ": [";
            for (uint32_t c = 0; c < width_; ++c) {
                std::cout << " " << std::setw(2) << table_[r][c];
            }
            std::cout << " ]\n";
        }
    }

    uint32_t width() const { return width_; }
    uint32_t depth() const { return depth_; }

private:
    // 32-bit MurmurHash3 — byte-for-byte identical to libdsa's CountMinSketch.
    static uint32_t murmur3_32(const void* key, size_t key_len, uint32_t seed) {
        const uint8_t* data = static_cast<const uint8_t*>(key);
        uint32_t h = seed;
        size_t nblocks = key_len / 4;
        for (size_t i = 0; i < nblocks; ++i) {
            uint32_t k;
            std::memcpy(&k, data + i * 4, 4);
            k *= 0xcc9e2d51u;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593u;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64u;
        }
        const uint8_t* tail = data + nblocks * 4;
        uint32_t k1 = 0;
        switch (key_len & 3u) {
            case 3: k1 ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
            case 2: k1 ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
            case 1: k1 ^= static_cast<uint32_t>(tail[0]);
                    k1 *= 0xcc9e2d51u;
                    k1 = (k1 << 15) | (k1 >> 17);
                    k1 *= 0x1b873593u;
                    h ^= k1;
        }
        h ^= static_cast<uint32_t>(key_len);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }

    uint32_t width_, depth_;
    std::vector<uint32_t> seeds_;
    std::vector<std::vector<uint64_t>> table_;
};

void trace_cms(const std::vector<std::string>& stream,
               const std::vector<std::string>& queries,
               uint32_t width, uint32_t depth) {
    print_header("COUNT-MIN SKETCH  —  step-by-step trace");
    std::cout << "Parameters : width w = " << width
              << " , depth d = " << depth << "\n";
    std::cout << "Guarantee  : f_hat(x) >= f(x)   (one-sided error, overestimate)\n";
    std::cout << "Bound      : P[ f_hat(x) - f(x) > eps * m ] <= delta\n";
    std::cout << "             with eps = e/w ~ " << std::fixed << std::setprecision(3)
              << (2.71828 / width) << "\n";
    std::cout << "             and delta = e^(-d) ~ "
              << std::fixed << std::setprecision(4);
    double dlt = 1.0;
    for (uint32_t i = 0; i < depth; ++i) dlt /= 2.71828;
    std::cout << dlt << "\n\n";
    print_stream(stream);

    CMSDirect sk(width, depth);

    for (size_t step = 0; step < stream.size(); ++step) {
        const auto& key = stream[step];
        auto cols = sk.columns_for(key);
        std::cout << "Step " << (step + 1) << ": INSERT \"" << key << "\"\n";
        for (uint32_t r = 0; r < depth; ++r) {
            std::cout << "    h" << r << "(\"" << key << "\") mod " << width
                      << " = " << cols[r] << "   -> table[" << r << "]["
                      << cols[r] << "] += 1\n";
        }
        sk.insert(key);
        std::cout << "    Matrix after step " << (step + 1) << ":\n";
        sk.print_matrix();
        std::cout << "\n";
    }

    std::cout << "--- QUERIES ---\n";
    auto exact = exact_counts(stream);
    std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << "key"
              << std::right << std::setw(6) << "f(x)"
              << std::setw(12) << "f_hat(x)"
              << "  cells(min)\n";
    for (const auto& q : queries) {
        auto cols = sk.columns_for(q);
        uint64_t f = exact.count(q) ? exact.at(q) : 0;
        uint64_t est = sk.query(q);
        std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << ("\"" + q + "\"")
                  << std::right << std::setw(6) << f
                  << std::setw(12) << est
                  << "  {";
        for (uint32_t r = 0; r < depth; ++r) {
            std::cout << "(" << r << "," << cols[r] << ")";
            if (r + 1 < depth) std::cout << ",";
        }
        std::cout << "}\n";
    }
    std::cout << "\nObservation: for every key, f_hat(x) >= f(x) — one-sided overestimate.\n";
}

// ------------------------- Misra-Gries trace --------------------------------

void print_counters(const std::map<std::string, uint64_t>& T) {
    std::cout << "T = { ";
    bool first = true;
    for (const auto& [k, v] : T) {
        if (!first) std::cout << ", ";
        std::cout << k << ":" << v;
        first = false;
    }
    if (T.empty()) std::cout << "(empty)";
    std::cout << " }";
}

void trace_misra(const std::vector<std::string>& stream,
                 const std::vector<std::string>& queries, uint32_t k) {
    print_header("MISRA-GRIES (Heavy Hitters)  —  step-by-step trace");
    std::cout << "Parameter : k = " << k
              << " (at most " << (k - 1) << " active counters)\n";
    std::cout << "Guarantees: f_hat(x) <= f(x)     (underestimate)\n";
    std::cout << "            f(x) - f_hat(x) <= m / k\n\n";
    print_stream(stream);

    // Use ordered map for deterministic print; semantics identical.
    std::map<std::string, uint64_t> T;

    for (size_t step = 0; step < stream.size(); ++step) {
        const auto& x = stream[step];
        std::cout << "Step " << (step + 1) << ": INSERT \"" << x << "\"\n";
        if (T.count(x)) {
            ++T[x];
            std::cout << "    \"" << x << "\" in T   -> increment counter\n";
        } else if (T.size() + 1 < k) {
            T[x] = 1;
            std::cout << "    \"" << x << "\" not in T, |T| = " << (T.size() - 1)
                      << " < k-1 = " << (k - 1) << "   -> add with count 1\n";
        } else {
            std::cout << "    \"" << x << "\" not in T and |T| = " << T.size()
                      << " = k-1 = " << (k - 1) << "   -> DECREMENT ALL\n";
            std::cout << "      before: ";
            print_counters(T);
            std::cout << "\n";
            for (auto it = T.begin(); it != T.end();) {
                if (--it->second == 0) it = T.erase(it);
                else ++it;
            }
            std::cout << "      after : ";
            print_counters(T);
            std::cout << "\n";
        }
        std::cout << "    ";
        print_counters(T);
        std::cout << "\n\n";
    }

    std::cout << "--- QUERIES ---\n";
    auto exact = exact_counts(stream);
    const size_t m = stream.size();
    std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << "key"
              << std::right << std::setw(6) << "f(x)"
              << std::setw(12) << "f_hat(x)"
              << std::setw(14) << "f - f_hat"
              << std::setw(10) << "<= m/k?\n";
    for (const auto& q : queries) {
        uint64_t f = exact.count(q) ? exact.at(q) : 0;
        uint64_t est = T.count(q) ? T.at(q) : 0;
        uint64_t gap = (f >= est) ? (f - est) : 0;
        const char* ok = (gap <= m / k) ? "yes" : "no ";
        std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << ("\"" + q + "\"")
                  << std::right << std::setw(6) << f
                  << std::setw(12) << est
                  << std::setw(14) << gap
                  << std::setw(10) << ok << "\n";
    }
    std::cout << "\nObservation: for every key, f_hat(x) <= f(x) — underestimate.\n";
    std::cout << "              error gap never exceeds m/k = " << (m / k) << ".\n";
}

// ------------------------- Compare --------------------------------------------

void trace_compare(const std::vector<std::string>& stream,
                   const std::vector<std::string>& queries,
                   uint32_t cms_w, uint32_t cms_d, uint32_t mg_k) {
    print_header("COMPARISON  —  Exact vs CMS vs Misra-Gries on the same stream");
    std::cout << "CMS(width=" << cms_w << ", depth=" << cms_d
              << ")   MisraGries(k=" << mg_k << ")\n\n";
    print_stream(stream);

    CMSDirect sk(cms_w, cms_d);
    HeavyHitters mg(mg_k);
    HashMapExact exact;
    for (const auto& x : stream) {
        sk.insert(x);
        mg.record(x.data(), x.size());
        exact.record(x.data(), x.size());
    }

    std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << "key"
              << std::right << std::setw(8) << "exact"
              << std::setw(8) << "CMS"
              << std::setw(10) << "MG"
              << std::setw(14) << "CMS >= exact?"
              << std::setw(14) << "MG <= exact?\n";
    for (const auto& q : queries) {
        uint64_t f = exact.estimate(q.data(), q.size());
        uint64_t c = sk.query(q);
        uint64_t m = mg.estimate(q.data(), q.size());
        std::cout << "    " << std::left << std::setw(kKeyColumnWidth) << ("\"" + q + "\"")
                  << std::right << std::setw(8) << f
                  << std::setw(8) << c
                  << std::setw(10) << m
                  << std::setw(14) << (c >= f ? "yes" : "NO ")
                  << std::setw(14) << (m <= f ? "yes" : "NO ") << "\n";
    }
    std::cout << "\nConclusion: CMS overestimates, Misra-Gries underestimates —\n";
    std::cout << "            the two algorithms bracket the true frequency.\n";
}

void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " {cms|misra|compare}\n"
              << "  cms      : Count-Min Sketch step-by-step trace\n"
              << "  misra    : Misra-Gries step-by-step trace\n"
              << "  compare  : run all three (exact, CMS, MG) side by side\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    std::string cmd = argv[1];

    // Small, human-inspectable parameters for all three traces.
    constexpr uint32_t CMS_WIDTH = 4;
    constexpr uint32_t CMS_DEPTH = 2;
    constexpr uint32_t MG_K      = 3;   // up to 2 active counters

    if (cmd == "cms") {
        trace_cms(kDemoStream, kDemoQueries, CMS_WIDTH, CMS_DEPTH);
    } else if (cmd == "misra") {
        trace_misra(kDemoStream, kDemoQueries, MG_K);
    } else if (cmd == "compare") {
        trace_compare(kDemoStream, kDemoQueries, CMS_WIDTH, CMS_DEPTH, MG_K);
    } else {
        usage(argv[0]);
        return 2;
    }
    return 0;
}
