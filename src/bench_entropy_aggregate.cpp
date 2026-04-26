/**
 * Exact guess-count distribution for the entropy (v2) strategy using the same
 * "partition tree" as partition_report (exact aggregate): at each state the secret is
 * uniform on the candidate set, so we group by feedback and recurse.
 *
 * Matches bench_solve / bench_nerdle for PlayStrategy::Entropy: fixed first
 * guess (first_guess_map), then best_guess_v2 on the filtered set.
 *
 * Default: 1-ply max-entropy (twoply_tiebreak=false) — fully deterministic, no
 * RNG, same definition as the v2 1-ply pass.  Use --2ply for the Rényi 2-ply
 * tiebreak; on large pools that path samples internally — we seed mt19937 from
 * the subgame (candidates + tries left) for reproducibility.
 */

#include "nerdle_core.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using CountMap = std::vector<size_t>;

static const char* kUsageExtra =
    " [--2ply]\n"
    "  --2ply  Use v2 2-ply tiebreak (reproducible per-subgame RNG for large n).\n"
    "          Default: 1-ply entropy only (deterministic, no sampling).\n";

inline const std::string& fixed_first_guess(int N) {
    static const std::unordered_map<int, std::string> m = {
        {5, "3+2=5"},
        {6, "4*7=28"},
        {7, "4+27=31"},
        {8, "48-32=16"},
        {10, "76+1-23=54"},
    };
    static const std::string empty;
    auto it = m.find(N);
    return it == m.end() ? empty : it->second;
}

inline void add_histo_shift(CountMap& acc, const CountMap& b, int shift, int fail_b) {
    for (int j = 0; j < static_cast<int>(b.size()); j++) {
        if (b[static_cast<size_t>(j)] == 0) continue;
        if (j == fail_b) {
            if (static_cast<int>(acc.size()) <= fail_b) acc.resize(static_cast<size_t>(fail_b) + 1, 0);
            acc[static_cast<size_t>(fail_b)] += b[static_cast<size_t>(fail_b)];
            continue;
        }
        int dst = shift + j;
        if (dst > fail_b) continue;
        if (static_cast<int>(acc.size()) <= dst) acc.resize(static_cast<size_t>(dst) + 1, 0);
        acc[static_cast<size_t>(dst)] += b[static_cast<size_t>(j)];
    }
}

inline uint64_t subgame_seed(const std::vector<size_t>& cands, int tries_left) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t x : cands) {
        h ^= x;
        h *= 1099511628211ULL;
    }
    h ^= static_cast<uint32_t>(tries_left) * 0xD6E8FEB8668FD209ULL;
    h *= 1099511628211ULL;
    return h;
}

inline size_t find_line(const std::vector<std::string>& all, const std::string& s) {
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i] == s) return i;
    }
    return all.size();
}

static CountMap entropy_histo(const std::vector<std::string>& all_eqs, const std::vector<size_t>& cands,
                              int N, int max_tries, int tries_left, int fail_b, bool use_fixed_opening,
                              bool use_twoply) {
    (void)max_tries;
    if (cands.empty()) return CountMap();
    if (tries_left < 1) {
        CountMap h(static_cast<size_t>(fail_b) + 1, 0);
        h[static_cast<size_t>(fail_b)] = cands.size();
        return h;
    }
    if (cands.size() == 1) {
        CountMap h(static_cast<size_t>(fail_b) + 1, 0);
        h[1] = 1;
        return h;
    }

    std::string g;
    if (use_fixed_opening) {
        const std::string& fg = fixed_first_guess(N);
        if (fg.empty()) {
            std::cerr << "No default first guess for this length; use first line as opening.\n";
            g = all_eqs[cands[0]];
        } else {
            size_t idx = find_line(all_eqs, fg);
            g = (idx < all_eqs.size()) ? all_eqs[idx] : all_eqs[cands[0]];
        }
    } else {
        std::unordered_set<size_t> cset(cands.begin(), cands.end());
        std::vector<int> hist;
        uint64_t s = subgame_seed(cands, tries_left);
        std::mt19937 rng(static_cast<std::mt19937::result_type>(s ^ (s >> 32)));
        g = nerdle::best_guess_v2(all_eqs, cands, cset, N, hist, rng, use_twoply);
        if (g.empty()) {
            CountMap h(static_cast<size_t>(fail_b) + 1, 0);
            h[static_cast<size_t>(fail_b)] = cands.size();
            return h;
        }
    }

    const uint32_t all_green = nerdle::all_green_packed(N);
    const char* gc = g.c_str();

    std::unordered_map<uint32_t, std::vector<size_t>> by_fb;
    by_fb.reserve(cands.size() * 2);
    for (size_t t : cands) {
        uint32_t f = nerdle::compute_feedback_packed(gc, all_eqs[t].c_str(), N);
        by_fb[f].push_back(t);
    }

    std::vector<std::pair<uint32_t, std::vector<size_t>>> groups;
    groups.reserve(by_fb.size());
    for (auto& kv : by_fb) groups.push_back({kv.first, std::move(kv.second)});

    const int m = static_cast<int>(groups.size());
    if (m == 0) return CountMap();

    const bool next_opening = false;
    std::vector<CountMap> per(static_cast<size_t>(m));
    /* Intentionally serial: each child may call `best_guess_v2` which already uses
     * OpenMP over the full pool; nesting a second team here segfaults on large N. */
    for (int i = 0; i < m; i++) {
        uint32_t f = groups[static_cast<size_t>(i)].first;
        const std::vector<size_t>& G = groups[static_cast<size_t>(i)].second;
        if (f == all_green) {
            CountMap h(static_cast<size_t>(fail_b) + 1, 0);
            h[1] = G.size();
            per[static_cast<size_t>(i)] = std::move(h);
        } else {
            per[static_cast<size_t>(i)] = entropy_histo(all_eqs, G, N, max_tries, tries_left - 1, fail_b,
                                                        next_opening, use_twoply);
        }
    }

    CountMap out(static_cast<size_t>(fail_b) + 1, 0);
    for (int i = 0; i < m; i++) {
        uint32_t f = groups[static_cast<size_t>(i)].first;
        if (f == all_green)
            add_histo_shift(out, per[static_cast<size_t>(i)], 0, fail_b);
        else
            add_histo_shift(out, per[static_cast<size_t>(i)], 1, fail_b);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_entropy_aggregate")
                  << " <equations.txt>" << kUsageExtra;
        return 1;
    }
    const char* path = argv[1];
    bool use_twoply = false;
    for (int a = 2; a < argc; a++) {
        std::string s = argv[a];
        if (s == "--2ply" || s == "--twoply") use_twoply = true;
    }

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::vector<std::string> all_eqs;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) all_eqs.push_back(line);
    }
    f.close();
    if (all_eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }
    int N = static_cast<int>(all_eqs[0].size());
    for (const auto& s : all_eqs) {
        if (static_cast<int>(s.size()) != N) {
            std::cerr << "Inconsistent length.\n";
            return 1;
        }
    }

    const int max_tries = 6;
    const int fail_b = max_tries + 1;

    std::vector<size_t> cand(all_eqs.size());
    for (size_t i = 0; i < all_eqs.size(); i++) cand[i] = i;

    CountMap h;
#ifdef _OPENMP
    double t0 = omp_get_wtime();
#endif
    h = entropy_histo(all_eqs, cand, N, max_tries, max_tries, fail_b, true, use_twoply);
#ifdef _OPENMP
    double t1 = omp_get_wtime();
#endif

    size_t n = 0;
    for (size_t g : h) n += g;
    double mean = 0.0;
    for (int k = 1; k < static_cast<int>(h.size()); k++) {
        if (h[static_cast<size_t>(k)] == 0) continue;
        mean += (double)k * (double)h[static_cast<size_t>(k)] / (double)n;
    }

    std::cout << "Entropy policy — exact aggregate (" << n << " equations, " << N << "-tile, "
              << max_tries << " tries";
    if (use_twoply)
        std::cout << ", v2+2-ply tiebreak (seeded per subgame)";
    else
        std::cout << ", 1-ply entropy (same first guess as bench_nerdle, then v2 1-ply only)";
    std::cout << ")\n"
              << "  Mean total guesses: " << std::setprecision(8) << mean << "\n"
              << "  Distribution: ";
    for (int k = 1; k < static_cast<int>(h.size()); k++) {
        if (h[static_cast<size_t>(k)] > 0) std::cout << k << ":" << h[static_cast<size_t>(k)] << " ";
    }
    std::cout << "\n"
              << "  (bucket " << fail_b << " = failed to solve in " << max_tries
              << "): " << (h.size() > static_cast<size_t>(fail_b) ? h[static_cast<size_t>(fail_b)] : 0)
              << "\n";
#ifdef _OPENMP
    std::cout << "  Wall time: " << (t1 - t0) << " s\n";
#endif
    return 0;
}
