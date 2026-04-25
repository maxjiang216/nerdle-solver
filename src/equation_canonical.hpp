/**
 * Canonical total order among equations of fixed length N, computed from the full pool
 * (same length, same generator rules). Used as deterministic tie-breaking after strategy
 * scores (entropy, Bellman EV, partition DP, etc.).
 *
 * Tuple order (earlier in sort = "better" for tie-breaks; compare lexicographically on this tuple,
 * except item 5 where smaller lex rank is better):
 *   1) number of distinct symbols — descending (more distinct symbols first; more symbols you learn)
 *   2) purple score — descending: sum over positions of P( pool equation has at least k copies of
 *      that position's character ), where k is 1-based occurrence from the left (expected purple+green
 *      for a random secret from the pool)
 *   3) green score — descending: sum over positions of P( char at that position in pool ) (expected
 *      greens for a random secret)
 *   4) partition score — descending: for this guess, number of distinct feedback patterns vs the
 *      whole pool (how many feedback equivalence classes the guess induces)
 *   5) lexicographic — ascending: smaller string wins; alphabet
 *        1,2,…,9,0,+,-,*,/,(,),^,^2,^3,=  where ^2/^3 are single-byte symbols 0x01/0x02 (maxi)
 */
#ifndef EQUATION_CANONICAL_HPP
#define EQUATION_CANONICAL_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nerdle {

struct CanonicalEqKey {
    int distinct = 0;
    double purple = 0.0;
    double green = 0.0;
    int partition = 0;
};

namespace canonical_detail {

/** 3^N for N in [0,10]; 0 if out of range. */
inline int pow3_n(int n) noexcept {
    static const int t[] = {1, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049};
    return (n >= 0 && n <= 10) ? t[n] : 0;
}

/**
 * Base-3 pack: trit i = B=0, P=1, G=2 at position i (LSB = position 0).
 * Must match compute_feedback_packed in nerdle_core.hpp.
 */
inline uint32_t feedback_packed(const char* guess, const char* solution, int N) {
    int remaining[256] = {};
    for (int i = 0; i < N; i++)
        remaining[static_cast<unsigned char>(solution[i])]++;

    unsigned char trits[16];
    for (int i = 0; i < N; i++) {
        if (guess[i] == solution[i]) {
            trits[i] = 2;
            remaining[static_cast<unsigned char>(guess[i])]--;
        } else {
            trits[i] = 0;
        }
    }
    for (int i = 0; i < N; i++) {
        if (trits[i] == 2) continue;
        unsigned char c = static_cast<unsigned char>(guess[i]);
        if (remaining[c] > 0) {
            trits[i] = 1;
            remaining[c]--;
        }
    }
    uint32_t code = 0;
    uint32_t mul = 1;
    for (int i = 0; i < N; i++) {
        code += trits[i] * mul;
        mul *= 3U;
    }
    return code;
}

} // namespace canonical_detail

/** Lexicographic ranks: digits 1–9,0, then + - * / ( ) ^, then ^2, ^3, then =. Unknown after '='. */
inline std::array<int, 256> make_equation_lex_ranks() {
    std::array<int, 256> r{};
    for (int i = 0; i < 256; i++)
        r[static_cast<size_t>(i)] = 10000 + i;
    int rk = 0;
    const char* order = "1234567890+-*/()^";
    for (const char* p = order; *p; ++p)
        r[static_cast<size_t>(static_cast<unsigned char>(*p))] = rk++;
    r[static_cast<size_t>(static_cast<unsigned char>('\x01'))] = rk++; // ^2 (maxi)
    r[static_cast<size_t>(static_cast<unsigned char>('\x02'))] = rk++; // ^3
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
    for (size_t ei = 0; ei < total; ei++) {
        const std::string& eq = eqs[ei];
        int freq[256] = {};
        bool seen[256] = {};
        int distinct = 0;
        double purp = 0.0;
        double grn = 0.0;

        for (int i = 0; i < N; i++) {
            unsigned char c = static_cast<unsigned char>(eq[static_cast<size_t>(i)]);
            if (!seen[c]) {
                seen[c] = true;
                distinct++;
            }
            freq[c]++;
            int occ = freq[c];
            double p_atleast = static_cast<double>(at_least[static_cast<size_t>(c)][static_cast<size_t>(occ)]) * inv_total;
            purp += p_atleast;

            double p_pos = static_cast<double>(pos_count[static_cast<size_t>(c)][static_cast<size_t>(i)]) * inv_total;
            grn += p_pos;
        }
        out[ei] = CanonicalEqKey{distinct, purp, grn, 0};
    }

    const int P = canonical_detail::pow3_n(N);
    if (P > 0) {
        std::vector<int> mark(static_cast<size_t>(P), -1);
        for (size_t g = 0; g < total; g++) {
            int dcount = 0;
            const char* guess = eqs[g].c_str();
            for (size_t j = 0; j < total; j++) {
                uint32_t code = canonical_detail::feedback_packed(guess, eqs[j].c_str(), N);
                if (mark[static_cast<size_t>(code)] != static_cast<int>(g)) {
                    mark[static_cast<size_t>(code)] = static_cast<int>(g);
                    dcount++;
                }
            }
            out[g].partition = dcount;
        }
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

constexpr double kCanonScoreEps = 1e-12;

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
    if (std::abs(ka.purple - kb.purple) > kCanonScoreEps)
        return ka.purple > kb.purple;
    if (std::abs(ka.green - kb.green) > kCanonScoreEps)
        return ka.green > kb.green;
    if (ka.partition != kb.partition)
        return ka.partition > kb.partition;
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
