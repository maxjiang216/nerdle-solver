/**
 * Nerdle solver - entropy-based optimal first guess.
 * Works for lengths 5-8 (classic) and 10 (Maxi).
 * Uses OpenMP for parallel computation.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o solve solve.cpp
 * Run:     ./solve equations_5.txt
 *          ./solve equations_10.txt
 */

#include "nerdle_core.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static const unsigned char PLACE_SQ = '\x01'; /* ² internal */
static const unsigned char PLACE_CB = '\x02'; /* ³ internal */

/* Normalize Maxi UTF-8 (² ³) to single-byte so each equation = 10 tiles */
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

static std::string maxi_to_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == PLACE_SQ)
            out += "\xc2\xb2";
        else if (c == PLACE_CB)
            out += "\xc2\xb3";
        else
            out += c;
    }
    return out;
}

int main(int argc, char** argv) {
    std::string path = "data/equations_8.txt";
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

    /* Detect Maxi (UTF-8 ²³): normalize to 10 single-byte tiles */
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
        std::cerr << "Length must be 5, 6, 7, 8, or 10 (Maxi).\n";
        return 1;
    }
    for (const auto& eq : equations) {
        if (static_cast<int>(eq.size()) != N) {
            std::cerr << "Inconsistent equation length.\n";
            return 1;
        }
    }

    std::vector<size_t> all_idx(equations.size());
    for (size_t i = 0; i < equations.size(); i++) all_idx[i] = i;

    std::string best;
    double best_h = -1.0;
    std::vector<int> hist;

#ifdef _OPENMP
#pragma omp parallel
    {
        std::vector<int> local_hist;
        double local_best_h = -1.0;
        std::string local_best;
#pragma omp for schedule(dynamic, 8)
        for (size_t i = 0; i < equations.size(); i++) {
            double H, sum_sq;
            nerdle::entropy_and_partitions(equations[i].c_str(), equations, all_idx, N, local_hist, H,
                                           sum_sq);
            (void)sum_sq;
            if (H > local_best_h) {
                local_best_h = H;
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
    for (const auto& g : equations) {
        double H, sum_sq;
        nerdle::entropy_and_partitions(g.c_str(), equations, all_idx, N, hist, H, sum_sq);
        (void)sum_sq;
        if (H > best_h) {
            best_h = H;
            best = g;
        }
    }
#endif

    std::cout << "Loaded " << equations.size() << " equations from " << path << "\n";
    std::cout << "Entropy-optimal first guess: " << (N == 10 ? maxi_to_display(best) : best)
              << " (entropy = " << best_h << " bits)\n";

    return 0;
}
