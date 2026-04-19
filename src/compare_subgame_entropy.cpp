/**
 * After first guess, compare optimal (full-pool Bellman) vs entropy v2 on each heuristic-hard
 * subgame in a size range. Bellman is optimal => EV_b <= EV_entropy always; count strict gaps.
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
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nerdle::all_green_packed;
using nerdle::compute_feedback_packed;
using nerdle::subgame::BellmanResult;
using nerdle::subgame::SubgameOptimizer;
using nerdle::subgame::build_fb_matrix;
using nerdle::subgame::injective_guess_in_candidates;
using nerdle::subgame::injective_guess_outside_candidates;
using nerdle::subgame::try_load_fb_cache;
using nerdle::subgame::write_fb_cache;

/** Mean additional guesses for secret uniform on candidates using best_guess_v2 each turn. */
static double entropy_mean_extra(const std::vector<std::string>& eqs, int N,
                                 const std::vector<int>& cand_global) {
    std::vector<size_t> c0;
    c0.reserve(cand_global.size());
    for (int g : cand_global)
        c0.push_back(static_cast<size_t>(g));
    std::unordered_set<size_t> cset(c0.begin(), c0.end());

    double sum = 0.0;
    for (size_t ti = 0; ti < c0.size(); ti++) {
        size_t secret_idx = c0[ti];
        const std::string& secret = eqs[secret_idx];

        std::vector<size_t> candidates = c0;
        std::unordered_set<size_t> cand_set = cset;
        std::vector<int> hist;

        uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)secret_idx * 1315423911ULL;
        std::mt19937 rng(static_cast<std::mt19937::result_type>(seed & 0xFFFFFFFFu) ^
                         static_cast<std::mt19937::result_type>(seed >> 32));

        int steps = 0;
        while (true) {
            std::string guess =
                nerdle::best_guess_v2(eqs, candidates, cand_set, N, hist, rng);
            steps++;
            uint32_t p = compute_feedback_packed(guess, secret, N);
            if (p == all_green_packed(N))
                break;

            std::vector<size_t> next;
            std::unordered_set<size_t> ns;
            for (size_t idx : candidates) {
                if (compute_feedback_packed(guess, eqs[idx], N) == p)
                    next.push_back(idx);
            }
            for (size_t x : next)
                ns.insert(x);
            candidates = std::move(next);
            cand_set = std::move(ns);
            if (candidates.empty()) {
                std::cerr << "entropy: empty candidates (bug), fb=" << secret_idx << "\n";
                std::exit(4);
            }
        }
        sum += static_cast<double>(steps);
    }
    return sum / static_cast<double>(c0.size());
}

int main(int argc, char** argv) {
    std::string pool_path = "data/equations_7.txt";
    std::string first_guess = "4+27=31";
    std::string fb_cache = "data/fb_7.bin";
    int k_lo = 8;
    int k_hi = 19;

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
        std::cerr << "first-guess length mismatch.\n";
        return 1;
    }

    std::unordered_map<uint32_t, std::vector<int>> by_fb;
    for (int i = 0; i < n_pool; i++) {
        uint32_t c = compute_feedback_packed(first_guess, eqs[static_cast<size_t>(i)], N);
        by_fb[c].push_back(i);
    }

    std::vector<std::pair<uint32_t, std::vector<int>>> bellman_classes;
    for (auto& pr : by_fb) {
        auto& cand = pr.second;
        int k = static_cast<int>(cand.size());
        if (k <= 2)
            continue;
        if (k > 256) {
            std::cerr << "k>256\n";
            return 1;
        }
        if (injective_guess_in_candidates(eqs, N, cand))
            continue;
        if (injective_guess_outside_candidates(eqs, n_pool, N, cand))
            continue;
        if (k < k_lo || k > k_hi)
            continue;
        bellman_classes.push_back({pr.first, std::move(cand)});
    }

    std::sort(bellman_classes.begin(), bellman_classes.end(),
              [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });

    std::vector<uint32_t> fb_full;
    int nc = 0, Nc = 0;
    if (!try_load_fb_cache(fb_cache, &nc, &Nc, &fb_full) || nc != n_pool || Nc != N) {
        std::cerr << "FB cache missing or mismatch; building " << fb_cache << " ...\n";
        fb_full.clear();
        build_fb_matrix(eqs, n_pool, N, &fb_full);
        write_fb_cache(fb_cache, n_pool, N, fb_full);
    }

    constexpr double eps = 1e-9;
    int strict_better = 0;
    int ties = 0;
    int entropy_worse = 0;

    std::cout << std::fixed << std::setprecision(10);
    std::cout << "Subgames (Bellman-needed) with " << k_lo << " <= |C| <= " << k_hi << ": "
              << bellman_classes.size() << " classes\n";
    std::cout << "class_fb\t|C|\tBellman_EV\tEntropy_EV\tdelta\n";

    for (const auto& pr : bellman_classes) {
        uint32_t code = pr.first;
        const std::vector<int>& cand = pr.second;
        int k = static_cast<int>(cand.size());

        SubgameOptimizer opt(eqs, n_pool, N, fb_full, cand);
        BellmanResult br = opt.solve();

        auto t0 = std::chrono::steady_clock::now();
        double ev_e = entropy_mean_extra(eqs, N, cand);
        auto t1 = std::chrono::steady_clock::now();
        (void)(t1 - t0);

        double delta = ev_e - br.ev;
        std::cout << code << "\t" << k << "\t" << br.ev << "\t" << ev_e << "\t" << delta << "\n";

        if (delta > eps) {
            strict_better++; /* Bellman strictly better than entropy */
            entropy_worse++;
        } else if (delta < -eps) {
            std::cerr << "Warning: Bellman EV > entropy (numerical issue?) code=" << code << "\n";
        } else
            ties++;
    }

    std::cout << "\nSummary:\n";
    std::cout << "  Classes where optimal (Bellman) EV < entropy v2 EV: " << strict_better << " / "
              << bellman_classes.size() << "\n";
    std::cout << "  Ties (|delta| <= " << eps << "): " << ties << "\n";
    std::cout << "Bellman is the exact optimum => EV_opt <= EV_entropy; strict inequality means "
                 "v2 is suboptimal on that subgame.\n";
    return 0;
}
