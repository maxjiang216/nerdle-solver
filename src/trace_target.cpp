/**
 * Compare Bellman-optimal play vs entropy-first + best_guess_v2 for a fixed target equation.
 *
 * Usage: ./trace_target data/equations_5.txt 9*1=9
 */

#include "equation_canonical.hpp"
#include "nerdle_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Mask128 {
    uint64_t lo = 0;
    uint64_t hi = 0;
};

inline bool operator==(Mask128 a, Mask128 b) {
    return a.lo == b.lo && a.hi == b.hi;
}

struct MaskHash {
    size_t operator()(Mask128 m) const noexcept {
        return static_cast<size_t>(m.lo ^ (m.hi * 0x9e3779b97f4a7c15ULL));
    }
};

inline int popcount(Mask128 m) {
    return __builtin_popcountll(m.lo) + __builtin_popcountll(m.hi);
}

inline Mask128 set_bit(Mask128 m, int i) {
    if (i < 64)
        m.lo |= (1ULL << i);
    else
        m.hi |= (1ULL << (i - 64));
    return m;
}

inline bool eq_mask(Mask128 a, Mask128 b) {
    return a.lo == b.lo && a.hi == b.hi;
}

Mask128 full_mask(int n) {
    Mask128 m{};
    for (int i = 0; i < n; i++)
        m = set_bit(m, i);
    return m;
}

template <typename F>
void for_each_bit(Mask128 m, F&& fn) {
    uint64_t x = m.lo;
    while (x) {
        int i = __builtin_ctzll(x);
        fn(i);
        x &= x - 1;
    }
    x = m.hi;
    while (x) {
        int i = __builtin_ctzll(x);
        fn(i + 64);
        x &= x - 1;
    }
}

void print_candidates(Mask128 mask, const std::vector<std::string>& eqs) {
    int k = popcount(mask);
    std::cout << "  " << k << " candidate(s):\n";
    for_each_bit(mask, [&](int i) {
        std::cout << "    " << eqs[static_cast<size_t>(i)] << "\n";
    });
}

void print_candidate_indices(const std::vector<size_t>& idx, const std::vector<std::string>& eqs) {
    std::cout << "  " << idx.size() << " candidate(s):\n";
    std::vector<std::string> lines;
    lines.reserve(idx.size());
    for (size_t i : idx)
        lines.push_back(eqs[i]);
    std::sort(lines.begin(), lines.end());
    for (const auto& s : lines)
        std::cout << "    " << s << "\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << (argc ? argv[0] : "trace_target")
                  << " <equations.txt> <target_equation>\n";
        return 1;
    }
    std::string path = argv[1];
    std::string target = argv[2];

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::vector<std::string> eqs;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            eqs.push_back(line);
    }
    f.close();

    const int n = static_cast<int>(eqs.size());
    const int N = static_cast<int>(eqs[0].size());
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Inconsistent lengths.\n";
            return 1;
        }
    }

    int target_idx = -1;
    for (int i = 0; i < n; i++) {
        if (eqs[static_cast<size_t>(i)] == target) {
            target_idx = i;
            break;
        }
    }
    if (target_idx < 0) {
        std::cerr << "Target \"" << target << "\" not in file.\n";
        return 1;
    }

    std::vector<std::vector<uint32_t>> fb(static_cast<size_t>(n),
                                          std::vector<uint32_t>(static_cast<size_t>(n)));
    for (int g = 0; g < n; g++) {
        for (int s = 0; s < n; s++) {
            fb[static_cast<size_t>(g)][static_cast<size_t>(s)] =
                nerdle::compute_feedback_packed(eqs[static_cast<size_t>(g)],
                                                eqs[static_cast<size_t>(s)], N);
        }
    }

    std::unordered_map<Mask128, double, MaskHash> memo;
    const double inf = std::numeric_limits<double>::infinity();
    constexpr double tie_eps = 1e-12;
    const std::vector<nerdle::CanonicalEqKey>& canon_keys = nerdle::canonical_keys_for_pool(eqs);

    auto V = [&](auto&& self, Mask128 mask) -> double {
        int k = popcount(mask);
        if (k == 0)
            return 0.0;
        if (k == 1)
            return 1.0;
        auto it = memo.find(mask);
        if (it != memo.end())
            return it->second;

        double best = inf;
        for (int g = 0; g < n; g++) {
            const std::string& Gstr = eqs[static_cast<size_t>(g)];
            std::unordered_map<uint32_t, Mask128> cells;
            for_each_bit(mask, [&](int i) {
                uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
                Mask128& cm = cells[code];
                cm = set_bit(cm, i);
            });

            bool gin = false;
            for_each_bit(mask, [&](int i) {
                if (eqs[static_cast<size_t>(i)] == Gstr)
                    gin = true;
            });
            if (!gin && cells.size() <= 1)
                continue;

            double sum = 0.0;
            bool bad = false;
            for_each_bit(mask, [&](int i) {
                if (eqs[static_cast<size_t>(i)] == Gstr) {
                    sum += 1.0;
                } else {
                    uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
                    Mask128 sub = cells[code];
                    if (eq_mask(sub, mask)) {
                        bad = true;
                        return;
                    }
                    sum += 1.0 + self(self, sub);
                }
            });
            if (bad)
                continue;
            double ev = sum / static_cast<double>(k);
            if (ev < best)
                best = ev;
        }

        if (std::isinf(best)) {
            std::cerr << "DP error.\n";
            std::exit(2);
        }
        memo[mask] = best;
        return best;
    };

    auto V_at = [&](Mask128 m) -> double {
        int k = popcount(m);
        if (k == 0)
            return 0.0;
        if (k == 1)
            return 1.0;
        return memo.at(m);
    };

    auto ev_for_guess = [&](Mask128 mask, int g, double& out_ev) -> bool {
        int k = popcount(mask);
        if (k < 2)
            return false;
        const std::string& Gstr = eqs[static_cast<size_t>(g)];
        std::unordered_map<uint32_t, Mask128> cells;
        for_each_bit(mask, [&](int i) {
            uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
            Mask128& cm = cells[code];
            cm = set_bit(cm, i);
        });
        bool gin = false;
        for_each_bit(mask, [&](int i) {
            if (eqs[static_cast<size_t>(i)] == Gstr)
                gin = true;
        });
        if (!gin && cells.size() <= 1)
            return false;
        double sum = 0.0;
        bool bad = false;
        for_each_bit(mask, [&](int i) {
            if (eqs[static_cast<size_t>(i)] == Gstr) {
                sum += 1.0;
            } else {
                uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
                Mask128 sub = cells[code];
                if (eq_mask(sub, mask)) {
                    bad = true;
                    return;
                }
                sum += 1.0 + V_at(sub);
            }
        });
        if (bad)
            return false;
        out_ev = sum / static_cast<double>(k);
        return true;
    };

    auto optimal_guess_index = [&](Mask128 mask) -> int {
        int k = popcount(mask);
        if (k == 1) {
            int only = -1;
            for_each_bit(mask, [&](int i) { only = i; });
            return only;
        }
        double best = inf;
        int best_g = -1;
        for (int g = 0; g < n; g++) {
            double ev = 0.0;
            if (!ev_for_guess(mask, g, ev))
                continue;
            if (ev < best - tie_eps ||
                (std::fabs(ev - best) <= tie_eps &&
                 (best_g < 0 || nerdle::canonical_less(static_cast<size_t>(g), static_cast<size_t>(best_g),
                                                       eqs, canon_keys)))) {
                best = ev;
                best_g = g;
            }
        }
        return best_g;
    };

    Mask128 full = full_mask(n);
    (void)V(V, full);

    std::cout << "=== Bellman-optimal play (min E[guesses], canonical-order tie-break)\n";
    std::cout << "Target: " << target << "\n\n";

    Mask128 mask = full;
    int step = 0;
    while (true) {
        step++;
        std::cout << "--- Step " << step << " (before guess) ---\n";
        print_candidates(mask, eqs);

        int g = optimal_guess_index(mask);
        uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(target_idx)];
        std::string fb_str = nerdle::feedback_packed_to_string(code, N);
        std::cout << "  Play: " << eqs[static_cast<size_t>(g)] << "  ->  feedback vs secret: " << fb_str
                  << "\n";

        if (eqs[static_cast<size_t>(g)] == target) {
            std::cout << "  (Correct.)\n\n";
            std::cout << "Total guesses (Bellman): " << step << "\n\n";
            break;
        }

        Mask128 newmask{};
        for_each_bit(mask, [&](int i) {
            if (fb[static_cast<size_t>(g)][static_cast<size_t>(i)] == code)
                newmask = set_bit(newmask, i);
        });
        mask = newmask;
    }

    /* Entropy-first (same as ./solve) then best_guess_v2 */
    std::cout << "=== Entropy-based strategy: max 1-ply entropy first (./solve), then best_guess_v2\n";
    std::vector<size_t> all_idx(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
        all_idx[static_cast<size_t>(i)] = static_cast<size_t>(i);

    double best_h = -1.0;
    std::string entropy_first;
    std::vector<int> hist;
    for (int gi = 0; gi < n; gi++) {
        double H, sum_sq;
        nerdle::entropy_and_partitions(eqs[static_cast<size_t>(gi)].c_str(), eqs, all_idx, N, hist, H,
                                       sum_sq);
        (void)sum_sq;
        if (H > best_h) {
            best_h = H;
            entropy_first = eqs[static_cast<size_t>(gi)];
        }
    }

    std::cout << "First guess (max partition entropy): " << entropy_first << " (" << std::fixed
              << std::setprecision(4) << best_h << " bits)\n";
    std::cout << "RNG for v2: fixed seed 0\n\n";

    std::vector<size_t> cands;
    std::unordered_set<size_t> cset;
    for (int i = 0; i < n; i++) {
        cands.push_back(static_cast<size_t>(i));
        cset.insert(static_cast<size_t>(i));
    }

    std::string guess = entropy_first;
    std::mt19937 rng(0);
    int turns = 0;
    while (true) {
        turns++;
        std::cout << "--- Entropy strategy step " << turns << " (before guess) ---\n";
        print_candidate_indices(cands, eqs);

        uint32_t pcode =
            nerdle::compute_feedback_packed(guess, eqs[static_cast<size_t>(target_idx)], N);
        std::string pfb = nerdle::feedback_packed_to_string(pcode, N);
        std::cout << "  Play: " << guess << "  ->  feedback vs secret: " << pfb << "\n";

        if (guess == target) {
            std::cout << "  (Correct.)\n\n";
            std::cout << "Total guesses (entropy + v2): " << turns << "\n";
            break;
        }

        std::vector<size_t> next;
        std::unordered_set<size_t> nset;
        for (size_t idx : cands) {
            if (nerdle::compute_feedback_packed(guess, eqs[idx], N) == pcode)
                next.push_back(idx);
        }
        for (size_t idx : next)
            nset.insert(idx);
        cands = std::move(next);
        cset = std::move(nset);

        if (cands.empty()) {
            std::cout << "  (No candidates — bug.)\n";
            return 1;
        }
        guess = nerdle::best_guess_v2(eqs, cands, cset, N, hist, rng);
    }

    return 0;
}
