/**
 * Binerdle optimal first guess - maximizes entropy over pair space (c1 × c2).
 * Run: ./solve_binerdle equations_6.txt
 *      ./solve_binerdle equations_8.txt
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

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

double entropy_over_pairs(const std::string& guess,
                          const std::vector<std::string>& all_eqs,
                          int N, bool use_mc, size_t mc_samples) {
    size_t n = all_eqs.size();
    size_t pair_count = n * n;
    std::unordered_map<std::string, int> pattern_count;

    if (!use_mc) {
        for (size_t i = 0; i < n; i++) {
            std::string fb1 = compute_feedback(guess, all_eqs[i], N);
            for (size_t j = 0; j < n; j++) {
                std::string fb2 = compute_feedback(guess, all_eqs[j], N);
                pattern_count[fb1 + "|" + fb2]++;
            }
        }
    } else {
        for (size_t k = 0; k < mc_samples; k++) {
            size_t ij = (k * 2654435761ULL) % pair_count;
            size_t i = ij / n, j = ij % n;
            std::string fb1 = compute_feedback(guess, all_eqs[i], N);
            std::string fb2 = compute_feedback(guess, all_eqs[j], N);
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

int main(int argc, char** argv) {
    std::string path = "data/equations_6.txt";
    if (argc >= 2) path = argv[1];

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
        std::cerr << "Binerdle: use equations_6.txt or equations_8.txt\n";
        return 1;
    }

    size_t n = equations.size();
    size_t pair_count = n * n;
    bool use_mc = (pair_count > 50000);
    size_t mc_samples = 10000;

    std::cout << "Binerdle optimal first guess (" << n << " equations, "
              << pair_count << " pairs";
    if (use_mc) std::cout << ", Monte Carlo " << mc_samples << " samples";
    std::cout << ")\n";

    std::string best;
    double best_h = -1.0;

#ifdef _OPENMP
#pragma omp parallel
    {
        double local_best_h = -1.0;
        std::string local_best;
#pragma omp for schedule(dynamic, 4)
        for (size_t i = 0; i < n; i++) {
            double h = entropy_over_pairs(equations[i], equations, N, use_mc, mc_samples);
            if (h > local_best_h) {
                local_best_h = h;
                local_best = equations[i];
            }
        }
#pragma omp critical
        {
            if (local_best_h > best_h) {
                best_h = local_best_h;
                best = local_best;
            }
        }
    }
#else
    for (size_t i = 0; i < n; i++) {
        double h = entropy_over_pairs(equations[i], equations, N, use_mc, mc_samples);
        if (h > best_h) {
            best_h = h;
            best = equations[i];
        }
    }
#endif

    std::cout << "Binerdle-optimal first guess: " << best
              << " (entropy = " << best_h << " bits)\n";

    static const std::unordered_map<int, std::string> SINGLE_FIRST = {
        {6, "4*7=28"}, {8, "48-32=16"},
    };
    std::string single_best = SINGLE_FIRST.at(N);
    if (best != single_best) {
        double single_h = entropy_over_pairs(single_best, equations, N, use_mc, mc_samples);
        std::cout << "Single-Nerdle optimal:    " << single_best
                  << " (entropy = " << single_h << " bits)\n";
    }

    return 0;
}
