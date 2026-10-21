/**
 * Search for a Nerdle strategy that solves every equation in the pool within --tries guesses
 * (any tile count; defaults are Maxi: data/equations_10.txt, 6 tries).
 *
 * Policy: fixed opening (or, with --opening search, the first opener by partition count that
 * works), then greedy partition (max distinct feedbacks among the remaining
 * candidates, ties → lowest pool index — the same rule as partition tie_depth=0). Wherever that
 * greedy subtree fails to finish within the remaining tries, backtrack: try other candidate
 * guesses (best partition first), then, if allowed, any equation from the full pool (normal mode;
 * the guess need not be a remaining candidate). Every node where the chosen guess differs from the
 * greedy pick is written as an override, so "greedy + overrides" reproduces the strategy.
 *
 * Usage: ./maxi_guarantee [--pool data/equations_10.txt] [--opening EQ | search [--openers K]] [--tries 6]
 *          [--greedy-only] [--hard-mode] [--exhaustive] [--cand-limit 64] [--pool-limit 40]
 *          [--pool-scan-max 400] [--max-three [--three-sample N]] [--overrides PATH] [--verify PATH] [--threads T]
 *
 * --max-three: guess 2 maximizes the secrets solved by guess 3 (its feedback classes, over the
 * whole pool) among choices that keep the tries guarantee; later guesses as above.
 *
 * Overrides file: one line per override,
 *   <opening feedback> <feedback after guess 2> ... <TAB> <guess>
 * where feedbacks are G/P/B strings (the path from the root to the node, excluding the node's own
 * guess) and <guess> is the equation to play there (UTF-8 ²/³).
 */
#include "feedback_batch.hpp"
#include "nerdle_core.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

int N = 10; /* tiles per equation; set from the pool in main */

std::string normalize_maxi_tiles(const std::string& s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2) {
            unsigned char c = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xB2 || c == 0xB3) {
                out += (c == 0xB2) ? '\x01' : '\x02';
                i++;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string maxi_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == 0x01)
            out += "\xc2\xb2";
        else if (c == 0x02)
            out += "\xc2\xb3";
        else
            out += static_cast<char>(c);
    }
    return out;
}

struct Options {
    bool greedy_only = false;
    bool hard_mode = false;    /* only remaining candidates may be guessed */
    int cand_limit = 64;       /* candidate guesses tried per node (after greedy) */
    int pool_limit = 40;       /* full-pool guesses tried per node */
    int pool_scan_max = 400;   /* only scan the full pool for nodes with at most this many secrets */
    bool max_three = false;    /* guess 2: maximize secrets solved by guess 3 (any pool equation) */
    int three_sample = 0;      /* --max-three: score this many random pool equations (0 = all) */
    bool stop_on_fail = false; /* opener search: give up on an opener at its first unsolved bucket */
};

struct Override {
    std::vector<uint32_t> path;
    int guess;
};

class Searcher {
  public:
    Searcher(const std::vector<std::string>& eqs, const Options& opt)
        : eqs_(eqs), opt_(opt), green_(nerdle::all_green_packed(N)),
          stamp_(static_cast<size_t>(nerdle::pow3_table(N)), 0),
          cnt_(static_cast<size_t>(nerdle::pow3_table(N)), 0) {}

    /** Codes of g against every secret in T (codes_[i] for the i-th secret). */
    const uint16_t* codes_of(int g, const nerdle::TransposedSecrets& T) {
        const size_t need = T.blocks() * nerdle::TransposedSecrets::kLanes;
        if (codes_.size() < need)
            codes_.resize(need);
        nerdle::feedback_all(eqs_[static_cast<size_t>(g)].c_str(), T, codes_.data());
        return codes_.data();
    }

    std::vector<Override> overrides;
    long long pool_scans = 0;
    std::vector<int> pool_sample; /* sorted; empty = --max-three scans the whole pool */

    /** True if every secret in S (sorted pool indices) is solved within k more guesses. */
    bool solve(const std::vector<int>& S, int k, std::vector<uint32_t>& path) {
        if (S.empty())
            return true;
        if (k <= 0)
            return false;
        if (S.size() == 1)
            return true;
        if (k == 1)
            return false;

        /* Score every remaining candidate; greedy = max partitions, lowest index on ties. */
        const nerdle::TransposedSecrets T(eqs_, S, N);
        std::vector<Scored> cands;
        cands.reserve(S.size());
        for (int g : S)
            cands.push_back(score(g, T));
        int greedy = cands[0].g;
        int greedy_parts = cands[0].parts;
        for (const Scored& c : cands) {
            if (c.parts > greedy_parts) {
                greedy_parts = c.parts;
                greedy = c.g;
            }
        }

        if (try_guess(S, k, greedy, greedy, path))
            return true;
        if (opt_.greedy_only)
            return false;

        std::sort(cands.begin(), cands.end(), better);
        int tried = 0;
        for (const Scored& c : cands) {
            if (c.g == greedy)
                continue;
            if (tried++ >= opt_.cand_limit)
                break;
            if (!can_work(c, S, k))
                break; /* sorted: nothing later can work either */
            if (try_guess(S, k, c.g, greedy, path))
                return true;
        }

        if (opt_.hard_mode || static_cast<int>(S.size()) > opt_.pool_scan_max)
            return false;

        /* Normal mode: any equation may be guessed. */
        pool_scans++;
        const int n = static_cast<int>(eqs_.size());
        if (k == 2) {
            /* Last decision: any guess that separates every secret wins, so take the first one. */
            for (int g = 0; g < n; g++)
                if (separates(g, T))
                    return try_guess(S, k, g, greedy, path);
            return false;
        }
        /* Otherwise keep the best pool_limit by score. */
        std::vector<Scored> best;
        best.reserve(std::min(static_cast<size_t>(opt_.pool_limit), eqs_.size()) + 1);
        for (int g = 0; g < n; g++) {
            Scored c = score(g, T);
            if (!can_work(c, S, k))
                continue;
            if (static_cast<int>(best.size()) < opt_.pool_limit || better(c, best.back())) {
                auto it = std::upper_bound(best.begin(), best.end(), c, better);
                best.insert(it, c);
                if (static_cast<int>(best.size()) > opt_.pool_limit)
                    best.pop_back();
            }
        }
        for (const Scored& c : best) {
            if (std::binary_search(S.begin(), S.end(), c.g))
                continue; /* already tried as a candidate */
            if (try_guess(S, k, c.g, greedy, path))
                return true;
        }
        return false;
    }

    /**
     * Guess-2 choice that maximizes the number of secrets solved by guess 3 (= distinct feedbacks
     * of the guess on S, the all-green one included) while every child stays solvable in k-1.
     * Scans the whole pool; a guess is dropped once it can no longer beat the current top_n.
     * Returns the chosen guess (-1 if none keeps the guarantee) and its class count in *classes.
     */
    int best_for_three(const std::vector<int>& S, int k, int top_n, std::vector<uint32_t>& path,
                       int* classes) {
        const int n = static_cast<int>(eqs_.size());
        const int m = static_cast<int>(S.size());
        const nerdle::TransposedSecrets T(eqs_, S, N);
        if (codes_.size() < static_cast<size_t>(nerdle::TransposedSecrets::kLanes))
            codes_.resize(nerdle::TransposedSecrets::kLanes);
        std::vector<std::pair<int, int>> top; /* (classes, guess), best first */
        auto bar = [&]() { return static_cast<int>(top.size()) < top_n ? 0 : top.back().first; };
        /* Remaining candidates first (they also win outright on all-green), then the pool or
         * the fixed pool sample. */
        std::vector<int> order(S.begin(), S.end());
        if (pool_sample.empty()) {
            for (int g = 0; g < n; g++)
                order.push_back(g);
        } else {
            order.insert(order.end(), pool_sample.begin(), pool_sample.end());
        }
        for (size_t oi = 0; oi < order.size(); oi++) {
            const int g = order[oi];
            if (oi >= S.size() && std::binary_search(S.begin(), S.end(), g))
                continue;
            if (static_cast<int>(top.size()) == top_n && top.back().first == m)
                break; /* top_n perfect splits already: nothing can beat them */
            ++epoch_;
            int parts = 0, need_beat = bar();
            bool dropped = false;
            const char* gs = eqs_[static_cast<size_t>(g)].c_str();
            for (size_t b = 0; b < T.blocks() && !dropped; b++) {
                nerdle::feedback_blocks(gs, T, b, b + 1, codes_.data());
                const int base = static_cast<int>(b) * nerdle::TransposedSecrets::kLanes;
                const int lanes = std::min(nerdle::TransposedSecrets::kLanes, m - base);
                for (int l = 0; l < lanes; l++) {
                    const uint16_t c = codes_[static_cast<size_t>(l)];
                    if (stamp_[c] != epoch_) {
                        stamp_[c] = epoch_;
                        parts++;
                    }
                }
                if (parts + (m - base - lanes) <= need_beat)
                    dropped = true;
            }
            if (dropped)
                continue;
            auto it = std::find_if(top.begin(), top.end(),
                                   [&](const std::pair<int, int>& t) { return t.first < parts; });
            top.insert(it, {parts, g});
            if (static_cast<int>(top.size()) > top_n)
                top.pop_back();
        }
        /* Greedy's own pick needs no override line. */
        int greedy = S[0], greedy_parts = -1;
        for (int c : S) {
            int p = score(c, T).parts;
            if (p > greedy_parts) {
                greedy_parts = p;
                greedy = c;
            }
        }
        for (const auto& t : top) {
            if (try_guess(S, k, t.second, greedy, path)) {
                *classes = t.first;
                return t.second;
            }
        }
        return -1;
    }

  private:
    struct Scored {
        int g;
        int parts;
        int maxc;
        bool in_s;
    };

    const std::vector<std::string>& eqs_;
    Options opt_;
    uint32_t green_;
    std::vector<int> stamp_;
    std::vector<int> cnt_;
    std::vector<uint16_t> codes_; /* feedback scratch for the batched routines */
    int epoch_ = 0;

    static bool better(const Scored& a, const Scored& b) {
        if (a.maxc != b.maxc)
            return a.maxc < b.maxc;
        if (a.parts != b.parts)
            return a.parts > b.parts;
        if (a.in_s != b.in_s)
            return a.in_s;
        return a.g < b.g;
    }

    uint32_t fb(int g, int s) const {
        return nerdle::compute_feedback_packed(eqs_[static_cast<size_t>(g)].c_str(),
                                               eqs_[static_cast<size_t>(s)].c_str(), N);
    }

    Scored score(int g, const nerdle::TransposedSecrets& T) {
        const uint16_t* codes = codes_of(g, T);
        ++epoch_;
        int parts = 0, maxc = 0;
        bool in_s = false;
        for (size_t i = 0; i < T.size(); i++) {
            const uint32_t c = codes[i];
            if (c == green_) {
                in_s = true;
                continue; /* solved by this guess; not a remaining child */
            }
            if (stamp_[c] != epoch_) {
                stamp_[c] = epoch_;
                cnt_[c] = 0;
                parts++;
            }
            maxc = std::max(maxc, ++cnt_[c]);
        }
        return {g, parts + (in_s ? 1 : 0), maxc, in_s};
    }

    /** True if g gives every secret in S a different feedback (stops at the first clash). */
    bool separates(int g, const nerdle::TransposedSecrets& T) {
        if (codes_.size() < static_cast<size_t>(nerdle::TransposedSecrets::kLanes))
            codes_.resize(nerdle::TransposedSecrets::kLanes);
        const char* gs = eqs_[static_cast<size_t>(g)].c_str();
        ++epoch_;
        for (size_t b = 0; b < T.blocks(); b++) {
            nerdle::feedback_blocks(gs, T, b, b + 1, codes_.data());
            const size_t base = b * nerdle::TransposedSecrets::kLanes;
            const size_t lanes = std::min<size_t>(nerdle::TransposedSecrets::kLanes, T.size() - base);
            for (size_t l = 0; l < lanes; l++) {
                const uint16_t c = codes_[l];
                if (stamp_[c] == epoch_)
                    return false;
                stamp_[c] = epoch_;
            }
        }
        return true;
    }

    /** Cheap necessary condition: with k-1 guesses left after g, every child must fit. */
    static bool can_work(const Scored& c, const std::vector<int>& S, int k) {
        (void)S;
        if (k - 1 == 1)
            return c.maxc <= 1;
        return true;
    }

    bool try_guess(const std::vector<int>& S, int k, int g, int greedy,
                   std::vector<uint32_t>& path) {
        std::vector<std::pair<uint32_t, int>> coded;
        coded.reserve(S.size());
        for (int s : S) {
            uint32_t c = fb(g, s);
            if (c != green_)
                coded.push_back({c, s});
        }
        std::sort(coded.begin(), coded.end());
        std::vector<std::pair<size_t, size_t>> ranges; /* [begin, end) per feedback */
        for (size_t i = 0; i < coded.size();) {
            size_t j = i;
            while (j < coded.size() && coded[j].first == coded[i].first)
                j++;
            ranges.push_back({i, j});
            i = j;
        }
        if (k - 1 == 1) {
            for (auto& r : ranges)
                if (r.second - r.first > 1)
                    return false;
        }
        /* Largest children first: they are the likeliest to fail. */
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
            return (a.second - a.first) > (b.second - b.first);
        });

        const size_t mark = overrides.size();
        if (g != greedy)
            overrides.push_back({path, g});
        std::vector<int> child;
        for (auto& r : ranges) {
            child.clear();
            for (size_t i = r.first; i < r.second; i++)
                child.push_back(coded[i].second);
            std::sort(child.begin(), child.end());
            path.push_back(coded[r.first].first);
            bool ok = solve(child, k - 1, path);
            path.pop_back();
            if (!ok) {
                overrides.resize(mark);
                return false;
            }
        }
        return true;
    }
};

/**
 * Replay "greedy + overrides" on every secret (tree walk, independent of Searcher) and add the
 * number of guesses each secret takes to hist (index tries+1 = not solved).
 */
class Verifier {
  public:
    Verifier(const std::vector<std::string>& eqs,
             const std::unordered_map<std::string, int>& overrides, int tries)
        : eqs_(eqs), over_(overrides), tries_(tries), green_(nerdle::all_green_packed(N)),
          stamp_(static_cast<size_t>(nerdle::pow3_table(N)), 0) {}

    /** S: secrets left before guess number `turn`; key: override path so far. */
    void walk(const std::vector<int>& S, int turn, const std::string& key,
              std::vector<long long>& hist) {
        if (S.empty())
            return;
        if (turn > tries_) {
            hist[static_cast<size_t>(tries_ + 1)] += static_cast<long long>(S.size());
            return;
        }
        int g;
        auto it = over_.find(key);
        if (it != over_.end()) {
            g = it->second;
        } else if (S.size() == 1) {
            g = S[0];
        } else {
            g = S[0];
            int best = -1;
            for (int c : S) {
                int p = parts(c, S);
                if (p > best) {
                    best = p;
                    g = c;
                }
            }
        }
        std::map<uint32_t, std::vector<int>> kids;
        for (int s : S) {
            uint32_t c = nerdle::compute_feedback_packed(eqs_[static_cast<size_t>(g)].c_str(),
                                                         eqs_[static_cast<size_t>(s)].c_str(), N);
            if (c == green_)
                hist[static_cast<size_t>(turn)]++;
            else
                kids[c].push_back(s);
        }
        for (auto& kv : kids) {
            std::string k2 = key.empty() ? "" : key + " ";
            walk(kv.second, turn + 1, k2 + nerdle::feedback_packed_to_string(kv.first, N), hist);
        }
    }

  private:
    const std::vector<std::string>& eqs_;
    const std::unordered_map<std::string, int>& over_;
    int tries_;
    uint32_t green_;
    std::vector<int> stamp_;
    int epoch_ = 0;

    int parts(int g, const std::vector<int>& S) {
        ++epoch_;
        int n = 0;
        for (int s : S) {
            uint32_t c = nerdle::compute_feedback_packed(eqs_[static_cast<size_t>(g)].c_str(),
                                                         eqs_[static_cast<size_t>(s)].c_str(), N);
            if (stamp_[c] != epoch_) {
                stamp_[c] = epoch_;
                n++;
            }
        }
        return n;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string path = "data/equations_10.txt";
    std::string opening; /* default: the partition opening for this length */
    int max_openers = 0; /* --opening search: try this many openers (0 = all) */
    std::string overrides_path;
    std::string verify_path;
    int tries = 6;
    Options opt;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--pool")
            path = next();
        else if (a == "--opening")
            opening = next();
        else if (a == "--tries")
            tries = std::atoi(next().c_str());
        else if (a == "--greedy-only")
            opt.greedy_only = true;
        else if (a == "--hard-mode")
            opt.hard_mode = true;
        else if (a == "--max-three")
            opt.max_three = true;
        else if (a == "--three-sample")
            opt.three_sample = std::atoi(next().c_str());
        else if (a == "--cand-limit")
            opt.cand_limit = std::atoi(next().c_str());
        else if (a == "--pool-limit")
            opt.pool_limit = std::atoi(next().c_str());
        else if (a == "--pool-scan-max")
            opt.pool_scan_max = std::atoi(next().c_str());
        else if (a == "--openers")
            max_openers = std::atoi(next().c_str());
        else if (a == "--exhaustive") {
            /* every candidate and every pool equation at every node: a failure is then a proof */
            opt.cand_limit = opt.pool_limit = opt.pool_scan_max = 1 << 30;
        } else if (a == "--overrides")
            overrides_path = next();
        else if (a == "--verify")
            verify_path = next();
#ifdef _OPENMP
        else if (a == "--threads")
            omp_set_num_threads(std::atoi(next().c_str()));
#endif
        else {
            std::cerr << "Unknown argument: " << a << "\n";
            return 1;
        }
    }

    std::vector<std::string> eqs;
    {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "Cannot open " << path << "\n";
            return 1;
        }
        std::string line;
        while (std::getline(f, line))
            if (!line.empty())
                eqs.push_back(normalize_maxi_tiles(line));
    }
    if (eqs.empty()) {
        std::cerr << "Empty pool\n";
        return 1;
    }
    N = static_cast<int>(eqs[0].size());
    for (const std::string& e : eqs) {
        if (static_cast<int>(e.size()) != N || N > 10) {
            std::cerr << "Pool rows must all have the same length (at most 10 tiles)\n";
            return 1;
        }
    }
    const int n = static_cast<int>(eqs.size());
    const uint32_t green = nerdle::all_green_packed(N);

    /* Bucket every secret by an opening's feedback; buckets largest first. */
    std::vector<std::vector<int>> by_code(static_cast<size_t>(nerdle::pow3_table(N)));
    std::vector<uint32_t> codes;
    auto bucketize = [&](int open_idx) {
        for (uint32_t c : codes)
            by_code[c].clear();
        codes.clear();
        for (int s = 0; s < n; s++) {
            uint32_t c = nerdle::compute_feedback_packed(eqs[static_cast<size_t>(open_idx)].c_str(),
                                                         eqs[static_cast<size_t>(s)].c_str(), N);
            if (c == green)
                continue;
            if (by_code[c].empty())
                codes.push_back(c);
            by_code[c].push_back(s);
        }
        std::sort(codes.begin(), codes.end(), [&](uint32_t a, uint32_t b) {
            return by_code[a].size() != by_code[b].size() ? by_code[a].size() > by_code[b].size()
                                                          : a < b;
        });
    };

    struct RunResult {
        long long unsolved_buckets = 0, unsolved_secrets = 0, overrides = 0, pool_scans = 0,
                  by_three = 0;
        std::vector<std::vector<Override>> bucket_overrides;
    };
    /* Solve every bucket of the current opening (bucketize first). */
    auto run_opening = [&](bool log) {
        RunResult r;
        std::vector<char> ok(codes.size(), 0);
        r.bucket_overrides.assign(codes.size(), {});
        std::atomic<long long> done{0}, pool_scans{0}, by_three{0};
        std::atomic<bool> abort{false};
        const auto t0 = std::chrono::steady_clock::now();
        std::mutex log_mu;
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            Searcher srch(eqs, opt);
            if (opt.three_sample > 0) {
                std::mt19937 rng(12345); /* same sample in every thread and run */
                std::vector<int> all(eqs.size());
                for (size_t i = 0; i < all.size(); i++)
                    all[i] = static_cast<int>(i);
                std::shuffle(all.begin(), all.end(), rng);
                all.resize(std::min(all.size(), static_cast<size_t>(opt.three_sample)));
                std::sort(all.begin(), all.end());
                srch.pool_sample = std::move(all);
            }
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
            for (int b = 0; b < static_cast<int>(codes.size()); b++) {
                if (abort.load(std::memory_order_relaxed))
                    continue;
                srch.overrides.clear();
                long long scans_before = srch.pool_scans;
                std::vector<uint32_t> p{codes[static_cast<size_t>(b)]};
                const std::vector<int>& S = by_code[codes[static_cast<size_t>(b)]];
                if (opt.max_three) {
                    int classes = 0;
                    ok[static_cast<size_t>(b)] = srch.best_for_three(S, tries - 1, 16, p, &classes) >= 0;
                    by_three += classes;
                } else {
                    ok[static_cast<size_t>(b)] = srch.solve(S, tries - 1, p);
                }
                if (!ok[static_cast<size_t>(b)] && opt.stop_on_fail)
                    abort = true;
                r.bucket_overrides[static_cast<size_t>(b)] = srch.overrides;
                pool_scans += srch.pool_scans - scans_before;
                long long d = ++done;
                if (log && (!ok[static_cast<size_t>(b)] || d % 2500 == 0)) {
                    std::lock_guard<std::mutex> lk(log_mu);
                    double el =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    if (!ok[static_cast<size_t>(b)])
                        std::cerr << "  UNSOLVED bucket "
                                  << nerdle::feedback_packed_to_string(codes[static_cast<size_t>(b)], N)
                                  << " size " << S.size() << "\n";
                    if (d % 2500 == 0)
                        std::cerr << "  [progress] " << d << "/" << codes.size() << " buckets, " << el
                                  << " s, pool scans " << pool_scans.load() << "\n";
                }
            }
        }
        for (size_t b = 0; b < codes.size(); b++) {
            if (!ok[b]) {
                r.unsolved_buckets++;
                r.unsolved_secrets += static_cast<long long>(by_code[codes[b]].size());
            }
            r.overrides += static_cast<long long>(r.bucket_overrides[b].size());
        }
        r.pool_scans = pool_scans.load();
        r.by_three = by_three.load();
        return r;
    };

    if (opening == "search") {
        /* Openers by partition count (most buckets first); the first that solves everything wins. */
        std::vector<std::pair<int, int>> order(static_cast<size_t>(n));
        nerdle::TransposedSecrets all;
        all.assign_all(eqs, N);
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<int> stamp(static_cast<size_t>(nerdle::pow3_table(N)), -1);
            std::vector<uint16_t> codes(all.blocks() * nerdle::TransposedSecrets::kLanes);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 64)
#endif
            for (int g = 0; g < n; g++) {
                int parts = 0;
                nerdle::feedback_all(eqs[static_cast<size_t>(g)].c_str(), all, codes.data());
                for (int s = 0; s < n; s++) {
                    const uint16_t c = codes[static_cast<size_t>(s)];
                    if (stamp[c] != g) {
                        stamp[c] = g;
                        parts++;
                    }
                }
                order[static_cast<size_t>(g)] = {-parts, g};
            }
        }
        std::sort(order.begin(), order.end());
        const int limit = (max_openers > 0) ? std::min(max_openers, n) : n;
        opt.stop_on_fail = true;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < limit; i++) {
            const int g = order[static_cast<size_t>(i)].second;
            bucketize(g);
            RunResult r = run_opening(false);
            if (r.unsolved_buckets == 0) {
                std::cout << "Opener " << maxi_display(eqs[static_cast<size_t>(g)]) << " ("
                          << -order[static_cast<size_t>(i)].first << " partitions, rank " << i + 1
                          << ") solves all " << n << " within " << tries << "  ["
                          << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
                          << " s]\n";
                return 0;
            }
        }
        std::cout << "No opener among the top " << limit << " of " << n << " solves all within "
                  << tries
                  << ((limit == n && opt.pool_limit >= n && opt.cand_limit >= n && opt.pool_scan_max >= n)
                          ? "  (exhaustive: no pool-equation strategy exists)"
                          : "  (search limits apply: not a proof)")
                  << "  ["
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
                  << " s]\n";
        return 2;
    }

    if (opening.empty()) {
        const char* def = nerdle::partition_fixed_opening_tie6(N);
        if (!def) {
            std::cerr << "No default opening for length " << N << "; pass --opening\n";
            return 1;
        }
        opening = def;
    }
    const std::string want = normalize_maxi_tiles(opening);
    int open_idx = -1;
    for (int i = 0; i < n; i++)
        if (eqs[static_cast<size_t>(i)] == want)
            open_idx = i;
    if (open_idx < 0) {
        std::cerr << "Opening not in pool: " << opening << "\n";
        return 1;
    }
    bucketize(open_idx);
    std::cerr << "Pool " << n << ", opening " << maxi_display(want) << ", " << codes.size()
              << " opening buckets, largest " << by_code[codes[0]].size() << "\n";

    if (!verify_path.empty()) {
        std::unordered_map<std::string, int> idx_of, over;
        for (size_t i = 0; i < eqs.size(); i++)
            idx_of[eqs[i]] = static_cast<int>(i);
        std::ifstream f(verify_path);
        if (!f) {
            std::cerr << "Cannot open " << verify_path << "\n";
            return 1;
        }
        std::string line;
        while (std::getline(f, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            auto it = idx_of.find(normalize_maxi_tiles(line.substr(tab + 1)));
            if (it == idx_of.end()) {
                std::cerr << "Override guess not in pool: " << line << "\n";
                return 1;
            }
            over[line.substr(0, tab)] = it->second;
        }
        std::vector<long long> hist(static_cast<size_t>(tries + 2), 0);
        hist[1] = 1; /* the opening itself */
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            Verifier v(eqs, over, tries);
            std::vector<long long> local(hist.size(), 0);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
            for (int b = 0; b < static_cast<int>(codes.size()); b++)
                v.walk(by_code[codes[static_cast<size_t>(b)]], 2,
                       nerdle::feedback_packed_to_string(codes[static_cast<size_t>(b)], N), local);
#ifdef _OPENMP
#pragma omp critical
#endif
            for (size_t i = 0; i < hist.size(); i++)
                hist[i] += local[i];
        }
        long long total = 0, sum = 0;
        for (int t = 1; t <= tries + 1; t++) {
            total += hist[static_cast<size_t>(t)];
            sum += hist[static_cast<size_t>(t)] * t;
        }
        std::cout << "Verify " << verify_path << " (" << over.size() << " overrides)\n"
                  << "Secrets: " << total << "  mean guesses: " << static_cast<double>(sum) / total
                  << "\nDistribution:";
        for (int t = 1; t <= tries; t++)
            std::cout << " " << t << ":" << hist[static_cast<size_t>(t)];
        std::cout << "  not solved in " << tries << ": " << hist[static_cast<size_t>(tries + 1)]
                  << "\n";
        return hist[static_cast<size_t>(tries + 1)] == 0 ? 0 : 2;
    }

    const auto t0 = std::chrono::steady_clock::now();
    RunResult r = run_opening(true);
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Opening: " << maxi_display(want) << "  tries: " << tries
              << (opt.greedy_only ? "  [greedy only]" : "") << (opt.hard_mode ? "  [hard mode]" : "")
              << "\n"
              << "Buckets: " << codes.size() << "  unsolved buckets: " << r.unsolved_buckets
              << "  (secrets in them: " << r.unsolved_secrets << ")\n"
              << (opt.max_three ? "Solved within 3: " + std::to_string(1 + r.by_three) + "\n"
                                : std::string())
              << "Overrides: " << r.overrides << "  full-pool scans: " << r.pool_scans
              << "  wall: " << el << " s\n";

    if (!overrides_path.empty()) {
        std::ofstream out(overrides_path);
        for (size_t b = 0; b < codes.size(); b++) {
            for (const Override& o : r.bucket_overrides[b]) {
                for (size_t i = 0; i < o.path.size(); i++)
                    out << (i ? " " : "") << nerdle::feedback_packed_to_string(o.path[i], N);
                out << "\t" << maxi_display(eqs[static_cast<size_t>(o.guess)]) << "\n";
            }
        }
        std::cout << "Wrote " << overrides_path << "\n";
    }
    return r.unsolved_buckets == 0 ? 0 : 2;
}
