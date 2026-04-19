/**
 * Shared benchmark simulation (used by bench_nerdle, compare_bellman, etc.).
 */
#ifndef BENCH_SOLVE_HPP
#define BENCH_SOLVE_HPP

#include "micro_policy.hpp"
#include "nerdle_core.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nerdle_bench {

constexpr int MAXI_TRIES = 6;

static const unsigned char PLACE_SQ = '\x01';
static const unsigned char PLACE_CB = '\x02';

inline std::string normalize_maxi(std::string s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2) {
            if ((unsigned char)s[i + 1] == 0xB2) {
                out += (char)PLACE_SQ;
                i++;
                continue;
            }
            if ((unsigned char)s[i + 1] == 0xB3) {
                out += (char)PLACE_CB;
                i++;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

/** Hardcoded first guesses for entropy baseline (must exist in equation set). */
inline const std::unordered_map<int, std::string>& first_guess_map() {
    static const std::unordered_map<int, std::string> m = {
        {5, "3+2=5"},
        {6, "4*7=28"},
        {7, "4+27=31"},
        {8, "48-32=16"},
        {10, "76+1-23=54"},
    };
    return m;
}

enum class Selector { V1, V2 };

/** Micro (N=5): bellman vs partition differ. Mini (N=6): use optimal only (unique min E[guesses] policy). */
enum class PlayStrategy { Bellman, Optimal, Partition, Entropy };

inline std::string pick_guess(const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& candidate_indices,
                              const std::unordered_set<size_t>& candidate_set, int N,
                              std::vector<int>& hist, std::mt19937& rng, Selector sel,
                              bool v2_twoply_tiebreak = true) {
    if (sel == Selector::V1)
        return nerdle::best_guess_v1(all_eqs, candidate_indices, candidate_set, N, hist);
    return nerdle::best_guess_v2(all_eqs, candidate_indices, candidate_set, N, hist, rng,
                                 v2_twoply_tiebreak);
}

/**
 * Recursive play heuristic (applies every turn after the fixed first guess):
 *   |C|<=2 — trivial (submit min-index candidate; E[remaining]=1 or 1.5),
 *   else if some g in C is feedback-injective on C — smallest such pool index,
 *   else if some g not in C is injective on C — smallest such g,
 *   else entropy (v1/v2) via pick_guess.
 * Optional fb_full row-major [g*n_pool+s] speeds injective checks; nullptr uses packed feedback.
 */
inline std::string pick_guess_tiered(const std::vector<std::string>& all_eqs, int n_pool,
                                   const std::vector<size_t>& candidate_indices,
                                   const std::unordered_set<size_t>& candidate_set, int N,
                                   std::vector<int>& hist, std::mt19937& rng, Selector sel,
                                   const std::vector<uint32_t>* fb_full_opt,
                                   bool v2_twoply_tiebreak = true) {
    const size_t k = candidate_indices.size();
    if (k == 0)
        return "";
    if (k == 1)
        return all_eqs[candidate_indices[0]];
    if (k == 2) {
        const size_t i0 = candidate_indices[0];
        const size_t i1 = candidate_indices[1];
        return all_eqs[std::min(i0, i1)];
    }

    auto injective_for = [&](int g) -> bool {
        std::unordered_set<uint32_t> seen;
        seen.reserve(k * 2 + 1);
        for (size_t s_pool : candidate_indices) {
            uint32_t code = 0;
            if (fb_full_opt &&
                fb_full_opt->size() == static_cast<size_t>(n_pool) * static_cast<size_t>(n_pool)) {
                code = (*fb_full_opt)[static_cast<size_t>(g) * static_cast<size_t>(n_pool) + s_pool];
            } else {
                code = nerdle::compute_feedback_packed(all_eqs[static_cast<size_t>(g)], all_eqs[s_pool], N);
            }
            if (!seen.insert(code).second)
                return false;
        }
        return true;
    };

    std::vector<size_t> sorted_c = candidate_indices;
    std::sort(sorted_c.begin(), sorted_c.end());
    for (size_t g : sorted_c) {
        if (injective_for(static_cast<int>(g)))
            return all_eqs[g];
    }
    for (int g = 0; g < n_pool; ++g) {
        if (candidate_set.count(static_cast<size_t>(g)) != 0)
            continue;
        if (injective_for(g))
            return all_eqs[static_cast<size_t>(g)];
    }
    return pick_guess(all_eqs, candidate_indices, candidate_set, N, hist, rng, sel,
                      v2_twoply_tiebreak);
}

/** Same contract as solve_one (entropy path), but subsequent guesses use pick_guess_tiered. */
inline int solve_one_tiered(const std::string& solution, const std::vector<std::string>& all_eqs,
                            const std::string& first_guess, int N, int max_tries, Selector sel,
                            uint64_t rng_seed, const std::vector<uint32_t>* fb_full_opt,
                            bool v2_twoply_tiebreak = true) {
    std::mt19937 rng(static_cast<std::mt19937::result_type>(rng_seed));
    std::vector<size_t> candidates;
    for (size_t i = 0; i < all_eqs.size(); i++)
        candidates.push_back(i);
    std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());
    const int n_pool = static_cast<int>(all_eqs.size());

    std::string guess = first_guess;
    std::vector<int> hist;

    for (int turn = 1; turn <= max_tries; turn++) {
        uint32_t packed = nerdle::compute_feedback_packed(guess, solution, N);
        if (packed == nerdle::all_green_packed(N))
            return turn;

        std::vector<size_t> next_candidates;
        std::unordered_set<size_t> next_set;
        for (size_t idx : candidates) {
            if (nerdle::compute_feedback_packed(guess, all_eqs[idx], N) == packed)
                next_candidates.push_back(idx);
        }
        for (size_t idx : next_candidates)
            next_set.insert(idx);
        candidates = std::move(next_candidates);
        candidate_set = std::move(next_set);

        if (candidates.empty())
            return max_tries + 1;

        guess = pick_guess_tiered(all_eqs, n_pool, candidates, candidate_set, N, hist, rng, sel,
                                  fb_full_opt, v2_twoply_tiebreak);
        if (guess.empty())
            return max_tries + 1;
    }
    return max_tries + 1;
}

/** Returns number of guesses to hit solution, or max_tries+1 on failure/empty candidates. */
inline int solve_one(const std::string& solution, const std::vector<std::string>& all_eqs,
                     const std::string& first_guess, int N, int max_tries, Selector sel,
                     PlayStrategy strat, uint64_t rng_seed,
                     const std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash>*
                         micro_policy) {
    std::mt19937 rng(static_cast<std::mt19937::result_type>(rng_seed));
    std::vector<size_t> candidates;
    for (size_t i = 0; i < all_eqs.size(); i++) candidates.push_back(i);
    std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());

    std::string guess;
    if (strat == PlayStrategy::Partition)
        guess = nerdle::best_guess_partition_policy(all_eqs, candidates, N, max_tries);
    else
        guess = first_guess;
    std::vector<int> hist;

    for (int turn = 1; turn <= max_tries; turn++) {
        uint32_t packed = nerdle::compute_feedback_packed(guess, solution, N);
        if (packed == nerdle::all_green_packed(N)) {
            return turn;
        }

        std::vector<size_t> next_candidates;
        std::unordered_set<size_t> next_set;
        for (size_t idx : candidates) {
            if (nerdle::compute_feedback_packed(guess, all_eqs[idx], N) == packed) {
                next_candidates.push_back(idx);
                next_set.insert(idx);
            }
        }
        candidates = std::move(next_candidates);
        candidate_set = std::move(next_set);

        if (candidates.empty())
            return max_tries + 1;

        if (strat == PlayStrategy::Partition) {
            guess = nerdle::best_guess_partition_policy(all_eqs, candidates, N, max_tries - turn);
        } else if ((((strat == PlayStrategy::Bellman && N == 5) ||
                      (strat == PlayStrategy::Optimal && N == 6)) &&
                     micro_policy && !micro_policy->empty())) {
            std::string pg = nerdle::guess_from_micro_policy(*micro_policy, all_eqs, candidates);
            if (!pg.empty())
                guess = pg;
            else
                guess = pick_guess(all_eqs, candidates, candidate_set, N, hist, rng, sel);
        } else {
            guess = pick_guess(all_eqs, candidates, candidate_set, N, hist, rng, sel);
        }
    }
    return max_tries + 1;
}

} // namespace nerdle_bench

#endif
