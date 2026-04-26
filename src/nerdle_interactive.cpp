#include "nerdle_interactive.hpp"

#include "micro_policy.hpp"
#include "nerdle_core.hpp"
#include "optimal_policy_build.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nerdle_interactive {

namespace {

constexpr int MAX_TRIES = 6;

const unsigned char PLACE_SQ = '\x01';
const unsigned char PLACE_CB = '\x02';

const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "3+2=5"},
    {6, "4*7=28"},
    {7, "4+27=31"},
    {8, "48-32=16"},
    {10, "56+4-21=39"},
};

std::string normalize_maxi(std::string s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2) {
            if ((unsigned char)s[i + 1] == 0xB2) {
                out += (char)PLACE_SQ;
                i++;
                continue;
            }
            if ((unsigned char)s[i + 1] == 0xB3) {
                out += (char)PLACE_CB;
                i++;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string maxi_to_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == PLACE_SQ)
            out += "\xc2\xb2";
        else if (c == PLACE_CB)
            out += "\xc2\xb3";
        else
            out += c;
    }
    return out;
}

std::string normalize_input(const std::string& s, bool is_maxi) {
    if (!is_maxi)
        return s;
    return normalize_maxi(s);
}

using PlayStrategy = nerdle_bench::PlayStrategy;

/** Length `n`, only B/G/P (any case) — user means “I played the suggested guess and got this feedback.” */
bool is_bgp_feedback_shorthand(const std::string& s, int n) {
    if (static_cast<int>(s.size()) != n)
        return false;
    for (unsigned char uc : s) {
        const int u = std::toupper(uc);
        if (u != 'B' && u != 'G' && u != 'P')
            return false;
    }
    return true;
}

/** true = start a new game; false = exit. */
bool prompt_new_game_or_quit() {
    std::cout << "r = new game, q = quit (Enter = new game): ";
    std::string line;
    if (!std::getline(std::cin, line))
        return false;
    if (line == "q" || line == "Q" || line == "quit")
        return false;
    return true; // r, Enter, or anything else → new game
}

/** If `fixed` is a row in `candidate_indices`, return it; else empty (UTF-8 already normalized in pool). */
std::string partition_fixed_if_in_pool(const std::vector<std::string>& equations,
                                       const std::vector<size_t>& candidate_indices,
                                       const char* fixed) {
    for (size_t idx : candidate_indices) {
        if (equations[idx] == fixed)
            return equations[idx];
    }
    return "";
}

} // namespace

int run(const Config& cfg) {
    const int N = cfg.N;
    const bool strategy_set = cfg.strategy.has_value();
    PlayStrategy strategy = strategy_set ? *cfg.strategy : PlayStrategy::Entropy;

    if (N != 5 && N != 6 && N != 7 && N != 8 && N != 10) {
        std::cerr << "Unsupported length " << N << ".\n";
        return 1;
    }

    std::string path = "data/equations_" + std::to_string(N) + ".txt";
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << ". Run ./generate";
        if (N == 10)
            std::cerr << "_maxi";
        else
            std::cerr << " --len " << N;
        std::cerr << " first.\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty())
            equations.push_back(line);
    }
    f.close();

    const bool is_maxi = (N == 10);
    if (is_maxi) {
        for (auto& eq : equations)
            eq = normalize_maxi(eq);
    }

    std::mt19937 rng(std::random_device{}());
    std::vector<int> hist;

    std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash> micro_policy;
    bool micro_policy_ok = false;
    if ((N == 5 || N == 6) && strategy != PlayStrategy::Partition) {
        const std::string pol_path =
            (N == 5) ? "data/optimal_policy_5.bin" : "data/optimal_policy_6.bin";
        const int neq = static_cast<int>(equations.size());
        micro_policy_ok = nerdle::load_micro_policy(pol_path, neq, micro_policy);
        if (!micro_policy_ok)
            micro_policy_ok = nerdle::try_build_optimal_policy_bin(N, std::cerr) &&
                              nerdle::load_micro_policy(pol_path, neq, micro_policy);
    }

    if (!strategy_set) {
        if (N == 5)
            strategy = micro_policy_ok ? PlayStrategy::Bellman : PlayStrategy::Partition;
        else if (N == 6) {
            if (!micro_policy_ok) {
                std::cerr << "Mini requires data/optimal_policy_6.bin (`make mini_policy` failed).\n";
                return 1;
            }
            strategy = PlayStrategy::Optimal;
        } else
            strategy = PlayStrategy::Entropy;
    } else if (strategy == PlayStrategy::Bellman && N == 5 && !micro_policy_ok) {
        std::cerr << "data/optimal_policy_5.bin still missing after build attempt — "
                     "using partition instead.\n";
        strategy = PlayStrategy::Partition;
    } else if (strategy == PlayStrategy::Optimal && N == 6 && !micro_policy_ok) {
        std::cerr << "data/optimal_policy_6.bin still missing after build attempt.\n";
        return 1;
    } else if (strategy == PlayStrategy::Bellman && N != 5) {
        std::cerr << "bellman applies to Micro (--len 5) only; using entropy.\n";
        strategy = PlayStrategy::Entropy;
    }

    std::string mode =
        (N == 5) ? "Micro" : (N == 6) ? "Mini" : (N == 7) ? "Midi" : (N == 8) ? "Classic" : "Maxi";
    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   N E R D L E   " << mode << std::string(7 - mode.size(), ' ') << "║\n";
    std::cout << "║   " << N << " tiles · " << MAX_TRIES << " tries              ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Play on nerdlegame.com. Enter your guess and feedback (G/P/B).\n";
    std::cout << "Type 'y' when correct. Loaded " << equations.size() << " equations.\n";
    if (strategy == PlayStrategy::Bellman && N == 5)
        std::cout << "Strategy: Bellman (min E[guesses], precomputed Micro policy).\n";
    else if (strategy == PlayStrategy::Optimal && N == 6)
        std::cout << "Strategy: optimal (unique min E[guesses], precomputed Mini policy).\n";
    else if (strategy == PlayStrategy::Partition) {
        const char* fixed_opening = nerdle::partition_fixed_opening_tie6(N);
        std::cout << "Strategy: partition — max distinct feedbacks; tie_depth="
                  << nerdle::kPartitionInteractiveTieDepth << " on equal-partition ties";
        if (fixed_opening)
            std::cout << "; suggest " << fixed_opening << " when in pool (first turn)\n";
        else
            std::cout << "\n";
    }
    else
        std::cout << "Strategy: entropy (v2).\n";
    std::cout << "\n";

    auto display = [is_maxi](const std::string& s) { return is_maxi ? maxi_to_display(s) : s; };

    int game_num = 0;
next_game:
    while (true) {
        if (game_num > 0)
            std::cout << "\n— New game —\n\n";
        ++game_num;

        std::vector<size_t> candidates;
        for (size_t i = 0; i < equations.size(); i++)
            candidates.push_back(i);
        std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());
        hist.clear();

        std::string guess;
        if (strategy == PlayStrategy::Partition) {
            const int ptd = nerdle::kPartitionInteractiveTieDepth;
            const char* fixed_c = nerdle::partition_fixed_opening_tie6(N);
            std::string fixed = fixed_c ? partition_fixed_if_in_pool(equations, candidates, fixed_c) : "";
            guess = !fixed.empty() ? fixed
                                   : nerdle::best_guess_partition_policy(
                                         equations, candidates, N, MAX_TRIES, ptd);
        } else if (((strategy == PlayStrategy::Bellman && N == 5) ||
                    (strategy == PlayStrategy::Optimal && N == 6)) &&
                   micro_policy_ok) {
            std::vector<size_t> all_idx(equations.size());
            for (size_t i = 0; i < equations.size(); i++)
                all_idx[i] = i;
            guess = nerdle::guess_from_micro_policy(micro_policy, equations, all_idx);
            if (guess.empty())
                guess = FIRST_GUESS.at(N);
        } else {
            guess = FIRST_GUESS.at(N);
        }

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        if (candidates.size() == 1) {
            std::cout << "\nOnly one equation is possible: " << display(equations[candidates[0]]) << "\n";
            if (!prompt_new_game_or_quit())
                return 0;
            goto next_game;
        }
        std::cout << "Guess " << turn << "/" << MAX_TRIES << "  (" << candidates.size()
                  << " candidates)\n";
        std::cout << "  Suggested: " << display(guess) << "\n";
        std::cout << "  Your guess (Enter = suggested; or " << N
                  << " letters B/G/P only = use suggested + that feedback): ";
        std::string user_guess;
        std::getline(std::cin, user_guess);
        if (user_guess == "q" || user_guess == "quit")
            return 0;

        std::string feedback;
        const bool bgp_shorthand = is_bgp_feedback_shorthand(user_guess, N);
        if (bgp_shorthand) {
            feedback = user_guess;
            for (char& c : feedback)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else if (!user_guess.empty() && static_cast<int>(user_guess.size()) >= N) {
            std::string norm = normalize_input(user_guess, is_maxi);
            if (static_cast<int>(norm.size()) == N)
                guess = norm;
        }

        std::cout << "  Using: " << display(guess) << "\n";
        if (bgp_shorthand)
            std::cout << "  (treating line as feedback for the suggested guess; skipping feedback prompt)\n";
        std::cout << "\n";

        if (!bgp_shorthand) {
            std::cout << "  Feedback (" << N << " chars G/P/B, or 'y' if correct): ";
            std::getline(std::cin, feedback);
            if (feedback == "q" || feedback == "quit")
                return 0;
            if (feedback == "y" || feedback == "Y") {
                std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
                if (!prompt_new_game_or_quit())
                    return 0;
                goto next_game;
            }
            while (static_cast<int>(feedback.size()) != N) {
                std::cout << "  Enter " << N << " characters: ";
                std::getline(std::cin, feedback);
                if (feedback == "q" || feedback == "quit")
                    return 0;
            }
            for (char& c : feedback)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (feedback == std::string(N, 'G')) {
            std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
            if (!prompt_new_game_or_quit())
                return 0;
            goto next_game;
        }

        std::vector<size_t> next_candidates;
        std::unordered_set<size_t> next_set;
        for (size_t idx : candidates) {
            if (nerdle::is_consistent_feedback_string(equations[idx], guess, feedback.c_str(), N)) {
                next_candidates.push_back(idx);
                next_set.insert(idx);
            }
        }
        candidates = std::move(next_candidates);
        candidate_set = std::move(next_set);

        if (candidates.empty()) {
            std::cout << "\nNo candidates remain. Check your feedback.\n";
            if (!prompt_new_game_or_quit())
                return 0;
            goto next_game;
        }

        if (strategy == PlayStrategy::Partition) {
            const int ptd = nerdle::kPartitionInteractiveTieDepth;
            guess = nerdle::best_guess_partition_policy(
                equations, candidates, N, MAX_TRIES - turn, ptd);
        } else if (((strategy == PlayStrategy::Bellman && N == 5) ||
                    (strategy == PlayStrategy::Optimal && N == 6)) &&
                   micro_policy_ok) {
            std::string pg = nerdle::guess_from_micro_policy(micro_policy, equations, candidates);
            if (!pg.empty())
                guess = pg;
            else
                guess = nerdle::best_guess_v2(equations, candidates, candidate_set, N, hist, rng);
        } else {
            guess = nerdle::best_guess_v2(equations, candidates, candidate_set, N, hist, rng);
        }
        std::cout << "\n";
    }

    std::cout << "Out of tries. Possible: " << display(equations[candidates[0]]) << "\n";
    if (!prompt_new_game_or_quit())
        return 0;
    goto next_game;
    }
}

} // namespace nerdle_interactive
