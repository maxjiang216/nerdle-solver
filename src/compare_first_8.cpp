/* Compare two fixed first guesses (8-tile) under the same partition policy as partition_report. */
#include "nerdle_core.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static int find_eq(const std::vector<std::string>& eqs, const std::string& target) {
    for (size_t i = 0; i < eqs.size(); i++) {
        if (eqs[i] == target)
            return static_cast<int>(i);
    }
    return -1;
}

static void print_block(const char* name, const nerdle::PartitionSolveDist& d, int n_pool, int max_tries) {
    double solved = 0, ev_solved = 0;
    for (int t = 1; t <= max_tries && t < static_cast<int>(d.solve_at.size()); t++) {
        ev_solved += static_cast<double>(t) * d.solve_at[static_cast<size_t>(t)];
        solved += d.solve_at[static_cast<size_t>(t)];
    }
    const double fail_p = std::max(0.0, 1.0 - solved);
    const double ev = ev_solved + static_cast<double>(max_tries + 1) * fail_p;
    std::cout << name << "  EV=" << std::setprecision(10) << ev << "  (fail in " << max_tries << " p=" << fail_p
              << ")\n";
    long long em = 0;
    for (int t = 1; t <= max_tries && t < static_cast<int>(d.solve_at.size()); t++) {
        long long c = std::llround(d.solve_at[static_cast<size_t>(t)] * static_cast<double>(n_pool));
        em += c;
        if (c)
            std::cout << "  " << t << " guesses: " << c << "\n";
    }
    if (n_pool - em > 0)
        std::cout << "  " << (max_tries + 1) << " (failed): " << (static_cast<long long>(n_pool) - em) << "\n";
}

int main() {
    const char* path = "data/equations_8.txt";
    int tie_depth = 0;
    const int max_tries = 6;
    std::vector<std::string> eqs;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Open " << path << "\n";
        return 1;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) eqs.push_back(line);
    }
    if (static_cast<int>(eqs[0].size()) != 8) {
        std::cerr << "Need 8-tile pool.\n";
        return 1;
    }
    const int N = 8;

    const std::string a = "52-34=18";
    const std::string b = "48-32=16";
    int ia = find_eq(eqs, a);
    int ib = find_eq(eqs, b);
    if (ia < 0) {
        std::cerr << "Missing " << a << " in pool\n";
        return 1;
    }
    if (ib < 0) {
        std::cerr << "Missing " << b << " in pool\n";
        return 1;
    }

    std::cout << "Pool: " << path << "  n=" << eqs.size() << "  tie_depth=" << tie_depth
              << "  (same engine as ./partition_report)\n\n";

    nerdle::PartitionGreedyEvaluator ev(eqs, N, max_tries, tie_depth, nerdle::PartitionFbBudget::Report);
    ev.build_feedback_matrix();
    if (!ev.has_full_feedback_matrix())
        std::cerr << "Warning: no full n×n fb table; results may be slow/inexact.\n";

    std::cout << "Index " << ia << "  " << a << "  (policy-optimal first guess for tie_depth=0)\n";
    std::cout << "Index " << ib << "  " << b << "  (classic hardcoded first from first_guess_map)\n\n";

    {
        std::vector<size_t> all(eqs.size());
        for (size_t i = 0; i < all.size(); i++) all[i] = i;
        nerdle::PartitionSolveDist root{};
        ev.best_guess_index(all, max_tries, &root);
        print_block("Optimal first guess (policy root, reference):", root, static_cast<int>(eqs.size()), max_tries);
    }

    std::cout << "\n";
    print_block(a.c_str(), ev.distribution_with_fixed_first_guess(ia), static_cast<int>(eqs.size()), max_tries);
    std::cout << "\n";
    print_block(b.c_str(), ev.distribution_with_fixed_first_guess(ib), static_cast<int>(eqs.size()), max_tries);
    return 0;
}
