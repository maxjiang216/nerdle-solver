#ifndef QUAD_PARTITION_HPP
#define QUAD_PARTITION_HPP

#include "binerdle_partition.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace nerdle {

/** Classic 8: same 6-try model for `best_guess_partition_policy` tie resolution (matches binerdle). */
inline constexpr int kSingleBoardOpeningTries = 6;

/**
 * When the live candidate set is still the full pool, first guess is the same as
 * one-board Nerdle partition: `partition_fixed_opening_tie6` (8-tile: **52-34=18**, see
 * `kClassicPartitionFixedOpening`) if that row is in the file, else `best_guess_partition_policy`
 * — same rule as `binerdle` / `nerdle --strategy partition` precompute.
 */
inline std::string quad_full_pool_partition_opening(
    const std::vector<std::string>& all_eqs, int n, int tries_remaining = kSingleBoardOpeningTries,
    int partition_tie_depth = kPartitionInteractiveTieDepth) {
    std::vector<size_t> all;
    all.reserve(all_eqs.size());
    for (size_t i = 0; i < all_eqs.size(); i++) all.push_back(i);
    if (const char* fo = partition_fixed_opening_tie6(n)) {
        for (size_t idx : all) {
            if (all_eqs[idx] == fo) return all_eqs[idx];
        }
    }
    const int tr = (n == 8) ? kSingleBoardOpeningTries : std::max(1, tries_remaining);
    return best_guess_partition_policy(all_eqs, all, n, tr, partition_tie_depth);
}

/** Boards that are not solved and still have candidate sets. */
inline void quad_active_indices(const bool solved[4], const std::vector<size_t>* B[4],
                                std::vector<int>& out_active) {
    out_active.clear();
    for (int b = 0; b < 4; b++) {
        if (solved[b] || B[b]->empty()) continue;
        out_active.push_back(b);
    }
}

/**
 * Partition strategy for Quad Nerdle: 3–4 active boards = maximize total partition
 * count (sum over active boards) over guesses in the union of remaining candidates; 2 boards =
 * binerdle partition; 1 = single partition policy; all active + same state = same as one board
 * (incl. fixed opening on full pool); known answer = play a singleton.
 */
inline std::string best_guess_quad_partition(
    const std::vector<std::string>& all_eqs, const std::vector<size_t>& c1,
    const std::vector<size_t>& c2, const std::vector<size_t>& c3, const std::vector<size_t>& c4, int n,
    int tries_remaining, bool solved[4], int partition_tie_depth = kPartitionInteractiveTieDepth) {
    const std::vector<size_t>* B[4] = {&c1, &c2, &c3, &c4};

    for (int b = 0; b < 4; b++) {
        if (solved[b]) continue;
        if (B[b]->empty()) return "";
    }

    for (int b = 0; b < 4; b++) {
        if (solved[b]) continue;
        if (B[b]->size() == 1) return all_eqs[(*B[b])[0]];
    }

    std::vector<int> act;
    quad_active_indices(solved, B, act);
    if (act.empty()) return "";

    if (act.size() == 1) {
        return best_guess_partition_policy(all_eqs, *B[act[0]], n, std::max(1, tries_remaining),
                                           partition_tie_depth);
    }

    if (act.size() == 2) {
        return best_guess_binerdle_partition(
            all_eqs, *B[act[0]], *B[act[1]], n, std::max(1, tries_remaining), solved[act[0]],
            solved[act[1]], partition_tie_depth);
    }

    const std::vector<size_t>& ca = *B[act[0]];
    bool all_same = true;
    for (size_t a = 1; a < act.size(); a++) {
        if (!binerdle_same_state(ca, *B[act[static_cast<int>(a)]])) {
            all_same = false;
            break;
        }
    }
    if (all_same) {
        if (ca.size() == all_eqs.size())
            return quad_full_pool_partition_opening(all_eqs, n, tries_remaining, partition_tie_depth);
        return best_guess_partition_policy(all_eqs, ca, n, std::max(1, tries_remaining), partition_tie_depth);
    }

    const std::vector<CanonicalEqKey>& ckeys = canonical_keys_for_pool(all_eqs);
    std::unordered_set<size_t> u;
    for (int bi : act) {
        for (size_t idx : *B[bi]) u.insert(idx);
    }
    std::vector<size_t> pool(u.begin(), u.end());
    std::sort(pool.begin(), pool.end());

    const int P = pow3_table(n);
    if (P <= 0) return "";
    std::vector<int> stamp(static_cast<size_t>(P), -1);
    int stamp_id = 0;
    int best_s = -1;
    size_t best_idx = 0;
    for (size_t g : pool) {
        int s = 0;
        for (int bi : act) {
            s += binerdle_partition_classes_excluding(all_eqs, *B[bi], g, n, stamp, stamp_id);
        }
        if (s > best_s || (s == best_s && canonical_less(g, best_idx, all_eqs, ckeys))) {
            best_s = s;
            best_idx = g;
        }
    }
    return all_eqs[best_idx];
}
} // namespace nerdle
#endif
