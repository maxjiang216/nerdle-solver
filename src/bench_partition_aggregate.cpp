/**
 * Exact guess-count distribution for the partition strategy, without one simulation per
 * target: recursively partition the candidate set by feedback, merge child histograms.
 * Child subproblems are independent and can be evaluated in parallel.
 *
 * See nerdle::best_guess_partition_policy in nerdle_core.hpp (same first guess and tie
 * order as solve_one with PlayStrategy::Partition), unless --opening fixes the root guess
 * (e.g. Maxi sweep winner 58+2-13=47). Later moves use greedy partition (tie_depth 0),
 * matching ./partition_report --tie-depth 0 style play.
 */

#include "nerdle_core.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using CountMap = std::vector<size_t>;

static std::string normalize_maxi_tiles(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xc2 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0xb2) {
                out.push_back('\x01');
                i++;
                continue;
            }
            if (d == 0xb3) {
                out.push_back('\x02');
                i++;
                continue;
            }
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

/** Add child histogram `b` into `acc` with this state's guess counting as +`shift` plies. */
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

static CountMap partition_histo(
    const std::vector<std::string>& all_eqs, const std::vector<size_t>& cands, int N, int tries_left,
    int fail_b, int root_force_idx, bool is_root) {
    if (cands.empty())
        return CountMap();
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

    std::string g_owned;
    const char* g_c;
    if (is_root && root_force_idx >= 0) {
        g_c = all_eqs[static_cast<size_t>(root_force_idx)].c_str();
    } else {
        g_owned = nerdle::best_guess_partition_policy(all_eqs, cands, N, tries_left);
        g_c = g_owned.c_str();
    }
    const uint32_t all_green = nerdle::all_green_packed(N);

    std::unordered_map<uint32_t, std::vector<size_t>> by_fb;
    by_fb.reserve(cands.size() * 2);
    for (size_t t : cands) {
        uint32_t f =
            nerdle::compute_feedback_packed(g_c, all_eqs[t].c_str(), N);
        by_fb[f].push_back(t);
    }

    std::vector<std::pair<uint32_t, std::vector<size_t>>> groups;
    groups.reserve(by_fb.size());
    for (auto& kv : by_fb)
        groups.push_back({kv.first, std::move(kv.second)});

    const int m = static_cast<int>(groups.size());
    if (m == 0)
        return CountMap();

    std::vector<CountMap> per(static_cast<size_t>(m));
#if defined(_OPENMP)
    const bool par = (m > 1) && (omp_get_level() == 0);
#pragma omp parallel for schedule(dynamic) if (par)
#endif
    for (int i = 0; i < m; i++) {
        uint32_t f = groups[static_cast<size_t>(i)].first;
        const std::vector<size_t>& G = groups[static_cast<size_t>(i)].second;
        if (f == all_green) {
            // Only the guessed row gives all-greens vs g; one turn to win.
            CountMap h(static_cast<size_t>(fail_b) + 1, 0);
            h[1] = G.size();
            per[static_cast<size_t>(i)] = std::move(h);
        } else {
            per[static_cast<size_t>(i)] =
                partition_histo(all_eqs, G, N, tries_left - 1, fail_b, -1, false);
        }
    }

    CountMap out(static_cast<size_t>(fail_b) + 1, 0);
    for (int i = 0; i < m; i++) {
        uint32_t f = groups[static_cast<size_t>(i)].first;
        if (f == all_green) {
            add_histo_shift(out, per[static_cast<size_t>(i)], 0, fail_b);
        } else {
            add_histo_shift(out, per[static_cast<size_t>(i)], 1, fail_b);
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string opening;
    size_t max_lines = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc) {
            path = argv[++i];
        } else if (a == "--opening" && i + 1 < argc) {
            opening = argv[++i];
        } else if (a == "--max" && i + 1 < argc) {
            max_lines = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (a[0] != '-') {
            if (path.empty())
                path = a;
            else {
                std::cerr << "Unexpected: " << a << "\n";
                return 1;
            }
        } else {
            std::cerr << "Usage: " << (argc ? argv[0] : "bench_partition_aggregate")
                      << " <equations.txt>\n"
                      << "       " << (argc ? argv[0] : "tool")
                      << " --pool PATH [--opening GUESS] [--max N]\n"
                      << "  Exact guess-count histogram for partition policy (greedy tie_depth=0);\n"
                      << "  --opening forces that pool row as the first guess (root only).\n"
                      << "  Maxi (N=10): UTF-8 ²/³ normalized to internal tiles in the pool.\n";
            return 1;
        }
    }
    if (path.empty()) {
        std::cerr << "Usage: " << (argc ? argv[0] : "bench_partition_aggregate")
                  << " <equations.txt>  |  --pool PATH [--opening GUESS] [--max N]\n";
        return 1;
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
        if (max_lines > 0 && all_eqs.size() >= max_lines)
            break;
    }
    f.close();
    if (all_eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }
    int N = static_cast<int>(all_eqs[0].size());
    if (N == 10) {
        for (auto& s : all_eqs)
            s = normalize_maxi_tiles(std::move(s));
    }
    for (const auto& s : all_eqs) {
        if (static_cast<int>(s.size()) != N) {
            std::cerr << "Inconsistent length.\n";
            return 1;
        }
    }

    int root_force_idx = -1;
    if (!opening.empty()) {
        const std::string want = (N == 10) ? normalize_maxi_tiles(opening) : opening;
        for (size_t i = 0; i < all_eqs.size(); i++) {
            if (all_eqs[i] == want) {
                root_force_idx = static_cast<int>(i);
                break;
            }
        }
        if (root_force_idx < 0) {
            std::cerr << "--opening not found in pool (after normalize): " << opening << "\n";
            return 1;
        }
    }

    const int max_tries = 6; // Micro…Maxi interactive / bench
    const int fail_b = max_tries + 1;

    std::vector<size_t> cand(all_eqs.size());
    for (size_t i = 0; i < all_eqs.size(); i++) cand[i] = i;

    CountMap h;
#ifdef _OPENMP
    double t0 = omp_get_wtime();
#endif
    h = partition_histo(all_eqs, cand, N, max_tries, fail_b, root_force_idx, true);
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

    std::cout << "Partition policy — exact aggregate (" << n << " equations, " << N
              << "-tile, " << max_tries << " tries)\n";
    if (root_force_idx >= 0)
        std::cout << "  Root first guess (fixed): " << opening << "\n";
    std::cout << "  Mean total guesses: " << std::setprecision(8) << mean << "\n"
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
