/**
 * For each Bellman-needed subgame in [k_lo, k_hi], compare:
 *   Full pool: SubgameOptimizer (Bellman, all legal guesses from full pool).
 *   Restricted:  Bellman on candidate equations only (same DP as optimal_expected on a k-line file).
 *
 * Full EV is always <= restricted EV (more strategies). Report strict improvements and timings.
 */

#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nerdle::PolicyMask;
using nerdle::PolicyMaskHash;
using nerdle::compute_feedback_packed;
using nerdle::eq_mask;
using nerdle::for_each_bit;
using nerdle::full_policy_mask;
using nerdle::popcount;
using nerdle::set_bit;
using nerdle::subgame::SubgameOptimizer;
using nerdle::subgame::build_fb_matrix;
using nerdle::subgame::injective_guess_in_candidates;
using nerdle::subgame::injective_guess_outside_candidates;
using nerdle::subgame::try_load_fb_cache;

/** Same DP as optimal_expected on a pool of exactly k candidate equations (guesses ∈ {0..k-1}). */
static double restricted_bellman_ev(const std::vector<std::string>& eqs, int N,
                                    std::unordered_map<PolicyMask, double, PolicyMaskHash>* out_memo) {
    const int n = static_cast<int>(eqs.size());
    if (n < 1)
        return 0.0;
    std::vector<std::vector<uint32_t>> fb(static_cast<size_t>(n),
                                          std::vector<uint32_t>(static_cast<size_t>(n)));
    for (int g = 0; g < n; g++) {
        for (int s = 0; s < n; s++) {
            fb[static_cast<size_t>(g)][static_cast<size_t>(s)] =
                compute_feedback_packed(eqs[static_cast<size_t>(g)], eqs[static_cast<size_t>(s)], N);
        }
    }

    std::unordered_map<PolicyMask, double, PolicyMaskHash> memo;
    const double inf = std::numeric_limits<double>::infinity();

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
            std::cerr << "restricted: no informative guess (bug).\n";
            std::exit(2);
        }
        memo[mask] = best;
        return best;
    };

    PolicyMask full = full_policy_mask(n);
    double ev = V(V, full);
    if (out_memo)
        *out_memo = std::move(memo);
    return ev;
}

struct Row {
    uint32_t code = 0;
    int k = 0;
    double ev_full = 0;
    double ev_rest = 0;
    size_t states_full = 0;
    size_t states_rest = 0;
    double sec_full = 0;
    double sec_rest = 0;
};

int main(int argc, char** argv) {
    std::string pool_path = "data/equations_7.txt";
    std::string first_guess = "4+27=31";
    std::string fb_cache = "data/fb_7.bin";
    int k_lo = 8;
    int k_hi = 29;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--first-guess" && i + 1 < argc)
            first_guess = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--k-lo" && i + 1 < argc)
            k_lo = std::atoi(argv[++i]);
        else if (a == "--k-hi" && i + 1 < argc)
            k_hi = std::atoi(argv[++i]);
    }

    std::vector<std::string> eqs;
    std::string line;
    {
        std::ifstream f(pool_path);
        if (!f) {
            std::cerr << "Cannot open " << pool_path << "\n";
            return 1;
        }
        while (std::getline(f, line)) {
            if (!line.empty())
                eqs.push_back(line);
        }
    }

    const int n_pool = static_cast<int>(eqs.size());
    const int N = static_cast<int>(eqs[0].size());
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Pool length mismatch.\n";
            return 1;
        }
    }
    if (static_cast<int>(first_guess.size()) != N) {
        std::cerr << "first-guess length.\n";
        return 1;
    }

    std::vector<uint32_t> fb_full;
    int nc = 0, Nc = 0;
    if (!try_load_fb_cache(fb_cache, &nc, &Nc, &fb_full) || nc != n_pool || Nc != N) {
        std::cerr << "Need valid --fb-cache for full-pool DP.\n";
        return 1;
    }

    std::unordered_map<uint32_t, std::vector<int>> by_fb;
    for (int i = 0; i < n_pool; i++) {
        uint32_t c = compute_feedback_packed(first_guess, eqs[static_cast<size_t>(i)], N);
        by_fb[c].push_back(i);
    }

    std::vector<std::pair<uint32_t, std::vector<int>>> classes;
    for (auto& pr : by_fb) {
        auto& cand = pr.second;
        int k = static_cast<int>(cand.size());
        if (k <= 2 || k > 256)
            continue;
        if (injective_guess_in_candidates(eqs, N, cand))
            continue;
        if (injective_guess_outside_candidates(eqs, n_pool, N, cand))
            continue;
        if (k < k_lo || k > k_hi)
            continue;
        classes.push_back({pr.first, std::move(cand)});
    }

    std::sort(classes.begin(), classes.end(),
              [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });

    constexpr double eps = 1e-9;
    std::vector<Row> rows;
    rows.reserve(classes.size());

    double sum_full = 0, sum_rest = 0;
    int strict_full_better = 0;
    int ties = 0;

    for (const auto& pr : classes) {
        uint32_t code = pr.first;
        const std::vector<int>& cg = pr.second;

        std::vector<std::string> sub;
        sub.reserve(cg.size());
        for (int gi : cg)
            sub.push_back(eqs[static_cast<size_t>(gi)]);

        Row r;
        r.code = code;
        r.k = static_cast<int>(cg.size());

        auto t0 = std::chrono::steady_clock::now();
        SubgameOptimizer opt(eqs, n_pool, N, fb_full, cg);
        auto br = opt.solve();
        auto t1 = std::chrono::steady_clock::now();
        r.ev_full = br.ev;
        r.states_full = br.dp_states;
        r.sec_full = std::chrono::duration<double>(t1 - t0).count();

        std::unordered_map<PolicyMask, double, PolicyMaskHash> rest_memo;
        auto t2 = std::chrono::steady_clock::now();
        r.ev_rest = restricted_bellman_ev(sub, N, &rest_memo);
        auto t3 = std::chrono::steady_clock::now();
        r.sec_rest = std::chrono::duration<double>(t3 - t2).count();
        r.states_rest = rest_memo.size();

        sum_full += r.sec_full;
        sum_rest += r.sec_rest;

        double delta = r.ev_rest - r.ev_full;
        if (delta > eps) {
            strict_full_better++;
        } else if (delta < -eps) {
            std::cerr << "Warning: full EV > restricted (bug?) code=" << code << "\n";
        } else
            ties++;

        rows.push_back(std::move(r));
    }

    std::cout << std::fixed << std::setprecision(10);
    std::cout << "Bellman-needed subgames, |C| in [" << k_lo << "," << k_hi << "]: " << rows.size()
              << " classes\n\n";
    std::cout << "Expected: full-pool EV <= restricted EV (more legal guesses).\n";
    std::cout << "Classes where full is strictly better (lower) EV: " << strict_full_better << " / "
              << rows.size() << "\n";
    std::cout << "Ties (|Δ| <= " << eps << "): " << ties << " / " << rows.size() << "\n\n";

    std::cout << "Timing (sum over all subgames):\n";
    std::cout << "  Full-pool Bellman (cached n×n fb + shortcuts): " << sum_full << " s\n";
    std::cout << "  Restricted Bellman (k×k fb per class, no shortcuts): " << sum_rest << " s\n";
    if (sum_full > 1e-12)
        std::cout << "  Restricted is " << (sum_full / sum_rest) << "× faster (total wall sums).\n";

    std::cout << "\ncode\tk\tEV_full\tEV_rest\tdelta\tt_full\tt_rest\n";
    for (const auto& r : rows) {
        std::cout << r.code << "\t" << r.k << "\t" << r.ev_full << "\t" << r.ev_rest << "\t"
                  << (r.ev_rest - r.ev_full) << "\t" << r.sec_full << "\t" << r.sec_rest << "\n";
    }

    return 0;
}
