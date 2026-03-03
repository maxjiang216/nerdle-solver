/**
 * Adaptive entropy-optimal first guess for large equation sets.
 * Uses successive elimination with confidence intervals: small sample → eliminate
 * low contenders → larger sample → repeat → finalists on full set.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o solve_adaptive solve_adaptive.cpp
 * Run:     ./solve_adaptive equations_10.txt
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

static const unsigned char PLACE_SQ = '\x01';
static const unsigned char PLACE_CB = '\x02';

static std::string normalize_maxi(std::string s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2) {
            if ((unsigned char)s[i + 1] == 0xB2) { out += (char)PLACE_SQ; i++; continue; }
            if ((unsigned char)s[i + 1] == 0xB3) { out += (char)PLACE_CB; i++; continue; }
        }
        out += s[i];
    }
    return out;
}

static std::string maxi_to_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == PLACE_SQ) out += "\xc2\xb2";
        else if (c == PLACE_CB) out += "\xc2\xb3";
        else out += c;
    }
    return out;
}

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

/* Entropy and its variance (plug-in estimator). Var(H) ≈ (1/n)[E[(log p)^2] - H^2] */
void entropy_and_var(const std::string& guess,
                     const std::vector<std::string>& solutions,
                     const std::vector<size_t>& indices, int N,
                     double& out_h, double& out_var) {
    std::unordered_map<std::string, int> pattern_count;
    for (size_t idx : indices) {
        std::string fb = compute_feedback(guess, solutions[idx], N);
        pattern_count[fb]++;
    }
    double n = static_cast<double>(indices.size());
    if (n <= 1) { out_h = 0; out_var = 0; return; }
    double h = 0.0, sum_log_sq = 0.0;
    for (const auto& kv : pattern_count) {
        double p = kv.second / n;
        if (p > 0) {
            double lp = std::log2(p);
            h -= p * lp;
            sum_log_sq += p * lp * lp;
        }
    }
    out_h = h;
    out_var = (sum_log_sq - h * h) / n;  /* can be slightly negative from sampling */
    if (out_var < 0) out_var = 0;
}

int main(int argc, char** argv) {
    std::string path = "data/equations_10.txt";
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

    bool is_maxi = false;
    for (unsigned char c : equations[0]) {
        if (c == 0xC2) { is_maxi = true; break; }
    }
    if (is_maxi) {
        for (auto& eq : equations) eq = normalize_maxi(eq);
    }

    int N = static_cast<int>(equations[0].size());
    if (N < 5 || N > 10 || (N > 8 && N != 10)) {
        std::cerr << "Length must be 5-10 (Maxi).\n";
        return 1;
    }

    size_t total = equations.size();
    std::cerr << "Loaded " << total << " equations. Adaptive solve...\n";

    /* Stratified sample indices (every k-th, from different starts) */
    auto sample_indices = [&](size_t target) {
        std::vector<size_t> idx;
        if (target >= total) {
            for (size_t i = 0; i < total; i++) idx.push_back(i);
            return idx;
        }
        size_t step = total / target;
        if (step < 1) step = 1;
        for (size_t i = 0; i < total && idx.size() < target; i += step)
            idx.push_back(i);
        return idx;
    };

    const double Z = 3.291;  /* ~99.9% CI */
    std::vector<size_t> candidates;
    for (size_t i = 0; i < total; i++) candidates.push_back(i);

    /* Phase 1: 1k sample, all candidates (most guesses weak → CI prunes aggressively) */
    size_t n1 = std::min(size_t(1000), total);
    std::vector<size_t> sol1 = sample_indices(n1);
    std::cerr << "Phase 1: " << n1 << " solutions, " << candidates.size() << " candidates... " << std::flush;

    std::vector<std::pair<double, double>> h_var(candidates.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (size_t c = 0; c < candidates.size(); c++) {
        entropy_and_var(equations[candidates[c]], equations, sol1, N, h_var[c].first, h_var[c].second);
    }

    double best_h = -1;
    for (size_t c = 0; c < candidates.size(); c++) {
        if (h_var[c].first > best_h) best_h = h_var[c].first;
    }
    double best_se = 0;
    for (size_t c = 0; c < candidates.size(); c++) {
        if (h_var[c].first == best_h) {
            best_se = std::sqrt(h_var[c].second);
            break;
        }
    }
    double cutoff = best_h - Z * best_se;

    std::vector<size_t> survivors;
    for (size_t c = 0; c < candidates.size(); c++) {
        double ub = h_var[c].first + Z * std::sqrt(h_var[c].second);
        if (ub >= cutoff) survivors.push_back(candidates[c]);
    }
    std::cerr << "→ " << survivors.size() << " survivors\n";

    candidates = std::move(survivors);
    if (candidates.size() <= 1 || n1 >= total) {
        std::string best_eq = equations[candidates[0]];
        std::cout << "Optimal first guess: " << (N == 10 ? maxi_to_display(best_eq) : best_eq) << "\n";
        return 0;
    }

    /* Phase 2: 25k sample, survivors only */
    size_t n2 = std::min(size_t(25000), total);
    std::vector<size_t> sol2 = sample_indices(n2);
    std::cerr << "Phase 2: " << n2 << " solutions, " << candidates.size() << " candidates... " << std::flush;

    h_var.resize(candidates.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (size_t c = 0; c < candidates.size(); c++) {
        entropy_and_var(equations[candidates[c]], equations, sol2, N, h_var[c].first, h_var[c].second);
    }

    best_h = -1;
    for (size_t c = 0; c < candidates.size(); c++) {
        if (h_var[c].first > best_h) best_h = h_var[c].first;
    }
    best_se = 0;
    for (size_t c = 0; c < candidates.size(); c++) {
        if (h_var[c].first == best_h) {
            best_se = std::sqrt(h_var[c].second);
            break;
        }
    }
    cutoff = best_h - Z * best_se;

    survivors.clear();
    for (size_t c = 0; c < candidates.size(); c++) {
        double ub = h_var[c].first + Z * std::sqrt(h_var[c].second);
        if (ub >= cutoff) survivors.push_back(candidates[c]);
    }
    std::cerr << "→ " << survivors.size() << " survivors\n";

    candidates = std::move(survivors);
    if (candidates.size() <= 1) {
        std::string best_eq = equations[candidates[0]];
        std::cout << "Optimal first guess: " << (N == 10 ? maxi_to_display(best_eq) : best_eq) << "\n";
        return 0;
    }

    /* Phase 3: full set, finalists only */
    std::vector<size_t> sol3(total);
    for (size_t i = 0; i < total; i++) sol3[i] = i;

    std::cerr << "Phase 3: " << total << " solutions, " << candidates.size() << " finalists... " << std::flush;

    double final_best_h = -1;
    size_t best_idx = candidates[0];
    for (size_t c : candidates) {
        double h, v;
        entropy_and_var(equations[c], equations, sol3, N, h, v);
        if (h > final_best_h) {
            final_best_h = h;
            best_idx = c;
        }
    }
    std::cerr << "done.\n";

    std::string best_eq = equations[best_idx];
    std::cout << "Optimal first guess: " << (N == 10 ? maxi_to_display(best_eq) : best_eq)
              << " (entropy = " << final_best_h << " bits)\n";

    return 0;
}
