/**
 * Candidate-only partition strategy: always maximize the number of distinct feedbacks vs the
 * current candidate set. Ties: optional -- at increasing depth, compare the recursive solve
 * distribution (P solve in 1, 2, …) among tied guesses. tie_depth=0 never does that: first
 * max-partition guess in candidate order wins.
 *
 * Full n×n feedback may be precomputed (fast); above kPartitionGreedyMaxFullFbN (or a memory
 * cap) feedback is computed on the fly.
 */
#ifndef PARTITION_GREEDY_HPP
#define PARTITION_GREEDY_HPP

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Memory budget for the optional full n×n feedback table.
 * - Interactive: small cap (safe for per-turn calls in the CLI / future UI).
 * - Report: allows ~8-tile Nerdle-sized pools (≈17k equations, ≈1.2 GB) for one-shot analysis.
 */
enum class PartitionFbBudget { Interactive, Report };

struct PartitionSolveDist {
    std::array<double, 7> solve_at{};
};

inline bool partition_solve_dist_less(const PartitionSolveDist& a, const PartitionSolveDist& b,
                                     int horizon) {
    constexpr double eps = 1e-12;
    for (int t = 1; t <= horizon && t < static_cast<int>(a.solve_at.size()); t++) {
        double da = a.solve_at[static_cast<size_t>(t)];
        double db = b.solve_at[static_cast<size_t>(t)];
        if (std::abs(da - db) > eps)
            return da < db;
    }
    return false;
}

class PartitionGreedyEvaluator {
  public:
    /**
     * If `shared_full_fb` is non-null, it must be length n*n; feedback is read from it and
     * build_feedback_matrix() is a no-op (for parallel per-secret sim with one pre-built table).
     */
    PartitionGreedyEvaluator(const std::vector<std::string>& pool, int N, int max_tries,
                            int tie_depth = 0, PartitionFbBudget fb_budget = PartitionFbBudget::Interactive,
                            const std::vector<uint32_t>* shared_full_fb = nullptr)
        : pool_(pool), n_(static_cast<int>(pool.size())), N_(N), max_tries_(max_tries),
          tie_depth_(tie_depth), fb_budget_(fb_budget), fb_external_(shared_full_fb),
          P_(pow3_table(N)), green_(all_green_packed(N)), stamp_(static_cast<size_t>(P_), -1) {}

    void build_feedback_matrix() {
        if (n_ <= 0)
            return;
        if (fb_external_) {
            if (static_cast<int>(fb_external_->size()) != n_ * n_)
                return;
            return;
        }
        const size_t n2 = static_cast<size_t>(n_) * static_cast<size_t>(n_);
        const int cap_n = (fb_budget_ == PartitionFbBudget::Report) ? 20000 : 8000;
        const size_t cap_bytes = (fb_budget_ == PartitionFbBudget::Report) ? 1400ull * 1024 * 1024
                                                                           : 300ull * 1024 * 1024;
        if (n_ > cap_n || n2 * sizeof(uint32_t) > cap_bytes) {
            fb_.clear();
            return;
        }
        fb_.resize(n2);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
        for (int g = 0; g < n_; g++) {
            for (int s = 0; s < n_; s++) {
                fb_[static_cast<size_t>(g) * static_cast<size_t>(n_) + static_cast<size_t>(s)] =
                    compute_feedback_packed(pool_[static_cast<size_t>(g)].c_str(),
                                            pool_[static_cast<size_t>(s)].c_str(), N_);
            }
        }
    }

    /** O(1) if full table present (owned or shared). */
    uint32_t feedback_code(int g, int s) const {
        if (fb_external_)
            return (*fb_external_)[static_cast<size_t>(g) * static_cast<size_t>(n_) + static_cast<size_t>(s)];
        if (!fb_.empty())
            return fb_[static_cast<size_t>(g) * static_cast<size_t>(n_) + static_cast<size_t>(s)];
        return compute_feedback_packed(pool_[static_cast<size_t>(g)], pool_[static_cast<size_t>(s)], N_);
    }

    const std::vector<uint32_t>& owned_feedback() const { return fb_; }

    PartitionSolveDist solve(const std::vector<int>& state, int k) {
        if (state.empty() || k < 1)
            return {};
        if (state.size() == 1) {
            PartitionSolveDist o{};
            o.solve_at[1] = 1.0;
            return o;
        }

        std::string key = make_key(state, k);
        auto it = memo_.find(key);
        if (it != memo_.end())
            return it->second;

        int best_g = choose_guess(state, k);
        PartitionSolveDist best = after_guess(state, best_g, k);
        memo_[std::move(key)] = best;
        return best;
    }

    int best_guess_index(const std::vector<size_t>& candidate_indices, int k,
                        PartitionSolveDist* out = nullptr) {
        std::vector<int> st(candidate_indices.size());
        for (size_t i = 0; i < candidate_indices.size(); i++)
            st[i] = static_cast<int>(candidate_indices[i]);
        int g = choose_guess(st, k);
        if (out)
            *out = after_guess(st, g, k);
        return g;
    }

    std::string best_guess_string(const std::vector<size_t>& candidate_indices, int k,
                                 PartitionSolveDist* out = nullptr) {
        int g = best_guess_index(candidate_indices, k, out);
        return pool_[static_cast<size_t>(g)];
    }

    size_t memo_size() const { return memo_.size(); }
    bool has_full_feedback_matrix() const noexcept {
        if (!fb_.empty())
            return true;
        return fb_external_ && static_cast<int>(fb_external_->size()) == n_ * n_;
    }

    /**
     * If the first guess is (must be) pool index guess_idx, return the distribution of total
     * guesses to win (uniform on secrets), with the same tie_depth policy in all later positions.
     */
    PartitionSolveDist distribution_with_fixed_first_guess(int guess_idx) {
        if (guess_idx < 0 || guess_idx >= n_)
            return {};
        std::vector<int> st(static_cast<size_t>(n_));
        for (int i = 0; i < n_; i++)
            st[static_cast<size_t>(i)] = i;
        return after_guess(st, guess_idx, max_tries_);
    }

  private:
    const std::vector<std::string>& pool_;
    int n_ = 0;
    int N_ = 0;
    int max_tries_ = 6;
    int tie_depth_ = 0;
    PartitionFbBudget fb_budget_ = PartitionFbBudget::Interactive;
    const std::vector<uint32_t>* fb_external_ = nullptr;
    int P_ = 0;
    uint32_t green_ = 0;
    std::vector<uint32_t> fb_;
    std::unordered_map<std::string, PartitionSolveDist> memo_;
    std::vector<int> stamp_;
    int stamp_id_ = 0;

    std::string make_key(const std::vector<int>& state, int k) const {
        std::string key;
        key.reserve(1 + state.size() * 2);
        key.push_back(static_cast<char>(k));
        for (int x : state) {
            uint16_t y = static_cast<uint16_t>(x);
            key.push_back(static_cast<char>(y & 0xff));
            key.push_back(static_cast<char>((y >> 8) & 0xff));
        }
        return key;
    }

    int partition_count(const std::vector<int>& state, int g) {
        ++stamp_id_;
        int count = 0;
        for (int s : state) {
            int code = static_cast<int>(feedback_code(g, s));
            if (stamp_[static_cast<size_t>(code)] != stamp_id_) {
                stamp_[static_cast<size_t>(code)] = stamp_id_;
                count++;
            }
        }
        return count;
    }

    std::vector<int> best_partition_guesses(const std::vector<int>& state, int k) {
        std::vector<int> out;
        if (k < 2) {
            return state;
        }
        int best = -1;
        for (int g : state) {
            int pc = partition_count(state, g);
            if (pc > best) {
                best = pc;
                out.clear();
                out.push_back(g);
            } else if (pc == best) {
                out.push_back(g);
            }
        }
        return out;
    }

    int choose_guess(const std::vector<int>& state, int k) {
        std::vector<int> guesses = best_partition_guesses(state, k);
        if (guesses.empty())
            return -1;
        if (guesses.size() == 1 || tie_depth_ <= 0 || k < 3)
            return guesses[0];

        const int horizon = (std::min)(k, tie_depth_ + 2);
        bool have_best = false;
        PartitionSolveDist best{};
        int best_g = guesses[0];
        for (int g : guesses) {
            PartitionSolveDist val = after_guess(state, g, k);
            if (!have_best || partition_solve_dist_less(best, val, horizon)) {
                have_best = true;
                best = val;
                best_g = g;
            }
        }
        return best_g;
    }

    PartitionSolveDist after_guess(const std::vector<int>& state, int g, int k) {
        PartitionSolveDist out{};
        const double inv = 1.0 / static_cast<double>(state.size());
        std::vector<std::vector<int>> buckets(static_cast<size_t>(P_));
        std::vector<int> used_codes;
        for (int s : state) {
            int code = static_cast<int>(feedback_code(g, s));
            if (static_cast<uint32_t>(code) == green_) {
                out.solve_at[1] += inv;
            } else {
                std::vector<int>& bucket = buckets[static_cast<size_t>(code)];
                if (bucket.empty())
                    used_codes.push_back(code);
                bucket.push_back(s);
            }
        }

        for (int code : used_codes) {
            std::vector<int>& child = buckets[static_cast<size_t>(code)];
            const double weight = static_cast<double>(child.size()) * inv;
            PartitionSolveDist v = solve(child, k - 1);
            for (int t = 2; t <= k && t <= max_tries_; t++)
                out.solve_at[static_cast<size_t>(t)] += weight * v.solve_at[static_cast<size_t>(t - 1)];
        }
        return out;
    }
};

/**
 * Fixed opening recommended for 10-tile (6-try) Maxi under partition: exact max-partition winner
 * over `data/equations_10.txt` (sweep / winners). Interactive and solver_json use this for the
 * first suggestion when still in the candidate set; the generic best_guess_partition_policy does
 * not special-case the opening.
 */
inline constexpr const char* kMaxiPartitionFixedOpening = "58+2-13=47";

/**
 * Fixed opening for 8-tile Classic under partition: max-partition first guess on the current
 * `data/equations_8.txt` pool. Interactive and solver_json use it when that row is in candidates.
 */
inline constexpr const char* kClassicPartitionFixedOpening = "52-34=18";

/**
 * Heuristic: cheap greedy max-partition (tie_depth=0) when the filtered set is large; enable
 * recursive tiebreak among equal-partition ties (tie_depth=1) when the set is small enough.
 */
inline int maxi_partition_tie_depth_for_interactive(size_t num_candidates) {
    constexpr size_t kThreshold = 512;
    if (num_candidates <= kThreshold)
        return 1;
    return 0;
}

/**
 * partition tie_depth: 0 = greedy max feedback classes only (no extra tiebreaks);
 * 1+ = on ties, compare solve distribution to that depth.
 */
inline std::string best_guess_partition_policy(const std::vector<std::string>& all_eqs,
                                              const std::vector<size_t>& candidate_indices, int N,
                                              int tries_remaining, int partition_tie_depth = 0) {
    if (candidate_indices.empty())
        return "";
    if (candidate_indices.size() == 1)
        return all_eqs[candidate_indices[0]];
    if (tries_remaining < 1)
        return all_eqs[candidate_indices[0]];

    PartitionGreedyEvaluator ev(all_eqs, N, tries_remaining, partition_tie_depth, PartitionFbBudget::Interactive);
    ev.build_feedback_matrix();
    return ev.best_guess_string(candidate_indices, tries_remaining);
}

inline std::string best_guess_greedy_max_feedback_partition(const std::vector<std::string>& all_eqs,
                                                            const std::vector<size_t>& candidate_indices,
                                                            int N, int tries_remaining = 6) {
    return best_guess_partition_policy(all_eqs, candidate_indices, N, tries_remaining, 0);
}

#endif
