/**
 * Fast C++ Nerdle benchmark - simulates the solver against all equations.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o bench_nerdle bench_nerdle.cpp
 * Run:     ./bench_nerdle equations_5.txt
 *          ./bench_nerdle equations_8.txt [--selector v1|v2] [--sample N]
 *
 * Uses OpenMP for parallel execution. Without -fopenmp, runs single-threaded.
 */

#include "nerdle_core.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int MAXI_TRIES = 6; /* Maxi has 6 tries */

static const unsigned char PLACE_SQ = '\x01';
static const unsigned char PLACE_CB = '\x02';

static std::string normalize_maxi(std::string s) {
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

/* Hardcoded first guesses (must exist in equation set) */
static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "4-1=3"},
    {6, "4*7=28"},
    {7, "6+18=24"},
    {8, "48-32=16"},
    {10, "76+1-23=54"}, /* Maxi (solve_adaptive optimal) */
};

enum class Selector { V1, V2 };

static std::string pick_guess(const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& candidate_indices,
                              const std::unordered_set<size_t>& candidate_set, int N,
                              std::vector<int>& hist, std::mt19937& rng, Selector sel) {
    if (sel == Selector::V1)
        return nerdle::best_guess_v1(all_eqs, candidate_indices, candidate_set, N, hist);
    return nerdle::best_guess_v2(all_eqs, candidate_indices, candidate_set, N, hist, rng);
}

/* Returns number of guesses (1-6 or 1-7) or 7/8 if failed */
static int solve_one(const std::string& solution, const std::vector<std::string>& all_eqs,
                     const std::string& first_guess, int N, int max_tries, Selector sel,
                     uint64_t rng_seed) {
    std::mt19937 rng(static_cast<std::mt19937::result_type>(rng_seed));
    std::vector<size_t> candidates;
    for (size_t i = 0; i < all_eqs.size(); i++) candidates.push_back(i);
    std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());

    std::string guess = first_guess;
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

        if (candidates.empty()) return max_tries + 1;

        guess = pick_guess(all_eqs, candidates, candidate_set, N, hist, rng, sel);
    }
    return max_tries + 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_nerdle")
                  << " <equations.txt> [--selector v1|v2] [--sample N]\n";
        return 1;
    }
    std::string path;
    size_t sample_size = 0;
    Selector sel = Selector::V2;
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
        } else if (arg[0] != '-') {
            path = arg;
        }
    }
    if (path.empty()) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_nerdle")
                  << " <equations.txt> [--selector v1|v2] [--sample N]\n";
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
        for (auto& eq : equations) eq = normalize_maxi(eq);
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

    int max_tries = (N == 10) ? MAXI_TRIES : 6;
    std::string first_guess = FIRST_GUESS.count(N) ? FIRST_GUESS.at(N) : equations[0];

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
    std::cout << " (" << N << "-tile, " << max_tries << " tries, selector "
              << (sel == Selector::V1 ? "v1" : "v2") << ")...\n";

    std::vector<int> results(n, 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
    for (size_t i = 0; i < n; i++) {
        uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)indices[i] * 1315423911ULL;
        results[i] = solve_one(equations[indices[i]], equations, first_guess, N, max_tries, sel,
                               seed);
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
