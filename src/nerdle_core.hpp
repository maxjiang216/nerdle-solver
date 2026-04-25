/**
 * Shared Nerdle solver core: packed base-3 feedback, histogram entropy (no string keys),
 * v1 (legacy 300-pool 1-ply) and v2 (full pool + candidate bonus + 2-ply Rényi-style tiebreak).
 */
#ifndef NERDLE_CORE_HPP
#define NERDLE_CORE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "equation_canonical.hpp"
#include "micro_policy.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace nerdle {

inline int pow3_table(int N) {
    static const int t[] = {1, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049};
    return (N >= 0 && N <= 10) ? t[N] : 0;
}

/** Base-3 pack: trit i = B=0, P=1, G=2 at position i (LSB = position 0). */
inline uint32_t compute_feedback_packed(const char* guess, const char* solution, int N) {
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

inline uint32_t compute_feedback_packed(const std::string& guess, const std::string& solution,
                                        int N) {
    return compute_feedback_packed(guess.c_str(), solution.c_str(), N);
}

/** Packed code to G/P/B string (same order as compute_feedback_packed). */
inline std::string feedback_packed_to_string(uint32_t code, int N) {
    std::string s(static_cast<size_t>(N), 'B');
    for (int i = 0; i < N; i++) {
        int t = static_cast<int>(code % 3U);
        s[static_cast<size_t>(i)] = (t == 2) ? 'G' : (t == 1) ? 'P' : 'B';
        code /= 3U;
    }
    return s;
}

inline std::string compute_feedback_string(const std::string& guess, const std::string& solution,
                                           int N) {
    return feedback_packed_to_string(compute_feedback_packed(guess, solution, N), N);
}

/** User feedback string "G"/"P"/"B" to packed code (must match compute_feedback_packed). */
inline uint32_t feedback_string_to_packed(const char* fb, int N) {
    uint32_t code = 0;
    uint32_t mul = 1;
    for (int i = 0; i < N; i++) {
        unsigned t = 0;
        char c = fb[i];
        if (c == 'G' || c == 'g') t = 2;
        else if (c == 'P' || c == 'p') t = 1;
        code += t * mul;
        mul *= 3U;
    }
    return code;
}

inline bool is_consistent_packed(uint32_t expected_packed, const char* guess,
                                 const char* candidate, int N) {
    return compute_feedback_packed(guess, candidate, N) == expected_packed;
}

inline bool is_consistent_packed(uint32_t expected_packed, const std::string& guess,
                                 const std::string& candidate, int N) {
    return compute_feedback_packed(guess, candidate, N) == expected_packed;
}

inline bool is_consistent_feedback_string(const std::string& candidate, const std::string& guess,
                                          const char* feedback, int N) {
    return compute_feedback_packed(guess, candidate, N) ==
           feedback_string_to_packed(feedback, N);
}

/**
 * Histogram over packed feedback codes; clears hist[0..pow3(N)).
 * Sets H = Shannon entropy (bits), sum_sq = sum_k count_k^2.
 */
inline void entropy_and_partitions(const char* guess, const std::vector<std::string>& eqs,
                                   const std::vector<size_t>& cand_indices, int N,
                                   std::vector<int>& hist, double& out_H, double& out_sum_sq) {
    const int P = pow3_table(N);
    if (P <= 0 || N < 1 || N > 10) {
        out_H = 0;
        out_sum_sq = 0;
        return;
    }
    hist.assign(static_cast<size_t>(P), 0);

    const double total = static_cast<double>(cand_indices.size());
    for (size_t idx : cand_indices) {
        uint32_t code = compute_feedback_packed(guess, eqs[idx].c_str(), N);
        hist[code]++;
    }

    double H = 0.0;
    long long sum_sq = 0;
    for (int k = 0; k < P; k++) {
        int c = hist[k];
        if (c == 0) continue;
        sum_sq += static_cast<long long>(c) * c;
        double p = static_cast<double>(c) / total;
        H -= p * std::log2(p);
    }
    out_H = H;
    out_sum_sq = static_cast<double>(sum_sq);
}

inline double entropy_of_guess_packed(const char* guess, const std::vector<std::string>& eqs,
                                     const std::vector<size_t>& cand_indices, int N,
                                     std::vector<int>& hist) {
    double H, sum_sq;
    entropy_and_partitions(guess, eqs, cand_indices, N, hist, H, sum_sq);
    (void)sum_sq;
    return H;
}

/** Plug-in variance of entropy estimate (same as solve_adaptive). */
inline void entropy_and_var_from_indices(const char* guess, const std::vector<std::string>& eqs,
                                         const std::vector<size_t>& indices, int N,
                                         std::vector<int>& hist, double& out_h, double& out_var) {
    const int P = pow3_table(N);
    if (P <= 0 || N < 1 || N > 10) {
        out_h = 0;
        out_var = 0;
        return;
    }
    hist.assign(static_cast<size_t>(P), 0);
    for (size_t idx : indices) {
        uint32_t code = compute_feedback_packed(guess, eqs[idx].c_str(), N);
        hist[code]++;
    }
    double n = static_cast<double>(indices.size());
    if (n <= 1.0) {
        out_h = 0;
        out_var = 0;
        return;
    }
    double h = 0.0, sum_log_sq = 0.0;
    for (int k = 0; k < P; k++) {
        int c = hist[k];
        if (c == 0) continue;
        double p = static_cast<double>(c) / n;
        double lp = std::log2(p);
        h -= p * lp;
        sum_log_sq += p * lp * lp;
    }
    out_h = h;
    out_var = (sum_log_sq - h * h) / n;
    if (out_var < 0) out_var = 0;
}

// --- v1 legacy: subsampled pool (matches old bench behavior) ---
constexpr int V1_SEARCH_CAP = 300;

inline std::vector<size_t> build_pool_v1(const std::vector<std::string>& all_eqs,
                                         const std::vector<size_t>& candidate_indices) {
    std::vector<size_t> pool_indices;
    size_t n = all_eqs.size();
    if (n <= static_cast<size_t>(V1_SEARCH_CAP)) {
        for (size_t i = 0; i < n; i++) pool_indices.push_back(i);
    } else {
        int step = static_cast<int>(n / V1_SEARCH_CAP);
        if (step < 1) step = 1;
        for (int i = 0; i < static_cast<int>(n) &&
                        static_cast<int>(pool_indices.size()) < V1_SEARCH_CAP;
             i += step)
            pool_indices.push_back(static_cast<size_t>(i));
    }
    return pool_indices;
}

inline std::string best_guess_v1(const std::vector<std::string>& all_eqs,
                                 const std::vector<size_t>& candidate_indices,
                                 const std::unordered_set<size_t>& candidate_set, int N,
                                 std::vector<int>& hist) {
    if (candidate_indices.empty()) return "";
    if (candidate_indices.size() == 1) return all_eqs[candidate_indices[0]];
    if (candidate_indices.size() <= 2) return all_eqs[candidate_indices[0]];

    std::vector<size_t> pool_indices = build_pool_v1(all_eqs, candidate_indices);

    const std::vector<CanonicalEqKey>& ckeys = canonical_keys_for_pool(all_eqs);
    size_t best_idx = pool_indices[0];
    double best_h = -1.0;
    for (size_t idx : pool_indices) {
        double h = entropy_of_guess_packed(all_eqs[idx].c_str(), all_eqs, candidate_indices, N, hist);
        bool is_cand = candidate_set.count(idx) > 0;
        bool best_is_cand = candidate_set.count(best_idx) > 0;
        if (h > best_h || (h == best_h && is_cand && !best_is_cand) ||
            (h == best_h && is_cand == best_is_cand && canonical_less(idx, best_idx, all_eqs, ckeys))) {
            best_h = h;
            best_idx = idx;
        }
    }
    return all_eqs[best_idx];
}

// --- v2: full pool, 2-ply ---
constexpr int TWOPLY_TOP_K = 15;
constexpr int TWOPLY_INNER_EXTRA = 50;
constexpr size_t MAXI_RANDOM_EXTRA = 400; /* union(cands, sample) when |all| huge */

inline void build_guess_pool_v2(const std::vector<std::string>& all_eqs, int N,
                                const std::vector<size_t>& candidate_indices,
                                const std::unordered_set<size_t>& candidate_set,
                                std::vector<size_t>& out_pool, std::mt19937& rng) {
    (void)candidate_set;
    out_pool.clear();
    size_t n = all_eqs.size();
    if (N != 10 || n <= 50000) {
        out_pool.reserve(n);
        for (size_t i = 0; i < n; i++) out_pool.push_back(i);
        return;
    }
    /* Maxi: union(candidates, random sample of non-candidates) */
    std::unordered_set<size_t> seen;
    for (size_t idx : candidate_indices) {
        if (seen.insert(idx).second) out_pool.push_back(idx);
    }
    std::uniform_int_distribution<size_t> dist(0, n - 1);
    while (out_pool.size() < candidate_indices.size() + MAXI_RANDOM_EXTRA && seen.size() < n) {
        size_t j = dist(rng);
        if (seen.insert(j).second) out_pool.push_back(j);
    }
}

/** All-greens packed code (winning feedback) for length N */
inline uint32_t all_green_packed(int N) {
    uint32_t code = 0;
    uint32_t mul = 1;
    for (int i = 0; i < N; i++) {
        code += 2U * mul;
        mul *= 3U;
    }
    return code;
}

/** Minimize sum_sq / |S| over g2 in pool2. */
inline double min_normalized_sum_sq(const std::vector<std::string>& all_eqs,
                                      const std::vector<size_t>& S_inner,
                                      const std::vector<size_t>& pool2, int N,
                                      std::vector<int>& hist) {
    if (S_inner.size() <= 1) return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (size_t idx : pool2) {
        double H, sum_sq;
        entropy_and_partitions(all_eqs[idx].c_str(), all_eqs, S_inner, N, hist, H, sum_sq);
        (void)H;
        double v = sum_sq / static_cast<double>(S_inner.size());
        if (v < best) best = v;
    }
    return best;
}

/** Add up to `extra` indices with high 1-ply entropy on S_inner (full scan if n small). */
inline void append_top_entropy_indices(const std::vector<std::string>& all_eqs,
                                       const std::vector<size_t>& S_inner, int N, size_t n_all,
                                       int extra, std::vector<size_t>& pool2, std::vector<int>& hist,
                                       std::mt19937& rng) {
    if (extra <= 0 || S_inner.size() <= 1) return;
    std::unordered_set<size_t> have(pool2.begin(), pool2.end());
    constexpr size_t FULL_SCAN_CAP = 4000;
    std::vector<std::pair<double, size_t>> scored;
    if (n_all <= FULL_SCAN_CAP) {
        scored.reserve(n_all);
        for (size_t j = 0; j < n_all; j++) {
            double H, sum_sq;
            entropy_and_partitions(all_eqs[j].c_str(), all_eqs, S_inner, N, hist, H, sum_sq);
            (void)sum_sq;
            scored.push_back({H, j});
        }
    } else {
        /* Sample candidates for inner pool2 to keep 2-ply fast on Maxi-sized sets */
        const size_t sample_n = std::min(static_cast<size_t>(800), n_all);
        std::uniform_int_distribution<size_t> dist(0, n_all - 1);
        scored.reserve(sample_n);
        for (size_t s = 0; s < sample_n; s++) {
            size_t j = dist(rng);
            double H, sum_sq;
            entropy_and_partitions(all_eqs[j].c_str(), all_eqs, S_inner, N, hist, H, sum_sq);
            (void)sum_sq;
            scored.push_back({H, j});
        }
    }
    std::partial_sort(scored.begin(),
                      scored.begin() + std::min(static_cast<size_t>(extra), scored.size()),
                      scored.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < scored.size() && i < static_cast<size_t>(extra); i++) {
        size_t j = scored[i].second;
        if (have.insert(j).second) pool2.push_back(j);
    }
}

inline double twoply_score(const std::vector<std::string>& all_eqs, size_t g1_idx,
                           const std::vector<size_t>& S, int N, std::vector<int>& hist,
                           std::vector<size_t>& part_buf, std::vector<size_t>& pool2_buf,
                           std::mt19937& rng) {
    const char* g1 = all_eqs[g1_idx].c_str();
    const int P = pow3_table(N);
    std::vector<uint32_t> codes(S.size());
    for (size_t i = 0; i < S.size(); i++)
        codes[i] = compute_feedback_packed(g1, all_eqs[S[i]].c_str(), N);

    if (P <= 0) return 0.0;
    hist.assign(static_cast<size_t>(P), 0);
    for (uint32_t c : codes) hist[c]++;

    const double inv_s = 1.0 / static_cast<double>(S.size());
    double total = 0.0;
    size_t n_all = all_eqs.size();

    for (int k = 0; k < P; k++) {
        int cnt = hist[k];
        if (cnt <= 1) continue;
        part_buf.clear();
        for (size_t i = 0; i < S.size(); i++) {
            if (codes[i] == static_cast<uint32_t>(k)) part_buf.push_back(S[i]);
        }
        if (part_buf.size() <= 1) continue;

        pool2_buf = part_buf;
        append_top_entropy_indices(all_eqs, part_buf, N, n_all, TWOPLY_INNER_EXTRA, pool2_buf, hist,
                                   rng);
        double next_c = min_normalized_sum_sq(all_eqs, part_buf, pool2_buf, N, hist);
        total += (static_cast<double>(cnt) * inv_s) * next_c;
    }
    return total;
}

struct ScoredGuess {
    size_t idx;
    double H;
    double sum_sq;
    double score1;
    bool in_S;
};

inline std::string best_guess_v2(const std::vector<std::string>& all_eqs,
                                 const std::vector<size_t>& candidate_indices,
                                 const std::unordered_set<size_t>& candidate_set, int N,
                                 std::vector<int>& hist, std::mt19937& rng,
                                 bool twoply_tiebreak = true) {
    if (candidate_indices.empty()) return "";
    if (candidate_indices.size() == 1) return all_eqs[candidate_indices[0]];

    std::vector<size_t> pool;
    build_guess_pool_v2(all_eqs, N, candidate_indices, candidate_set, pool, rng);

    const size_t kS = candidate_indices.size();
    const double bonus = std::log2(static_cast<double>(kS)) / static_cast<double>(kS);

    std::vector<ScoredGuess> scored;
    scored.resize(pool.size());

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (size_t t = 0; t < pool.size(); t++) {
        size_t idx = pool[t];
        std::vector<int> local_hist;
        double H, sum_sq;
        entropy_and_partitions(all_eqs[idx].c_str(), all_eqs, candidate_indices, N, local_hist, H,
                               sum_sq);
        bool ins = candidate_set.count(idx) > 0;
        double s1 = H + (ins ? bonus : 0.0);
        scored[t] = ScoredGuess{idx, H, sum_sq, s1, ins};
    }

    const std::vector<CanonicalEqKey>& ckeys = canonical_keys_for_pool(all_eqs);
    std::sort(scored.begin(), scored.end(),
              [&](const ScoredGuess& a, const ScoredGuess& b) {
                  if (a.score1 != b.score1) return a.score1 > b.score1;
                  if (a.in_S != b.in_S) return a.in_S > b.in_S;
                  if (a.H != b.H) return a.H > b.H;
                  return canonical_less(a.idx, b.idx, all_eqs, ckeys);
              });

    if (scored.empty())
        return "";
    if (!twoply_tiebreak)
        return all_eqs[scored[0].idx];

    size_t K = std::min(scored.size(), static_cast<size_t>(TWOPLY_TOP_K));
    std::vector<size_t> part_buf;
    std::vector<size_t> pool2_buf;
    double best_2 = std::numeric_limits<double>::infinity();
    size_t best_idx = scored[0].idx;

    constexpr double twoply_eps = 1e-15;
    for (size_t i = 0; i < K; i++) {
        double ts = twoply_score(all_eqs, scored[i].idx, candidate_indices, N, hist, part_buf,
                                 pool2_buf, rng);
        if (ts < best_2 - twoply_eps ||
            (std::abs(ts - best_2) <= twoply_eps &&
             canonical_less(scored[i].idx, best_idx, all_eqs, ckeys))) {
            best_2 = ts;
            best_idx = scored[i].idx;
        }
    }
    return all_eqs[best_idx];
}

/** Union of all boards' candidates; same Maxi sampling rule as build_guess_pool_v2. */
inline void build_guess_pool_v2_multi(const std::vector<std::string>& all_eqs, int N,
                                      const std::vector<std::vector<size_t>>& boards,
                                      std::vector<size_t>& out_pool, std::mt19937& rng) {
    std::unordered_set<size_t> seen;
    for (const auto& b : boards) {
        for (size_t idx : b) seen.insert(idx);
    }
    size_t n = all_eqs.size();
    out_pool.clear();
    if (N != 10 || n <= 50000) {
        out_pool.reserve(n);
        for (size_t i = 0; i < n; i++) out_pool.push_back(i);
        return;
    }
    for (size_t idx : seen) out_pool.push_back(idx);
    std::uniform_int_distribution<size_t> dist(0, n - 1);
    std::unordered_set<size_t> have(seen.begin(), seen.end());
    while (out_pool.size() < seen.size() + MAXI_RANDOM_EXTRA && have.size() < n) {
        size_t j = dist(rng);
        if (have.insert(j).second) out_pool.push_back(j);
    }
}

/** Sum of twoply_score over boards with |S| > 1. */
inline double twoply_score_multi(const std::vector<std::string>& all_eqs, size_t g1_idx,
                                 const std::vector<std::vector<size_t>>& boards, int N,
                                 std::vector<int>& hist, std::vector<size_t>& part_buf,
                                 std::vector<size_t>& pool2_buf, std::mt19937& rng) {
    double total = 0.0;
    for (const auto& Sb : boards) {
        if (Sb.size() <= 1) continue;
        total += twoply_score(all_eqs, g1_idx, Sb, N, hist, part_buf, pool2_buf, rng);
    }
    return total;
}

struct ScoredGuessMulti {
    size_t idx;
    double H_sum;
    double score1;
    int in_count;
};

/**
 * Multi-board selector: independent-board entropy sum H_total = sum_b H_b(g) plus per-board
 * candidate bonus (same as v2). 2-ply tiebreak minimizes sum_b twoply_score on each board.
 * Caller handles edge cases (all singletons, prefer singleton guess when h>0, etc.).
 */
inline std::string best_guess_v2_multi(const std::vector<std::string>& all_eqs,
                                       const std::vector<std::vector<size_t>>& boards, int N,
                                       std::vector<int>& hist, std::mt19937& rng) {
    bool any_multi = false;
    for (const auto& b : boards) {
        if (b.size() > 1) {
            any_multi = true;
            break;
        }
    }
    if (!any_multi) {
        for (const auto& b : boards) {
            if (!b.empty()) return all_eqs[b[0]];
        }
        return "";
    }

    std::vector<std::unordered_set<size_t>> board_sets;
    board_sets.reserve(boards.size());
    for (const auto& b : boards) board_sets.emplace_back(b.begin(), b.end());

    std::vector<size_t> pool;
    build_guess_pool_v2_multi(all_eqs, N, boards, pool, rng);

    std::vector<ScoredGuessMulti> scored;
    scored.resize(pool.size());

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (size_t t = 0; t < pool.size(); t++) {
        size_t idx = pool[t];
        std::vector<int> local_hist;
        double H_sum = 0.0;
        double score1 = 0.0;
        int in_count = 0;
        for (size_t bi = 0; bi < boards.size(); bi++) {
            if (board_sets[bi].count(idx)) in_count++;
            const auto& Sb = boards[bi];
            if (Sb.size() <= 1) continue;
            double H, sum_sq;
            entropy_and_partitions(all_eqs[idx].c_str(), all_eqs, Sb, N, local_hist, H, sum_sq);
            (void)sum_sq;
            H_sum += H;
            score1 += H;
            if (board_sets[bi].count(idx)) {
                const double bonus =
                    std::log2(static_cast<double>(Sb.size())) / static_cast<double>(Sb.size());
                score1 += bonus;
            }
        }
        scored[t] = ScoredGuessMulti{idx, H_sum, score1, in_count};
    }

    const std::vector<CanonicalEqKey>& ckeys_m = canonical_keys_for_pool(all_eqs);
    std::sort(scored.begin(), scored.end(),
              [&](const ScoredGuessMulti& a, const ScoredGuessMulti& b) {
                  if (a.score1 != b.score1) return a.score1 > b.score1;
                  if (a.in_count != b.in_count) return a.in_count > b.in_count;
                  if (a.H_sum != b.H_sum) return a.H_sum > b.H_sum;
                  return canonical_less(a.idx, b.idx, all_eqs, ckeys_m);
              });

    size_t K = std::min(scored.size(), static_cast<size_t>(TWOPLY_TOP_K));
    std::vector<size_t> part_buf;
    std::vector<size_t> pool2_buf;
    double best_2 = std::numeric_limits<double>::infinity();
    size_t best_idx = scored.empty() ? 0 : scored[0].idx;

    constexpr double twoply_eps_m = 1e-15;
    for (size_t i = 0; i < K; i++) {
        double ts =
            twoply_score_multi(all_eqs, scored[i].idx, boards, N, hist, part_buf, pool2_buf, rng);
        if (ts < best_2 - twoply_eps_m ||
            (std::abs(ts - best_2) <= twoply_eps_m &&
             canonical_less(scored[i].idx, best_idx, all_eqs, ckeys_m))) {
            best_2 = ts;
            best_idx = scored[i].idx;
        }
    }
    return all_eqs[best_idx];
}

#include "partition_greedy.hpp"

} // namespace nerdle

#endif
