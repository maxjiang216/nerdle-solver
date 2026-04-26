#ifndef BINERDLE_PARTITION_HPP
#define BINERDLE_PARTITION_HPP

#include "nerdle_core.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace nerdle {

struct BinerdlePartitionScore {
    size_t idx = 0;
    int classes = 0;
};

inline bool binerdle_same_state(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    if (a.size() != b.size())
        return false;
    std::unordered_set<size_t> seen(a.begin(), a.end());
    for (size_t x : b) {
        if (!seen.count(x))
            return false;
    }
    return true;
}

inline int binerdle_partition_classes_excluding(const std::vector<std::string>& all_eqs,
                                                const std::vector<size_t>& state, size_t guess_idx,
                                                int N, std::vector<int>& stamp,
                                                int& stamp_id) {
    const int P = pow3_table(N);
    if (P <= 0 || state.empty())
        return 0;
    if (stamp.size() != static_cast<size_t>(P))
        stamp.assign(static_cast<size_t>(P), -1);
    ++stamp_id;
    int count = 0;
    const std::string& guess = all_eqs[guess_idx];
    for (size_t s : state) {
        if (s == guess_idx)
            continue;
        uint32_t code = compute_feedback_packed(guess, all_eqs[s], N);
        if (stamp[code] != stamp_id) {
            stamp[code] = stamp_id;
            count++;
        }
    }
    return count;
}

inline std::string best_guess_binerdle_partition(const std::vector<std::string>& all_eqs,
                                                 const std::vector<size_t>& c1,
                                                 const std::vector<size_t>& c2, int N,
                                                 int tries_remaining, bool solved1 = false,
                                                 bool solved2 = false,
                                                 int partition_tie_depth = kPartitionInteractiveTieDepth) {
    if (c1.empty() || c2.empty())
        return "";

    const std::vector<CanonicalEqKey>& ckeys = canonical_keys_for_pool(all_eqs);

    auto canonical_best_singleton = [&]() -> size_t {
        if (c1.empty()) return c2[0];
        if (c2.empty()) return c1[0];
        if (solved1 && !solved2) return c2[0];
        if (solved2 && !solved1) return c1[0];
        return canonical_less(c1[0], c2[0], all_eqs, ckeys) ? c1[0] : c2[0];
    };

    if (c1.size() == 1 && c2.size() == 1)
        return all_eqs[canonical_best_singleton()];
    if (!solved1 && c1.size() == 1)
        return all_eqs[c1[0]];
    if (!solved2 && c2.size() == 1)
        return all_eqs[c2[0]];

    if (solved1 && !solved2) {
        return best_guess_partition_policy(all_eqs, c2, N, tries_remaining, partition_tie_depth);
    }
    if (solved2 && !solved1) {
        return best_guess_partition_policy(all_eqs, c1, N, tries_remaining, partition_tie_depth);
    }

    if (binerdle_same_state(c1, c2)) {
        if (c1.size() == all_eqs.size()) {
            if (const char* fixed = partition_fixed_opening_tie6(N)) {
                for (size_t idx : c1) {
                    if (all_eqs[idx] == fixed)
                        return all_eqs[idx];
                }
            }
        }
        return best_guess_partition_policy(all_eqs, c1, N, tries_remaining, partition_tie_depth);
    }

    std::unordered_set<size_t> in1(c1.begin(), c1.end());
    std::unordered_set<size_t> in2(c2.begin(), c2.end());
    std::vector<size_t> pool = c1;
    pool.reserve(c1.size() + c2.size());
    for (size_t idx : c2) {
        if (!in1.count(idx))
            pool.push_back(idx);
    }

    std::vector<int> stamp(static_cast<size_t>(pow3_table(N)), -1);
    int stamp_id = 0;
    bool have_best = false;
    BinerdlePartitionScore best{};

    for (size_t idx : pool) {
        int classes = 0;
        if (in1.count(idx))
            classes += binerdle_partition_classes_excluding(all_eqs, c2, idx, N, stamp, stamp_id);
        if (in2.count(idx))
            classes += binerdle_partition_classes_excluding(all_eqs, c1, idx, N, stamp, stamp_id);

        if (!have_best || classes > best.classes ||
            (classes == best.classes && canonical_less(idx, best.idx, all_eqs, ckeys))) {
            best = BinerdlePartitionScore{idx, classes};
            have_best = true;
        }
    }

    return have_best ? all_eqs[best.idx] : "";
}

} // namespace nerdle

#endif
