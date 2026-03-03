/**
 * Quad Nerdle optimal first guess - maximizes entropy over DISTINCT quad space.
 * All 4 equations are always distinct: space is P(n,4) = n*(n-1)*(n-2)*(n-3).
 * Uses adaptive strategy: prune via confidence intervals, then more MC trials for finalists.
 *
 * Run: ./solve_quadnerdle data/equations_8.txt
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

constexpr size_t PHASE1_SAMPLES = 5000;   /* Phase 1: small sample, all candidates */
constexpr size_t PHASE2_SAMPLES = 25000;  /* Phase 2: larger sample, survivors */
constexpr size_t PHASE3_SAMPLES = 50000;  /* Phase 3: final round, top ~50 by Phase 2 */
constexpr size_t PHASE3_TOP = 50;         /* Max finalists for Phase 3 (avoids 5k*50k) */
constexpr size_t TIEBREAKER_SAMPLES = 200000;  /* Extra samples if top 2 within epsilon */
constexpr double TIE_EPSILON = 0.02;       /* Bits; run tiebreaker if |h1-h2| < this */
constexpr double Z = 3.291;  /* ~99.9% confidence interval */

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

/* Compute entropy and its variance over a fixed sample of distinct quads. */
void entropy_and_var_distinct(const std::string& guess,
                              const std::vector<std::string>& all_eqs,
                              const std::vector<std::array<size_t,4>>& quads,
                              int N, double& out_h, double& out_var) {
    std::unordered_map<std::string, int> pattern_count;
    for (const auto& q : quads) {
        std::string fb1 = compute_feedback(guess, all_eqs[q[0]], N);
        std::string fb2 = compute_feedback(guess, all_eqs[q[1]], N);
        std::string fb3 = compute_feedback(guess, all_eqs[q[2]], N);
        std::string fb4 = compute_feedback(guess, all_eqs[q[3]], N);
        pattern_count[fb1 + "|" + fb2 + "|" + fb3 + "|" + fb4]++;
    }
    double n = static_cast<double>(quads.size());
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
    out_var = (sum_log_sq - h * h) / n;
    if (out_var < 0) out_var = 0;
}

/* Classify equation for stratification: type = f(operators, result magnitude).
   Returns 0..NUM_STRATA-1. Reduces variance by ensuring coverage across equation "families". */
static const int NUM_STRATA = 16;
int equation_type(const std::string& eq) {
    size_t eq_pos = eq.find('=');
    if (eq_pos == std::string::npos) return 0;
    long long rhs = 0;
    for (size_t i = eq_pos + 1; i < eq.size(); i++) {
        if (eq[i] >= '0' && eq[i] <= '9')
            rhs = rhs * 10 + (eq[i] - '0');
    }
    int op_mask = 0;  /* bits: + -, * / */
    for (size_t i = 0; i < eq_pos; i++) {
        if (eq[i] == '+') op_mask |= 1;
        else if (eq[i] == '-') op_mask |= 2;
        else if (eq[i] == '*') op_mask |= 4;
        else if (eq[i] == '/') op_mask |= 8;
    }
    int result_bucket = (rhs <= 9) ? 0 : (rhs <= 99) ? 1 : 2;
    int type = (op_mask % 8) * 3 + result_bucket;  /* 0..23, wrap to 0..15 */
    return type % NUM_STRATA;
}

/* Proportional stratified sampling: partition by first equation's type.
   N_h = |strata_h| * P(n-1,3), so n_h ∝ |strata_h|. Sample n_h from stratum h. */
std::vector<std::array<size_t,4>> sample_distinct_quads_stratified(
    const std::vector<std::string>& equations, size_t target) {
    size_t n = equations.size();
    if (n < 4) return {};
    std::vector<std::vector<size_t>> strata(NUM_STRATA);
    for (size_t i = 0; i < n; i++) {
        int t = equation_type(equations[i]);
        strata[t].push_back(i);
    }
    std::vector<std::array<size_t,4>> quads;
    quads.reserve(target);
    for (int h = 0; h < NUM_STRATA; h++) {
        if (strata[h].empty()) continue;
        size_t n_h = (target * strata[h].size() + n/2) / n;
        if (n_h == 0) n_h = 1;
        for (size_t sh = 0; sh < n_h && quads.size() < target; sh++) {
            size_t r = (h * 7919ULL + sh * 2654435761ULL);
            size_t i = strata[h][r % strata[h].size()];
            /* Pick j,k,l distinct and != i via rejection */
            for (int attempt = 0; attempt < 50; attempt++) {
                r = r * 7919 + 2654435761;
                size_t j = r % n; r /= n;
                size_t k = r % n; r /= n;
                size_t l = r % n;
                if (j != i && k != i && l != i && j != k && j != l && k != l) {
                    quads.push_back({i, j, k, l});
                    break;
                }
            }
        }
    }
    /* Fill remainder with plain rejection if we're short */
    while (quads.size() < target) {
        size_t s = quads.size() * 2654435761ULL;
        size_t i = (s) % n; s /= n;
        size_t j = (s) % n; s /= n;
        size_t k = (s) % n; s /= n;
        size_t l = (s) % n;
        if (i != j && i != k && i != l && j != k && j != l && k != l)
            quads.push_back({i, j, k, l});
    }
    return quads;
}

/* Fallback: plain rejection sampling. */
std::vector<std::array<size_t,4>> sample_distinct_quads(size_t n, size_t target) {
    std::vector<std::array<size_t,4>> quads;
    quads.reserve(target);
    for (size_t s = 0; quads.size() < target && s < target * 5; s++) {
        size_t r = (s * 2654435761ULL);
        size_t i = (r) % n; r /= n;
        size_t j = (r) % n; r /= n;
        size_t k = (r) % n; r /= n;
        size_t l = (r) % n;
        if (i != j && i != k && i != l && j != k && j != l && k != l) {
            quads.push_back({i, j, k, l});
        }
    }
    return quads;
}

int main(int argc, char** argv) {
    std::string path = "data/equations_8.txt";
    bool use_stratification = true;
    for (int a = 1; a < argc; a++) {
        std::string arg = argv[a];
        if (arg == "--no-stratify") use_stratification = false;
        else if (arg[0] != '-') path = arg;
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
    size_t P4 = (n >= 4) ? (n * (n-1) * (n-2) * (n-3)) : 0;

    std::cout << "Quad Nerdle optimal first guess (" << n << " equations, "
              << P4 << " distinct quadruples, adaptive MC"
              << (use_stratification ? ", stratified" : "") << ")\n\n";

    /* Benchmark first guesses */
    std::string single_first = "48-32=16";
    std::string binerdle_first = "43-27=16";

    /* Phase 1: distinct quads (stratified by equation type for lower variance), evaluate ALL */
    std::cerr << "Phase 1: " << PHASE1_SAMPLES << " quads, all " << n << " candidates... " << std::flush;
    std::vector<std::array<size_t,4>> quads1 = use_stratification
        ? sample_distinct_quads_stratified(equations, PHASE1_SAMPLES)
        : sample_distinct_quads(n, PHASE1_SAMPLES);
    if (quads1.size() < PHASE1_SAMPLES / 2) {
        std::cerr << "Warning: only got " << quads1.size() << " distinct quads\n";
    }

    std::vector<std::pair<double, double>> h_var(n);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (size_t c = 0; c < n; c++) {
        entropy_and_var_distinct(equations[c], equations, quads1, N, h_var[c].first, h_var[c].second);
    }

    double best_h = -1;
    for (size_t c = 0; c < n; c++) {
        if (h_var[c].first > best_h) best_h = h_var[c].first;
    }
    double best_se = 0;
    for (size_t c = 0; c < n; c++) {
        if (h_var[c].first == best_h) {
            best_se = std::sqrt(h_var[c].second);
            break;
        }
    }
    double cutoff = best_h - Z * best_se;

    std::vector<size_t> survivors;
    for (size_t c = 0; c < n; c++) {
        double ub = h_var[c].first + Z * std::sqrt(h_var[c].second);
        if (ub >= cutoff) survivors.push_back(c);
    }
    std::cerr << "→ " << survivors.size() << " survivors\n";

    /* Add benchmark guesses to survivors if not already */
    size_t single_idx = n, binerdle_idx = n;
    for (size_t i = 0; i < n; i++) {
        if (equations[i] == single_first) single_idx = i;
        if (equations[i] == binerdle_first) binerdle_idx = i;
    }
    if (single_idx < n) {
        auto it = std::find(survivors.begin(), survivors.end(), single_idx);
        if (it == survivors.end()) survivors.push_back(single_idx);
    }
    if (binerdle_idx < n) {
        auto it = std::find(survivors.begin(), survivors.end(), binerdle_idx);
        if (it == survivors.end()) survivors.push_back(binerdle_idx);
    }

    if (survivors.size() <= 1) {
        std::string best_eq = equations[survivors[0]];
        std::cout << "Quad Nerdle-optimal first guess: " << best_eq << "\n";
        return 0;
    }

    /* Phase 2: larger sample, survivors only */
    std::cerr << "Phase 2: " << PHASE2_SAMPLES << " quads, " << survivors.size() << " survivors... " << std::flush;
    std::vector<std::array<size_t,4>> quads2 = use_stratification
        ? sample_distinct_quads_stratified(equations, PHASE2_SAMPLES)
        : sample_distinct_quads(n, PHASE2_SAMPLES);

    std::vector<std::pair<double, double>> h_var2(survivors.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (size_t s = 0; s < survivors.size(); s++) {
        size_t c = survivors[s];
        entropy_and_var_distinct(equations[c], equations, quads2, N, h_var2[s].first, h_var2[s].second);
    }

    best_h = -1;
    for (size_t s = 0; s < survivors.size(); s++) {
        if (h_var2[s].first > best_h) best_h = h_var2[s].first;
    }
    best_se = 0;
    for (size_t s = 0; s < survivors.size(); s++) {
        if (h_var2[s].first == best_h) {
            best_se = std::sqrt(h_var2[s].second);
            break;
        }
    }
    cutoff = best_h - Z * best_se;

    std::vector<size_t> finalists;
    for (size_t s = 0; s < survivors.size(); s++) {
        double ub = h_var2[s].first + Z * std::sqrt(h_var2[s].second);
        if (ub >= cutoff) finalists.push_back(survivors[s]);
    }
    /* Trim to top PHASE3_TOP by point estimate for Phase 3 (avoid 5k*50k evals) */
    if (finalists.size() > PHASE3_TOP) {
        std::vector<std::pair<double, size_t>> ranked;
        for (size_t c : finalists) {
            auto it = std::find(survivors.begin(), survivors.end(), c);
            if (it != survivors.end())
                ranked.push_back({h_var2[it - survivors.begin()].first, c});
        }
        std::partial_sort(ranked.begin(), ranked.begin() + std::min(PHASE3_TOP, ranked.size()),
                         ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        finalists.clear();
        for (size_t i = 0; i < std::min(PHASE3_TOP, ranked.size()); i++)
            finalists.push_back(ranked[i].second);
    }
    std::cerr << "→ " << finalists.size() << " finalists (Phase 3)\n";

    /* Phase 3: largest sample, finalists only */
    std::cerr << "Phase 3: " << PHASE3_SAMPLES << " quads, " << finalists.size() << " finalists... " << std::flush;
    std::vector<std::array<size_t,4>> quads3 = use_stratification
        ? sample_distinct_quads_stratified(equations, PHASE3_SAMPLES)
        : sample_distinct_quads(n, PHASE3_SAMPLES);

    std::vector<std::pair<double, size_t>> phase3_results;
    for (size_t c : finalists) {
        double h, v;
        entropy_and_var_distinct(equations[c], equations, quads3, N, h, v);
        phase3_results.push_back({h, c});
    }
    std::sort(phase3_results.begin(), phase3_results.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::cerr << "done.\n";

    size_t best_idx = phase3_results[0].second;
    double final_best_h = phase3_results[0].first;
    std::vector<std::array<size_t,4>> quads_tb;  /* used if tiebreaker runs */
    /* Tiebreaker: if top 2 are within epsilon, run larger sample on just them */
    if (phase3_results.size() >= 2 &&
        std::abs(phase3_results[0].first - phase3_results[1].first) < TIE_EPSILON) {
        std::cerr << "Tiebreaker: top 2 within " << TIE_EPSILON << " bits, "
                  << TIEBREAKER_SAMPLES << " quads... " << std::flush;
        std::vector<size_t> top2 = {phase3_results[0].second, phase3_results[1].second};
        quads_tb = use_stratification
            ? sample_distinct_quads_stratified(equations, TIEBREAKER_SAMPLES)
            : sample_distinct_quads(n, TIEBREAKER_SAMPLES);
        double h0, h1, v0, v1;
        entropy_and_var_distinct(equations[top2[0]], equations, quads_tb, N, h0, v0);
        entropy_and_var_distinct(equations[top2[1]], equations, quads_tb, N, h1, v1);
        best_idx = (h0 >= h1) ? top2[0] : top2[1];
        final_best_h = (h0 >= h1) ? h0 : h1;
        std::cerr << "winner: " << equations[best_idx] << "\n";
    }

    std::string best = equations[best_idx];
    std::cout << "\nQuad Nerdle-optimal first guess: " << best
              << " (entropy ≈ " << final_best_h << " bits)\n";

    /* Report benchmarks on same sample as winner */
    const auto& quads_bench = !quads_tb.empty() ? quads_tb : quads3;
    size_t bench_n = quads_bench.size();
    double h_single = 0, h_binerdle = 0, v_single, v_binerdle;
    bool has_single = (single_idx < n), has_binerdle = (binerdle_idx < n);
    if (has_single) {
        entropy_and_var_distinct(single_first, equations, quads_bench, N, h_single, v_single);
    }
    if (has_binerdle) {
        entropy_and_var_distinct(binerdle_first, equations, quads_bench, N, h_binerdle, v_binerdle);
    }
    std::cout << "Benchmark (same " << bench_n << " quads):\n";
    if (has_single) std::cout << "  Single Nerdle: " << single_first << " (entropy ≈ " << h_single << " bits)\n";
    if (has_binerdle) std::cout << "  Binerdle:      " << binerdle_first << " (entropy ≈ " << h_binerdle << " bits)\n";

    return 0;
}
