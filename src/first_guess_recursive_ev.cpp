/**
 * Expected guesses for a fixed opening via recursive partitioning (no per-secret game simulation).
 *
 * After the opening, for each feedback class C (uniform prior on secrets in C):
 *   |C|==1 → 1 more guess
 *   |C|==2 → 1.5 expected further guesses
 *   exists injective g in C: (2|C|-1)/|C| (= 2 - 1/|C|)
 *   exists injective g not in C: 2
 *   else → one step of v2 (with 2-ply tiebreak by default), partition C by that guess's feedback, recurse:
 *          V(C) = 1 + (1/|C|) Σ_cells |cell|·V(cell)
 *
 * Total: E = 1 + (1/n_pool) Σ_classes |C|·V(C).
 *
 * Usage: ./first_guess_recursive_ev [--pool ...] [--fb-cache ...] [--limit N] [--skip M] [--highlight ...]
 *          [--no-2ply] [--only a,b,c]
 *
 * Default: v2 uses 2-ply tiebreak (same as bench v2). Use --no-2ply for 1-ply-only v2 (faster).
 * After sorting unique-symbol openings by 1-ply entropy (desc), skip the first M (default 0), then
 * evaluate up to N openings (--limit). Example: --skip 15 --limit 30 = ranks 16..45 by entropy.
 * --only eq1,eq2,... evaluates exactly those pool strings (comma-separated), in that order.
 */

#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <cctype>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

static bool injective_guess_on_class(int g_pool, const std::vector<int>& cand,
                                     const std::vector<uint32_t>& fb_full, int n_pool) {
    std::unordered_set<uint32_t> seen;
    seen.reserve(cand.size() * 2 + 1);
    for (int s : cand) {
        uint32_t code = fb_full[static_cast<size_t>(g_pool) * static_cast<size_t>(n_pool) +
                                static_cast<size_t>(s)];
        if (!seen.insert(code).second)
            return false;
    }
    return static_cast<int>(seen.size()) == static_cast<int>(cand.size());
}

/** Optional closed-form value for whole class C before entropy (same order as tiered shortcuts). */
static bool try_shortcut_value(const std::vector<int>& cand, const std::vector<uint32_t>& fb_full,
                               int n_pool, double& out_v) {
    const int k = static_cast<int>(cand.size());
    if (k <= 0)
        return false;
    if (k == 1) {
        out_v = 1.0;
        return true;
    }
    if (k == 2) {
        out_v = 1.5;
        return true;
    }
    std::vector<int> sorted_c = cand;
    std::sort(sorted_c.begin(), sorted_c.end());
    for (int g : sorted_c) {
        if (injective_guess_on_class(g, cand, fb_full, n_pool)) {
            out_v = (2.0 * static_cast<double>(k) - 1.0) / static_cast<double>(k);
            return true;
        }
    }
    std::unordered_set<int> in_c(cand.begin(), cand.end());
    for (int g = 0; g < n_pool; g++) {
        if (in_c.count(g))
            continue;
        if (injective_guess_on_class(g, cand, fb_full, n_pool)) {
            out_v = 2.0;
            return true;
        }
    }
    return false;
}

static int equation_index(const std::vector<std::string>& eqs, const std::string& g) {
    for (size_t i = 0; i < eqs.size(); i++) {
        if (eqs[i] == g)
            return static_cast<int>(i);
    }
    return -1;
}

static void trim_inplace(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

static void split_csv_eqs(const std::string& line, std::vector<std::string>& out) {
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, ',')) {
        trim_inplace(part);
        if (!part.empty())
            out.push_back(part);
    }
}

static void partition_by_guess(int g_pool, const std::vector<int>& cand,
                               const std::vector<uint32_t>& fb_full, int n_pool,
                               std::unordered_map<uint32_t, std::vector<int>>& cells) {
    cells.clear();
    for (int s : cand) {
        uint32_t code = fb_full[static_cast<size_t>(g_pool) * static_cast<size_t>(n_pool) +
                                static_cast<size_t>(s)];
        cells[code].push_back(s);
    }
}

/** First g that yields ≥2 feedback cells (always exists for |C|>1 in valid Nerdle pools). */
static int find_splitting_guess(const std::vector<int>& cand, const std::vector<uint32_t>& fb_full,
                                int n_pool) {
    std::unordered_map<uint32_t, std::vector<int>> cells;
    for (int g = 0; g < n_pool; g++) {
        partition_by_guess(g, cand, fb_full, n_pool, cells);
        if (cells.size() >= 2)
            return g;
    }
    return -1;
}

static double V_recursive(const std::vector<std::string>& eqs, int n_pool, int N,
                            const std::vector<uint32_t>& fb_full, std::vector<int> cand,
                            std::map<std::vector<int>, double>& memo, std::mt19937& rng,
                            std::vector<int>& hist, bool v2_twoply, int depth) {
    std::sort(cand.begin(), cand.end());
    const int k = static_cast<int>(cand.size());
    if (k == 0)
        return 0.0;
    if (k == 1)
        return 1.0;

    auto it = memo.find(cand);
    if (it != memo.end())
        return it->second;

    double shortcut = 0.0;
    if (try_shortcut_value(cand, fb_full, n_pool, shortcut)) {
        memo[cand] = shortcut;
        return shortcut;
    }

    if (depth > 40) {
        memo[cand] = static_cast<double>(k);
        return static_cast<double>(k);
    }

    std::vector<size_t> cidx(cand.begin(), cand.end());
    std::unordered_set<size_t> cset(cidx.begin(), cidx.end());
    std::string gstr =
        nerdle::best_guess_v2(eqs, cidx, cset, N, hist, rng, v2_twoply);
    int g_pool = equation_index(eqs, gstr);
    if (g_pool < 0) {
        memo[cand] = static_cast<double>(k);
        return static_cast<double>(k);
    }

    std::unordered_map<uint32_t, std::vector<int>> cells;
    partition_by_guess(g_pool, cand, fb_full, n_pool, cells);

    if (cells.size() <= 1) {
        int g2 = find_splitting_guess(cand, fb_full, n_pool);
        if (g2 < 0) {
            memo[cand] = static_cast<double>(k);
            return static_cast<double>(k);
        }
        g_pool = g2;
        partition_by_guess(g_pool, cand, fb_full, n_pool, cells);
    }

    std::vector<std::vector<int>> children;
    children.reserve(cells.size());
    for (auto& pr : cells) {
        std::vector<int> ch = pr.second;
        std::sort(ch.begin(), ch.end());
        children.push_back(std::move(ch));
    }
    double sum = 0.0;
    for (auto& ch : children) {
        const int sz = static_cast<int>(ch.size());
        sum += static_cast<double>(sz) * V_recursive(eqs, n_pool, N, fb_full, std::move(ch), memo,
                                                     rng, hist, v2_twoply, depth + 1);
    }
    double v = 1.0 + sum / static_cast<double>(k);
    memo[cand] = v;
    return v;
}

static double expected_total(const std::vector<std::string>& eqs, int n_pool, int N,
                             int first_guess_idx, const std::vector<uint32_t>& fb_full,
                             bool v2_twoply, std::map<std::vector<int>, double>& memo) {
    std::unordered_map<uint32_t, std::vector<int>> by_fb;
    for (int s = 0; s < n_pool; s++) {
        uint32_t code =
            fb_full[static_cast<size_t>(first_guess_idx) * static_cast<size_t>(n_pool) +
                    static_cast<size_t>(s)];
        by_fb[code].push_back(s);
    }

    std::mt19937 rng(0xC0FFEEu);
    std::vector<int> hist;
    double sum_c = 0.0;
    for (auto& pr : by_fb) {
        std::vector<int> cg = pr.second;
        int k = static_cast<int>(cg.size());
        if (k == 0)
            continue;
        std::sort(cg.begin(), cg.end());
        sum_c += static_cast<double>(k) *
                 V_recursive(eqs, n_pool, N, fb_full, std::move(cg), memo, rng, hist, v2_twoply, 0);
    }
    return 1.0 + sum_c / static_cast<double>(n_pool);
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::string pool_path = "data/equations_7.txt";
    std::string fb_cache = "data/fb_7.bin";
    std::string highlight = "4+27=31";
    int limit = 30;
    int skip = 0;
    bool v2_twoply = true;
    std::vector<std::string> only_eqs;

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
        else if (a.rfind("--skip=", 0) == 0 && a.size() > 7)
            skip = std::atoi(a.c_str() + 7);
        else if (a == "--skip" && i + 1 < argc)
            skip = std::atoi(argv[++i]);
        else if (a == "--v2-2ply")
            v2_twoply = true;
        else if (a == "--no-2ply")
            v2_twoply = false;
        else if (a.rfind("--only=", 0) == 0 && a.size() > 7)
            split_csv_eqs(a.substr(7), only_eqs);
        else if (a == "--only" && i + 1 < argc)
            split_csv_eqs(argv[++i], only_eqs);
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

    if (!only_eqs.empty()) {
        for (const std::string& es : only_eqs) {
            int idx = equation_index(eqs, es);
            if (idx < 0) {
                std::cerr << "Opening not in pool: \"" << es << "\"\n";
                return 1;
            }
            double H = 0, sum_sq = 0;
            entropy_and_partitions(eqs[static_cast<size_t>(idx)].c_str(), eqs, all_idx, N, hist, H,
                                   sum_sq);
            (void)sum_sq;
            cands.push_back({idx, H, eqs[static_cast<size_t>(idx)]});
        }
    } else {
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
    }

    const size_t begin_idx =
        only_eqs.empty()
            ? std::min(static_cast<size_t>(std::max(0, skip)), cands.size())
            : 0;
    const size_t n_eval =
        only_eqs.empty()
            ? ((limit > 0) ? std::min(static_cast<size_t>(limit), cands.size() - begin_idx)
                           : (cands.size() - begin_idx))
            : cands.size();

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Recursive partition EV (not Monte Carlo). Pool " << pool_path << " n=" << n_pool
              << " N=" << N << "\n";
    std::cout << "v2 twoply tiebreak: " << (v2_twoply ? "on" : "off") << "\n";
    if (!only_eqs.empty()) {
        std::cout << "--only mode: " << n_eval << " openings (fixed list)\n\n";
    } else {
        std::cout << "Unique-symbol openings by 1-ply entropy: ranks " << (begin_idx + 1) << "–"
                  << (begin_idx + n_eval) << " of " << cands.size() << " (skip=" << skip
                  << ", count=" << n_eval << ")\n\n";
    }
    std::cout << "rank\tE_total\tH_bits\topening\n";

    struct Row {
        std::string eq;
        double H = 0;
        double E = 0;
        double sec = 0;
    };
    std::vector<Row> rows(n_eval);
    std::map<std::vector<int>, double> memo;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t ti = 0; ti < n_eval; ti++) {
        size_t t = begin_idx + ti;
        auto t1 = std::chrono::steady_clock::now();
        double E = expected_total(eqs, n_pool, N, cands[t].idx, fb_full, v2_twoply, memo);
        auto t2 = std::chrono::steady_clock::now();
        rows[ti].eq = cands[t].eq;
        rows[ti].H = cands[t].H_bits;
        rows[ti].E = E;
        rows[ti].sec = std::chrono::duration<double>(t2 - t1).count();
    }
    auto t_end = std::chrono::steady_clock::now();

    std::vector<size_t> order(n_eval);
    for (size_t i = 0; i < n_eval; i++)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (rows[a].E != rows[b].E)
            return rows[a].E < rows[b].E;
        return rows[a].H > rows[b].H;
    });

    for (size_t r = 0; r < n_eval; r++) {
        const Row& row = rows[order[r]];
        std::cout << (r + 1) << "\t" << row.E << "\t" << row.H << "\t" << row.eq << "\n";
    }
    if (n_eval > 0) {
        std::cout << "\nBest E in this batch: " << rows[order[0]].E << "  (\"" << rows[order[0]].eq
                  << "\")\n";
    }

    std::cout << "\nPer-opening compute times (same order as rank table): ";
    for (size_t r = 0; r < n_eval; r++)
        std::cout << rows[order[r]].sec << "s ";
    std::cout << "\nTotal wall: " << std::chrono::duration<double>(t_end - t0).count() << " s\n";

    auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& x) { return x.eq == highlight; });
    if (it != rows.end()) {
        size_t pos = static_cast<size_t>(it - rows.begin());
        size_t rk = 1;
        for (size_t r = 0; r < n_eval; r++) {
            if (order[r] == pos) {
                rk = r + 1;
                break;
            }
        }
        std::cout << "\nHighlight \"" << highlight << "\": E=" << it->E << " rank " << rk << "\n";
    }

    return 0;
}
