/**
 * Rank opening equations (Midi N=7): unique symbols only, sort by partition entropy (1-ply),
 * estimate total E[guesses] under restricted Bellman with injective shortcuts (see SubgameOptimizer
 * restricted_guesses=true). Requires max feedback class size <= 256 (bitmask).
 *
 * Usage: ./explore_first_guess [--pool data/equations_7.txt] [--fb-cache data/fb_7.bin]
 *          [--highlight 4+27=31] [--limit 40]
 *
 * --limit: evaluate only the first K candidates after the entropy sort (default: all). Full scan
 * of thousands of openings is expensive (restricted Bellman per feedback class per opening).
 */

#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using nerdle::compute_feedback_packed;
using nerdle::entropy_and_partitions;
using nerdle::subgame::BellmanResult;
using nerdle::subgame::SubgameOptimizer;
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

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::string pool_path = "data/equations_7.txt";
    std::string fb_cache = "data/fb_7.bin";
    std::string highlight = "4+27=31";
    int limit = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--highlight" && i + 1 < argc)
            highlight = argv[++i];
        else if (a == "--limit" && i + 1 < argc)
            limit = std::atoi(argv[++i]);
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

    struct Result {
        std::string eq;
        double H_bits = 0;
        double E_total = 0;
        bool ok = false;
        int max_class = 0;
    };
    const size_t n_eval =
        (limit > 0) ? std::min(static_cast<size_t>(limit), cands.size()) : cands.size();
    std::vector<Result> results(n_eval);

    std::cout << std::fixed << std::setprecision(10);
    std::cout << "Pool " << pool_path << "  n=" << n_pool << "  N=" << N << "\n";
    std::cout << "Unique-symbol openings: " << cands.size() << "  (evaluating first " << n_eval
              << " by entropy)\n";
    std::cout << "Model: restricted Bellman + injective shortcuts (incl. outside-C), "
                 "max class |C| must be <= 256.\n\n";

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
    for (size_t ti = 0; ti < n_eval; ti++) {
        const Cand& c = cands[ti];
        int gi = c.idx;
        std::unordered_map<uint32_t, std::vector<int>> by_fb;
        for (int sj = 0; sj < n_pool; sj++) {
            uint32_t code =
                fb_full[static_cast<size_t>(gi) * static_cast<size_t>(n_pool) + static_cast<size_t>(sj)];
            by_fb[code].push_back(sj);
        }

        int max_k = 0;
        for (const auto& pr : by_fb)
            max_k = std::max(max_k, static_cast<int>(pr.second.size()));

        Result r;
        r.eq = c.eq;
        r.H_bits = c.H_bits;
        r.max_class = max_k;

        if (max_k > 256) {
            r.ok = false;
            r.E_total = 0;
            results[ti] = std::move(r);
            continue;
        }

        double sum_c = 0.0;
        r.ok = true;
        for (const auto& pr : by_fb) {
            const std::vector<int>& cg = pr.second;
            int k = static_cast<int>(cg.size());
            if (k == 0)
                continue;
            SubgameOptimizer opt(eqs, n_pool, N, fb_full, cg, true);
            BellmanResult br = opt.solve();
            if (!br.ok || std::isnan(br.ev)) {
                r.ok = false;
                break;
            }
            sum_c += static_cast<double>(k) * br.ev;
        }
        if (r.ok)
            r.E_total = 1.0 + sum_c / static_cast<double>(n_pool);
        results[ti] = std::move(r);
    }

    std::vector<size_t> feas;
    feas.reserve(results.size());
    for (size_t i = 0; i < results.size(); i++) {
        if (results[i].ok)
            feas.push_back(i);
    }
    std::sort(feas.begin(), feas.end(), [&](size_t a, size_t b) {
        if (results[a].E_total != results[b].E_total)
            return results[a].E_total < results[b].E_total;
        return results[a].H_bits > results[b].H_bits;
    });

    int rank_highlight = -1;
    double E_highlight = 0;
    bool ok_highlight = false;

    std::cout << "rank\tE_total\tH_bits\tmax|C|\topening\n";
    for (size_t rank = 0; rank < feas.size(); rank++) {
        const Result& r = results[feas[rank]];
        std::cout << (rank + 1) << "\t" << r.E_total << "\t" << r.H_bits << "\t" << r.max_class << "\t"
                  << r.eq << "\n";
        if (r.eq == highlight) {
            rank_highlight = static_cast<int>(rank) + 1;
            E_highlight = r.E_total;
            ok_highlight = true;
        }
    }

    int n_skip = static_cast<int>(results.size() - feas.size());
    std::cout << "\nAmong evaluated: " << n_skip << " openings skipped (max|C|>256 and/or restricted "
                                                 "subgame infeasible).\n";

    std::cout << "\nUnique-symbol openings by entropy — evaluated rows show E_total or skip:\n";
    for (size_t j = 0; j < cands.size(); j++) {
        const Cand& c = cands[j];
        if (j >= n_eval) {
            std::cout << c.H_bits << "\t(not in --limit eval)\t" << c.eq << "\n";
            continue;
        }
        const Result& rr = results[j];
        if (!rr.ok)
            std::cout << c.H_bits << "\tskip(max|C|=" << rr.max_class << " / restricted)\t" << c.eq
                      << "\n";
        else
            std::cout << c.H_bits << "\t" << rr.E_total << "\t" << c.eq << "\n";
    }

    if (ok_highlight) {
        std::cout << "\nHighlight \"" << highlight << "\": rank by E among feasible = " << rank_highlight
                  << "  E_total = " << E_highlight << "\n";
    } else {
        std::cout << "\nHighlight \"" << highlight
                  << "\" not among feasible ranked set (or not unique-symbol / not in pool).\n";
    }

    return 0;
}
