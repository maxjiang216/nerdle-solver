/**
 * Binerdle solver - guess 2 Nerdles in 7 tries.
 * Supports Mini (6-tile) and Normal (8-tile).
 *
 * First guess: same rule as single-board `nerdle` — entropy: fixed map; partition: fixed
 * opening in pool orelse `best_guess_partition_policy` on the full pool.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o binerdle binerdle.cpp
 * Run:     ./binerdle --len 6     # mini
 *          ./binerdle --len 8     # normal
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "binerdle_partition.hpp"
#include "nerdle_core.hpp"

constexpr int MAX_TRIES = 7;
/** Single-board Nerdle uses 6 tries (classic) — first guess is computed in that same model. */
constexpr int kSingleNerdleMaxTries = 6;

namespace {

/** Same opening strings as `nerdle_interactive.cpp` (entropy path, single-board). */
const std::unordered_map<int, std::string> kSingleBoardEntropyFirstGuess = {
    {6, "4*7=28"},
    {8, "48-32=16"},
};

bool is_bgp_feedback_shorthand(const std::string& s, int n) {
    if (static_cast<int>(s.size()) != n)
        return false;
    for (unsigned char uc : s) {
        const int u = std::toupper(static_cast<int>(uc));
        if (u != 'B' && u != 'G' && u != 'P')
            return false;
    }
    return true;
}

void uppercase_fb(std::string& s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

/** true = start a new game; false = exit. (Same prompt as single `nerdle`.) */
bool prompt_new_game_or_quit() {
    std::cout << "r = new game, q = quit (Enter = new game): ";
    std::string line;
    if (!std::getline(std::cin, line))
        return false;
    if (line == "q" || line == "Q" || line == "quit")
        return false;
    return true;
}

static std::string precompute_opening_first_guess(const std::vector<std::string>& eqs, int n,
                                                 bool use_partition) {
    std::vector<size_t> all;
    all.reserve(eqs.size());
    for (size_t i = 0; i < eqs.size(); i++) all.push_back(i);

    if (use_partition) {
        if (const char* fixed_c = nerdle::partition_fixed_opening_tie6(n)) {
            for (size_t idx : all) {
                if (eqs[idx] == fixed_c)
                    return eqs[idx];
            }
        }
        return nerdle::best_guess_partition_policy(
            eqs, all, n, kSingleNerdleMaxTries, nerdle::kPartitionInteractiveTieDepth);
    }
    return kSingleBoardEntropyFirstGuess.at(n);
}

static std::string best_guess_pairs(const std::vector<std::string>& all_eqs,
                                    const std::vector<size_t>& c1, const std::vector<size_t>& c2, int n,
                                    bool solved1, bool solved2, bool use_partition, int tries_remaining) {
    if (use_partition) {
        return nerdle::best_guess_binerdle_partition(
            all_eqs, c1, c2, n, std::max(1, tries_remaining), solved1, solved2);
    }
    if (c1.size() == 1 && c2.size() == 1) {
        if (solved1 && !solved2) return all_eqs[c2[0]];
        if (solved2 && !solved1) return all_eqs[c1[0]];
        return all_eqs[c1[0]];
    }
    std::vector<int> hist;
    if (c1.size() == 1 && c2.size() > 1) {
        double h2 = nerdle::entropy_of_guess_packed(all_eqs[c1[0]].c_str(), all_eqs, c2, n, hist);
        if (h2 > 0) return all_eqs[c1[0]];
    }
    if (c1.size() > 1 && c2.size() == 1) {
        double h1 = nerdle::entropy_of_guess_packed(all_eqs[c2[0]].c_str(), all_eqs, c1, n, hist);
        if (h1 > 0) return all_eqs[c2[0]];
    }
    std::vector<std::vector<size_t>> boards = {c1, c2};
    std::mt19937 rng(std::random_device{}());
    return nerdle::best_guess_v2_multi(all_eqs, boards, n, hist, rng);
}

static void filter_candidates(const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& in, const std::string& guess,
                              const std::string& feedback, int n, std::vector<size_t>& out,
                              std::unordered_set<size_t>& out_set) {
    out.clear();
    out_set.clear();
    for (size_t idx : in) {
        if (nerdle::is_consistent_feedback_string(all_eqs[idx], guess, feedback.c_str(), n)) {
            out.push_back(idx);
            out_set.insert(idx);
        }
    }
}

/**
 * When we do not need the user to type G/P/B: already solved, or singleton with known secret
 * (guess matches → all green; otherwise feedback is implied and does not narrow candidates).
 */
enum class FeedbackAsk { NeedPrompt, Solved, KnownMustBeThisGuess, KnownAnswerInfer };

static FeedbackAsk classify_feedback_ask(int n, const std::vector<std::string>& eqs,
                                         const std::vector<size_t>& c, const std::string& guess,
                                         bool board_solved) {
    (void)n;
    if (board_solved) return FeedbackAsk::Solved;
    if (c.size() != 1) return FeedbackAsk::NeedPrompt;
    if (c[0] >= eqs.size()) return FeedbackAsk::NeedPrompt;
    if (guess == eqs[c[0]]) return FeedbackAsk::KnownMustBeThisGuess;
    return FeedbackAsk::KnownAnswerInfer;
}

static bool read_feedback_line(const char* what, int n, std::string& out) {
    std::cout << what;
    std::getline(std::cin, out);
    if (out == "q" || out == "quit")
        return false;
    if (out == "y" || out == "Y")
        out = std::string(static_cast<size_t>(n), 'G');
    while (static_cast<int>(out.size()) != n) {
        if (out == "q" || out == "quit")
            return false;
        std::cout << "  Enter " << n << " characters: ";
        std::getline(std::cin, out);
    }
    uppercase_fb(out);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    int n = 8;
    bool use_partition = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--len" && i + 1 < argc) {
            n = std::atoi(argv[++i]);
        } else if (arg == "--strategy" && i + 1 < argc) {
            std::string s = argv[++i];
            if (s == "partition")
                use_partition = true;
            else if (s == "entropy" || s == "ev")
                use_partition = false;
            else {
                std::cerr << "Unknown strategy: " << s << " (use entropy or partition)\n";
                return 1;
            }
        }
    }

    if (n != 6 && n != 8) {
        std::cerr << "Binerdle: use --len 6 (mini) or --len 8 (normal)\n";
        return 1;
    }

    std::string path = "data/equations_" + std::to_string(n) + ".txt";
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << ". Run ./generate --len " << n << " first.\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    const std::string opening_guess = precompute_opening_first_guess(equations, n, use_partition);

    const std::string mode = (n == 6) ? "Mini" : "Normal";
    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   B I N E R D L E   " << mode << "  ║\n";
    std::cout << "║   2 equations · 7 tries       ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Feedback: G=Green  P=Purple  B=Black\n";
    std::cout << "Loaded " << equations.size() << " equations. Strategy: " << (use_partition ? "partition" : "entropy")
              << ".\n";
    std::cout << "Opening (first suggestion, same as single-nerdle for this mode): " << opening_guess << "\n\n";

    int game_num = 0;
next_game:
    while (true) {
        if (game_num > 0)
            std::cout << "\n— New game —\n\n";
        ++game_num;

        std::vector<size_t> c1, c2;
        for (size_t i = 0; i < equations.size(); i++) {
            c1.push_back(i);
            c2.push_back(i);
        }
        std::string guess = opening_guess;
        bool solved1 = false, solved2 = false;

        for (int turn = 1; turn <= MAX_TRIES; turn++) {
            if (c1.size() == 1 && c2.size() == 1) {
                std::cout << "\nOne equation per board: " << equations[c1[0]] << " | " << equations[c2[0]] << "\n";
                if (!prompt_new_game_or_quit())
                    return 0;
                goto next_game;
            }
            std::cout << "Guess " << turn << "/7\n";
            std::cout << "  Eq 1: " << (solved1 ? "✓ done" : std::to_string(c1.size()) + " candidates")
                      << " | Eq 2: " << (solved2 ? "✓ done" : std::to_string(c2.size()) + " candidates")
                      << "\n";
            std::cout << "  Suggested: " << guess << "\n";
            const std::string guess_for_bgp = guess;
            const bool n1 = (classify_feedback_ask(n, equations, c1, guess_for_bgp, solved1) == FeedbackAsk::NeedPrompt);
            const bool n2 = (classify_feedback_ask(n, equations, c2, guess_for_bgp, solved2) == FeedbackAsk::NeedPrompt);
            std::cout << "  Your guess (Enter = suggested; " << n << " B/G/P = feedback for next board that "
                      << "needs it; " << 2 * n
                      << " B/G/P = both boards; else type an equation): ";
            std::string user;
            std::getline(std::cin, user);
            if (user == "q" || user == "quit")
                return 0;

            std::string f1, f2;
            bool f1_set = false, f2_set = false;
            if (user.empty()) {
                /* keep guess */
            } else if (static_cast<int>(user.size()) == 2 * n && is_bgp_feedback_shorthand(user, 2 * n) && n1 &&
                       n2) {
                f1 = user.substr(0, static_cast<size_t>(n));
                f2 = user.substr(static_cast<size_t>(n), static_cast<size_t>(n));
                uppercase_fb(f1);
                uppercase_fb(f2);
                f1_set = f2_set = true;
            } else if (static_cast<int>(user.size()) == n && is_bgp_feedback_shorthand(user, n)) {
                if (n1) {
                    f1 = user;
                    uppercase_fb(f1);
                    f1_set = true;
                } else if (n2) {
                    f2 = user;
                    uppercase_fb(f2);
                    f2_set = true;
                } else {
                    std::cout << "  (ignoring B/G/P line: no board needs feedback)\n";
                }
            } else if (static_cast<int>(user.size()) == n) {
                guess = user;
            }

            std::cout << "  Using: " << guess << "\n\n";

            const FeedbackAsk ask1 = classify_feedback_ask(n, equations, c1, guess, solved1);
            const FeedbackAsk ask2 = classify_feedback_ask(n, equations, c2, guess, solved2);

            if (solved1) {
                f1 = nerdle::compute_feedback_string(guess, equations[c1[0]], n);
                std::cout
                    << "  Eq 1: (no input) already solved — feedback vs this guess is automatic (not for narrowing).\n";
            } else if (ask1 == FeedbackAsk::KnownMustBeThisGuess) {
                f1 = std::string(static_cast<size_t>(n), 'G');
                std::cout
                    << "  Eq 1: (no input) only remaining candidate is your guess — must be all green (correct).\n";
            } else if (ask1 == FeedbackAsk::KnownAnswerInfer) {
                f1 = nerdle::compute_feedback_string(guess, equations[c1[0]], n);
                std::cout << "  Eq 1: (no input) only candidate left is known; feedback for this guess is implied and "
                             "does not narrow the search (you are staging other boards).\n";
            } else if (f1_set) {
                /* f1 from B/G/P shorthand; only when we still need typed feedback for this board */
            } else {
                if (!read_feedback_line("  Feedback eq 1 (G/P/B, or y if correct): ", n, f1))
                    return 0;
            }

            if (solved2) {
                f2 = nerdle::compute_feedback_string(guess, equations[c2[0]], n);
                std::cout
                    << "  Eq 2: (no input) already solved — feedback vs this guess is automatic (not for narrowing).\n";
            } else if (ask2 == FeedbackAsk::KnownMustBeThisGuess) {
                f2 = std::string(static_cast<size_t>(n), 'G');
                std::cout
                    << "  Eq 2: (no input) only remaining candidate is your guess — must be all green (correct).\n";
            } else if (ask2 == FeedbackAsk::KnownAnswerInfer) {
                f2 = nerdle::compute_feedback_string(guess, equations[c2[0]], n);
                std::cout << "  Eq 2: (no input) only candidate left is known; feedback for this guess is implied and "
                             "does not narrow the search (you are staging other boards).\n";
            } else if (f2_set) {
            } else {
                if (!read_feedback_line("  Feedback eq 2 (G/P/B, or y if correct): ", n, f2))
                    return 0;
            }
            for (char& c : f1) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            for (char& c : f2) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            if (f1 == std::string(static_cast<size_t>(n), 'G')) solved1 = true;
            if (f2 == std::string(static_cast<size_t>(n), 'G')) solved2 = true;
            if (solved1 && solved2) {
                std::cout << "\n✓ Both solved in " << turn << " guess(es)!\n";
                if (!prompt_new_game_or_quit())
                    return 0;
                goto next_game;
            }

            std::vector<size_t> next_c1, next_c2;
            std::unordered_set<size_t> next_s1, next_s2;
            if (!solved1)
                filter_candidates(equations, c1, guess, f1, n, next_c1, next_s1);
            else {
                next_c1 = c1;
                for (size_t i : c1) next_s1.insert(i);
            }
            if (!solved2)
                filter_candidates(equations, c2, guess, f2, n, next_c2, next_s2);
            else {
                next_c2 = c2;
                for (size_t i : c2) next_s2.insert(i);
            }

            c1 = std::move(next_c1);
            c2 = std::move(next_c2);

            if ((!solved1 && c1.empty()) || (!solved2 && c2.empty())) {
                std::cout << "\nNo candidates remain. Check your feedback.\n";
                if (!prompt_new_game_or_quit())
                    return 0;
                goto next_game;
            }

            guess = best_guess_pairs(equations, c1, c2, n, solved1, solved2, use_partition, MAX_TRIES - turn);
            std::cout << "\n";
        }

        std::cout << "Out of guesses. Suggestions: " << equations[c1[0]] << " | " << equations[c2[0]] << "\n";
        if (!prompt_new_game_or_quit())
            return 0;
    }
}
