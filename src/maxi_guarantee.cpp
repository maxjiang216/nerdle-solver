/**
 * Search for a Maxi (10-tile, 6-try) strategy that solves every equation in the pool.
 *
 * Policy: fixed opening, then greedy partition (max distinct feedbacks among the remaining
 * candidates, ties → lowest pool index — the same rule as partition tie_depth=0). Wherever that
 * greedy subtree fails to finish within the remaining tries, backtrack: try other candidate
 * guesses (best partition first), then, if allowed, any equation from the full pool (normal mode;
 * the guess need not be a remaining candidate). Every node where the chosen guess differs from the
 * greedy pick is written as an override, so "greedy + overrides" reproduces the strategy.
 *
 * Usage: ./maxi_guarantee [--pool data/equations_10.txt] [--opening 56+4-21=39] [--tries 6]
 *          [--greedy-only] [--hard-mode] [--cand-limit 64] [--pool-limit 40]
 *          [--pool-scan-max 400] [--overrides PATH] [--threads T]
 *
 * Overrides file: one line per override,
 *   <opening feedback> <feedback after guess 2> ... <TAB> <guess>
 * where feedbacks are G/P/B strings (the path from the root to the node, excluding the node's own
 * guess) and <guess> is the equation to play there (UTF-8 ²/³).
 */
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
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr int N = 10;

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

    std::vector<Override> overrides;
    long long pool_scans = 0;

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
        std::vector<Scored> cands;
        cands.reserve(S.size());
        for (int g : S)
            cands.push_back(score(g, S));
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

        /* Normal mode: any equation may be guessed. Keep the best pool_limit by score. */
        pool_scans++;
        std::vector<Scored> best;
        best.reserve(static_cast<size_t>(opt_.pool_limit) + 1);
        const int n = static_cast<int>(eqs_.size());
        for (int g = 0; g < n; g++) {
            Scored c = score(g, S);
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

    Scored score(int g, const std::vector<int>& S) {
        ++epoch_;
        int parts = 0, maxc = 0;
        bool in_s = false;
        for (int s : S) {
            uint32_t c = fb(g, s);
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
    std::string opening = nerdle::kMaxiPartitionFixedOpening;
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
        else if (a == "--cand-limit")
            opt.cand_limit = std::atoi(next().c_str());
        else if (a == "--pool-limit")
            opt.pool_limit = std::atoi(next().c_str());
        else if (a == "--pool-scan-max")
            opt.pool_scan_max = std::atoi(next().c_str());
        else if (a == "--overrides")
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
    const std::string want = normalize_maxi_tiles(opening);
    int open_idx = -1;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (static_cast<int>(eqs[i].size()) != N) {
            std::cerr << "Not a 10-tile pool: " << maxi_display(eqs[i]) << "\n";
            return 1;
        }
        if (eqs[i] == want)
            open_idx = static_cast<int>(i);
    }
    if (open_idx < 0) {
        std::cerr << "Opening not in pool: " << opening << "\n";
        return 1;
    }

    /* Bucket every secret by the opening's feedback. */
    const uint32_t green = nerdle::all_green_packed(N);
    std::vector<std::vector<int>> by_code(static_cast<size_t>(nerdle::pow3_table(N)));
    for (int s = 0; s < static_cast<int>(eqs.size()); s++) {
        uint32_t c = nerdle::compute_feedback_packed(eqs[static_cast<size_t>(open_idx)].c_str(),
                                                     eqs[static_cast<size_t>(s)].c_str(), N);
        if (c != green)
            by_code[c].push_back(s);
    }
    std::vector<uint32_t> codes;
    for (uint32_t c = 0; c < by_code.size(); c++)
        if (!by_code[c].empty())
            codes.push_back(c);
    std::sort(codes.begin(), codes.end(), [&](uint32_t a, uint32_t b) {
        return by_code[a].size() > by_code[b].size();
    });
    std::cerr << "Pool " << eqs.size() << ", opening " << maxi_display(want) << ", "
              << codes.size() << " opening buckets, largest " << by_code[codes[0]].size() << "\n";

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

    std::vector<char> ok(codes.size(), 0);
    std::vector<std::vector<Override>> bucket_overrides(codes.size());
    std::atomic<long long> done{0}, pool_scans{0};
    const auto t0 = std::chrono::steady_clock::now();
    std::mutex log_mu;

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        Searcher srch(eqs, opt);
#ifdef _OPENMP
#pragma omp for schedule(dynamic, 1)
#endif
        for (int b = 0; b < static_cast<int>(codes.size()); b++) {
            srch.overrides.clear();
            long long scans_before = srch.pool_scans;
            std::vector<uint32_t> p{codes[static_cast<size_t>(b)]};
            ok[static_cast<size_t>(b)] = srch.solve(by_code[codes[static_cast<size_t>(b)]], tries - 1, p);
            bucket_overrides[static_cast<size_t>(b)] = srch.overrides;
            pool_scans += srch.pool_scans - scans_before;
            long long d = ++done;
            if (!ok[static_cast<size_t>(b)] || d % 2500 == 0) {
                std::lock_guard<std::mutex> lk(log_mu);
                double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (!ok[static_cast<size_t>(b)])
                    std::cerr << "  UNSOLVED bucket "
                              << nerdle::feedback_packed_to_string(codes[static_cast<size_t>(b)], N)
                              << " size " << by_code[codes[static_cast<size_t>(b)]].size() << "\n";
                if (d % 2500 == 0)
                    std::cerr << "  [progress] " << d << "/" << codes.size() << " buckets, "
                              << el << " s, pool scans " << pool_scans.load() << "\n";
            }
        }
    }

    long long unsolved_buckets = 0, unsolved_secrets = 0, n_over = 0;
    for (size_t b = 0; b < codes.size(); b++) {
        if (!ok[b]) {
            unsolved_buckets++;
            unsolved_secrets += static_cast<long long>(by_code[codes[b]].size());
        }
        n_over += static_cast<long long>(bucket_overrides[b].size());
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Opening: " << maxi_display(want) << "  tries: " << tries
              << (opt.greedy_only ? "  [greedy only]" : "") << (opt.hard_mode ? "  [hard mode]" : "")
              << "\n"
              << "Buckets: " << codes.size() << "  unsolved buckets: " << unsolved_buckets
              << "  (secrets in them: " << unsolved_secrets << ")\n"
              << "Overrides: " << n_over << "  full-pool scans: " << pool_scans.load()
              << "  wall: " << el << " s\n";

    if (!overrides_path.empty()) {
        std::ofstream out(overrides_path);
        for (size_t b = 0; b < codes.size(); b++) {
            for (const Override& o : bucket_overrides[b]) {
                for (size_t i = 0; i < o.path.size(); i++)
                    out << (i ? " " : "") << nerdle::feedback_packed_to_string(o.path[i], N);
                out << "\t" << maxi_display(eqs[static_cast<size_t>(o.guess)]) << "\n";
            }
        }
        std::cout << "Wrote " << overrides_path << "\n";
    }
    return unsolved_buckets == 0 ? 0 : 2;
}
