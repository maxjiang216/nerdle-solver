/**
 * Binerdle optimal first guess - maximizes sum of independent board entropies:
 * H_total = H_1(g) + H_2(g) = 2 * H(g) when both boards use the full equation list.
 *
 * Run: ./solve_binerdle equations_6.txt
 *      ./solve_binerdle equations_8.txt
 */

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "nerdle_core.hpp"

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
    std::vector<size_t> all_indices;
    all_indices.reserve(n);
    for (size_t i = 0; i < n; i++) all_indices.push_back(i);

    std::cout << "Binerdle optimal first guess (" << n << " equations; H_pair = 2 × H_single)\n";

    std::string best;
    double best_h = -1.0;

#ifdef _OPENMP
#pragma omp parallel
    {
        double local_best_h = -1.0;
        std::string local_best;
#pragma omp for schedule(dynamic, 4)
        for (size_t i = 0; i < n; i++) {
            std::vector<int> hist;
            double h1 =
                nerdle::entropy_of_guess_packed(equations[i].c_str(), equations, all_indices, N, hist);
            double h_pair = 2.0 * h1;
            if (h_pair > local_best_h) {
                local_best_h = h_pair;
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
        std::vector<int> hist;
        double h1 =
            nerdle::entropy_of_guess_packed(equations[i].c_str(), equations, all_indices, N, hist);
        double h_pair = 2.0 * h1;
        if (h_pair > best_h) {
            best_h = h_pair;
            best = equations[i];
        }
    }
#endif

    std::cout << "Binerdle-optimal first guess: " << best << " (entropy = " << best_h << " bits)\n";

    static const std::unordered_map<int, std::string> SINGLE_FIRST = {
        {6, "4*7=28"}, {8, "48-32=16"},
    };
    std::string single_best = SINGLE_FIRST.at(N);
    if (best != single_best) {
        std::vector<int> hist;
        double h1 = nerdle::entropy_of_guess_packed(single_best.c_str(), equations, all_indices, N, hist);
        double single_pair = 2.0 * h1;
        std::cout << "Single-Nerdle optimal:    " << single_best << " (pair entropy = " << single_pair
                  << " bits)\n";
    }

    return 0;
}
