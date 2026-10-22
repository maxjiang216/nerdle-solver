/**
 * Smallest number of guesses that always solves Binerdle (two boards, one shared guess per turn,
 * a board is done once it has been guessed exactly; the two answers are different equations).
 *
 * Exact search over the set of still-possible answer pairs A x B minus the diagonal: a guess
 * splits each board's candidates by its feedback, children are (class of A) x (class of B), a
 * board whose class is the all-green one is solved. After the opening both boards share the same
 * buckets, so the work splits into independent (bucket i, bucket j) subproblems (i <= j by board
 * symmetry), solved in parallel with one cache per thread.
 *
 * With --width 0 (default) every pool equation is tried at every node, so "impossible" is a proof
 * over pool-equation guesses. --width M tries only the M most promising guesses per node, drawn
 * from the remaining candidates plus the --good best openers: faster, but "impossible" then only
 * means "not found".
 *
 * Usage: ./binerdle_guarantee [--pool data/equations_6.txt] (--tries K | --min | --single)
 *          [--opening EQ | --openers N] [--width M] [--good K] [--replay N]
 *   --min      smallest K (tries 2, 3, ... until one works)
 *   --single   cross-check: the same search on one board
 *   --replay   play N random answer pairs as real games with the found strategy (default: all
 *              pairs for pools up to 1000 equations, 200000 random pairs otherwise)
 */
#include "nerdle_core.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

std::string normalize(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 &&
            (static_cast<unsigned char>(s[i + 1]) == 0xB2 || static_cast<unsigned char>(s[i + 1]) == 0xB3)) {
            out += (static_cast<unsigned char>(s[i + 1]) == 0xB2) ? '\x01' : '\x02';
            i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

/** n x n feedback codes (guess-major), shared read-only by every thread. */
struct FbTable {
    int n = 0, N = 0, P = 0;
    uint32_t green = 0;
    std::vector<uint16_t> fb;

    FbTable(const std::vector<std::string>& eqs, int tiles)
        : n(static_cast<int>(eqs.size())), N(tiles), P(nerdle::pow3_table(tiles)),
          green(nerdle::all_green_packed(tiles)), fb(static_cast<size_t>(n) * static_cast<size_t>(n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
        for (int g = 0; g < n; g++)
            for (int s = 0; s < n; s++)
                fb[static_cast<size_t>(g) * static_cast<size_t>(n) + static_cast<size_t>(s)] =
                    static_cast<uint16_t>(nerdle::compute_feedback_packed(
                        eqs[static_cast<size_t>(g)].c_str(), eqs[static_cast<size_t>(s)].c_str(), tiles));
    }
    uint32_t at(int g, int s) const { return fb[static_cast<size_t>(g) * static_cast<size_t>(n) + static_cast<size_t>(s)]; }
};

class PairSolver {
  public:
    PairSolver(const FbTable& t, int width, const std::vector<int>& good)
        : t_(t), width_(width), good_(good), stamp_(static_cast<size_t>(t.P), 0),
          cnt_(static_cast<size_t>(t.P), 0), gstamp_(static_cast<size_t>(t.n), 0) {}

    long long nodes = 0;

    /** One board left: can every secret in S be guessed within k? */
    bool solve1(const std::vector<int>& S, int k) {
        if (S.empty())
            return true;
        if (k <= 0)
            return false;
        if (S.size() == 1)
            return true;
        if (k == 1)
            return false;
        std::string key = make_key(1, k, S, {});
        auto it = memo_.find(key);
        if (it != memo_.end())
            return it->second;
        nodes++;
        const bool ok = choose1(S, k) >= 0;
        memo_.emplace(std::move(key), ok);
        return ok;
    }

    bool try1(const std::vector<int>& S, int k, int g) {
        for (const auto& c : split(g, S))
            if (!(c.size() == 1 && c[0] == g) && !solve1(c, k - 1))
                return false;
        return true;
    }

    /** Both boards open: pairs (a, b), a in A, b in B, a != b. Can all be finished within k? */
    bool solve2(const std::vector<int>& A, const std::vector<int>& B, int k) {
        if (pairs_empty(A, B))
            return true;
        if (k < 2)
            return false; /* two different answers need two winning guesses */
        if (A.size() == 1 && B.size() == 1)
            return true; /* k >= 2: guess one, then the other */
        std::string key = (A <= B) ? make_key(2, k, A, B) : make_key(2, k, B, A); /* boards swap */
        auto it = memo_.find(key);
        if (it != memo_.end())
            return it->second;
        nodes++;
        const bool ok = choose2(A, B, k) >= 0;
        memo_.emplace(std::move(key), ok);
        return ok;
    }

    /** After guess g has split the boards into classes a and b: finish within k? */
    bool child(const std::vector<int>& a, const std::vector<int>& b, int g, int k) {
        if (pairs_empty(a, b))
            return true;
        const bool a_won = (a.size() == 1 && a[0] == g), b_won = (b.size() == 1 && b[0] == g);
        if (a_won)
            return solve1(minus(b, a[0]), k);
        if (b_won)
            return solve1(minus(a, b[0]), k);
        return solve2(a, b, k);
    }

    /** Does guess g, then best play, finish every pair of A x B (minus diagonal) within k? */
    bool try2(const std::vector<int>& A, const std::vector<int>& B, int k, int g) {
        std::vector<std::vector<int>> ca = split(g, A), cb = split(g, B);
        std::sort(ca.begin(), ca.end(), [](const auto& x, const auto& y) { return x.size() > y.size(); });
        std::sort(cb.begin(), cb.end(), [](const auto& x, const auto& y) { return x.size() > y.size(); });
        for (const auto& a : ca)
            for (const auto& b : cb)
                if (!child(a, b, g, k - 1))
                    return false;
        return true;
    }

    /* With --width, the remaining candidates and the good openers are tried first; only if none
     * works is the whole pool scored (top width again). Without --width every guess is tried. */
    int choose1(const std::vector<int>& S, int k) {
        if (S.size() == 1 && k >= 1)
            return S[0]; /* the shortcut solve1 takes */
        for (int phase = 0; phase < (width_ > 0 ? 2 : 1); phase++)
            for (int g : order_for(S, {}, phase == 1))
                if (try1(S, k, g))
                    return g;
        return -1;
    }
    int choose2(const std::vector<int>& A, const std::vector<int>& B, int k) {
        if (A.size() == 1 && B.size() == 1 && A[0] != B[0] && k >= 2)
            return A[0]; /* the shortcut solve2 takes: one answer, then the other */
        for (int phase = 0; phase < (width_ > 0 ? 2 : 1); phase++)
            for (int g : order_for(A, B, phase == 1))
                if (try2(A, B, k, g))
                    return g;
        return -1;
    }

    /** Play the strategy against actual answers (s1, s2); returns guesses used (k+1 = fail). */
    int play(int s1, int s2, int k, int opener, const std::vector<int>& all) {
        std::vector<int> A = all, B = all;
        bool won1 = false, won2 = false;
        for (int turn = 1; turn <= k; turn++) {
            int g;
            if (turn == 1)
                g = opener;
            else if (!won1 && !won2)
                g = choose2(A, B, k - turn + 1);
            else
                g = choose1(won1 ? B : A, k - turn + 1);
            if (g < 0)
                return k + 1;
            if (!won1) {
                const uint32_t f = t_.at(g, s1);
                won1 = (g == s1);
                A.erase(std::remove_if(A.begin(), A.end(), [&](int x) { return t_.at(g, x) != f; }), A.end());
            }
            if (!won2) {
                const uint32_t f = t_.at(g, s2);
                won2 = (g == s2);
                B.erase(std::remove_if(B.begin(), B.end(), [&](int x) { return t_.at(g, x) != f; }), B.end());
            }
            if (won1 && won2)
                return turn;
            /* The open board's answer differs from the solved one's. */
            if (won1)
                B.erase(std::remove(B.begin(), B.end(), s1), B.end());
            if (won2)
                A.erase(std::remove(A.begin(), A.end(), s2), A.end());
        }
        return k + 1;
    }

    std::vector<std::vector<int>> split(int g, const std::vector<int>& S) const {
        std::vector<std::pair<uint32_t, int>> coded;
        coded.reserve(S.size());
        for (int s : S)
            coded.push_back({t_.at(g, s), s});
        std::sort(coded.begin(), coded.end());
        std::vector<std::vector<int>> out;
        for (size_t i = 0; i < coded.size();) {
            size_t j = i;
            std::vector<int> c;
            while (j < coded.size() && coded[j].first == coded[i].first)
                c.push_back(coded[j++].second);
            out.push_back(std::move(c));
            i = j;
        }
        return out;
    }

  private:
    const FbTable& t_;
    int width_;
    const std::vector<int>& good_;
    std::vector<int> stamp_, cnt_, gstamp_;
    int epoch_ = 0, gepoch_ = 0;
    std::unordered_map<std::string, bool> memo_;

    static bool pairs_empty(const std::vector<int>& A, const std::vector<int>& B) {
        return A.empty() || B.empty() || (A.size() == 1 && B.size() == 1 && A[0] == B[0]);
    }

    static std::vector<int> minus(const std::vector<int>& S, int x) {
        std::vector<int> out;
        out.reserve(S.size());
        for (int s : S)
            if (s != x)
                out.push_back(s);
        return out;
    }

    /** Guesses to try, most promising first: smallest largest class over both boards, then most classes. */
    std::vector<int> order_for(const std::vector<int>& A, const std::vector<int>& B, bool whole_pool = false) {
        std::vector<int> cand;
        if (width_ <= 0 || whole_pool) {
            cand.resize(static_cast<size_t>(t_.n));
            for (int g = 0; g < t_.n; g++)
                cand[static_cast<size_t>(g)] = g;
        } else {
            ++gepoch_;
            for (const std::vector<int>* S : {&A, &B, &good_})
                for (int g : *S)
                    if (gstamp_[static_cast<size_t>(g)] != gepoch_) {
                        gstamp_[static_cast<size_t>(g)] = gepoch_;
                        cand.push_back(g);
                    }
        }
        /* (largest class, -classes, -boards g could win, index): ties favour possible answers. */
        std::vector<std::pair<std::pair<std::pair<int, int>, int>, int>> scored;
        scored.reserve(cand.size());
        for (int g : cand) {
            int maxc = 0, parts = 0, wins = 0;
            for (const std::vector<int>* S : {&A, &B}) {
                ++epoch_;
                for (int s : *S) {
                    const uint32_t c = t_.at(g, s);
                    if (stamp_[c] != epoch_) {
                        stamp_[c] = epoch_;
                        cnt_[c] = 0;
                        parts++;
                    }
                    if (c != t_.green)
                        maxc = std::max(maxc, ++cnt_[c]);
                    else
                        wins++;
                }
            }
            scored.push_back({{{maxc, -parts}, -wins}, g});
        }
        const size_t keep = (width_ > 0) ? std::min(scored.size(), static_cast<size_t>(width_)) : scored.size();
        std::partial_sort(scored.begin(), scored.begin() + static_cast<long>(keep), scored.end());
        scored.resize(keep);
        std::vector<int> out;
        out.reserve(scored.size());
        for (const auto& s : scored)
            out.push_back(s.second);
        return out;
    }

    static std::string make_key(int kind, int k, const std::vector<int>& A, const std::vector<int>& B) {
        std::string key;
        key.reserve(10 + 4 * (A.size() + B.size()));
        key.push_back(static_cast<char>(kind));
        key.push_back(static_cast<char>(k));
        auto put = [&](const std::vector<int>& S) {
            uint32_t m = static_cast<uint32_t>(S.size());
            key.append(reinterpret_cast<const char*>(&m), sizeof(m));
            for (int x : S) {
                uint32_t y = static_cast<uint32_t>(x);
                key.append(reinterpret_cast<const char*>(&y), sizeof(y));
            }
        };
        put(A);
        put(B);
        return key;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string path = "data/equations_6.txt", opening;
    int tries = 0, max_openers = 0, width = 0, good_n = 300;
    long long replay = -1;
    bool find_min = false, single = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--pool")
            path = next();
        else if (a == "--tries")
            tries = std::atoi(next().c_str());
        else if (a == "--min")
            find_min = true;
        else if (a == "--single")
            single = true;
        else if (a == "--opening")
            opening = next();
        else if (a == "--openers")
            max_openers = std::atoi(next().c_str());
        else if (a == "--width")
            width = std::atoi(next().c_str());
        else if (a == "--good")
            good_n = std::atoi(next().c_str());
        else if (a == "--replay")
            replay = std::atoll(next().c_str());
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            return 1;
        }
    }
    if (!find_min && !single && tries <= 0) {
        std::cerr << "Pass --tries K, --min or --single\n";
        return 1;
    }
    std::vector<std::string> eqs;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
            if (!line.empty())
                eqs.push_back(normalize(line));
    }
    if (eqs.empty()) {
        std::cerr << "Cannot read " << path << "\n";
        return 1;
    }
    const int n = static_cast<int>(eqs.size());
    const FbTable table(eqs, static_cast<int>(eqs[0].size()));
    std::vector<int> all(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
        all[static_cast<size_t>(i)] = i;
    auto display = [&](int g) {
        std::string d;
        for (unsigned char c : eqs[static_cast<size_t>(g)])
            d += (c == 1) ? "²" : (c == 2) ? "³" : std::string(1, static_cast<char>(c));
        return d;
    };

    /* Openers by partition count on the full pool (most first); the top ones also seed --width. */
    std::vector<int> by_parts(static_cast<size_t>(n));
    {
        std::vector<int> parts(static_cast<size_t>(n));
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<int> stamp(static_cast<size_t>(table.P), -1);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 64)
#endif
            for (int g = 0; g < n; g++) {
                int p = 0;
                for (int s = 0; s < n; s++) {
                    const uint32_t c = table.at(g, s);
                    if (stamp[c] != g) {
                        stamp[c] = g;
                        p++;
                    }
                }
                parts[static_cast<size_t>(g)] = p;
            }
        }
        for (int i = 0; i < n; i++)
            by_parts[static_cast<size_t>(i)] = i;
        std::stable_sort(by_parts.begin(), by_parts.end(),
                         [&](int a, int b) { return parts[static_cast<size_t>(a)] > parts[static_cast<size_t>(b)]; });
    }
    const std::vector<int> good(by_parts.begin(), by_parts.begin() + std::min(n, std::max(0, good_n)));

    if (single) {
        PairSolver ps(table, width, good);
        for (int k = 1; k <= 12; k++)
            if (ps.solve1(all, k)) {
                std::cout << "single board: smallest guarantee " << k
                          << (width > 0 ? " (with --width: an upper bound)" : " (exhaustive)") << "\n";
                return 0;
            }
        return 2;
    }

    std::vector<int> openers;
    if (!opening.empty()) {
        const std::string want = normalize(opening);
        for (int i = 0; i < n; i++)
            if (eqs[static_cast<size_t>(i)] == want)
                openers.push_back(i);
        if (openers.empty()) {
            std::cerr << "Opening not in pool\n";
            return 1;
        }
    } else {
        openers = by_parts;
        if (max_openers > 0 && max_openers < n)
            openers.resize(static_cast<size_t>(max_openers));
    }
    const bool exhaustive = width <= 0 && opening.empty() && static_cast<int>(openers.size()) == n;

    /* Every bucket pair after opener g (i <= j: boards are symmetric), largest first, in parallel. */
    auto run_opener = [&](int g, int k, long long* failed_tasks) {
        PairSolver splitter(table, width, good);
        const std::vector<std::vector<int>> cls = splitter.split(g, all);
        std::vector<std::pair<int, int>> tasks;
        for (int i = 0; i < static_cast<int>(cls.size()); i++)
            for (int j = i; j < static_cast<int>(cls.size()); j++)
                tasks.push_back({i, j});
        std::sort(tasks.begin(), tasks.end(), [&](const auto& x, const auto& y) {
            return cls[static_cast<size_t>(x.first)].size() * cls[static_cast<size_t>(x.second)].size() >
                   cls[static_cast<size_t>(y.first)].size() * cls[static_cast<size_t>(y.second)].size();
        });
        std::atomic<bool> fail{false};
        std::atomic<long long> fails{0};
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            PairSolver ps(table, width, good);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
            for (int ti = 0; ti < static_cast<int>(tasks.size()); ti++) {
                if (fail.load(std::memory_order_relaxed) && !failed_tasks)
                    continue;
                const auto& a = cls[static_cast<size_t>(tasks[static_cast<size_t>(ti)].first)];
                const auto& b = cls[static_cast<size_t>(tasks[static_cast<size_t>(ti)].second)];
                if (!ps.child(a, b, g, k - 1)) {
                    fail = true;
                    fails++;
                }
            }
        }
        if (failed_tasks)
            *failed_tasks = fails.load();
        return !fail.load();
    };

    const long long nn = static_cast<long long>(n);
    std::cout << "Pool " << path << ": " << n << " equations, " << nn * (nn - 1)
              << " ordered answer pairs (two different equations)"
              << (width > 0 ? ", width " + std::to_string(width) + " (heuristic)" : "") << "\n";
    for (int k = find_min ? 2 : tries; k <= (find_min ? 12 : tries); k++) {
        const auto t0 = std::chrono::steady_clock::now();
        int found = -1;
        for (int g : openers) {
            if (run_opener(g, k, nullptr)) {
                found = g;
                break;
            }
        }
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (found < 0) {
            std::cout << "tries " << k << ": "
                      << (exhaustive ? "impossible (exhaustive over every opener and guess)"
                                     : "not found (openers / width limited: not a proof)")
                      << "  (" << el << " s)\n";
            continue;
        }
        std::cout << "tries " << k << ": SOLVABLE with opener " << display(found) << "  (" << el << " s)\n";

        /* Replay answer pairs as real games with the found strategy. */
        const bool all_pairs = (replay < 0 && n <= 1000) || replay == 0;
        const long long count = all_pairs ? nn * (nn - 1) : (replay < 0 ? 200000 : replay);
        std::vector<long long> hist(static_cast<size_t>(k + 2), 0);
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            PairSolver ps(table, width, good);
            std::vector<long long> local(hist.size(), 0);
            std::mt19937_64 rng(12345);
#ifdef _OPENMP
            rng.seed(12345 + static_cast<unsigned>(omp_get_thread_num()));
#pragma omp for schedule(dynamic, 256)
#endif
            for (long long r = 0; r < count; r++) {
                int s1, s2;
                if (all_pairs) {
                    s1 = static_cast<int>(r / (nn - 1));
                    s2 = static_cast<int>(r % (nn - 1));
                    if (s2 >= s1)
                        s2++;
                } else {
                    do {
                        s1 = static_cast<int>(rng() % static_cast<uint64_t>(n));
                        s2 = static_cast<int>(rng() % static_cast<uint64_t>(n));
                    } while (s1 == s2);
                }
                local[static_cast<size_t>(ps.play(s1, s2, k, found, all))]++;
            }
#ifdef _OPENMP
#pragma omp critical
#endif
            for (size_t i = 0; i < hist.size(); i++)
                hist[i] += local[i];
        }
        long long total = 0, sum = 0;
        std::cout << (all_pairs ? "replay of all pairs:" : "replay of random pairs:");
        for (int t = 1; t <= k + 1; t++) {
            total += hist[static_cast<size_t>(t)];
            sum += hist[static_cast<size_t>(t)] * t;
            if (t <= k)
                std::cout << " " << t << ":" << hist[static_cast<size_t>(t)];
        }
        std::cout << "  not finished in " << k << ": " << hist[static_cast<size_t>(k + 1)] << "  mean "
                  << static_cast<double>(sum) / static_cast<double>(total) << "\n";
        return hist[static_cast<size_t>(k + 1)] == 0 ? 0 : 2;
    }
    return 2;
}
