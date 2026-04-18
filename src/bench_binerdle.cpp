/**
 * Binerdle benchmark - simulates solver over all equation pairs.
 * For Mini (6): full benchmark. For Normal (8): uses distribution from
 * single-equation solve times (pairs need max(t1,t2) turns).
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o bench_binerdle bench_binerdle.cpp
 * Run:     ./bench_binerdle equations_6.txt   # mini
 *          ./bench_binerdle equations_8.txt   # normal
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int SEARCH_CAP = 600;
constexpr size_t MC_PAIR_THRESHOLD = 50000;
constexpr size_t MC_SAMPLE_SIZE = 8000;
constexpr int MAX_TRIES = 7;

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "3+2=5"}, {6, "4*7=28"}, {7, "6+18=24"},
    {8, "43-27=16"},  /* Binerdle-optimal */
};

std::string compute_feedback(const std::string& guess, const std::string& solution, int N) {
    std::string result(N, 'B');
    int sol_count[256] = {0};
    for (char c : solution) sol_count[static_cast<unsigned char>(c)]++;

    for (int i = 0; i < N; i++) {
        if (guess[i] == solution[i]) {
            result[i] = 'G';
            sol_count[static_cast<unsigned char>(guess[i])]--;
        }
    }
    for (int i = 0; i < N; i++) {
        if (result[i] == 'G') continue;
        char c = guess[i];
        if (sol_count[static_cast<unsigned char>(c)] > 0) {
            result[i] = 'P';
            sol_count[static_cast<unsigned char>(c)]--;
        }
    }
    return result;
}

bool is_consistent(const std::string& candidate, const std::string& guess,
                   const std::string& feedback, int N) {
    return compute_feedback(guess, candidate, N) == feedback;
}

/** Entropy of guess over PAIR space. Monte Carlo when too large. */
double entropy_of_guess_pairs(const std::string& guess,
                              const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                              int N) {
    size_t pair_count = c1.size() * c2.size();
    std::unordered_map<std::string, int> pattern_count;

    if (pair_count <= MC_PAIR_THRESHOLD) {
        for (size_t i : c1) {
            std::string fb1 = compute_feedback(guess, all_eqs[i], N);
            for (size_t j : c2) {
                std::string fb2 = compute_feedback(guess, all_eqs[j], N);
                pattern_count[fb1 + "|" + fb2]++;
            }
        }
    } else {
        size_t step = pair_count / MC_SAMPLE_SIZE;
        if (step < 1) step = 1;
        for (size_t ij = 0; ij < pair_count; ij += step) {
            size_t i = ij % c1.size(), j = ij / c1.size();
            std::string fb1 = compute_feedback(guess, all_eqs[c1[i]], N);
            std::string fb2 = compute_feedback(guess, all_eqs[c2[j]], N);
            pattern_count[fb1 + "|" + fb2]++;
        }
    }

    double total = 0;
    for (const auto& kv : pattern_count) total += kv.second;
    if (total <= 0) return 0.0;
    double h = 0.0;
    for (const auto& kv : pattern_count) {
        double p = kv.second / total;
        h -= p * std::log2(p);
    }
    return h;
}

/** Best guess that maximizes entropy. Prefer c1∪c2 when tied. */
std::string best_guess_pairs(const std::vector<std::string>& all_eqs,
                             const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                             int N) {
    if (c1.size() == 1 && c2.size() == 1) return all_eqs[c1[0]];

    std::unordered_set<size_t> union_set(c1.begin(), c1.end());
    union_set.insert(c2.begin(), c2.end());

    std::vector<size_t> pool_indices;
    size_t n = all_eqs.size();
    if (n <= (size_t)SEARCH_CAP) {
        for (size_t i = 0; i < n; i++) pool_indices.push_back(i);
    } else {
        size_t step = n / SEARCH_CAP;
        for (size_t i = 0; i < n && pool_indices.size() < (size_t)SEARCH_CAP; i += (step < 1 ? 1 : step))
            pool_indices.push_back(i);
    }

    size_t best_idx = pool_indices[0];
    double best_h = -1.0;
    bool best_in_union = union_set.count(best_idx) > 0;
    for (size_t idx : pool_indices) {
        double h = entropy_of_guess_pairs(all_eqs[idx], all_eqs, c1, c2, N);
        bool in_union = union_set.count(idx) > 0;
        if (h > best_h || (h == best_h && in_union && !best_in_union)) {
            best_h = h;
            best_idx = idx;
            best_in_union = in_union;
        }
    }
    return all_eqs[best_idx];
}

/** Solve Binerdle: one guess per turn for BOTH equations. */
int solve_binerdle_pair(size_t sol1, size_t sol2,
                        const std::vector<std::string>& all_eqs,
                        const std::string& first_guess, int N,
                        bool verbose = false) {
    std::vector<size_t> c1, c2;
    for (size_t i = 0; i < all_eqs.size(); i++) {
        c1.push_back(i);
        c2.push_back(i);
    }

    std::string guess = first_guess;

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        std::string fb1 = compute_feedback(guess, all_eqs[sol1], N);
        std::string fb2 = compute_feedback(guess, all_eqs[sol2], N);

        if (verbose) {
            std::cerr << "  Turn " << turn << " guess " << guess
                      << " -> fb1=" << fb1 << " fb2=" << fb2 << "\n";
        }

        std::vector<size_t> next_c1, next_c2;
        for (size_t idx : c1) {
            if (is_consistent(all_eqs[idx], guess, fb1, N)) next_c1.push_back(idx);
        }
        for (size_t idx : c2) {
            if (is_consistent(all_eqs[idx], guess, fb2, N)) next_c2.push_back(idx);
        }

        c1 = std::move(next_c1);
        c2 = std::move(next_c2);

        if (verbose) {
            std::cerr << "    c1=" << c1.size() << " c2=" << c2.size() << "\n";
        }

        if (c1.empty() || c2.empty()) return MAX_TRIES + 1;
        if (c1.size() == 1 && c2.size() == 1) return turn;  /* Identified both */

        guess = best_guess_pairs(all_eqs, c1, c2, N);
    }
    return MAX_TRIES + 1;
}

int main(int argc, char** argv) {
    bool verbose = false;
    int test_i = -1, test_j = -1;
    std::string path;
    for (int a = 1; a < argc; ) {
        std::string arg = argv[a];
        if (arg == "--verbose") { verbose = true; a++; }
        else if (arg == "--test" && a + 3 < argc) {
            test_i = std::atoi(argv[a + 1]);
            test_j = std::atoi(argv[a + 2]);
            path = argv[a + 3];
            a += 4;
        }
        else if (arg[0] != '-' && path.empty()) { path = arg; a++; }
        else a++;
    }
    if (path.empty() && test_i >= 0 && argc >= 5) path = argv[argc - 1];
    if (path.empty()) {
        std::cerr << "Usage: bench_binerdle [--verbose] [--test i j] <equations.txt>\n";
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

    int N = static_cast<int>(equations[0].size());
    if (N != 6 && N != 8) {
        std::cerr << "Binerdle: use equations_6.txt (mini) or equations_8.txt (normal)\n";
        return 1;
    }

    if (test_i >= 0 && test_j >= 0) {
        size_t i = static_cast<size_t>(test_i), j = static_cast<size_t>(test_j);
        if (i >= equations.size() || j >= equations.size()) {
            std::cerr << "Invalid test indices\n";
            return 1;
        }
        std::string fg = FIRST_GUESS.at(N);
        std::cerr << "Test pair (" << i << "," << j << "): " << equations[i]
                  << " and " << equations[j] << "\n";
        int t = solve_binerdle_pair(i, j, equations, fg, N, true);
        std::cerr << "Result: " << t << " turns\n";
        return 0;
    }

    std::string first_guess = FIRST_GUESS.at(N);
    size_t n = equations.size();

    std::cout << "Binerdle benchmark (" << (N == 6 ? "Mini" : "Normal") << ") "
              << n << " equations, " << (n * n) << " pairs...\n";

    std::vector<int> turn_dist(MAX_TRIES + 3, 0);
    double sum_turns = 0;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8) reduction(+:sum_turns)
#endif
    for (size_t ij = 0; ij < n * n; ij++) {
        size_t i = ij / n, j = ij % n;
        int t = solve_binerdle_pair(i, j, equations, first_guess, N);
#ifdef _OPENMP
#pragma omp atomic
#endif
        turn_dist[t]++;
        sum_turns += t;
    }

    std::cout << "\nBinerdle (one guess for both, 7 tries):\n";
    std::cout << "  Mean guesses: " << (sum_turns / (n * n)) << "\n";
    std::cout << "  Distribution: ";
    for (int k = 1; k <= MAX_TRIES + 1; k++) {
        if (turn_dist[k] > 0) std::cout << k << ":" << turn_dist[k] << " ";
    }
    std::cout << "\n";
    std::cout << "  Failures (8+): " << turn_dist[MAX_TRIES + 1] << "\n";

    return 0;
}
