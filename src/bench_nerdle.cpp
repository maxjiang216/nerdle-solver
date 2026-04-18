/**
 * Fast C++ Nerdle benchmark - simulates the solver against all equations.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o bench_nerdle bench_nerdle.cpp
 * Run:     ./bench_nerdle equations_5.txt
 *          ./bench_nerdle equations_8.txt [--selector v1|v2] [--strategy bellman|partition|entropy] [--sample N]
 *
 * Uses OpenMP for parallel execution. Without -fopenmp, runs single-threaded.
 */

#include "bench_solve.hpp"
#include "micro_policy.hpp"
#include "nerdle_core.hpp"
#include "optimal_policy_build.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Selector = nerdle_bench::Selector;
using PlayStrategy = nerdle_bench::PlayStrategy;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_nerdle")
                  << " <equations.txt> [--selector v1|v2] [--strategy ...] [--sample N]\n";
        std::cerr << "  N=5 (Micro): bellman | partition  (entropy: optional v2 comparison)\n";
        std::cerr << "  N=6 (Mini):  optimal  (entropy: optional v2 comparison)\n";
        std::cerr << "  other lengths: partition | entropy\n";
        return 1;
    }
    std::string path;
    size_t sample_size = 0;
    Selector sel = Selector::V2;
    bool strategy_set = false;
    PlayStrategy strat = PlayStrategy::Entropy;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--sample" && i + 1 < argc) {
            sample_size = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--selector" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "v1" || v == "V1")
                sel = Selector::V1;
            else
                sel = Selector::V2;
        } else if (arg == "--strategy" && i + 1 < argc) {
            std::string s = argv[++i];
            strategy_set = true;
            if (s == "bellman")
                strat = PlayStrategy::Bellman;
            else if (s == "optimal")
                strat = PlayStrategy::Optimal;
            else if (s == "partition")
                strat = PlayStrategy::Partition;
            else if (s == "entropy" || s == "v2")
                strat = PlayStrategy::Entropy;
            else {
                std::cerr << "Invalid --strategy (see usage)\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            path = arg;
        }
    }
    if (path.empty()) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_nerdle")
                  << " <equations.txt> [--selector v1|v2] [--strategy ...] [--sample N]\n";
        return 1;
    }

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    if (equations.empty()) {
        std::cerr << "No equations loaded.\n";
        return 1;
    }

    bool is_maxi = false;
    for (unsigned char c : equations[0]) {
        if (c == 0xC2) {
            is_maxi = true;
            break;
        }
    }
    if (is_maxi) {
        for (auto& eq : equations) eq = nerdle_bench::normalize_maxi(eq);
    }

    int N = static_cast<int>(equations[0].size());
    if (N < 5 || N > 10 || (N > 8 && N != 10)) {
        std::cerr << "Length must be 5-8 or 10 (Maxi).\n";
        return 1;
    }
    for (const auto& eq : equations) {
        if (static_cast<int>(eq.size()) != N) {
            std::cerr << "Inconsistent equation length.\n";
            return 1;
        }
    }

    int max_tries = (N == 10) ? nerdle_bench::MAXI_TRIES : 6;
    const auto& fg_map = nerdle_bench::first_guess_map();
    std::string first_guess = fg_map.count(N) ? fg_map.at(N) : equations[0];

    if (N == 6 && (strat == PlayStrategy::Bellman || strat == PlayStrategy::Partition)) {
        std::cerr << "Mini (6-tile): use --strategy optimal (or entropy for v2 comparison).\n";
        return 1;
    }
    if (N == 5 && strat == PlayStrategy::Optimal) {
        std::cerr << "Micro (5-tile): use --strategy bellman or partition "
                     "(or entropy for v2 comparison).\n";
        return 1;
    }

    std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash> micro_policy;
    bool micro_policy_ok = false;
    if ((N == 5 || N == 6) && !(N == 5 && strat == PlayStrategy::Partition)) {
        const std::string pol_path =
            (N == 5) ? "data/optimal_policy_5.bin" : "data/optimal_policy_6.bin";
        const int neq = static_cast<int>(equations.size());
        micro_policy_ok = nerdle::load_micro_policy(pol_path, neq, micro_policy);
        if (!micro_policy_ok)
            micro_policy_ok = nerdle::try_build_optimal_policy_bin(N, std::cerr) &&
                              nerdle::load_micro_policy(pol_path, neq, micro_policy);
    }

    if (!strategy_set) {
        if (N == 5 && micro_policy_ok)
            strat = PlayStrategy::Bellman;
        else if (N == 6 && micro_policy_ok)
            strat = PlayStrategy::Optimal;
        else
            strat = PlayStrategy::Entropy;
    } else if (strat == PlayStrategy::Bellman && N != 5) {
        std::cerr << "bellman applies to Micro (5-tile) only; using entropy.\n";
        strat = PlayStrategy::Entropy;
    } else if (strat == PlayStrategy::Optimal && N != 6) {
        std::cerr << "optimal applies to Mini (6-tile) only; using entropy.\n";
        strat = PlayStrategy::Entropy;
    } else if (strat == PlayStrategy::Bellman && N == 5 && !micro_policy_ok) {
        std::cerr << "Bellman policy missing (data/optimal_policy_5.bin); using entropy.\n";
        strat = PlayStrategy::Entropy;
    } else if (strat == PlayStrategy::Optimal && N == 6 && !micro_policy_ok) {
        std::cerr << "Optimal policy missing (data/optimal_policy_6.bin); using entropy.\n";
        strat = PlayStrategy::Entropy;
    }

    if (((strat == PlayStrategy::Bellman && N == 5) || (strat == PlayStrategy::Optimal && N == 6)) &&
        micro_policy_ok) {
        std::vector<size_t> all_idx(equations.size());
        for (size_t i = 0; i < equations.size(); i++) all_idx[i] = i;
        std::string pg = nerdle::guess_from_micro_policy(micro_policy, equations, all_idx);
        if (!pg.empty())
            first_guess = pg;
    }

    std::vector<size_t> indices;
    if (sample_size > 0 && sample_size < equations.size()) {
        indices.resize(equations.size());
        for (size_t i = 0; i < equations.size(); i++) indices[i] = i;
        std::mt19937 rng(42);
        std::shuffle(indices.begin(), indices.end(), rng);
        indices.resize(sample_size);
    } else {
        indices.resize(equations.size());
        for (size_t i = 0; i < equations.size(); i++) indices[i] = i;
    }

    size_t n = indices.size();
    std::cout << "Benchmarking " << n << " equations";
    if (sample_size > 0 && sample_size < equations.size())
        std::cout << " (sampled from " << equations.size() << ")";
    std::cout << " (" << N << "-tile, " << max_tries << " tries";
    if (strat == PlayStrategy::Partition)
        std::cout << ", strategy partition (max classes, then P(win), min E[guesses] recursively)";
    else if (strat == PlayStrategy::Bellman && N == 5)
        std::cout << ", strategy Bellman (precomputed policy)";
    else if (strat == PlayStrategy::Optimal && N == 6)
        std::cout << ", strategy optimal (unique min E[guesses], precomputed)";
    else {
        std::cout << ", strategy entropy, selector " << (sel == Selector::V1 ? "v1" : "v2");
        if (((N == 5 && strat == PlayStrategy::Bellman) || (N == 6 && strat == PlayStrategy::Optimal)) &&
            !micro_policy_ok)
            std::cout << ", optimal/Bellman policy missing";
    }
    std::cout << ")...\n";

    std::vector<int> results(n, 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
    for (size_t i = 0; i < n; i++) {
        uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)indices[i] * 1315423911ULL;
        results[i] = nerdle_bench::solve_one(equations[indices[i]], equations, first_guess, N,
                                             max_tries, sel, strat, seed,
                                             micro_policy_ok ? &micro_policy : nullptr);
    }

    // Aggregate stats
    double sum = 0;
    int max_g = 0;
    std::unordered_map<int, int> dist;
    std::vector<std::string> failures;
    std::vector<std::string> hardest;

    for (size_t i = 0; i < n; i++) {
        int g = results[i];
        sum += g;
        max_g = std::max(max_g, g);
        dist[g]++;
        if (g == max_tries + 1) failures.push_back(equations[indices[i]]);
    }
    for (size_t i = 0; i < n; i++) {
        if (results[i] == max_g) hardest.push_back(equations[indices[i]]);
    }

    int fail_val = max_tries + 1;
    std::cout << "\nResults over " << n << " equations:\n";
    std::cout << "  Mean guesses : " << (sum / n) << "\n";
    std::cout << "  Max guesses  : " << max_g << "\n";
    std::cout << "  Distribution: ";
    for (int k = 1; k <= fail_val; k++) {
        if (dist.count(k)) std::cout << k << ":" << dist[k] << " ";
    }
    std::cout << "\n";
    std::cout << "  Failures    : " << failures.size();
    if (!failures.empty() && failures.size() <= 5) {
        for (const auto& s : failures) std::cout << " (" << s << ")";
    } else if (!failures.empty()) {
        std::cout << " (" << failures[0] << " ...)";
    }
    std::cout << "\n";
    if (!hardest.empty() && hardest.size() <= 50) {
        std::cout << "  Hardest (" << max_g << " guesses): ";
        for (size_t i = 0; i < hardest.size(); i++) {
            if (i) std::cout << ", ";
            std::cout << hardest[i];
        }
        std::cout << "\n";
    }

    return 0;
}
