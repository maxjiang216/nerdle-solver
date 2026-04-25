/**
 * CLI for candidate-only partition strategy stats (same engine as nerdle::PartitionGreedyEvaluator).
 */

#include "nerdle_core.hpp"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string path = "data/equations_7.txt";
    int max_tries = 6;
    int tie_depth = 0;
    size_t max_pool = 0; // 0 = use all lines (may be infeasible for very large maxi pools)
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc) {
            path = argv[++i];
        } else if (a == "--tries" && i + 1 < argc) {
            max_tries = std::atoi(argv[++i]);
        } else if (a == "--tie-depth" && i + 1 < argc) {
            tie_depth = std::atoi(argv[++i]);
        } else if (a == "--max" && i + 1 < argc) {
            max_pool = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--pool PATH]  (8-tile: data/equations_8.txt)  "
                      << "[--tries 6] [--tie-depth 0]  (try 0,1,2,... for tie-break depth)\n"
                      << "  [--max N]  (use only first N equations; for huge pools e.g. maxi, "
                      << "or to fit the report n×n table)\n";
            return 1;
        }
    }

    std::vector<std::string> eqs;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            eqs.push_back(line);
    }
    if (eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }
    if (max_pool > 0 && eqs.size() > max_pool)
        eqs.resize(max_pool);

    const int N = static_cast<int>(eqs[0].size());
    std::vector<size_t> all_idx(eqs.size());
    for (size_t i = 0; i < all_idx.size(); i++)
        all_idx[i] = i;

    std::cout.setf(std::ios::unitbuf);
    std::cout << "Pool: " << path << "  n=" << eqs.size() << "  N=" << N;
    if (max_pool > 0)
        std::cout << "  (first " << max_pool << " lines only, --max)";
    std::cout << "\n";
    std::cout << "Tie depth: " << tie_depth << "  (0 = greedy max partition count only)\n";
    std::cout << "FB budget: report (8-tile may use ~1.2GB n×n table when it fits in cap)\n";

    nerdle::PartitionGreedyEvaluator ev(eqs, N, max_tries, tie_depth, nerdle::PartitionFbBudget::Report);
    auto t0 = std::chrono::steady_clock::now();
    ev.build_feedback_matrix();
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "Feedback matrix: " << std::chrono::duration<double>(t1 - t0).count() << " s"
              << (ev.has_full_feedback_matrix() ? " (full n×n table)\n" : " (on-the-fly; no full table)\n");

    nerdle::PartitionSolveDist val{};
    int g = ev.best_guess_index(all_idx, max_tries, &val);
    auto t2 = std::chrono::steady_clock::now();
    std::cout << "Policy eval: " << std::chrono::duration<double>(t2 - t1).count() << " s"
              << "  memo_states=" << ev.memo_size() << "\n";

    std::cout << std::setprecision(10);
    std::cout << "First guess: " << eqs[static_cast<size_t>(g)] << "\n";
    double solved_p = 0.0;
    double ev_solved_part = 0.0;
    for (int t = 1; t <= max_tries && t < static_cast<int>(val.solve_at.size()); t++)
        ev_solved_part += static_cast<double>(t) * val.solve_at[static_cast<size_t>(t)];
    for (int t = 1; t <= max_tries && t < static_cast<int>(val.solve_at.size()); t++)
        solved_p += val.solve_at[static_cast<size_t>(t)];
    double fail_p = std::max(0.0, 1.0 - solved_p);
    double evm = ev_solved_part + static_cast<double>(max_tries + 1) * fail_p;
    std::cout << "EV: " << evm << "\n";
    std::cout << "Distribution:\n";
    long long emitted = 0;
    for (int t = 1; t <= max_tries && t < static_cast<int>(val.solve_at.size()); t++) {
        double p = val.solve_at[static_cast<size_t>(t)];
        long long cnt = std::llround(p * static_cast<double>(eqs.size()));
        emitted += cnt;
        if (cnt > 0)
            std::cout << "  " << t << ": " << cnt << " (" << (100.0 * p) << "%)\n";
    }
    long long fail_cnt = static_cast<long long>(eqs.size()) - emitted;
    if (fail_cnt > 0)
        std::cout << "  " << (max_tries + 1) << ": " << fail_cnt << " (" << (100.0 * fail_p)
                  << "%)  # failed within " << max_tries << "\n";
    return 0;
}
