/**
 * Canonical total order among equations of fixed length N, computed from the full pool
 * (same length, same generator rules). Used as deterministic tie-breaking after strategy
 * scores (entropy, Bellman EV, partition DP, etc.).
 *
 * Tuple order (earlier in sort = "better" for tie-breaks; compare lexicographically on this tuple):
 *   1) number of distinct symbols — descending (more distinct symbols first)
 *   2) symbol score — descending: product over positions i of P(freq(c) >= occ_i), occ_i = 1-based
 *      occurrence of that character from the left (stored as sum of logs)
 *   3) position score — descending: product over i of P(ith symbol equals eq[i])
 *   4) lexicographic — ascending in alphabet 1,2,...,9,0,+,-,*,/, (, ), ^, ², ³, = where ²/³ are
 *      single-byte symbols (0x01/0x02) for maxi, as in generate_maxi / nerdle.cpp
 */
#ifndef EQUATION_CANONICAL_HPP
#define EQUATION_CANONICAL_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace nerdle {

struct CanonicalEqKey {
    int distinct = 0;
    double sym_log_sum = 0.0;
    double pos_log_sum = 0.0;
};

/** Lexicographic ranks for equation characters; unknown bytes sort after '=' by raw value. */
inline std::array<int, 256> make_equation_lex_ranks() {
    std::array<int, 256> r{};
    for (int i = 0; i < 256; i++)
        r[static_cast<size_t>(i)] = 10000 + i;
    int rk = 0;
    const char* order = "1234567890+-*/()^";
    for (const char* p = order; *p; ++p)
        r[static_cast<size_t>(static_cast<unsigned char>(*p))] = rk++;
    r[static_cast<size_t>(static_cast<unsigned char>('\x01'))] = rk++; // ² (maxi)
    r[static_cast<size_t>(static_cast<unsigned char>('\x02'))] = rk++; // ³
    r[static_cast<size_t>(static_cast<unsigned char>('='))] = rk++;
    return r;
}

inline const std::array<int, 256>& equation_lex_ranks() {
    static const std::array<int, 256> r = make_equation_lex_ranks();
    return r;
}

/** True if a is strictly before b in lex order (using equation_lex_ranks). */
inline bool equation_lex_less(const std::string& a, const std::string& b) {
    const std::array<int, 256>& r = equation_lex_ranks();
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        int ra = r[static_cast<size_t>(static_cast<unsigned char>(a[i]))];
        int rb = r[static_cast<size_t>(static_cast<unsigned char>(b[i]))];
        if (ra != rb)
            return ra < rb;
    }
    return a.size() < b.size();
}

/**
 * Precompute keys for every index in `eqs` (all strings must have the same length N > 0).
 */
inline std::vector<CanonicalEqKey> compute_canonical_keys(const std::vector<std::string>& eqs) {
    const size_t total = eqs.size();
    std::vector<CanonicalEqKey> out;
    if (total == 0)
        return out;
    const int N = static_cast<int>(eqs[0].size());
    out.resize(total);

    /* at_least[c][k] = # equations where char c appears at least k times */
    std::vector<std::vector<int>> at_least(256, std::vector<int>(static_cast<size_t>(N + 2), 0));
    /* pos_count[c][i] = # equations with char c at position i */
    std::vector<std::vector<int>> pos_count(256, std::vector<int>(static_cast<size_t>(N), 0));

    for (const auto& eq : eqs) {
        int freq[256] = {};
        for (int i = 0; i < N; i++) {
            unsigned char c = static_cast<unsigned char>(eq[static_cast<size_t>(i)]);
            freq[c]++;
            pos_count[c][static_cast<size_t>(i)]++;
        }
        for (int c = 0; c < 256; c++) {
            for (int k = 1; k <= N; k++) {
                if (freq[c] >= k)
                    at_least[static_cast<size_t>(c)][static_cast<size_t>(k)]++;
            }
        }
    }

    const double inv_total = 1.0 / static_cast<double>(total);
    constexpr double floor_p = 1e-300;

    for (size_t ei = 0; ei < total; ei++) {
        const std::string& eq = eqs[ei];
        int freq[256] = {};
        bool seen[256] = {};
        int distinct = 0;
        double sym_log = 0.0;
        double pos_log = 0.0;

        for (int i = 0; i < N; i++) {
            unsigned char c = static_cast<unsigned char>(eq[static_cast<size_t>(i)]);
            if (!seen[c]) {
                seen[c] = true;
                distinct++;
            }
            freq[c]++;
            int occ = freq[c];
            double p = static_cast<double>(at_least[static_cast<size_t>(c)][static_cast<size_t>(occ)]) *
                       inv_total;
            sym_log += std::log(std::max(p, floor_p));

            double q = static_cast<double>(pos_count[static_cast<size_t>(c)][static_cast<size_t>(i)]) *
                       inv_total;
            pos_log += std::log(std::max(q, floor_p));
        }
        out[ei] = CanonicalEqKey{distinct, sym_log, pos_log};
    }
    return out;
}

/** Cached keys for the lifetime of a given `eqs` vector object (same address). */
inline const std::vector<CanonicalEqKey>& canonical_keys_for_pool(const std::vector<std::string>& eqs) {
    static const std::vector<std::string>* last = nullptr;
    static std::vector<CanonicalEqKey> cached;
    if (&eqs != last) {
        last = &eqs;
        cached = compute_canonical_keys(eqs);
    }
    return cached;
}

constexpr double kCanonLogEps = 1e-15;

/**
 * True if index `a` is strictly before `b` in canonical order (wins tie-breaks that used
 * "smallest index" before).
 */
inline bool canonical_less(size_t a, size_t b, const std::vector<std::string>& eqs,
                           const std::vector<CanonicalEqKey>& keys) {
    if (a == b)
        return false;
    const CanonicalEqKey& ka = keys[a];
    const CanonicalEqKey& kb = keys[b];
    if (ka.distinct != kb.distinct)
        return ka.distinct > kb.distinct;
    if (std::abs(ka.sym_log_sum - kb.sym_log_sum) > kCanonLogEps)
        return ka.sym_log_sum > kb.sym_log_sum;
    if (std::abs(ka.pos_log_sum - kb.pos_log_sum) > kCanonLogEps)
        return ka.pos_log_sum > kb.pos_log_sum;
    return equation_lex_less(eqs[a], eqs[b]);
}

/** Sort a list of distinct same-length equations into canonical pool order. */
inline void sort_equations_canonical(std::vector<std::string>& eqs) {
    if (eqs.size() <= 1)
        return;
    std::vector<CanonicalEqKey> keys = compute_canonical_keys(eqs);
    std::vector<size_t> ord(eqs.size());
    for (size_t i = 0; i < ord.size(); i++)
        ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        return canonical_less(a, b, eqs, keys);
    });
    std::vector<std::string> sorted;
    sorted.reserve(eqs.size());
    for (size_t idx : ord)
        sorted.push_back(eqs[idx]);
    eqs = std::move(sorted);
}

} // namespace nerdle

#endif
