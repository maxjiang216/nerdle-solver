/**
 * Partition strategy: root policy (EV + theoretical distribution) and optional exact aggregate
 * over all secrets in the pool (same recursive tree as the former ./bench_partition_aggregate).
 *
 * Replaces: legacy partition_report (model-only) + bench_partition_aggregate (exact only).
 * Identical behavior at tie_depth=0, no opening: old partition_report + old bench.
 */

#include "nerdle_core.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

/* Same caps as PartitionGreedyEvaluator::build_feedback_matrix (Report) — one shared n×n table. */
/* Why build it at all?  `max_tries=6` is the *play depth* — not "6 table lookups per pair". At each
    tree node, `PartitionGreedyEvaluator` may read many (g,s) pairs when comparing guesses or
    simulating; O(1) `feedback_code` from a one-time n² precompute is usually a big win for n
    in the high hundreds / low thousands, until RAM caps force on-the-fly `compute_feedback_packed`. */
static bool pool_fits_full_feedback_table(int n) {
    if (n <= 0) return false;
    const int cap_n = 20000;
    const size_t cap_bytes = 1400ull * 1024 * 1024;
    if (n > cap_n) return false;
    const size_t n2 = static_cast<size_t>(n) * static_cast<size_t>(n);
    return n2 * sizeof(uint32_t) <= cap_bytes;
}

static void build_feedback_table(const std::vector<std::string>& eqs, int N,
                                 std::vector<uint32_t>& out) {
    const int n = static_cast<int>(eqs.size());
    out.resize(static_cast<size_t>(n) * static_cast<size_t>(n));
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 16)
#endif
    for (int g = 0; g < n; g++) {
        for (int s = 0; s < n; s++) {
            out[static_cast<size_t>(g) * static_cast<size_t>(n) + static_cast<size_t>(s)] =
                nerdle::compute_feedback_packed(
                    eqs[static_cast<size_t>(g)].c_str(), eqs[static_cast<size_t>(s)].c_str(), N);
        }
    }
}

struct Progress {
    std::atomic<uint64_t> calls{0};
    bool enabled = false;
    std::chrono::steady_clock::time_point t0{};

    void on_enter() {
        if (!enabled) return;
        const uint64_t c = ++calls;
        if (c == 1u)
            t0 = std::chrono::steady_clock::now();
        if (c % 200000u == 0u || c == 1u) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                                   .count();
            std::cerr << "  [progress] partition_histo subproblems: " << c << "  elapsed " << s
                      << " s (shared n×n feedback; tie_depth as set)\n"
                      << std::flush;
        }
    }
};

static Progress* g_progress = nullptr;

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

using CountMap = std::vector<size_t>;

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
    int fail_b, int root_force_idx, bool is_root, int tie_depth, const std::vector<uint32_t>* shared_full_fb,
    std::string* out_root_first_guess) {
    if (g_progress)
        g_progress->on_enter();
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
        if (out_root_first_guess)
            *out_root_first_guess = all_eqs[static_cast<size_t>(root_force_idx)];
    } else {
        g_owned = nerdle::best_guess_partition_policy(
            all_eqs, cands, N, tries_left, tie_depth, nerdle::PartitionFbBudget::Report, shared_full_fb);
        if (is_root && out_root_first_guess)
            *out_root_first_guess = g_owned;
        g_c = g_owned.c_str();
    }
    const uint32_t all_green = nerdle::all_green_packed(N);

    std::unordered_map<uint32_t, std::vector<size_t>> by_fb;
    by_fb.reserve(cands.size() * 2);
    for (size_t t : cands) {
        uint32_t f = nerdle::compute_feedback_packed(g_c, all_eqs[t].c_str(), N);
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
            CountMap h(static_cast<size_t>(fail_b) + 1, 0);
            h[1] = G.size();
            per[static_cast<size_t>(i)] = std::move(h);
        } else {
            per[static_cast<size_t>(i)] = partition_histo(
                all_eqs, G, N, tries_left - 1, fail_b, -1, false, tie_depth, shared_full_fb, nullptr);
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

static CountMap partition_histo_local_pool(const std::vector<std::string>& eqs, int N, int tries_left,
                                           int fail_b, int tie_depth, bool use_child_nxn) {
    std::vector<size_t> all_idx(eqs.size());
    for (size_t i = 0; i < all_idx.size(); i++)
        all_idx[i] = i;

    std::vector<uint32_t> child_fb;
    const std::vector<uint32_t>* pfb = nullptr;
    if (use_child_nxn && pool_fits_full_feedback_table(static_cast<int>(eqs.size()))) {
        build_feedback_table(eqs, N, child_fb);
        pfb = &child_fb;
    }
    return partition_histo(eqs, all_idx, N, tries_left, fail_b, -1, false, tie_depth, pfb, nullptr);
}

static CountMap fixed_opening_histo_with_child_tables(const std::vector<std::string>& all_eqs, int N,
                                                      int max_tries, int fail_b, int root_force_idx,
                                                      int tie_depth, bool use_child_nxn, bool show_bucket_progress,
                                                      std::string* out_root_first_guess) {
    if (g_progress)
        g_progress->on_enter();

    const char* g = all_eqs[static_cast<size_t>(root_force_idx)].c_str();
    if (out_root_first_guess)
        *out_root_first_guess = all_eqs[static_cast<size_t>(root_force_idx)];

    const uint32_t all_green = nerdle::all_green_packed(N);
    std::cerr << "Root opening feedback scan: " << all_eqs.size() << " rows…\n" << std::flush;
    const auto t_root_scan0 = std::chrono::steady_clock::now();
    std::unordered_map<uint32_t, std::vector<size_t>> by_fb;
    by_fb.reserve(all_eqs.size() * 2);
    for (size_t i = 0; i < all_eqs.size(); i++) {
        uint32_t f = nerdle::compute_feedback_packed(g, all_eqs[i].c_str(), N);
        by_fb[f].push_back(i);
    }
    {
        const double root_scan_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_root_scan0).count();
        std::cerr << "  Root feedback scan done: " << root_scan_s << " s, " << by_fb.size()
                  << " non-empty root-feedback buckets\n"
                  << std::flush;
    }

    std::vector<std::pair<uint32_t, std::vector<size_t>>> groups;
    groups.reserve(by_fb.size());
    for (auto& kv : by_fb)
        groups.push_back({kv.first, std::move(kv.second)});

    size_t largest = 0;
    uint64_t sum_sq = 0;
    for (const auto& kv : groups) {
        const size_t n = kv.second.size();
        if (kv.first != all_green) {
            largest = std::max(largest, n);
            sum_sq += static_cast<uint64_t>(n) * static_cast<uint64_t>(n);
        }
    }
    std::cerr << "Fixed opening creates " << groups.size()
              << " non-empty feedback buckets; largest non-green bucket=" << largest
              << ", sum(non-green n^2)=" << sum_sq << ".\n"
              << (use_child_nxn
                     ? "Exact aggregate: per-bucket subpool + build child n×n when size/cap allow.\n"
                     : "Exact aggregate: per-bucket subpool, child n×n **disabled** (--no-child-tables); "
                       "slower, less RAM per bucket.\n")
              << std::flush;

    const int ngroup = static_cast<int>(groups.size());
    std::vector<CountMap> per(static_cast<size_t>(ngroup));
    std::atomic<int> completed{0};
    const std::chrono::steady_clock::time_point t_bucket0 = std::chrono::steady_clock::now();
#if defined(_OPENMP)
    const bool par = (ngroup > 1) && (omp_get_level() == 0);
#pragma omp parallel for schedule(dynamic) if (par)
#endif
    for (int i = 0; i < ngroup; i++) {
        const uint32_t f = groups[static_cast<size_t>(i)].first;
        const std::vector<size_t>& G = groups[static_cast<size_t>(i)].second;
        if (f == all_green) {
            CountMap h(static_cast<size_t>(fail_b) + 1, 0);
            h[1] = G.size();
            per[static_cast<size_t>(i)] = std::move(h);
        } else {
            std::vector<std::string> child_eqs;
            child_eqs.reserve(G.size());
            for (size_t idx : G)
                child_eqs.push_back(all_eqs[idx]);
            per[static_cast<size_t>(i)] =
                partition_histo_local_pool(child_eqs, N, max_tries - 1, fail_b, tie_depth, use_child_nxn);
        }

        if (show_bucket_progress) {
            const int d = completed.fetch_add(1, std::memory_order_relaxed) + 1;
            const int step = std::max(1, ngroup / 200);
            if (d == 1 || d == ngroup || (d % step == 0)) {
                const double el =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t_bucket0).count();
                const double rate = (el > 0.0) ? (static_cast<double>(d) / el) : 0.0;
                const double eta = (rate > 0.0) ? (static_cast<double>(ngroup - d) / rate) : 0.0;
#if defined(_OPENMP)
#pragma omp critical(fixed_opening_progress)
#endif
                {
                    std::cerr << "  [progress] fixed-opening buckets: " << d << "/" << ngroup
                              << "  elapsed_s=" << std::setprecision(4) << el << "  rate_buckets/s=" << rate
                              << "  eta_s(rough)=" << std::setprecision(3) << eta << "\n"
                              << std::flush;
                }
            }
        }
    }

    CountMap out(static_cast<size_t>(fail_b) + 1, 0);
    for (int i = 0; i < static_cast<int>(groups.size()); i++) {
        if (groups[static_cast<size_t>(i)].first == all_green)
            add_histo_shift(out, per[static_cast<size_t>(i)], 0, fail_b);
        else
            add_histo_shift(out, per[static_cast<size_t>(i)], 1, fail_b);
    }
    return out;
}

static void print_root_model(const nerdle::PartitionSolveDist& val, const std::vector<std::string>& eqs,
                            int max_tries) {
    double solved_p = 0.0;
    double ev_solved_part = 0.0;
    for (int t = 1; t <= max_tries && t < static_cast<int>(val.solve_at.size()); t++)
        ev_solved_part += static_cast<double>(t) * val.solve_at[static_cast<size_t>(t)];
    for (int t = 1; t <= max_tries && t < static_cast<int>(val.solve_at.size()); t++)
        solved_p += val.solve_at[static_cast<size_t>(t)];
    double fail_p = std::max(0.0, 1.0 - solved_p);
    double evm = ev_solved_part + static_cast<double>(max_tries + 1) * fail_p;
    std::cout << "EV: " << std::setprecision(10) << evm
              << "  (PartitionGreedyEvaluator at root; uniform on secrets)\n";
    std::cout << "Distribution (model, rounded to pool size):\n";
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
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string opening;
    int max_tries = 6;
    int tie_depth = 0;
    size_t max_pool = 0;
    bool show_progress = false;
    bool do_exact_aggregate = true;
    bool use_child_nxn = true;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc) {
            path = argv[++i];
        } else if (a == "--opening" && i + 1 < argc) {
            opening = argv[++i];
        } else if (a == "--tries" && i + 1 < argc) {
            max_tries = std::atoi(argv[++i]);
        } else if (a == "--tie-depth" && i + 1 < argc) {
            tie_depth = std::atoi(argv[++i]);
        } else if (a == "--max" && i + 1 < argc) {
            max_pool = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (a == "--progress" || a == "-v") {
            show_progress = true;
        } else if (a == "--no-exact-aggregate" || a == "--model-only") {
            do_exact_aggregate = false;
        } else if (a == "--no-child-tables") {
            use_child_nxn = false;
        } else if (a[0] == '-') {
            std::cerr
                << "Usage: " << (argc ? argv[0] : "partition_report")
                << " [--pool PATH]  [PATH]  (default pool: data/equations_7.txt)\n"
                << "  [--tries 6] [--tie-depth 0]  (1,2,… = recursive tie-break among equal max-partition "
                   "guesses)\n"
                << "  [--max N]  (first N lines only)\n"
                << "  [--opening GUESS]  (root first guess; must be a row in the pool, after Maxi "
                   "normalize)\n"
                << "  [--progress|-v]  (with exact aggregate: subproblem counter on stderr)\n"
                << "  [--no-exact-aggregate]  (root EV/distribution only; no exact histogram)\n"
                << "  [--no-child-tables]  (fixed opening + large pool: skip per-bucket n×n; less RAM, slower)\n"
                << "  Maxi: UTF-8 ²/³ normalized in the pool the same as other tools.\n";
            return 1;
        } else {
            if (path.empty())
                path = a;
            else {
                std::cerr << "Unexpected: " << a << "\n";
                return 1;
            }
        }
    }
    if (path.empty())
        path = "data/equations_7.txt";

    std::vector<std::string> eqs;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) eqs.push_back(line);
    }
    f.close();
    if (eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }
    if (max_pool > 0 && eqs.size() > max_pool)
        eqs.resize(max_pool);

    int N = static_cast<int>(eqs[0].size());
    if (N == 10) {
        for (auto& s : eqs) s = normalize_maxi_tiles(std::move(s));
    }
    for (const auto& s : eqs) {
        if (static_cast<int>(s.size()) != N) {
            std::cerr << "Inconsistent length.\n";
            return 1;
        }
    }

    int root_force_idx = -1;
    if (!opening.empty()) {
        const std::string want = (N == 10) ? normalize_maxi_tiles(opening) : opening;
        for (size_t i = 0; i < eqs.size(); i++) {
            if (eqs[i] == want) {
                root_force_idx = static_cast<int>(i);
                break;
            }
        }
        if (root_force_idx < 0) {
            std::cerr << "--opening not found in pool (after normalize): " << opening << "\n";
            return 1;
        }
    }

    const int fail_b = max_tries + 1;
    const int npool = static_cast<int>(eqs.size());
    std::vector<size_t> all_idx(eqs.size());
    for (size_t i = 0; i < all_idx.size(); i++) all_idx[i] = i;

    std::vector<uint32_t> full_fb;
    const std::vector<uint32_t>* pfb = nullptr;
    if (pool_fits_full_feedback_table(npool)) {
        std::cerr << "Prebuilding shared " << npool << "×" << npool
                  << " feedback table (PartitionGreedyEvaluator + exact aggregate)…\n"
                  << std::flush;
        const auto t_fb0 = std::chrono::steady_clock::now();
        build_feedback_table(eqs, N, full_fb);
        std::cerr << "  Feedback table wall time: "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - t_fb0).count()
                  << " s\n" << std::flush;
        pfb = &full_fb;
    } else {
        std::cerr << "Pool " << npool
                  << " does not fit the n×n table cap; feedback computed on the fly (slower).\n"
                  << std::flush;
    }

    const bool use_child_tables_for_opening = (root_force_idx >= 0 && pfb == nullptr);
    const auto t_ev0 = std::chrono::steady_clock::now();
    nerdle::PartitionSolveDist val{};
    int g = root_force_idx;
    size_t root_memo_states = 0;
    std::chrono::steady_clock::time_point t_ev1 = t_ev0;
    std::chrono::steady_clock::time_point t_ev2 = t_ev0;
    if (!use_child_tables_for_opening) {
        nerdle::PartitionGreedyEvaluator ev(
            eqs, N, max_tries, tie_depth, nerdle::PartitionFbBudget::Report, pfb);
        ev.build_feedback_matrix();
        t_ev1 = std::chrono::steady_clock::now();
        if (root_force_idx < 0) {
            g = ev.best_guess_index(all_idx, max_tries, &val);
        } else {
            val = ev.distribution_with_fixed_first_guess(root_force_idx);
        }
        t_ev2 = std::chrono::steady_clock::now();
        root_memo_states = ev.memo_size();
    }
    std::cout.setf(std::ios::unitbuf);
    std::cout << "Pool: " << path << "  n=" << eqs.size() << "  N=" << N << "  max_tries=" << max_tries
              << "\n";
    std::cout << "Tie depth: " << tie_depth
              << "  (0 = max distinct feedbacks only; 1+ adds recursive distribution tie-break)\n";
    if (use_child_tables_for_opening) {
        std::cout << "Feedback matrix: skipped root n×n (fixed opening on large pool; per-bucket child tables "
                  << (use_child_nxn ? "on when they fit" : "disabled (--no-child-tables)")
                  << ")\n";
        std::cout << "Policy eval (root model): skipped; exact aggregate below computes the fixed-opening "
                     "distribution.\n";
    } else {
        std::cout << "Feedback matrix: " << std::chrono::duration<double>(t_ev1 - t_ev0).count() << " s"
                  << (pfb ? " (full n×n)\n" : " (on-the-fly; no full table)\n");
        std::cout << "Policy eval (root): " << std::chrono::duration<double>(t_ev2 - t_ev1).count() << " s  "
                  << "memo_states=" << root_memo_states << "\n";
    }

    if (root_force_idx < 0)
        std::cout << "First guess (from policy, tie_depth=" << tie_depth << "): " << eqs[static_cast<size_t>(g)]
                  << "\n";
    else
        std::cout << "First guess (fixed, --opening): " << eqs[static_cast<size_t>(g)] << "\n";

    if (root_force_idx >= 0 && !use_child_tables_for_opening)
        std::cout << "EV / distribution (fixed root; same tie rules on later moves as --tie-depth):\n";
    if (!use_child_tables_for_opening)
        print_root_model(val, eqs, max_tries);

    if (!do_exact_aggregate) {
        return 0;
    }

    Progress progress_state;
    if (show_progress) {
        progress_state.enabled = true;
        g_progress = &progress_state;
    }
    const auto t_agg0 = std::chrono::steady_clock::now();
    std::string exact_first;
    CountMap h;
    if (use_child_tables_for_opening) {
        const bool show_bucket_progress = true;
        h = fixed_opening_histo_with_child_tables(eqs, N, max_tries, fail_b, root_force_idx, tie_depth,
                                                  use_child_nxn, show_bucket_progress, &exact_first);
    } else {
        h = partition_histo(
            eqs, all_idx, N, max_tries, fail_b, root_force_idx, true, tie_depth, pfb, &exact_first);
    }
    g_progress = nullptr;
    const double agg_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_agg0).count();

    size_t ntot = 0;
    for (size_t g2 : h) ntot += g2;
    double mean = 0.0;
    for (int k = 1; k < static_cast<int>(h.size()); k++) {
        if (h[static_cast<size_t>(k)] == 0) continue;
        mean += (double)k * (double)h[static_cast<size_t>(k)] / (double)ntot;
    }

    std::cout << "\n--- Exact aggregate over all " << ntot << " secrets (same policy; no sampling) ---\n";
    std::cout << "First guess in walk: " << exact_first
              << (root_force_idx < 0 ? "  (per --tie-depth at each node)\n" : "  (root from --opening)\n");
    if (root_force_idx < 0 && exact_first != eqs[static_cast<size_t>(g)])
        std::cerr << "Warning: model first-guess index " << g << " string differs from exact-walk first string "
                     "(duplicate rows in pool?)\n";
    std::cout << "Mean total guesses: " << std::setprecision(10) << mean << "\n"
              << "Exact distribution: ";
    for (int k = 1; k < static_cast<int>(h.size()); k++) {
        if (h[static_cast<size_t>(k)] > 0) std::cout << k << ":" << h[static_cast<size_t>(k)] << " ";
    }
    std::cout << "\n  (bucket " << fail_b << " = not solved in " << max_tries
              << "): " << (h.size() > static_cast<size_t>(fail_b) ? h[static_cast<size_t>(fail_b)] : 0) << "\n";
    std::cout << "Exact-aggregate wall time: " << agg_s << " s\n";
    return 0;
}
