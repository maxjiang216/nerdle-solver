/**
 * Head-to-head: Bellman (precomputed) vs entropy (v2) or vs partition on the same targets.
 *
 * Run: ./compare_bellman data/equations_6.txt
 *      ./compare_bellman data/equations_5.txt --vs partition
 */

#include "bench_solve.hpp"
#include "micro_policy.hpp"
#include "nerdle_core.hpp"
#include "optimal_policy_build.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

enum class Baseline { Entropy, Partition };

/** Along Bellman's play vs `target`, compare next-move Bellman vs partition at each visited candidate set. */
static size_t audit_bellman_vs_partition(
    const std::string& target, const std::vector<std::string>& equations, int N, int max_tries,
    const std::string& fg_bellman,
    const std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash>& policy,
    std::string* example_b_out, std::string* example_p_out, std::string* example_target_out) {
    std::vector<size_t> candidates;
    for (size_t i = 0; i < equations.size(); i++)
        candidates.push_back(i);

    size_t disagreements = 0;
    std::string guess = fg_bellman;
    for (int turn = 1; turn <= max_tries; turn++) {
        uint32_t packed = nerdle::compute_feedback_packed(guess, target, N);
        if (packed == nerdle::all_green_packed(N))
            return disagreements;

        std::vector<size_t> next;
        for (size_t idx : candidates) {
            if (nerdle::compute_feedback_packed(guess, equations[idx], N) == packed)
                next.push_back(idx);
        }
        candidates = std::move(next);
        if (candidates.empty())
            return disagreements;

        /* Next-move Bellman vs partition uses tries_remaining = max_tries - turn (same as bench_solve). */
        int tries_left = max_tries - turn;
        std::string gb = nerdle::guess_from_micro_policy(policy, equations, candidates);
        if (gb.empty()) {
            if (example_b_out && example_p_out && example_target_out &&
                example_target_out->empty()) {
                *example_b_out = "(policy miss)";
                *example_p_out = nerdle::best_guess_partition_policy(equations, candidates, N,
                                                                      tries_left);
                *example_target_out = target;
            }
            disagreements++;
            return disagreements;
        }

        std::string gp =
            nerdle::best_guess_partition_policy(equations, candidates, N, tries_left);
        if (gb != gp) {
            if (example_b_out && example_p_out && example_target_out &&
                example_target_out->empty()) {
                *example_b_out = gb;
                *example_p_out = gp;
                *example_target_out = target;
            }
            disagreements++;
        }
        guess = gb;
    }
    return disagreements;
}

/** Along **partition's** play vs `target`, Bellman next move vs partition at each visited set. */
static size_t audit_partition_path_vs_bellman(
    const std::string& target, const std::vector<std::string>& equations, int N, int max_tries,
    const std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash>& policy,
    std::string* example_b_out, std::string* example_p_out, std::string* example_target_out) {
    std::vector<size_t> candidates;
    for (size_t i = 0; i < equations.size(); i++)
        candidates.push_back(i);
    std::string guess =
        nerdle::best_guess_partition_policy(equations, candidates, N, max_tries);
    size_t disagreements = 0;

    for (int turn = 1; turn <= max_tries; turn++) {
        std::string gb = nerdle::guess_from_micro_policy(policy, equations, candidates);
        std::string gp = guess;
        if (gb.empty()) {
            disagreements++;
            if (example_b_out && example_target_out && example_target_out->empty()) {
                *example_b_out = "(policy miss)";
                *example_p_out = gp;
                *example_target_out = target;
            }
            return disagreements;
        }
        if (gb != gp) {
            disagreements++;
            if (example_b_out && example_p_out && example_target_out &&
                example_target_out->empty()) {
                *example_b_out = gb;
                *example_p_out = gp;
                *example_target_out = target;
            }
        }

        uint32_t packed = nerdle::compute_feedback_packed(guess, target, N);
        if (packed == nerdle::all_green_packed(N))
            return disagreements;

        std::vector<size_t> next;
        for (size_t idx : candidates) {
            if (nerdle::compute_feedback_packed(guess, equations[idx], N) == packed)
                next.push_back(idx);
        }
        candidates = std::move(next);
        if (candidates.empty())
            return disagreements;

        if (turn < max_tries)
            guess = nerdle::best_guess_partition_policy(equations, candidates, N,
                                                         max_tries - turn);
    }
    return disagreements;
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    Baseline baseline = Baseline::Entropy;
    bool audit_partition = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--vs" && i + 1 < argc) {
            std::string b = argv[++i];
            if (b == "entropy" || b == "v2")
                baseline = Baseline::Entropy;
            else if (b == "partition")
                baseline = Baseline::Partition;
            else {
                std::cerr << "--vs must be entropy or partition\n";
                return 1;
            }
        } else if (a == "--audit-partition") {
            audit_partition = true;
        } else if (a[0] != '-')
            path = a;
    }
    if (path.empty()) {
        std::cerr << "Usage: " << (argc ? argv[0] : "compare_bellman")
                  << " <equations.txt> [--vs entropy|partition]\n";
        std::cerr << "  Compares optimal policy (Micro: Bellman, Mini: optimal) vs entropy v2 or partition.\n";
        std::cerr << "  Requires Micro/Mini equation files and data/optimal_policy_5.bin or _6.bin.\n";
        std::cerr << "  --audit-partition : compare policy vs partition (Micro only; Mini policy = partition in practice)\n";
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
        if (!line.empty())
            equations.push_back(line);
    }
    f.close();

    if (equations.empty()) {
        std::cerr << "No equations loaded.\n";
        return 1;
    }

    int N = static_cast<int>(equations[0].size());
    for (const auto& e : equations) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Inconsistent equation lengths.\n";
            return 1;
        }
    }
    if (N != 5 && N != 6) {
        std::cerr << "Bellman policy comparison is only set up for --len 5 or 6 (Micro/Mini).\n";
        return 1;
    }

    int n_eq = static_cast<int>(equations.size());
    std::string pol_path = (N == 5) ? "data/optimal_policy_5.bin" : "data/optimal_policy_6.bin";
    std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash> policy;
    if (!nerdle::load_micro_policy(pol_path, n_eq, policy) &&
        !(nerdle::try_build_optimal_policy_bin(N, std::cerr) &&
          nerdle::load_micro_policy(pol_path, n_eq, policy))) {
        std::cerr << "Cannot load " << pol_path << " (build attempt failed).\n";
        return 1;
    }

    int max_tries = 6;
    const auto& fg_map = nerdle_bench::first_guess_map();
    std::string fg_entropy = fg_map.count(N) ? fg_map.at(N) : equations[0];

    std::vector<size_t> all_idx(equations.size());
    for (size_t i = 0; i < equations.size(); i++)
        all_idx[i] = i;
    std::string fg_bellman = nerdle::guess_from_micro_policy(policy, equations, all_idx);
    if (fg_bellman.empty())
        fg_bellman = fg_entropy;

    /** Same seeds as bench_nerdle (per-target). */
    auto seed_for = [](size_t idx) -> uint64_t {
        return 0x9E3779B97F4A7C15ULL ^ (uint64_t)idx * 1315423911ULL;
    };

    const nerdle_bench::Selector sel = nerdle_bench::Selector::V2;
    const nerdle_bench::PlayStrategy policy_strat = (N == 6) ? nerdle_bench::PlayStrategy::Optimal
                                                             : nerdle_bench::PlayStrategy::Bellman;
    size_t n = equations.size();
    std::vector<int> bellman_guesses(n), base_guesses(n);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (size_t i = 0; i < n; i++) {
        uint64_t s = seed_for(i);
        bellman_guesses[i] = nerdle_bench::solve_one(equations[i], equations, fg_bellman, N,
                                                     max_tries, sel, policy_strat, s, &policy);
        nerdle_bench::PlayStrategy bs =
            (baseline == Baseline::Entropy) ? nerdle_bench::PlayStrategy::Entropy
                                           : nerdle_bench::PlayStrategy::Partition;
        base_guesses[i] = nerdle_bench::solve_one(equations[i], equations, fg_entropy, N,
                                                  max_tries, sel, bs, s, nullptr);
    }

    long long sum_b = 0, sum_base = 0;
    int b_strict = 0, tie = 0, base_strict = 0;
    std::vector<std::string> base_winners;
    std::vector<std::string> bellman_winners;

    for (size_t i = 0; i < n; i++) {
        int b = bellman_guesses[i];
        int c = base_guesses[i];
        sum_b += b;
        sum_base += c;
        if (b < c) {
            b_strict++;
            bellman_winners.push_back(equations[i]);
        } else if (b == c) {
            tie++;
        } else {
            base_strict++;
            base_winners.push_back(equations[i]);
        }
    }

    double mean_b = static_cast<double>(sum_b) / static_cast<double>(n);
    double mean_base = static_cast<double>(sum_base) / static_cast<double>(n);
    double edge = mean_base - mean_b;
    const char* policy_label = (N == 6) ? "Optimal" : "Bellman";

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "File: " << path << "  (" << n << " equations, N=" << N << ")\n";
    std::cout << policy_label << " first guess (from policy): " << fg_bellman << "\n";
    if (baseline == Baseline::Entropy) {
        std::cout << "Entropy baseline first guess (table): " << fg_entropy << "\n";
    } else {
        std::vector<size_t> all_cand(equations.size());
        for (size_t i = 0; i < equations.size(); i++)
            all_cand[i] = i;
        std::string gp0 = nerdle::best_guess_partition_policy(equations, all_cand, N, max_tries);
        std::cout << "Partition baseline first guess (computed): " << gp0
                  << "  (not the entropy table line when they differ)\n";
    }
    std::cout << "\n";

    if (audit_partition) {
        std::vector<size_t> all_cand(n);
        for (size_t i = 0; i < n; i++)
            all_cand[i] = i;
        std::string gp_full =
            nerdle::best_guess_partition_policy(equations, all_cand, N, max_tries);
        std::cout << "--- " << policy_label << " vs partition (first guess, full pool) ---\n";
        std::cout << "  " << policy_label << " (policy):   " << fg_bellman << "\n";
        std::cout << "  Partition:          " << gp_full
                  << (fg_bellman == gp_full ? "  (match)\n" : "  (DIFFER)\n");

        size_t total_step_mismatches = 0;
        size_t targets_with_any_mismatch = 0;
        std::string ex_t, ex_b, ex_p;
        for (size_t i = 0; i < n; i++) {
            std::string eb, ep, et;
            size_t d = audit_bellman_vs_partition(equations[i], equations, N, max_tries, fg_bellman,
                                                  policy, &eb, &ep, &et);
            total_step_mismatches += d;
            if (d > 0) {
                targets_with_any_mismatch++;
                if (ex_t.empty() && !et.empty()) {
                    ex_t = et;
                    ex_b = eb;
                    ex_p = ep;
                }
            }
        }
        std::cout << "\n--- Along " << policy_label
                  << " trajectories (moves after 1st guess vs target) ---\n";
        std::cout << "The table minimizes infinite-horizon E[guesses]. Partition uses tries left\n"
                     "in P(win) / recursive E under partition — different objectives.\n\n";
        std::cout << "  Steps where next " << policy_label << " move ≠ partition move: "
                  << total_step_mismatches << "\n";
        std::cout << "  Targets with ≥1 such step: " << targets_with_any_mismatch << " / " << n << "\n";
        if (!ex_t.empty()) {
            std::cout << "  Example (secret " << ex_t << "): " << policy_label << " `" << ex_b
                      << "` vs partition `" << ex_p << "`\n";
        }

        size_t total2 = 0;
        size_t tgt2 = 0;
        std::string ex2t, ex2b, ex2p;
        for (size_t i = 0; i < n; i++) {
            std::string eb, ep, et;
            size_t d = audit_partition_path_vs_bellman(equations[i], equations, N, max_tries, policy,
                                                      &eb, &ep, &et);
            total2 += d;
            if (d > 0) {
                tgt2++;
                if (ex2t.empty() && !et.empty()) {
                    ex2t = et;
                    ex2b = eb;
                    ex2p = ep;
                }
            }
        }
        std::cout << "--- Along **partition** trajectories (same comparison) ---\n";
        std::cout << "  Steps where " << policy_label << " move ≠ partition move: " << total2 << "\n";
        std::cout << "  Targets with ≥1 such step: " << tgt2 << " / " << n << "\n";
        if (!ex2t.empty()) {
            std::cout << "  Example (secret " << ex2t << "): " << policy_label << " `" << ex2b
                      << "` vs partition `" << ex2p << "`\n";
        }
        std::cout << "\n";
    }

    std::cout << "Mean guesses to solve (uniform random target):\n";
    std::cout << "  " << policy_label << ":  " << mean_b << "\n";
    std::cout << "  Baseline: " << mean_base << "\n";
    std::cout << "  Average edge (baseline minus " << policy_label << "): " << edge
              << " guesses per target";
    if (edge > 1e-9)
        std::cout << "  (" << policy_label << " better on average)\n";
    else if (edge < -1e-9)
        std::cout << "  (baseline better on average)\n";
    else
        std::cout << "  (same mean)\n";

    std::cout << "\nHead-to-head (fewer guesses wins):\n";
    std::cout << "  " << policy_label << " strictly better: " << b_strict << " / " << n << " ("
              << (100.0 * static_cast<double>(b_strict) / static_cast<double>(n)) << "%)\n";
    std::cout << "  Tie:                     " << tie << " / " << n << " ("
              << (100.0 * static_cast<double>(tie) / static_cast<double>(n)) << "%)\n";
    std::cout << "  Baseline strictly better: " << base_strict << " / " << n << " ("
              << (100.0 * static_cast<double>(base_strict) / static_cast<double>(n)) << "%)\n";

    auto show_list = [](const char* title, const std::vector<std::string>& v, size_t cap) {
        if (v.empty())
            return;
        std::cout << "\n" << title << " (" << v.size() << "):\n";
        size_t m = std::min(cap, v.size());
        for (size_t i = 0; i < m; i++)
            std::cout << "  " << v[i] << "\n";
        if (v.size() > cap)
            std::cout << "  ... (" << (v.size() - cap) << " more)\n";
    };

    std::string t1 = std::string("Equations where ") + policy_label + " uses fewer guesses than baseline";
    std::string t2 = std::string("Equations where baseline uses fewer guesses than ") + policy_label;
    show_list(t1.c_str(), bellman_winners, 12);
    show_list(t2.c_str(), base_winners, 40);

    return 0;
}
