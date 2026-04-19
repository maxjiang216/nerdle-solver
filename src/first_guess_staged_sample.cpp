/**
 * Staged sampling over all unique-symbol first guesses: estimate mean guesses (tiered + v2),
 * use normal-approx CIs on the mean, drop candidates dominated by the current sample leader,
 * increase sample size until full pool or one survivor.
 *
 * Sample is cumulative: stage sizes must be non-decreasing (e.g. 400,2000,6661); each round
 * adds new secrets from one global permutation (common random numbers across openings).
 *
 * Elimination: let b = argmin sample mean, U_b = mean_b + z*SE_b, L_i = mean_i - z*SE_i.
 * Drop i if L_i > U_b (i's lower CI edge above best's upper edge).
 *
 * Usage:
 *   ./first_guess_staged_sample [--pool data/equations_7.txt] [--fb-cache data/fb_7.bin]
 *       [--stages 400,2000,6661] [--append-full] [--confidence 0.95] [--no-2ply] [--threads 8]
 *
 * [--append-full] appends n_pool to the stage list if missing (run all secrets last).
 */

#include "bench_solve.hpp"
#include "nerdle_core.hpp"
#include "subgame_optimal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

using nerdle::subgame::try_load_fb_cache;

static bool all_symbols_unique(const std::string& s) {
    std::unordered_map<char, int> cnt;
    for (unsigned char c : s) {
        if (++cnt[c] > 1)
            return false;
    }
    return static_cast<int>(cnt.size()) == static_cast<int>(s.size());
}

static double Phi(double z) {
    return 0.5 * std::erfc(-z / std::sqrt(2.0));
}

/** Two-sided normal critical z for (1 - alpha) confidence interval: Phi(z)=1-alpha/2. */
static double z_two_sided(double confidence) {
    if (confidence <= 0.0 || confidence >= 1.0)
        return 1.96;
    double target = 1.0 - (1.0 - confidence) * 0.5;
    double lo = 0.0, hi = 12.0;
    for (int it = 0; it < 60; it++) {
        double mid = 0.5 * (lo + hi);
        if (Phi(mid) < target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

struct Cand {
    std::string eq;
};

struct Acc {
    double sum = 0.0;
    double sumsq = 0.0;
    long long n = 0;
    void add(int g) {
        sum += static_cast<double>(g);
        sumsq += static_cast<double>(g) * static_cast<double>(g);
        n++;
    }
    double mean() const { return n > 0 ? sum / static_cast<double>(n) : 0.0; }
    double stderr_mean() const {
        if (n < 2)
            return 1e9;
        double v = (sumsq - sum * sum / static_cast<double>(n)) / static_cast<double>(n - 1);
        if (v < 0.0)
            v = 0.0;
        return std::sqrt(v / static_cast<double>(n));
    }
};

static std::vector<int> parse_stages(const std::string& s, int n_pool, bool append_full) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty())
            continue;
        int v = std::atoi(part.c_str());
        if (v > 0)
            out.push_back(std::min(v, n_pool));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty())
        out = {std::min(400, n_pool), std::min(2000, n_pool), n_pool};
    if (append_full && !out.empty() && out.back() < n_pool)
        out.push_back(n_pool);
    return out;
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf << std::fixed << std::setprecision(6);
    std::string pool_path = "data/equations_7.txt";
    std::string fb_cache = "data/fb_7.bin";
    std::string stages_str = "400,2000,6661";
    double confidence = 0.95;
    bool v2_twoply = false;
    bool append_full = false;
    int threads = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--stages" && i + 1 < argc)
            stages_str = argv[++i];
        else if (a.rfind("--confidence=", 0) == 0 && a.size() > 13)
            confidence = std::atof(a.c_str() + 13);
        else if (a == "--confidence" && i + 1 < argc)
            confidence = std::atof(argv[++i]);
        else if (a == "--v2-2ply")
            v2_twoply = true;
        else if (a == "--no-2ply")
            v2_twoply = false;
        else if (a == "--threads" && i + 1 < argc)
            threads = std::atoi(argv[++i]);
        else if (a == "--append-full")
            append_full = true;
    }

#ifdef _OPENMP
    if (threads > 0)
        omp_set_num_threads(threads);
#endif

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

    std::vector<Cand> survivors;
    for (int i = 0; i < n_pool; i++) {
        if (all_symbols_unique(eqs[static_cast<size_t>(i)]))
            survivors.push_back({eqs[static_cast<size_t>(i)]});
    }

    std::vector<int> stage_ns = parse_stages(stages_str, n_pool, append_full);
    const double z = z_two_sided(confidence);
    const int max_tries = (N == 10) ? nerdle_bench::MAXI_TRIES : 6;
    const nerdle_bench::Selector sel = nerdle_bench::Selector::V2;

    std::vector<int> perm(n_pool);
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937 rng_perm(0xD0E1A2D5u);
    std::shuffle(perm.begin(), perm.end(), rng_perm);

    std::vector<Acc> acc(survivors.size());
    int prev_n = 0;

    std::cout << "Pool " << pool_path << "  n=" << n_pool << "  N=" << N << "\n";
    std::cout << "Policy: solve_one_tiered, v2 twoply=" << (v2_twoply ? "on" : "off")
              << "  max_tries=" << max_tries << "\n";
    std::cout << "Unique-symbol first guesses: " << survivors.size() << "\n";
    std::cout << "Confidence level (two-sided normal CI on mean): " << confidence * 100.0
              << "%  z=" << z << "\n";
    std::cout << "Cumulative stage sample sizes: ";
    for (size_t i = 0; i < stage_ns.size(); i++)
        std::cout << stage_ns[i] << (i + 1 < stage_ns.size() ? ", " : "");
    std::cout << "\n\n";

    for (size_t st = 0; st < stage_ns.size(); st++) {
        const int cur_n = stage_ns[st];
        if (cur_n <= prev_n) {
            std::cout << "Stage " << (st + 1) << ": skip (cumulative n " << cur_n
                      << " <= previous " << prev_n << ")\n\n";
            continue;
        }

        const size_t n_in = survivors.size();
        auto t0 = std::chrono::steady_clock::now();

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1)
#endif
        for (size_t i = 0; i < survivors.size(); i++) {
            for (int t = prev_n; t < cur_n; t++) {
                int si = perm[static_cast<size_t>(t)];
                uint64_t seed = 0x9E3779B97F4A7C15ULL ^
                                (uint64_t)(uint32_t)(si * 1315423911u + (uint32_t)(i * 17u + 3u));
                int g = nerdle_bench::solve_one_tiered(eqs[static_cast<size_t>(si)], eqs,
                                                       survivors[i].eq, N, max_tries, sel, seed,
                                                       &fb_full, v2_twoply);
                acc[i].sum += static_cast<double>(g);
                acc[i].sumsq += static_cast<double>(g) * static_cast<double>(g);
                acc[i].n++;
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();

        std::vector<double> means(n_in), ses(n_in);
        size_t best_i = 0;
        for (size_t i = 0; i < n_in; i++) {
            means[i] = acc[i].mean();
            ses[i] = acc[i].stderr_mean();
            if (means[i] < means[best_i] - 1e-15 ||
                (std::abs(means[i] - means[best_i]) < 1e-15 && survivors[i].eq < survivors[best_i].eq))
                best_i = i;
        }

        const double mean_b = means[best_i];
        const double se_b = ses[best_i];
        const double U_b = mean_b + z * se_b;

        std::vector<Cand> next_surv;
        std::vector<Acc> next_acc;
        next_surv.reserve(n_in);
        next_acc.reserve(n_in);
        size_t eliminated = 0;
        for (size_t i = 0; i < n_in; i++) {
            const double L_i = means[i] - z * ses[i];
            if (L_i <= U_b) {
                next_surv.push_back(survivors[i]);
                next_acc.push_back(acc[i]);
            } else
                eliminated++;
        }

        std::cout << "=== Stage " << (st + 1) << " / " << stage_ns.size() << " ===\n";
        std::cout << "Cumulative secrets evaluated per opening: " << cur_n << " (added " << (cur_n - prev_n)
                  << " this stage)\n";
        std::cout << "Wall time (stage): " << sec << " s\n";
        std::cout << "Inference: i.i.d. sample mean per opening; SE = s/sqrt(n). "
                     "Normal approx (not full multinomial on the guess-count histogram).\n";
        std::cout << "Elimination: L_i = mean_i - z*SE_i, U_best = mean_best + z*SE_best; "
                     "drop i if L_i > U_best.\n";
        std::cout << "Survivors entering stage: " << n_in << "\n";
        std::cout << "Eliminated this stage: " << eliminated << "\n";
        std::cout << "Survivors after stage: " << next_surv.size() << "\n";
        std::cout << "Sample leader: mean=" << mean_b << "  SE=" << se_b << "  opening \""
                  << survivors[best_i].eq << "\"\n";
        std::cout << "Leader interval: [" << (mean_b - z * se_b) << ", " << U_b << "]\n";

        if (next_surv.size() <= 15) {
            std::vector<double> m_rem(next_surv.size()), s_rem(next_surv.size());
            for (size_t i = 0; i < next_surv.size(); i++) {
                m_rem[i] = next_acc[i].mean();
                s_rem[i] = next_acc[i].stderr_mean();
            }
            std::cout << "Remaining openings (mean, SE):\n";
            std::vector<size_t> ord(next_surv.size());
            std::iota(ord.begin(), ord.end(), 0);
            std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
                if (std::abs(m_rem[a] - m_rem[b]) > 1e-12)
                    return m_rem[a] < m_rem[b];
                return next_surv[a].eq < next_surv[b].eq;
            });
            for (size_t k = 0; k < ord.size(); k++) {
                size_t i = ord[k];
                std::cout << "  " << m_rem[i] << "  SE=" << s_rem[i] << "  " << next_surv[i].eq << "\n";
            }
        }
        std::cout << "\n";

        survivors = std::move(next_surv);
        acc = std::move(next_acc);
        prev_n = cur_n;

        if (survivors.size() <= 1)
            break;
    }

    std::cout << "=== Final ===\n";
    std::cout << "Survivors: " << survivors.size() << "\n";
    for (size_t i = 0; i < survivors.size(); i++)
        std::cout << "  " << acc[i].mean() << "  (n=" << acc[i].n << " secrets)  " << survivors[i].eq
                  << "\n";

    return 0;
}
