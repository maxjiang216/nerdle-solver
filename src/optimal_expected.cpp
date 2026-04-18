/**
 * Exact minimum expected number of guesses (uniform prior over equations).
 *
 * Model: each guess may be any equation in the pool; feedback matches nerdle_core
 * (G/P/B, Wordle-style multiset). A guess counts toward the total until the secret
 * is played and matches (all green). Optimal play minimizes E[guesses] via Bellman DP
 * over candidate-set states (bitmask over equation indices).
 *
 * Feasible when the memoized state count stays modest (e.g. Micro ~127 or Mini ~206: on the
 * order of 10^4–10^5 states). For very large pools this may be slow or memory-heavy.
 *
 * Horizon: minimizes E[guesses until the secret is guessed correctly], with no turn limit
 * (infinite guesses allowed). This is the exact Bellman optimum for that objective; it does
 * not model a hard cap such as the website's 6 tries (which would change the MDP).
 */

#include "micro_policy.hpp"
#include "nerdle_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

using nerdle::PolicyMask;
using nerdle::PolicyMaskHash;
using nerdle::eq_mask;
using nerdle::for_each_bit;
using nerdle::full_policy_mask;
using nerdle::popcount;
using nerdle::set_bit;

int main(int argc, char** argv) {
    std::string path = "data/equations_5.txt";
    std::string write_policy_path;
    bool list_all_first = false;
    bool do_simulate = true;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--list-first")
            list_all_first = true;
        else if (a == "--no-simulate")
            do_simulate = false;
        else if (a == "--quiet")
            quiet = true;
        else if (a == "--write-policy" && i + 1 < argc)
            write_policy_path = argv[++i];
        else if (a[0] != '-')
            path = a;
    }

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
    if (n < 1) {
        std::cerr << "No equations in file.\n";
        return 1;
    }
    if (n > 256) {
        std::cerr << "At most 256 equations supported (bitmask).\n";
        return 1;
    }
    const int N = static_cast<int>(eqs[0].size());
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Inconsistent equation lengths.\n";
            return 1;
        }
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

    std::unordered_map<PolicyMask, double, PolicyMaskHash> memo;

    const double inf = std::numeric_limits<double>::infinity();
    constexpr double tie_eps = 1e-12;

    auto V = [&](auto&& self, PolicyMask mask) -> double {
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
            std::unordered_map<uint32_t, PolicyMask> cells;
            for_each_bit(mask, [&](int i) {
                uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
                PolicyMask& cm = cells[code];
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
                    PolicyMask sub = cells[code];
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
            std::cerr << "No informative guess from a nonempty state (bug).\n";
            std::exit(2);
        }
        memo[mask] = best;
        return best;
    };

    auto V_at = [&](PolicyMask m) -> double {
        int k = popcount(m);
        if (k == 0)
            return 0.0;
        if (k == 1)
            return 1.0;
        return memo.at(m);
    };

    /** Expected value for mask if we play guess g (uses memoized V on proper subsets). */
    auto ev_for_guess = [&](PolicyMask mask, int g, double& out_ev) -> bool {
        int k = popcount(mask);
        if (k < 2)
            return false;
        const std::string& Gstr = eqs[static_cast<size_t>(g)];
        std::unordered_map<uint32_t, PolicyMask> cells;
        for_each_bit(mask, [&](int i) {
            uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(i)];
            PolicyMask& cm = cells[code];
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
                PolicyMask sub = cells[code];
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

    /** Deterministic Bellman-optimal action: minimize E; tie-break smallest index g. */
    auto optimal_guess_index = [&](PolicyMask mask) -> int {
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
            if (ev < best - tie_eps || (std::fabs(ev - best) <= tie_eps && (best_g < 0 || g < best_g))) {
                best = ev;
                best_g = g;
            }
        }
        if (best_g < 0) {
            std::cerr << "optimal_guess_index: no valid guess (bug).\n";
            std::exit(3);
        }
        return best_g;
    };

    PolicyMask full = full_policy_mask(n);
    auto t0 = std::chrono::steady_clock::now();
    double optimal = V(V, full);
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    if (!write_policy_path.empty()) {
        std::vector<std::pair<PolicyMask, uint8_t>> entries;
        entries.reserve(memo.size());
        for (const auto& kv : memo) {
            if (popcount(kv.first) < 2)
                continue;
            int g = optimal_guess_index(kv.first);
            entries.push_back({kv.first, static_cast<uint8_t>(g)});
        }
        std::ofstream wf(write_policy_path, std::ios::binary);
        if (!wf) {
            std::cerr << "Cannot write " << write_policy_path << "\n";
            return 1;
        }
        const uint32_t magic = nerdle::kMicroPolicyMagic;
        const uint32_t ver =
            (n <= 128) ? nerdle::kPolicyFormatVer1 : nerdle::kPolicyFormatVer2;
        const uint32_t nent = static_cast<uint32_t>(entries.size());
        const uint8_t neq = static_cast<uint8_t>(n);
        wf.write(reinterpret_cast<const char*>(&magic), 4);
        wf.write(reinterpret_cast<const char*>(&ver), 4);
        wf.write(reinterpret_cast<const char*>(&neq), 1);
        char pad[3] = {};
        wf.write(pad, 3);
        wf.write(reinterpret_cast<const char*>(&nent), 4);
        for (const auto& e : entries) {
            if (ver == nerdle::kPolicyFormatVer1) {
                wf.write(reinterpret_cast<const char*>(&e.first.w[0]), 8);
                wf.write(reinterpret_cast<const char*>(&e.first.w[1]), 8);
            } else {
                wf.write(reinterpret_cast<const char*>(e.first.w), 32);
            }
            wf.write(reinterpret_cast<const char*>(&e.second), 1);
        }
        wf.close();
        if (!quiet) {
            std::cout << "Wrote " << nent << " Micro policy entries to " << write_policy_path << "\n";
        }
        if (quiet)
            return 0;
    }

    std::cout << std::fixed << std::setprecision(10);
    if (!quiet) {
        std::cout << "File: " << path << "  (" << n << " equations, " << N << " tiles)\n";
        std::cout << "Optimal E[guesses] (uniform prior): " << optimal << "\n";
        std::cout << "DP states: " << memo.size() << "\n";
        std::cout << "Time: " << sec << " s\n\n";
    }

    auto E_first = [&](int g) -> double {
        std::unordered_map<uint32_t, PolicyMask> cells;
        for (int s = 0; s < n; s++) {
            uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(s)];
            PolicyMask& cm = cells[code];
            cm = set_bit(cm, s);
        }
        double sum = 0.0;
        for (int s = 0; s < n; s++) {
            if (eqs[static_cast<size_t>(s)] == eqs[static_cast<size_t>(g)]) {
                sum += 1.0;
            } else {
                uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(s)];
                sum += 1.0 + V(V, cells[code]);
            }
        }
        return sum / static_cast<double>(n);
    };

    std::vector<double> e_first(static_cast<size_t>(n));
    for (int g = 0; g < n; g++)
        e_first[static_cast<size_t>(g)] = E_first(g);
    double min_e = e_first[0];
    for (int g = 1; g < n; g++)
        min_e = std::min(min_e, e_first[static_cast<size_t>(g)]);

    std::cout << "All Bellman-optimal first guesses (E[guesses] within 1e-12 of minimum " << min_e
              << "):\n";
    int tie_count = 0;
    for (int g = 0; g < n; g++) {
        double e = e_first[static_cast<size_t>(g)];
        if (std::fabs(e - min_e) <= tie_eps) {
            std::cout << "  " << eqs[static_cast<size_t>(g)] << "\n";
            tie_count++;
        }
    }
    std::cout << "  (" << tie_count << " total)\n\n";

    int best_g = optimal_guess_index(full);
    double best_e = e_first[static_cast<size_t>(best_g)];

    std::cout << "Policy tie-break (smallest index among the above): " << eqs[static_cast<size_t>(best_g)]
              << "  (E = " << best_e << ")\n";

    if (list_all_first) {
        std::cout << "\nAll first guesses (sorted by E[guesses]):\n";
        std::vector<std::pair<double, int>> rows;
        for (int g = 0; g < n; g++)
            rows.push_back({E_first(g), g});
        std::sort(rows.begin(), rows.end());
        for (const auto& pr : rows) {
            std::cout << "  " << std::setw(12) << pr.first << "  " << eqs[static_cast<size_t>(pr.second)]
                      << "\n";
        }
    }

    if (do_simulate) {
        std::cout << "\nSimulation: Bellman-optimal policy with smallest-index tie-break.\n";
        std::cout << "Guesses until correct (each of " << n << " targets):\n";

        std::vector<int> hist(16, 0);
        int worst = 0;
        std::vector<int> steps_for(static_cast<size_t>(n));

        for (int t = 0; t < n; t++) {
            PolicyMask mask = full;
            int steps = 0;
            while (true) {
                int g = optimal_guess_index(mask);
                steps++;
                if (eqs[static_cast<size_t>(g)] == eqs[static_cast<size_t>(t)])
                    break;
                uint32_t code = fb[static_cast<size_t>(g)][static_cast<size_t>(t)];
                PolicyMask newmask{};
                for_each_bit(mask, [&](int i) {
                    if (fb[static_cast<size_t>(g)][static_cast<size_t>(i)] == code)
                        newmask = set_bit(newmask, i);
                });
                mask = newmask;
            }
            steps_for[static_cast<size_t>(t)] = steps;
            if (steps >= static_cast<int>(hist.size()))
                hist.resize(static_cast<size_t>(steps + 1));
            hist[static_cast<size_t>(steps)]++;
            if (steps > worst)
                worst = steps;
        }

        for (int k = 1; k <= worst; k++) {
            if (hist[static_cast<size_t>(k)] > 0)
                std::cout << "  " << k << " guess(es): " << hist[static_cast<size_t>(k)] << " target(s)\n";
        }
        std::cout << "Worst case: " << worst << " guess(es)\n";
        if (worst >= 4) {
            std::cout << "Targets at worst case (" << worst << " guesses):\n";
            for (int t = 0; t < n; t++) {
                if (steps_for[static_cast<size_t>(t)] == worst)
                    std::cout << "  " << eqs[static_cast<size_t>(t)] << "\n";
            }
        }

        bool any_over6 = false;
        for (int t = 0; t < n; t++) {
            if (steps_for[static_cast<size_t>(t)] > 6) {
                if (!any_over6) {
                    std::cout << "Targets needing more than 6 guesses:\n";
                    any_over6 = true;
                }
                std::cout << "  " << steps_for[static_cast<size_t>(t)] << "  " << eqs[static_cast<size_t>(t)]
                          << "\n";
            }
        }
        if (!any_over6)
            std::cout << "No target needs more than 6 guesses (for this tie-break).\n";
    }

    return 0;
}
