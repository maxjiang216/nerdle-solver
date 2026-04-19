/**
 * Full-pool-guess Bellman on a candidate subset + feedback matrix cache + injectivity heuristics.
 *
 * Recursive DP shortcuts (same logic as top-level first-guess analysis): if the remaining mask S
 * admits an injective guess, the optimal continuation has a closed form — try a guess in S first
 * (E=(2|S|-1)/|S|), else an injective guess outside S (E=2). Otherwise fall back to full Bellman.
 */
#ifndef SUBGAME_OPTIMAL_HPP
#define SUBGAME_OPTIMAL_HPP

#include "micro_policy.hpp"
#include "nerdle_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nerdle {
namespace subgame {

inline constexpr uint32_t kFbCacheMagic = 0x4E424657u;

inline bool try_load_fb_cache(const std::string& path, int* out_n, int* out_N,
                             std::vector<uint32_t>* out_fb) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    uint32_t mag = 0, ver = 1;
    uint32_t n = 0, N = 0;
    f.read(reinterpret_cast<char*>(&mag), 4);
    f.read(reinterpret_cast<char*>(&ver), 4);
    f.read(reinterpret_cast<char*>(&n), 4);
    f.read(reinterpret_cast<char*>(&N), 4);
    (void)ver;
    if (mag != kFbCacheMagic || n == 0 || n > 100000)
        return false;
    size_t sz = static_cast<size_t>(n) * static_cast<size_t>(n);
    out_fb->resize(sz);
    f.read(reinterpret_cast<char*>(out_fb->data()), static_cast<std::streamsize>(sz * sizeof(uint32_t)));
    if (!f)
        return false;
    *out_n = static_cast<int>(n);
    *out_N = static_cast<int>(N);
    return true;
}

inline bool write_fb_cache(const std::string& path, int n, int N, const std::vector<uint32_t>& fb) {
    if (fb.size() != static_cast<size_t>(n) * static_cast<size_t>(n))
        return false;
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    uint32_t ver = 1;
    uint32_t nu = static_cast<uint32_t>(n);
    uint32_t Nt = static_cast<uint32_t>(N);
    f.write(reinterpret_cast<const char*>(&kFbCacheMagic), 4);
    f.write(reinterpret_cast<const char*>(&ver), 4);
    f.write(reinterpret_cast<const char*>(&nu), 4);
    f.write(reinterpret_cast<const char*>(&Nt), 4);
    f.write(reinterpret_cast<const char*>(fb.data()),
            static_cast<std::streamsize>(fb.size() * sizeof(uint32_t)));
    return static_cast<bool>(f);
}

/**
 * True iff some equation index in cand_global, used as guess, assigns pairwise distinct feedback
 * codes to every pair of candidate secrets.
 */
inline bool injective_guess_in_candidates(const std::vector<std::string>& eqs, int N,
                                          const std::vector<int>& cand_global) {
    for (int gi : cand_global) {
        std::unordered_set<uint32_t> seen;
        seen.reserve(static_cast<size_t>(cand_global.size() * 2));
        bool ok = true;
        for (int si : cand_global) {
            uint32_t c =
                compute_feedback_packed(eqs[static_cast<size_t>(gi)], eqs[static_cast<size_t>(si)], N);
            if (!seen.insert(c).second) {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

/** Injective guess exists with index not in cand (pool-wide indices). */
inline bool injective_guess_outside_candidates(const std::vector<std::string>& eqs, int n_pool, int N,
                                               const std::vector<int>& cand_global) {
    std::unordered_set<int> inn;
    inn.reserve(static_cast<size_t>(cand_global.size() * 2));
    for (int x : cand_global)
        inn.insert(x);
    for (int g = 0; g < n_pool; g++) {
        if (inn.count(g))
            continue;
        std::unordered_set<uint32_t> seen;
        seen.reserve(static_cast<size_t>(cand_global.size() * 2));
        bool ok = true;
        for (int si : cand_global) {
            uint32_t c =
                compute_feedback_packed(eqs[static_cast<size_t>(g)], eqs[static_cast<size_t>(si)], N);
            if (!seen.insert(c).second) {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
    }
    return false;
}

struct BellmanResult {
    double ev = 0;
    size_t dp_states = 0;
    double dp_seconds = 0;
    /** Smallest-index optimal first guess in full pool (global index). */
    int optimal_first_global = -1;
    /** False if no legal policy exists under current guess restriction (restricted-only mode). */
    bool ok = true;
};

/**
 * Bellman DP: uniform prior on candidates.
 *   restricted_guesses == false: any pool equation may be guessed (full optimal).
 *   restricted_guesses == true: only equations that are still candidate-secrets may be played —
 *      plus injective shortcuts (including a one-shot injective probe outside the candidate set),
 *      same as at top-level restricted bash with outside injective.
 */
class SubgameOptimizer {
  public:
    SubgameOptimizer(const std::vector<std::string>& eqs, int n_pool, int N,
                     const std::vector<uint32_t>& fb_full, const std::vector<int>& cand_global,
                     bool restricted_guesses = false)
        : eqs_(eqs), n_pool_(n_pool), N_(N), fb_full_(fb_full), cand_global_(cand_global),
          k_(static_cast<int>(cand_global.size())), restricted_guesses_(restricted_guesses) {}

    BellmanResult solve() {
        BellmanResult out{};
        if (k_ < 1)
            return out;
        // Singleton — secret known; one guess to lock in (ev_for_guess needs pc>=2).
        if (k_ == 1) {
            out.ev = 1.0;
            out.optimal_first_global = cand_global_[0];
            out.ok = true;
            return out;
        }
        constexpr double tie_eps = 1e-12;
        memo_.clear();

        PolicyMask full = nerdle::full_policy_mask(k_);
        auto t0 = std::chrono::steady_clock::now();
        out.ev = V_dp(full);
        auto t1 = std::chrono::steady_clock::now();
        out.dp_seconds = std::chrono::duration<double>(t1 - t0).count();
        out.dp_states = memo_.size();

        double best = inf_;
        int best_g = -1;
        if (restricted_guesses_) {
            for (int j = 0; j < k_; j++) {
                int g = cand_global_[static_cast<size_t>(j)];
                double ev = 0.0;
                if (!ev_for_guess(full, g, ev))
                    continue;
                if (ev < best - tie_eps || (std::fabs(ev - best) <= tie_eps && (best_g < 0 || g < best_g))) {
                    best = ev;
                    best_g = g;
                }
            }
        } else {
            for (int g = 0; g < n_pool_; g++) {
                double ev = 0.0;
                if (!ev_for_guess(full, g, ev))
                    continue;
                if (ev < best - tie_eps ||
                    (std::fabs(ev - best) <= tie_eps && (best_g < 0 || g < best_g))) {
                    best = ev;
                    best_g = g;
                }
            }
        }
        if (best_g < 0) {
            out.ok = false;
            out.ev = std::numeric_limits<double>::quiet_NaN();
            out.optimal_first_global = -1;
            return out;
        }
        out.optimal_first_global = best_g;
        return out;
    }

    /** Smallest-index optimal guess for this mask (after solve()). */
    int optimal_guess(const nerdle::PolicyMask& mask) {
        int pc = nerdle::popcount(mask);
        if (pc == 1) {
            int only = -1;
            nerdle::for_each_bit(mask, [&](int j) { only = cand_global_[static_cast<size_t>(j)]; });
            return only;
        }
        constexpr double tie_eps = 1e-12;
        double best = inf_;
        int best_g = -1;
        if (restricted_guesses_) {
            nerdle::for_each_bit(mask, [&](int j) {
                int g = cand_global_[static_cast<size_t>(j)];
                double ev = 0.0;
                if (!ev_for_guess(mask, g, ev))
                    return;
                if (ev < best - tie_eps || (std::fabs(ev - best) <= tie_eps && (best_g < 0 || g < best_g))) {
                    best = ev;
                    best_g = g;
                }
            });
        } else {
            for (int g = 0; g < n_pool_; g++) {
                double ev = 0.0;
                if (!ev_for_guess(mask, g, ev))
                    continue;
                if (ev < best - tie_eps ||
                    (std::fabs(ev - best) <= tie_eps && (best_g < 0 || g < best_g))) {
                    best = ev;
                    best_g = g;
                }
            }
        }
        if (best_g < 0) {
            std::cerr << "optimal_guess: no valid action (bug).\n";
            std::exit(3);
        }
        return best_g;
    }

  private:
    using PolicyMask = nerdle::PolicyMask;

    const std::vector<std::string>& eqs_;
    int n_pool_;
    int N_;
    const std::vector<uint32_t>& fb_full_;
    const std::vector<int>& cand_global_;
    int k_;
    bool restricted_guesses_;
    std::unordered_map<PolicyMask, double, nerdle::PolicyMaskHash> memo_;
    const double inf_ = std::numeric_limits<double>::infinity();

    /** True iff guess g gives pairwise distinct feedback vs every secret in mask. */
    bool feedback_injective_for_guess(int g_pool, nerdle::PolicyMask mask) const {
        std::unordered_set<uint32_t> seen;
        const int need = nerdle::popcount(mask);
        seen.reserve(static_cast<size_t>(need * 2));
        nerdle::for_each_bit(mask, [&](int j) {
            int s_gl = cand_global_[static_cast<size_t>(j)];
            uint32_t code = fb_full_[static_cast<size_t>(g_pool) * static_cast<size_t>(n_pool_) +
                                       static_cast<size_t>(s_gl)];
            seen.insert(code);
        });
        return static_cast<int>(seen.size()) == need;
    }

    /**
     * Optimal value when an injective probe exists (uniform prior on mask):
     * - Guess in remaining secrets: E = (2k-1)/k (win in one if secret hits the played equation).
     * - Guess outside: never wins on move 1; identify in one feedback then win → E = 2.
     * Prefer inside when both exist (same or strictly better EV).
     */
    double try_injective_shortcuts(nerdle::PolicyMask mask, int pc) {
        std::vector<int> in_pool;
        in_pool.reserve(static_cast<size_t>(pc));
        nerdle::for_each_bit(mask, [&](int j) { in_pool.push_back(cand_global_[static_cast<size_t>(j)]); });
        std::sort(in_pool.begin(), in_pool.end());
        in_pool.erase(std::unique(in_pool.begin(), in_pool.end()), in_pool.end());

        for (int g_pool : in_pool) {
            if (feedback_injective_for_guess(g_pool, mask))
                return (2.0 * static_cast<double>(pc) - 1.0) / static_cast<double>(pc);
        }

        for (int g_pool = 0; g_pool < n_pool_; g_pool++) {
            if (std::binary_search(in_pool.begin(), in_pool.end(), g_pool))
                continue;
            if (feedback_injective_for_guess(g_pool, mask))
                return 2.0;
        }
        return -1.0;
    }

    /** Value at mask (after solve-time V_dp(full), or on-demand for ev_for_guess submasks). */
    double V_at(nerdle::PolicyMask m) {
        int pc = nerdle::popcount(m);
        if (pc == 0)
            return 0.0;
        if (pc == 1)
            return 1.0;
        auto it = memo_.find(m);
        if (it != memo_.end())
            return it->second;
        // Injective shortcut at an ancestor can skip filling strict submasks; compute lazily.
        return V_dp(m);
    }

    bool ev_for_guess(nerdle::PolicyMask mask, int g, double& out_ev) {
        int pc = nerdle::popcount(mask);
        if (pc < 2)
            return false;
        std::unordered_map<uint32_t, PolicyMask> cells;
        nerdle::for_each_bit(mask, [&](int j) {
            int s_gl = cand_global_[static_cast<size_t>(j)];
            uint32_t code = fb_full_[static_cast<size_t>(g) * static_cast<size_t>(n_pool_) +
                                     static_cast<size_t>(s_gl)];
            PolicyMask& cm = cells[code];
            cm = nerdle::set_bit(cm, j);
        });
        bool gin = false;
        nerdle::for_each_bit(mask, [&](int j) {
            if (cand_global_[static_cast<size_t>(j)] == g)
                gin = true;
        });
        if (!gin && cells.size() <= 1)
            return false;
        double sum = 0.0;
        bool bad = false;
        nerdle::for_each_bit(mask, [&](int j) {
            int s_gl = cand_global_[static_cast<size_t>(j)];
            if (s_gl == g) {
                sum += 1.0;
            } else {
                uint32_t code = fb_full_[static_cast<size_t>(g) * static_cast<size_t>(n_pool_) +
                                         static_cast<size_t>(s_gl)];
                PolicyMask sub = cells[code];
                if (nerdle::eq_mask(sub, mask)) {
                    bad = true;
                    return;
                }
                double vsub = V_at(sub);
                if (std::isinf(vsub)) {
                    bad = true;
                    return;
                }
                sum += 1.0 + vsub;
            }
        });
        if (bad)
            return false;
        out_ev = sum / static_cast<double>(pc);
        return true;
    }

    double V_dp(nerdle::PolicyMask mask) {
        int pc = nerdle::popcount(mask);
        if (pc == 0)
            return 0.0;
        if (pc == 1)
            return 1.0;
        auto it = memo_.find(mask);
        if (it != memo_.end())
            return it->second;

        double shortcut = try_injective_shortcuts(mask, pc);
        if (shortcut >= 0.0) {
            memo_[mask] = shortcut;
            return shortcut;
        }

        double best = inf_;
        auto consider_g = [&](int g) {
            std::unordered_map<uint32_t, PolicyMask> cells;
            nerdle::for_each_bit(mask, [&](int j) {
                int s_gl = cand_global_[static_cast<size_t>(j)];
                uint32_t code = fb_full_[static_cast<size_t>(g) * static_cast<size_t>(n_pool_) +
                                       static_cast<size_t>(s_gl)];
                PolicyMask& cm = cells[code];
                cm = nerdle::set_bit(cm, j);
            });

            bool gin = false;
            nerdle::for_each_bit(mask, [&](int j) {
                if (cand_global_[static_cast<size_t>(j)] == g)
                    gin = true;
            });
            if (!gin && cells.size() <= 1)
                return;

            double sum = 0.0;
            bool bad = false;
            nerdle::for_each_bit(mask, [&](int j) {
                int s_gl = cand_global_[static_cast<size_t>(j)];
                if (s_gl == g) {
                    sum += 1.0;
                } else {
                    uint32_t code =
                        fb_full_[static_cast<size_t>(g) * static_cast<size_t>(n_pool_) +
                                static_cast<size_t>(s_gl)];
                    PolicyMask sub = cells[code];
                    if (nerdle::eq_mask(sub, mask)) {
                        bad = true;
                        return;
                    }
                    double vsub = V_dp(sub);
                    if (std::isinf(vsub)) {
                        bad = true;
                        return;
                    }
                    sum += 1.0 + vsub;
                }
            });
            if (bad)
                return;
            double ev = sum / static_cast<double>(pc);
            if (ev < best)
                best = ev;
        };

        if (restricted_guesses_) {
            nerdle::for_each_bit(mask, [&](int j) { consider_g(cand_global_[static_cast<size_t>(j)]); });
        } else {
            for (int g = 0; g < n_pool_; g++)
                consider_g(g);
        }

        if (std::isinf(best)) {
            if (!restricted_guesses_) {
                std::cerr << "bellman: no informative guess (bug).\n";
                std::exit(2);
            }
            memo_[mask] = inf_;
            return inf_;
        }
        memo_[mask] = best;
        return best;
    }
};

inline BellmanResult bellman_uniform_full_pool(const std::vector<std::string>& eqs, int n_pool, int N,
                                               const std::vector<uint32_t>& fb_full,
                                               const std::vector<int>& cand_global) {
    SubgameOptimizer opt(eqs, n_pool, N, fb_full, cand_global);
    return opt.solve();
}

inline void build_fb_matrix(const std::vector<std::string>& eqs, int n_pool, int N,
                            std::vector<uint32_t>* out_fb) {
    out_fb->resize(static_cast<size_t>(n_pool) * static_cast<size_t>(n_pool));
    for (int g = 0; g < n_pool; g++) {
        for (int s = 0; s < n_pool; s++) {
            (*out_fb)[static_cast<size_t>(g) * static_cast<size_t>(n_pool) + static_cast<size_t>(s)] =
                compute_feedback_packed(eqs[static_cast<size_t>(g)], eqs[static_cast<size_t>(s)], N);
        }
    }
}

} // namespace subgame
} // namespace nerdle

#endif
