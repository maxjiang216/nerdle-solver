/**
 * Full-pool Monte Carlo: mean guesses to solve when the opening is fixed and every later move
 * uses the tiered policy (|C|<=2 trivial, injective g in C, injective g not in C, else v2 entropy).
 *
 * Usage: ./first_guess_tiered_sim [--pool data/equations_7.txt] [--fb-cache data/fb_7.bin]
 *          [--limit 50] [--highlight 4+27=31] [--selector v1|v2] [--sample M] [--no-2ply]
 *
 * Default evaluates every secret (slow). Use --sample M (e.g. 2000) for approximate ranking
 * like bench_nerdle / README benchmarks.
 * --no-2ply: v2 uses 1-ply score only (skip twoply_score tiebreak) — faster first-guess sweeps.
 */

#include "bench_solve.hpp"
#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <random>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using nerdle::entropy_and_partitions;
using nerdle::subgame::try_load_fb_cache;

static bool all_symbols_unique(const std::string& s) {
    std::unordered_map<char, int> cnt;
    for (unsigned char c : s) {
        if (++cnt[c] > 1)
            return false;
    }
    return static_cast<int>(cnt.size()) == static_cast<int>(s.size());
}

struct Cand {
    int idx = 0;
    double H_bits = 0.0;
    std::string eq;
};

/** Sequential mean over `secret_idx` (caller parallelizes over openings to avoid nested OpenMP). */
static double mean_guesses_tiered(const std::vector<std::string>& eqs, int n_pool, int N,
                                  const std::string& first_guess, int max_tries,
                                  nerdle_bench::Selector sel, const std::vector<uint32_t>& fb_full,
                                  const std::vector<int>& secret_idx, bool v2_twoply_tiebreak) {
    double sum = 0.0;
    for (int si : secret_idx) {
        uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uint32_t)(si * 1315423911u + 17u);
        int g = nerdle_bench::solve_one_tiered(eqs[static_cast<size_t>(si)], eqs, first_guess, N,
                                               max_tries, sel, seed, &fb_full, v2_twoply_tiebreak);
        sum += static_cast<double>(g);
    }
    return sum / static_cast<double>(secret_idx.size());
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::string pool_path = "data/equations_7.txt";
    std::string fb_cache = "data/fb_7.bin";
    std::string highlight = "4+27=31";
    int limit = 50;
    size_t sample_size = 0;
    bool v2_twoply_tiebreak = true;
    nerdle_bench::Selector sel = nerdle_bench::Selector::V2;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--highlight" && i + 1 < argc)
            highlight = argv[++i];
        else if (a.rfind("--limit=", 0) == 0 && a.size() > 8)
            limit = std::atoi(a.c_str() + 8);
        else if (a == "--limit" && i + 1 < argc)
            limit = std::atoi(argv[++i]);
        else if (a == "--selector" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "v1" || v == "V1")
                sel = nerdle_bench::Selector::V1;
            else
                sel = nerdle_bench::Selector::V2;
        } else if (a == "--sample" && i + 1 < argc)
            sample_size = static_cast<size_t>(std::atoi(argv[++i]));
        else if (a == "--no-2ply")
            v2_twoply_tiebreak = false;
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
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Pool length mismatch.\n";
            return 1;
        }
    }

    std::vector<uint32_t> fb_full;
    int nc = 0, Nc = 0;
    if (!try_load_fb_cache(fb_cache, &nc, &Nc, &fb_full) || nc != n_pool || Nc != N) {
        std::cerr << "Load or build fb-cache first: " << fb_cache << "\n";
        return 1;
    }

    std::vector<size_t> all_idx(static_cast<size_t>(n_pool));
    for (int i = 0; i < n_pool; i++)
        all_idx[static_cast<size_t>(i)] = static_cast<size_t>(i);

    std::vector<Cand> cands;
    std::vector<int> hist;
    for (int i = 0; i < n_pool; i++) {
        const std::string& g = eqs[static_cast<size_t>(i)];
        if (!all_symbols_unique(g))
            continue;
        double H = 0, sum_sq = 0;
        entropy_and_partitions(g.c_str(), eqs, all_idx, N, hist, H, sum_sq);
        (void)sum_sq;
        cands.push_back({i, H, g});
    }

    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.H_bits > b.H_bits; });

    const size_t n_eval = (limit > 0) ? std::min(static_cast<size_t>(limit), cands.size()) : cands.size();
    const int max_tries = (N == 10) ? nerdle_bench::MAXI_TRIES : 6;

    std::vector<int> secret_idx;
    if (sample_size > 0 && sample_size < static_cast<size_t>(n_pool)) {
        secret_idx.resize(static_cast<size_t>(n_pool));
        std::iota(secret_idx.begin(), secret_idx.end(), 0);
        std::mt19937 rng(42);
        std::shuffle(secret_idx.begin(), secret_idx.end(), rng);
        secret_idx.resize(sample_size);
    } else {
        secret_idx.resize(static_cast<size_t>(n_pool));
        std::iota(secret_idx.begin(), secret_idx.end(), 0);
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Pool " << pool_path << "  n=" << n_pool << "  N=" << N << "\n";
    std::cout << "Policy: tiered (|C|<=2 trivial, injective in C, injective outside C, then ";
    std::cout << (sel == nerdle_bench::Selector::V2 ? "v2" : "v1");
    if (sel == nerdle_bench::Selector::V2)
        std::cout << (v2_twoply_tiebreak ? ", 2-ply tiebreak" : ", 1-ply only (--no-2ply)");
    std::cout << ")\n";
    std::cout << "Unique-symbol openings: " << cands.size() << "  (evaluating top " << n_eval
              << " by 1-ply entropy)\n";
    std::cout << "Secrets per opening: " << secret_idx.size()
              << (sample_size > 0 && sample_size < static_cast<size_t>(n_pool) ? " (sampled)\n" : "\n");
    std::cout << "max_tries=" << max_tries << "  (OpenMP parallel over openings)\n\n";

    struct Row {
        std::string eq;
        double H_bits = 0;
        double mean_g = 0;
        double seconds = 0;
    };
    std::vector<Row> rows;
    rows.resize(n_eval);

    auto t0 = std::chrono::steady_clock::now();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
    for (size_t t = 0; t < n_eval; t++) {
        const Cand& c = cands[t];
        auto t_row0 = std::chrono::steady_clock::now();
        double mean = mean_guesses_tiered(eqs, n_pool, N, c.eq, max_tries, sel, fb_full, secret_idx,
                                          v2_twoply_tiebreak);
        auto t_row1 = std::chrono::steady_clock::now();
        rows[t].eq = c.eq;
        rows[t].H_bits = c.H_bits;
        rows[t].mean_g = mean;
        rows[t].seconds = std::chrono::duration<double>(t_row1 - t_row0).count();
    }
    auto t1 = std::chrono::steady_clock::now();
    double total_s = std::chrono::duration<double>(t1 - t0).count();

    std::vector<size_t> order(n_eval);
    for (size_t i = 0; i < n_eval; i++)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (rows[a].mean_g != rows[b].mean_g)
            return rows[a].mean_g < rows[b].mean_g;
        return rows[a].H_bits > rows[b].H_bits;
    });

    std::cout << "rank\tmean_guesses\tH_bits\tt_open_s\topening\n";
    for (size_t r = 0; r < n_eval; r++) {
        const Row& row = rows[order[r]];
        std::cout << (r + 1) << "\t" << row.mean_g << "\t" << row.H_bits << "\t" << row.seconds
                  << "\t" << row.eq << "\n";
    }

    std::cout << "\nTotal eval wall time: " << total_s << " s\n";

    auto it_hl =
        std::find_if(rows.begin(), rows.end(), [&](const Row& rr) { return rr.eq == highlight; });
    if (it_hl != rows.end()) {
        const size_t pos = static_cast<size_t>(it_hl - rows.begin());
        size_t rank_by_mean = 0;
        for (size_t r = 0; r < n_eval; r++) {
            if (order[r] == pos) {
                rank_by_mean = r + 1;
                break;
            }
        }
        std::cout << "\nHighlight \"" << highlight << "\": mean " << it_hl->mean_g
                  << "  rank by mean " << rank_by_mean << "\n";
    } else {
        std::cout << "\nHighlight \"" << highlight
                  << "\" not in evaluated set (not among top --limit unique-symbol).\n";
    }

    return 0;
}
