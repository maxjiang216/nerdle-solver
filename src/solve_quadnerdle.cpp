/**
 * Quad Nerdle optimal first guess - sum of independent board entropies:
 * H_total = H_1(g)+...+H_4(g) = 4 * H(g) when all four boards use the full equation list.
 *
 * Run: ./solve_quadnerdle data/equations_8.txt
 */

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "nerdle_core.hpp"

int main(int argc, char** argv) {
    std::string path = "data/equations_8.txt";
    for (int a = 1; a < argc; a++) {
        std::string arg = argv[a];
        if (arg[0] != '-') path = arg;
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
    if (N != 8) {
        std::cerr << "Quad Nerdle: use equations_8.txt\n";
        return 1;
    }

    size_t n = equations.size();
    std::vector<size_t> all_indices;
    all_indices.reserve(n);
    for (size_t i = 0; i < n; i++) all_indices.push_back(i);

    std::cout << "Quad Nerdle optimal first guess (" << n << " equations; H_quad = 4 × H_single)\n";

    std::string best;
    double best_h = -1.0;

#ifdef _OPENMP
#pragma omp parallel
    {
        double local_best_h = -1.0;
        std::string local_best;
#pragma omp for schedule(dynamic, 8)
        for (size_t i = 0; i < n; i++) {
            std::vector<int> hist;
            double h1 =
                nerdle::entropy_of_guess_packed(equations[i].c_str(), equations, all_indices, N, hist);
            double h_quad = 4.0 * h1;
            if (h_quad > local_best_h) {
                local_best_h = h_quad;
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
        double h_quad = 4.0 * h1;
        if (h_quad > best_h) {
            best_h = h_quad;
            best = equations[i];
        }
    }
#endif

    std::cout << "\nQuad Nerdle-optimal first guess: " << best << " (entropy = " << best_h << " bits)\n";

    std::string single_first = "48-32=16";
    std::string binerdle_first = "43-27=16";
    std::vector<int> hist;

    auto entropy_quad = [&](const std::string& g) {
        double h1 = nerdle::entropy_of_guess_packed(g.c_str(), equations, all_indices, N, hist);
        return 4.0 * h1;
    };

    double h_single = entropy_quad(single_first);
    double h_binerdle = entropy_quad(binerdle_first);
    std::cout << "Benchmark (same model):\n";
    std::cout << "  Single Nerdle: " << single_first << " (quad entropy = " << h_single << " bits)\n";
    std::cout << "  Binerdle:      " << binerdle_first << " (quad entropy = " << h_binerdle
              << " bits)\n";

    return 0;
}
