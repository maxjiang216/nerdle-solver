/**
 * Bellman optimal E[guesses] for a candidate subset with guesses from full pool.
 *
 * Usage: ./optimal_subgame <full_pool.txt> --candidates <subset.txt>
 *          [--fb-cache path.bin]  (load if exists, else compute and save)
 *          [--no-simulate] [--quiet]
 */

#include "subgame_optimal.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace nerdle;
using namespace nerdle::subgame;

int main(int argc, char** argv) {
    std::string pool_path;
    std::string cand_path;
    std::string fb_cache;
    bool do_simulate = true;
    bool quiet = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--candidates" && i + 1 < argc)
            cand_path = argv[++i];
        else if (a == "--fb-cache" && i + 1 < argc)
            fb_cache = argv[++i];
        else if (a == "--no-simulate")
            do_simulate = false;
        else if (a == "--quiet")
            quiet = true;
        else if (a[0] != '-')
            pool_path = a;
    }

    if (pool_path.empty() || cand_path.empty()) {
        std::cerr << "Usage: ./optimal_subgame <full_pool.txt> --candidates <subset.txt> "
                     "[--fb-cache path.bin] [--no-simulate] [--quiet]\n";
        return 1;
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
    if (n_pool < 1) {
        std::cerr << "Empty pool.\n";
        return 1;
    }
    const int N = static_cast<int>(eqs[0].size());
    for (const auto& e : eqs) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "Inconsistent equation lengths in pool.\n";
            return 1;
        }
    }

    std::unordered_map<std::string, int> pool_index;
    pool_index.reserve(static_cast<size_t>(n_pool * 2));
    for (int i = 0; i < n_pool; i++)
        pool_index[eqs[static_cast<size_t>(i)]] = i;

    std::vector<int> cand_global;
    {
        std::ifstream f(cand_path);
        if (!f) {
            std::cerr << "Cannot open " << cand_path << "\n";
            return 1;
        }
        std::unordered_set<std::string> seen;
        while (std::getline(f, line)) {
            if (line.empty())
                continue;
            if (static_cast<int>(line.size()) != N) {
                std::cerr << "Candidate length mismatch.\n";
                return 1;
            }
            auto it = pool_index.find(line);
            if (it == pool_index.end()) {
                std::cerr << "Candidate not in pool: " << line << "\n";
                return 1;
            }
            if (seen.insert(line).second)
                cand_global.push_back(it->second);
        }
    }

    const int k = static_cast<int>(cand_global.size());
    if (k < 1) {
        std::cerr << "No candidates.\n";
        return 1;
    }
    if (k > 256) {
        std::cerr << "At most 256 candidates (bitmask).\n";
        return 1;
    }

    std::vector<uint32_t> fb_full;
    double sec_fb = 0;
    int nc = 0, Nc = 0;
    if (!fb_cache.empty() && try_load_fb_cache(fb_cache, &nc, &Nc, &fb_full)) {
        if (nc != n_pool || Nc != N) {
            std::cerr << "fb-cache dimension mismatch; rebuilding.\n";
            fb_full.clear();
        }
    }
    if (fb_full.empty()) {
        auto t_fb0 = std::chrono::steady_clock::now();
        build_fb_matrix(eqs, n_pool, N, &fb_full);
        auto t_fb1 = std::chrono::steady_clock::now();
        sec_fb = std::chrono::duration<double>(t_fb1 - t_fb0).count();
        if (!fb_cache.empty()) {
            if (write_fb_cache(fb_cache, n_pool, N, fb_full))
                std::cerr << "Wrote fb-cache " << fb_cache << "\n";
            else
                std::cerr << "Warning: could not write fb-cache.\n";
        }
    } else {
        if (!quiet)
            std::cerr << "Loaded fb-cache " << fb_cache << "\n";
    }

    SubgameOptimizer opt(eqs, n_pool, N, fb_full, cand_global);
    BellmanResult br = opt.solve();

    if (!quiet) {
        std::cout << std::fixed << std::setprecision(10);
        std::cout << "Full pool: " << pool_path << "  (" << n_pool << " equations)\n";
        std::cout << "Candidates: " << cand_path << "  (k=" << k << ")\n";
        if (sec_fb > 0)
            std::cout << "Precompute fb matrix: " << sec_fb << " s\n";
        std::cout << "Optimal E[guesses] (uniform on candidates, guesses from full pool): " << br.ev
                  << "\n";
        std::cout << "DP states: " << br.dp_states << "\n";
        std::cout << "DP time: " << br.dp_seconds << " s\n";
        std::cout << "Optimal first move from this candidate set: "
                  << eqs[static_cast<size_t>(br.optimal_first_global)] << "\n";
    }

    if (do_simulate && !quiet) {
        using PM = nerdle::PolicyMask;
        std::cout << "\nSimulation (each candidate as secret):\n";
        std::vector<int> hist(32, 0);
        int worst = 0;
        PM full = nerdle::full_policy_mask(k);
        for (int t = 0; t < k; t++) {
            PM mask = full;
            int secret_gl = cand_global[static_cast<size_t>(t)];
            int steps = 0;
            while (true) {
                int g = opt.optimal_guess(mask);
                steps++;
                if (g == secret_gl)
                    break;
                uint32_t code =
                    fb_full[static_cast<size_t>(g) * static_cast<size_t>(n_pool) +
                            static_cast<size_t>(secret_gl)];
                PM newmask{};
                nerdle::for_each_bit(mask, [&](int j) {
                    int s_gl = cand_global[static_cast<size_t>(j)];
                    uint32_t c2 =
                        fb_full[static_cast<size_t>(g) * static_cast<size_t>(n_pool) +
                                static_cast<size_t>(s_gl)];
                    if (c2 == code)
                        newmask = nerdle::set_bit(newmask, j);
                });
                mask = newmask;
            }
            if (steps >= static_cast<int>(hist.size()))
                hist.resize(static_cast<size_t>(steps + 1));
            hist[static_cast<size_t>(steps)]++;
            if (steps > worst)
                worst = steps;
        }
        for (int s = 1; s <= worst; s++) {
            if (hist[static_cast<size_t>(s)] > 0)
                std::cout << "  " << s << " guess(es): " << hist[static_cast<size_t>(s)] << "\n";
        }
    }

    return 0;
}
