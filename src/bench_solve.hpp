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
        {7, "6+18=24"},
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
                              std::vector<int>& hist, std::mt19937& rng, Selector sel) {
    if (sel == Selector::V1)
        return nerdle::best_guess_v1(all_eqs, candidate_indices, candidate_set, N, hist);
    return nerdle::best_guess_v2(all_eqs, candidate_indices, candidate_set, N, hist, rng);
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
