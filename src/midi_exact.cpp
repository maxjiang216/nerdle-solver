/**
 * Exact-play classification for Midi (or any pool) after a fixed first guess.
 *
 * Heuristic (in order):
 *   1. |C|<=2     -> trivial optimal from state after first guess
 *   2. injective g in C   -> 2 more guesses (optimal for this objective)
 *   3. injective g not in C (full pool) -> same
 *   4. else -> Bellman with guesses from full pool (uniform on C); process smallest |C| first
 *
 * Usage: ./midi_exact --pool data/equations_7.txt --first-guess '4+27=31' [--fb-cache data/fb_7.bin]
 *        [--bash-subgames]   (run DP for remaining classes; default: classification only)
 *        [--bash-k-lo N] [--bash-k-hi M]   (with --bash-subgames: only classes with N <= |C| <= M;
 *                                         omit for all Bellman-eligible classes)
 */

#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using nerdle::compute_feedback_packed;
using nerdle::subgame::BellmanResult;
using nerdle::subgame::SubgameOptimizer;
using nerdle::subgame::build_fb_matrix;
using nerdle::subgame::injective_guess_in_candidates;
using nerdle::subgame::injective_guess_outside_candidates;
using nerdle::subgame::try_load_fb_cache;
using nerdle::subgame::write_fb_cache;

struct ClassInfo {
    uint32_t first_fb = 0;
    std::vector<int> cand_global;
};

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);

    std::string pool_path = "data/equations_7.txt";
    std::string first_guess = "4+27=31";
    std::string fb_cache;
    bool bash_subgames = false;
    int bash_k_lo = 0;
    int bash_k_hi = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--first-guess" && i + 1 < argc)
            first_guess = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--bash-subgames")
            bash_subgames = true;
        else if (a == "--bash-k-lo" && i + 1 < argc)
            bash_k_lo = std::atoi(argv[++i]);
        else if (a == "--bash-k-hi" && i + 1 < argc)
            bash_k_hi = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout
                << "Usage: ./midi_exact [--pool PATH] [--first-guess STR] [--fb-cache PATH.bin] "
                   "[--bash-subgames] [--bash-k-lo N] [--bash-k-hi M]\n";
            return 0;
        }
    }

    if (static_cast<int>(first_guess.size()) < 1) {
        std::cerr << "Bad --first-guess\n";
        return 1;
    }

    std::vector<std::string> eqs;
    std::string line;
    {
        std::ifstream f(pool_path);
        if (!f) {
            std::cerr << "Cannot open " << pool_path << "\n";
            return 1;
        }
        while (std::getline(f, line)) {
            if (!line.empty())
                eqs.push_back(line);
        }
    }

    const int n_pool = static_cast<int>(eqs.size());
    const int N = static_cast<int>(eqs[0].size());
    if (static_cast<int>(first_guess.size()) != N) {
        std::cerr << "first-guess length must match pool (N=" << N << ").\n";
        return 1;
    }
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Pool length mismatch.\n";
            return 1;
        }
    }

    std::unordered_map<uint32_t, std::vector<int>> by_fb;
    by_fb.reserve(700);
    for (int i = 0; i < n_pool; i++) {
        uint32_t c =
            compute_feedback_packed(first_guess, eqs[static_cast<size_t>(i)], N);
        by_fb[c].push_back(i);
    }

    long long cnt_trivial = 0;
    long long cnt_inj_in = 0;
    long long cnt_inj_out = 0;
    long long cnt_bellman = 0;

    std::vector<ClassInfo> bellman_queue;

    for (auto& pr : by_fb) {
        uint32_t code = pr.first;
        std::vector<int>& cand = pr.second;
        int k = static_cast<int>(cand.size());
        if (k <= 2) {
            cnt_trivial += k;
            continue;
        }
        if (k > 256) {
            std::cerr << "Class with k>256 (feedback=" << code << ") — cannot bitmask DP.\n";
            return 1;
        }
        if (injective_guess_in_candidates(eqs, N, cand)) {
            cnt_inj_in += k;
            continue;
        }
        if (injective_guess_outside_candidates(eqs, n_pool, N, cand)) {
            cnt_inj_out += k;
            continue;
        }
        cnt_bellman += k;
        bellman_queue.push_back({code, std::move(cand)});
    }

    std::sort(bellman_queue.begin(), bellman_queue.end(), [](const ClassInfo& a, const ClassInfo& b) {
        return a.cand_global.size() < b.cand_global.size();
    });

    std::cout << "Pool: " << pool_path << "  n=" << n_pool << "  N=" << N << "\n";
    std::cout << "First guess: " << first_guess << "\n\n";
    std::cout << "Heuristic classification (equations in pool):\n";
    std::cout << "  trivial |C|<=2:              " << cnt_trivial << "\n";
    std::cout << "  injective guess in C:        " << cnt_inj_in << "\n";
    std::cout << "  injective guess outside C:   " << cnt_inj_out << "\n";
    std::cout << "  need Bellman (full-pool g): " << cnt_bellman << "  (" << bellman_queue.size()
              << " classes)\n";

    if (!bash_subgames) {
        std::cout << "\nRe-run with --bash-subgames to compute Bellman for " << bellman_queue.size()
                  << " classes (smallest |C| first).\n";
        return 0;
    }

    const bool bash_k_filter = (bash_k_lo > 0 && bash_k_hi > 0 && bash_k_lo <= bash_k_hi);
    std::vector<ClassInfo> to_bash = bellman_queue;
    if (bash_k_filter) {
        std::vector<ClassInfo> f;
        f.reserve(to_bash.size());
        for (const auto& cl : to_bash) {
            int k = static_cast<int>(cl.cand_global.size());
            if (k >= bash_k_lo && k <= bash_k_hi)
                f.push_back(cl);
        }
        to_bash = std::move(f);
        std::cout << "\nBash filter: |C| in [" << bash_k_lo << ", " << bash_k_hi << "] -> "
                  << to_bash.size() << " classes, " << [&] {
                      long long s = 0;
                      for (const auto& c : to_bash)
                          s += static_cast<long long>(c.cand_global.size());
                      return s;
                  }() << " equations\n";
    }

    std::vector<uint32_t> fb_full;
    double sec_fb = 0;
    int nc = 0, Nc = 0;
    if (!fb_cache.empty() && try_load_fb_cache(fb_cache, &nc, &Nc, &fb_full)) {
        if (nc != n_pool || Nc != N) {
            std::cerr << "fb-cache mismatch; rebuilding.\n";
            fb_full.clear();
        }
    }
    if (fb_full.empty()) {
        auto t0 = std::chrono::steady_clock::now();
        build_fb_matrix(eqs, n_pool, N, &fb_full);
        auto t1 = std::chrono::steady_clock::now();
        sec_fb = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "\nFB matrix build: " << sec_fb << " s\n";
        if (!fb_cache.empty() && write_fb_cache(fb_cache, n_pool, N, fb_full))
            std::cout << "Wrote " << fb_cache << "\n";
    } else {
        std::cout << "\nLoaded FB cache " << fb_cache << "\n";
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\n"
              << "class_fb"
              << "\t|C|"
              << "\tE[add.guesses|C]"
              << "\tstates"
              << "\tdp_s"
              << "\topt_2nd\n";

    auto wall_t0 = std::chrono::steady_clock::now();
    double sum_dp = 0;
    for (const auto& cl : to_bash) {
        SubgameOptimizer opt(eqs, n_pool, N, fb_full, cl.cand_global);
        auto t0 = std::chrono::steady_clock::now();
        BellmanResult br = opt.solve();
        auto t1 = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        sum_dp += sec;
        std::cout << cl.first_fb << "\t" << cl.cand_global.size() << "\t" << br.ev << "\t" << br.dp_states
                  << "\t" << sec << "\t" << eqs[static_cast<size_t>(br.optimal_first_global)] << "\n";
    }
    auto wall_t1 = std::chrono::steady_clock::now();
    double wall_sec = std::chrono::duration<double>(wall_t1 - wall_t0).count();

    std::cout << "\nTotal Bellman DP time (sum over subgames): " << sum_dp << " s\n";
    std::cout << "Wall time (bash loop only): " << wall_sec << " s\n";
    return 0;
}
