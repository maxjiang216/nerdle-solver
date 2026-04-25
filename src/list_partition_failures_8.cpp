/**
 * List equations that are not won within 6 guesses under partition (tie_depth=0, Report fb),
 * matching partition_report for 8-tile.
 *
 * One n×n feedback table is built once.  Evaluation follows the game tree: the same
 * (candidate set, turn) node is never duplicated per secret — secrets are split by
 * feedback after each shared best_guess, so this is O(tree nodes) not O(n × depth) replays.
 */
#include "nerdle_core.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

static void dfs_failures(nerdle::PartitionGreedyEvaluator& ev, const std::vector<size_t>& cands,
                        const std::vector<int>& secrets, int turn, int max_tries, uint32_t green,
                        std::vector<int>& fail_si) {
    if (secrets.empty())
        return;
    int k = max_tries - turn + 1;
    if (k < 1) {
        for (int si : secrets)
            fail_si.push_back(si);
        return;
    }
    int g = ev.best_guess_index(cands, k, nullptr);
    if (k == 1) {
        for (int si : secrets) {
            if (ev.feedback_code(g, si) != green)
                fail_si.push_back(si);
        }
        return;
    }
    std::unordered_map<uint32_t, std::vector<int>> by_fb;
    by_fb.reserve(64);
    for (int si : secrets) {
        const uint32_t p = ev.feedback_code(g, si);
        if (p == green)
            continue;
        by_fb[p].push_back(si);
    }
    for (auto& e : by_fb) {
        const uint32_t p = e.first;
        std::vector<int>& sids = e.second;
        std::vector<size_t> nc;
        nc.reserve(cands.size());
        for (size_t idx : cands) {
            if (ev.feedback_code(g, static_cast<int>(idx)) == p)
                nc.push_back(idx);
        }
        dfs_failures(ev, nc, sids, turn + 1, max_tries, green, fail_si);
    }
}

int main() {
    const char* path = "data/equations_8.txt";
    const int max_tries = 6;
    const int tie_depth = 0;
    std::vector<std::string> eqs;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Open " << path << "\n";
        return 1;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            eqs.push_back(line);
    }
    const int n = static_cast<int>(eqs.size());
    const int N = static_cast<int>(eqs[0].size());
    const uint32_t green = nerdle::all_green_packed(N);

    nerdle::PartitionGreedyEvaluator ev(eqs, N, max_tries, tie_depth, nerdle::PartitionFbBudget::Report);
    ev.build_feedback_matrix();
    if (!ev.has_full_feedback_matrix()) {
        std::cerr << "Expected full n×n feedback table; pool too large or OOM\n";
        return 1;
    }

    std::vector<size_t> cands(static_cast<size_t>(n));
    std::iota(cands.begin(), cands.end(), 0);
    std::vector<int> all_sec(static_cast<size_t>(n));
    std::iota(all_sec.begin(), all_sec.end(), 0);

    std::vector<int> fail_si;
    fail_si.reserve(32);
    dfs_failures(ev, cands, all_sec, 1, max_tries, green, fail_si);

    for (int si : fail_si)
        std::cout << eqs[static_cast<size_t>(si)] << "\n";
    std::cout << "# count: " << fail_si.size() << " of " << eqs.size() << "\n";
    return 0;
}
