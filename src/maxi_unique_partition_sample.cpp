/**
 * Exact first-guess partition/entropy stats for a small, reproducible sample of
 * Maxi equations grouped by number of distinct symbols.
 *
 * This is meant to test whether "more unique symbols" is a good heuristic before
 * spending EC2 time on an exact sweep over every all-unique candidate.
 */
#include "nerdle_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
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

GuessStats score_guess(const std::vector<std::string>& eqs, int N, int guess_idx) {
    const int P = nerdle::pow3_table(N);
    std::vector<int> counts(static_cast<size_t>(P), 0);
    std::vector<int> touched;
    touched.reserve(static_cast<size_t>(P));

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
    }
    out.expected_bucket = sum_sq / n;
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string path = "data/equations_10.txt";
    int per_distinct = 8;
    uint32_t seed = 12345;
    bool details = true;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if ((a == "--pool" || a == "-p") && i + 1 < argc) {
            path = argv[++i];
        } else if ((a == "--per-distinct" || a == "-n") && i + 1 < argc) {
            per_distinct = std::atoi(argv[++i]);
        } else if (a == "--seed" && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--summary-only") {
            details = false;
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--pool data/equations_10.txt] [--per-distinct 8] [--seed 12345]"
                      << " [--summary-only]\n";
            return 1;
        }
    }

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
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
    std::vector<std::vector<int>> by_distinct(static_cast<size_t>(N + 1));
    for (int i = 0; i < static_cast<int>(eqs.size()); i++) {
        if (static_cast<int>(eqs[static_cast<size_t>(i)].size()) != N) {
            std::cerr << "Inconsistent equation length.\n";
            return 1;
        }
        by_distinct[static_cast<size_t>(distinct_symbols(eqs[static_cast<size_t>(i)]))].push_back(i);
    }

    std::mt19937 rng(seed);
    std::vector<int> sample;
    for (int d = 1; d <= N; d++) {
        std::vector<int>& group = by_distinct[static_cast<size_t>(d)];
        std::shuffle(group.begin(), group.end(), rng);
        const int take = std::min(per_distinct, static_cast<int>(group.size()));
        for (int i = 0; i < take; i++)
            sample.push_back(group[static_cast<size_t>(i)]);
    }

    std::cout.setf(std::ios::unitbuf);
    std::cout << "Pool: " << path << "  equations=" << eqs.size() << "  N=" << N << "\n";
    std::cout << "Sample: up to " << per_distinct << " guesses per distinct-symbol count"
              << "  seed=" << seed << "  guesses=" << sample.size() << "\n";
    std::cout << "Distinct-count population:\n";
    for (int d = 1; d <= N; d++) {
        if (!by_distinct[static_cast<size_t>(d)].empty())
            std::cout << "  " << d << ": " << by_distinct[static_cast<size_t>(d)].size() << "\n";
    }

    std::vector<GuessStats> stats(sample.size());
#ifdef _OPENMP
    const double t0 = omp_get_wtime();
#pragma omp parallel for schedule(dynamic, 1)
#endif
    for (int i = 0; i < static_cast<int>(sample.size()); i++) {
        stats[static_cast<size_t>(i)] = score_guess(eqs, N, sample[static_cast<size_t>(i)]);
    }
#ifdef _OPENMP
    const double t1 = omp_get_wtime();
#endif

    std::sort(stats.begin(), stats.end(), [&](const GuessStats& a, const GuessStats& b) {
        if (a.distinct != b.distinct)
            return a.distinct > b.distinct;
        if (a.partitions != b.partitions)
            return a.partitions > b.partitions;
        if (std::abs(a.entropy - b.entropy) > 1e-12)
            return a.entropy > b.entropy;
        return nerdle::equation_lex_less(eqs[static_cast<size_t>(a.idx)], eqs[static_cast<size_t>(b.idx)]);
    });

    std::cout << std::setprecision(10);
    if (details) {
        std::cout << "\nPer-guess exact stats over full pool:\n";
        std::cout << "distinct\tpartitions\tentropy_bits\tmax_bucket\tsingletons\texpected_bucket\tidx\tequation\n";
        for (const GuessStats& s : stats) {
            std::cout << s.distinct << '\t' << s.partitions << '\t' << s.entropy << '\t'
                      << s.max_bucket << '\t' << s.singletons << '\t' << s.expected_bucket << '\t'
                      << s.idx << '\t' << display_eqs[static_cast<size_t>(s.idx)] << '\n';
        }
    }

    std::cout << "\nSummary by distinct count (sample only):\n";
    std::cout << "distinct\tn\tmean_partitions\tbest_partitions\tmean_entropy\tbest_entropy\tbest_equation\n";
    for (int d = N; d >= 1; d--) {
        int n = 0;
        double part_sum = 0.0;
        double ent_sum = 0.0;
        const GuessStats* best = nullptr;
        for (const GuessStats& s : stats) {
            if (s.distinct != d)
                continue;
            n++;
            part_sum += s.partitions;
            ent_sum += s.entropy;
            if (!best || s.partitions > best->partitions ||
                (s.partitions == best->partitions && s.entropy > best->entropy)) {
                best = &s;
            }
        }
        if (n > 0 && best) {
            std::cout << d << '\t' << n << '\t' << (part_sum / static_cast<double>(n)) << '\t'
                      << best->partitions << '\t' << (ent_sum / static_cast<double>(n)) << '\t'
                      << best->entropy << '\t' << display_eqs[static_cast<size_t>(best->idx)] << '\n';
        }
    }
#ifdef _OPENMP
    std::cout << "\nWall time: " << (t1 - t0) << " s\n";
#endif
    return 0;
}
