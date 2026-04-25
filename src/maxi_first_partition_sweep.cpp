/**
 * Exact first-guess sweep for Maxi Nerdle.
 *
 * Scores candidate first guesses by the number of distinct feedback partitions they
 * induce over the full solution pool. No n*n table is stored: each worker reuses a
 * 3^10 feedback-count buffer for one candidate at a time.
 *
 * Default mode sweeps all 10-distinct-symbol equations, which is the practical
 * "likely best first guesses" set identified by sampling.
 */
#include "nerdle_core.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

struct GuessStats {
    int idx = -1;
    int distinct = 0;
    int partitions = 0;
    int singletons = 0;
    int max_bucket = 0;
    double entropy = 0.0;
    double expected_bucket = 0.0;
};

std::string normalize_tiles(const std::string& s) {
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

int distinct_symbols(const std::string& s) {
    bool seen[256] = {};
    int count = 0;
    for (unsigned char c : s) {
        if (!seen[c]) {
            seen[c] = true;
            count++;
        }
    }
    return count;
}

GuessStats score_guess_reusing_buffers(const std::vector<std::string>& eqs, int N, int guess_idx,
                                       std::vector<int>& counts, std::vector<int>& touched) {
    touched.clear();
    const char* g = eqs[static_cast<size_t>(guess_idx)].c_str();
    for (const std::string& s : eqs) {
        const uint32_t code = nerdle::compute_feedback_packed(g, s.c_str(), N);
        int& c = counts[static_cast<size_t>(code)];
        if (c == 0)
            touched.push_back(static_cast<int>(code));
        c++;
    }

    GuessStats out;
    out.idx = guess_idx;
    out.distinct = distinct_symbols(eqs[static_cast<size_t>(guess_idx)]);
    out.partitions = static_cast<int>(touched.size());

    const double n = static_cast<double>(eqs.size());
    double sum_sq = 0.0;
    for (int code : touched) {
        const int c = counts[static_cast<size_t>(code)];
        const double p = static_cast<double>(c) / n;
        out.entropy -= p * std::log2(p);
        sum_sq += static_cast<double>(c) * static_cast<double>(c);
        if (c == 1)
            out.singletons++;
        if (c > out.max_bucket)
            out.max_bucket = c;
        counts[static_cast<size_t>(code)] = 0;
    }
    out.expected_bucket = sum_sq / n;
    return out;
}

bool better_for_output(const GuessStats& a, const GuessStats& b, const std::vector<std::string>& eqs) {
    if (a.partitions != b.partitions)
        return a.partitions > b.partitions;
    if (std::abs(a.entropy - b.entropy) > 1e-12)
        return a.entropy > b.entropy;
    if (a.max_bucket != b.max_bucket)
        return a.max_bucket < b.max_bucket;
    if (std::abs(a.expected_bucket - b.expected_bucket) > 1e-12)
        return a.expected_bucket < b.expected_bucket;
    return nerdle::equation_lex_less(eqs[static_cast<size_t>(a.idx)], eqs[static_cast<size_t>(b.idx)]);
}

void write_stats_tsv(const std::string& path, const std::vector<GuessStats>& stats,
                     const std::vector<std::string>& display_eqs) {
    std::ofstream out(path);
    if (!out) {
        std::cerr << "Cannot write " << path << "\n";
        std::exit(1);
    }
    out << "rank\tpartitions\tentropy_bits\tmax_bucket\tsingletons\texpected_bucket\tdistinct\tidx\tequation\n";
    out << std::setprecision(12);
    for (size_t i = 0; i < stats.size(); i++) {
        const GuessStats& s = stats[i];
        out << (i + 1) << '\t' << s.partitions << '\t' << s.entropy << '\t' << s.max_bucket << '\t'
            << s.singletons << '\t' << s.expected_bucket << '\t' << s.distinct << '\t' << s.idx
            << '\t' << display_eqs[static_cast<size_t>(s.idx)] << '\n';
    }
}

void write_winners(const std::string& path, const std::vector<GuessStats>& stats,
                   const std::vector<std::string>& display_eqs) {
    std::ofstream out(path);
    if (!out) {
        std::cerr << "Cannot write " << path << "\n";
        std::exit(1);
    }
    if (stats.empty()) {
        out << "# no candidates\n";
        return;
    }
    const int best = stats.front().partitions;
    out << "# max_partitions\t" << best << "\n";
    out << "# equation\n";
    for (const GuessStats& s : stats) {
        if (s.partitions != best)
            break;
        out << display_eqs[static_cast<size_t>(s.idx)] << '\n';
    }
}

void write_progress_snapshot(const std::string& path, int min_distinct, int max_distinct, int done, int total,
                            double wall_elapsed_s) {
    if (path.empty())
        return;
    const double rate = (wall_elapsed_s > 0.0) ? (static_cast<double>(done) / wall_elapsed_s) : 0.0;
    const double rem = static_cast<double>(total - done);
    const double eta_s = (rate > 0.0) ? (rem / rate) : 0.0;
    std::ostringstream ss;
    ss << std::setprecision(6) << std::fixed;
    ss << "stage_min_distinct\t" << min_distinct << "\n";
    ss << "stage_max_distinct\t" << max_distinct << "\n";
    ss << "candidates_total\t" << total << "\n";
    ss << "candidates_done\t" << done << "\n";
    ss << "wall_elapsed_s\t" << wall_elapsed_s << "\n";
    ss << "rate_guesses_per_s\t" << rate << "  # linear: done/elapsed\n";
    ss << "eta_remaining_s\t" << eta_s << "  # linear: (total-done)/rate\n";
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f)
            return;
        f << ss.str();
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::ofstream f(path, std::ios::trunc);
        if (f)
            f << ss.str();
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string pool_path = "data/equations_10.txt";
    std::string out_dir = "data/maxi_first_partition";
    std::string progress_file;
    int min_distinct = 10;
    int max_distinct = 10;
    int progress_every = 500;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--pool" || a == "-p") && i + 1 < argc) {
            pool_path = argv[++i];
        } else if ((a == "--out-dir" || a == "-o") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (a == "--min-distinct" && i + 1 < argc) {
            min_distinct = std::atoi(argv[++i]);
        } else if (a == "--max-distinct" && i + 1 < argc) {
            max_distinct = std::atoi(argv[++i]);
        } else if (a == "--progress-every" && i + 1 < argc) {
            progress_every = std::atoi(argv[++i]);
        } else if (a == "--progress-file" && i + 1 < argc) {
            progress_file = argv[++i];
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--pool data/equations_10.txt] [--out-dir data/maxi_first_partition]\n"
                      << "  [--min-distinct 10] [--max-distinct 10] [--progress-every 500]\n"
                      << "  [--progress-file PATH]  (tab-separated progress snapshot, overwrite each tick)\n";
            return 1;
        }
    }
    if (min_distinct > max_distinct)
        std::swap(min_distinct, max_distinct);

    std::ifstream f(pool_path);
    if (!f) {
        std::cerr << "Cannot open " << pool_path << "\n";
        return 1;
    }
    std::vector<std::string> display_eqs;
    std::vector<std::string> eqs;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) {
            display_eqs.push_back(line);
            eqs.push_back(normalize_tiles(line));
        }
    }
    if (eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }
    const int N = static_cast<int>(eqs[0].size());
    const int P = nerdle::pow3_table(N);
    if (P <= 0) {
        std::cerr << "Unsupported equation length " << N << "\n";
        return 1;
    }

    std::vector<size_t> distinct_pop(static_cast<size_t>(N + 1), 0);
    std::vector<int> candidates;
    candidates.reserve(eqs.size());
    for (int i = 0; i < static_cast<int>(eqs.size()); i++) {
        if (static_cast<int>(eqs[static_cast<size_t>(i)].size()) != N) {
            std::cerr << "Inconsistent equation length after normalizing tiles.\n";
            return 1;
        }
        const int d = distinct_symbols(eqs[static_cast<size_t>(i)]);
        if (d >= 0 && d <= N)
            distinct_pop[static_cast<size_t>(d)]++;
        if (d >= min_distinct && d <= max_distinct)
            candidates.push_back(i);
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        const int da = distinct_symbols(eqs[static_cast<size_t>(a)]);
        const int db = distinct_symbols(eqs[static_cast<size_t>(b)]);
        if (da != db)
            return da > db;
        return a < b;
    });

    std::cout.setf(std::ios::unitbuf);
    std::cout << "Pool: " << pool_path << "  equations=" << eqs.size() << "  N=" << N << "\n";
    std::cout << "Candidate distinct symbols: [" << min_distinct << ", " << max_distinct
              << "]  candidates=" << candidates.size() << "\n";
    std::cout << "Feedback buckets: 3^" << N << " = " << P << "\n";
#ifdef _OPENMP
    std::cout << "OpenMP max threads: " << omp_get_max_threads() << "\n";
#endif
    std::cout << "Distinct-count population:\n";
    for (int d = N; d >= 1; d--) {
        if (distinct_pop[static_cast<size_t>(d)] > 0)
            std::cout << "  " << d << ": " << distinct_pop[static_cast<size_t>(d)] << "\n";
    }
    if (candidates.empty()) {
        std::cerr << "No candidates selected.\n";
        return 1;
    }

    std::filesystem::create_directories(out_dir);
    write_progress_snapshot(progress_file, min_distinct, max_distinct, 0, static_cast<int>(candidates.size()), 0.0);

    std::vector<GuessStats> stats(candidates.size());
    std::atomic<int> completed{0};
    const auto wall0 = std::chrono::steady_clock::now();
#ifdef _OPENMP
    const double t0 = omp_get_wtime();
#pragma omp parallel
    {
        std::vector<int> counts(static_cast<size_t>(P), 0);
        std::vector<int> touched;
        touched.reserve(static_cast<size_t>(P));
#pragma omp for schedule(dynamic, 1)
#endif
        for (int i = 0; i < static_cast<int>(candidates.size()); i++) {
#ifndef _OPENMP
            static std::vector<int> counts;
            static std::vector<int> touched;
            if (counts.empty()) {
                counts.assign(static_cast<size_t>(P), 0);
                touched.reserve(static_cast<size_t>(P));
            }
#endif
            stats[static_cast<size_t>(i)] =
                score_guess_reusing_buffers(eqs, N, candidates[static_cast<size_t>(i)], counts, touched);
            const int done = ++completed;
            const bool tick = (progress_every > 0 && (done % progress_every == 0 || done == static_cast<int>(candidates.size()))) ||
                                (progress_every <= 0 && done == static_cast<int>(candidates.size()));
            if (tick) {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - wall0).count();
                const double rate = static_cast<double>(done) / (elapsed > 0.0 ? elapsed : 1.0);
                const double remaining = static_cast<double>(candidates.size() - done) / (rate > 0.0 ? rate : 1.0);
#ifdef _OPENMP
#pragma omp critical(progress_print)
#endif
                {
                    std::cout << "progress [" << min_distinct << "-" << max_distinct << "] " << done << "/"
                              << candidates.size() << "  rate=" << std::setprecision(4) << rate
                              << " guesses/s  eta_s=" << std::setprecision(0) << remaining << "\n";
                    write_progress_snapshot(progress_file, min_distinct, max_distinct, done,
                                           static_cast<int>(candidates.size()), elapsed);
                }
            }
        }
#ifdef _OPENMP
    }
    const double t1 = omp_get_wtime();
#endif
    {
        const auto wall_end = std::chrono::steady_clock::now();
        const double elapsed_total = std::chrono::duration<double>(wall_end - wall0).count();
        write_progress_snapshot(progress_file, min_distinct, max_distinct, static_cast<int>(candidates.size()),
                                static_cast<int>(candidates.size()), elapsed_total);
    }

    std::sort(stats.begin(), stats.end(), [&](const GuessStats& a, const GuessStats& b) {
        return better_for_output(a, b, eqs);
    });

    const std::string all_path = out_dir + "/all_candidates.tsv";
    const std::string winners_path = out_dir + "/winners.txt";
    const std::string summary_path = out_dir + "/summary.txt";
    const std::string max_part_path = out_dir + "/max_partitions.txt";
    write_stats_tsv(all_path, stats, display_eqs);
    write_winners(winners_path, stats, display_eqs);

    std::ofstream summary(summary_path);
    if (!summary) {
        std::cerr << "Cannot write " << summary_path << "\n";
        return 1;
    }
    const int best = stats.empty() ? 0 : stats.front().partitions;
    {
        std::ofstream mp(max_part_path, std::ios::trunc);
        if (mp)
            mp << best << "\n";
    }
    int tied = 0;
    for (const GuessStats& s : stats) {
        if (s.partitions != best)
            break;
        tied++;
    }
    summary << std::setprecision(12);
    summary << "pool\t" << pool_path << "\n";
    summary << "equations\t" << eqs.size() << "\n";
    summary << "N\t" << N << "\n";
    summary << "min_distinct\t" << min_distinct << "\n";
    summary << "max_distinct\t" << max_distinct << "\n";
    summary << "candidates\t" << candidates.size() << "\n";
    summary << "best_partitions\t" << best << "\n";
    summary << "best_tie_count\t" << tied << "\n";
    if (!stats.empty()) {
        const GuessStats& s = stats.front();
        summary << "top_equation\t" << display_eqs[static_cast<size_t>(s.idx)] << "\n";
        summary << "top_entropy_bits\t" << s.entropy << "\n";
        summary << "top_max_bucket\t" << s.max_bucket << "\n";
        summary << "top_expected_bucket\t" << s.expected_bucket << "\n";
    }
#ifdef _OPENMP
    summary << "wall_time_s\t" << (t1 - t0) << "\n";
#endif

    std::cout << std::setprecision(12);
    std::cout << "Done. best_partitions=" << best << " tied=" << tied << "\n";
    if (!stats.empty())
        std::cout << "Top equation: " << display_eqs[static_cast<size_t>(stats.front().idx)] << "\n";
    std::cout << "Wrote:\n"
              << "  " << winners_path << "\n"
              << "  " << all_path << "\n"
              << "  " << summary_path << "\n"
              << "  " << max_part_path << "\n";
#ifdef _OPENMP
    std::cout << "Wall time: " << (t1 - t0) << " s\n";
#endif
    return 0;
}
