/**
 * Quad Nerdle solver — guess 4 Nerdles in 10 tries (classic 8 only).
 * First guess (same as one-board Nerdle for each mode): entropy **48-32=16**; partition **52-34=18**
 * when that row is in `data/equations_8.txt` (see `kClassicPartitionFixedOpening` / `partition_fixed_opening_tie6(8)`),
 * else the full-pool partition policy — via `quad_full_pool_partition_opening`.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o quadnerdle quadnerdle.cpp
 * Run:     ./quadnerdle --len 8 [--strategy entropy|partition]
 */

#include <algorithm>
#include <cctype>
#include <cstdio>
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
#include "quad_partition.hpp"

constexpr int MAX_TRIES = 10;

namespace {

/** One-board Nerdle entropy first guess (joint multi-board path uses the same). */
const std::unordered_map<int, std::string> kEntropyFirstGuess = {
    {8, "48-32=16"},
};

/** Classic 8 — must match `nerdle::kClassicPartitionFixedOpening` / `partition_fixed_opening_tie6(8)` (the actual
 *  pick is `quad_full_pool_partition_opening`, which returns this row when it appears in the pool). */
[[maybe_unused]] static constexpr const char* kPartitionFirstClassic8 = "52-34=18";

bool is_bgp_feedback_shorthand(const std::string& s, int total_len) {
    if (static_cast<int>(s.size()) != total_len) return false;
    for (unsigned char uc : s) {
        const int u = std::toupper(static_cast<int>(uc));
        if (u != 'B' && u != 'G' && u != 'P') return false;
    }
    return true;
}

void uppercase_fb(std::string& s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool prompt_new_game_or_quit() {
    std::cout << "r = new game, q = quit (Enter = new game): ";
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    if (line == "q" || line == "Q" || line == "quit") return false;
    return true;
}

static std::string precompute_opening(const std::vector<std::string>& eqs, int n, bool use_partition) {
    if (use_partition) {
        return nerdle::quad_full_pool_partition_opening(eqs, n, MAX_TRIES, nerdle::kPartitionInteractiveTieDepth);
    }
    return kEntropyFirstGuess.at(n);
}

static std::string best_guess_quads(const std::vector<std::string>& all_eqs,
                                    const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                                    const std::vector<size_t>& c3, const std::vector<size_t>& c4, int n,
                                    bool solved[4], bool use_partition, int tries_remaining) {
    if (use_partition) {
        return nerdle::best_guess_quad_partition(
            all_eqs, c1, c2, c3, c4, n, std::max(1, tries_remaining), solved, nerdle::kPartitionInteractiveTieDepth);
    }

    int num_identified = (c1.size() == 1 ? 1 : 0) + (c2.size() == 1 ? 1 : 0) + (c3.size() == 1 ? 1 : 0) +
                         (c4.size() == 1 ? 1 : 0);

    if (num_identified == 4) {
        if (!solved[0]) return all_eqs[c1[0]];
        if (!solved[1]) return all_eqs[c2[0]];
        if (!solved[2]) return all_eqs[c3[0]];
        return all_eqs[c4[0]];
    }

    std::vector<int> hist;
    const std::vector<size_t>* B[4] = {&c1, &c2, &c3, &c4};
    auto singleton_positive = [&](int skip_bi, size_t guess_idx) -> bool {
        double hsum = 0.0;
        for (int bi = 0; bi < 4; bi++) {
            if (bi == skip_bi) continue;
            const std::vector<size_t>& Sb = *B[bi];
            if (Sb.size() <= 1) continue;
            hsum += nerdle::entropy_of_guess_packed(all_eqs[guess_idx].c_str(), all_eqs, Sb, n, hist);
        }
        return hsum > 0.0;
    };
    if (c1.size() == 1 && (c2.size() > 1 || c3.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(0, c1[0])) return all_eqs[c1[0]];
    }
    if (c2.size() == 1 && (c1.size() > 1 || c3.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(1, c2[0])) return all_eqs[c2[0]];
    }
    if (c3.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(2, c3[0])) return all_eqs[c3[0]];
    }
    if (c4.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c3.size() > 1)) {
        if (singleton_positive(3, c4[0])) return all_eqs[c4[0]];
    }

    std::vector<std::vector<size_t>> boards = {c1, c2, c3, c4};
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

enum class FeedbackAsk { NeedPrompt, Solved, KnownMustBeThisGuess, KnownAnswerInfer };

static FeedbackAsk classify_feedback_ask(int n, const std::vector<std::string>& eqs,
                                          const std::vector<size_t>& c, const std::string& guess, bool board_solved) {
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
    if (out == "q" || out == "quit") return false;
    if (out == "y" || out == "Y") out = std::string(static_cast<size_t>(n), 'G');
    while (static_cast<int>(out.size()) != n) {
        if (out == "q" || out == "quit") return false;
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

    if (n != 8) {
        std::cerr << "Quad Nerdle: use --len 8 (classic)\n";
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

    const std::string opening_guess = precompute_opening(equations, n, use_partition);

    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   Q U A D   N E R D L E       ║\n";
    std::cout << "║   4 equations · 10 tries      ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Feedback: G=Green  P=Purple  B=Black\n";
    std::cout << "Loaded " << equations.size() << " equations. Strategy: " << (use_partition ? "partition" : "entropy")
              << ".\n";
    std::cout << "Opening (first suggestion, same as single-nerdle for this mode): " << opening_guess << "\n\n";

    int game_num = 0;
next_game:
    while (true) {
        if (game_num > 0) std::cout << "\n— New game —\n\n";
        ++game_num;

        std::vector<size_t> c1, c2, c3, c4;
        for (size_t i = 0; i < equations.size(); i++) {
            c1.push_back(i);
            c2.push_back(i);
            c3.push_back(i);
            c4.push_back(i);
        }
        std::string guess = opening_guess;
        bool solved[4] = {false, false, false, false};

        for (int turn = 1; turn <= MAX_TRIES; turn++) {
            if (c1.size() == 1 && c2.size() == 1 && c3.size() == 1 && c4.size() == 1) {
                std::cout << "\nOne candidate per board: " << equations[c1[0]] << " | " << equations[c2[0]] << " | "
                          << equations[c3[0]] << " | " << equations[c4[0]] << "\n";
                if (!prompt_new_game_or_quit()) return 0;
                goto next_game;
            }
            std::cout << "Guess " << turn << "/10\n";
            std::cout << "  Eq 1: " << (solved[0] ? "✓ done" : std::to_string(c1.size()) + " candidates")
                      << " | Eq 2: " << (solved[1] ? "✓ done" : std::to_string(c2.size()) + " candidates")
                      << " | Eq 3: " << (solved[2] ? "✓ done" : std::to_string(c3.size()) + " candidates")
                      << " | Eq 4: " << (solved[3] ? "✓ done" : std::to_string(c4.size()) + " candidates")
                      << "\n";
            std::cout << "  Suggested: " << guess << "\n";

            const std::vector<size_t>* cands[4] = {&c1, &c2, &c3, &c4};
            const std::string guess_for_bgp = guess;
            bool nfb[4];
            int needn = 0;
            for (int b = 0; b < 4; b++) {
                nfb[b] = (classify_feedback_ask(n, equations, *cands[b], guess_for_bgp, solved[b]) ==
                          FeedbackAsk::NeedPrompt);
                if (nfb[b]) needn++;
            }

            std::cout << "  Your guess (Enter = suggested; " << n << " B/G/P = one board (first that needs it); "
                      << 4 * n << " B/G/P = all boards that need feedback in order; else type an equation): ";
            std::string user;
            std::getline(std::cin, user);
            if (user == "q" || user == "quit") return 0;

            std::string f[4];
            bool fset[4] = {false, false, false, false};

            if (user.empty()) {
            } else if (needn > 0 && static_cast<int>(user.size()) == n * needn &&
                       is_bgp_feedback_shorthand(user, n * needn)) {
                int pos = 0;
                for (int b = 0; b < 4; b++) {
                    if (!nfb[b]) continue;
                    f[b] = user.substr(static_cast<size_t>(pos), static_cast<size_t>(n));
                    pos += n;
                    uppercase_fb(f[b]);
                    fset[b] = true;
                }
            } else if (static_cast<int>(user.size()) == n && is_bgp_feedback_shorthand(user, n)) {
                for (int b = 0; b < 4; b++) {
                    if (!nfb[b]) continue;
                    f[b] = user;
                    uppercase_fb(f[b]);
                    fset[b] = true;
                    break;
                }
            } else if (static_cast<int>(user.size()) == n) {
                guess = user;
            }

            std::cout << "  Using: " << guess << "\n\n";

            FeedbackAsk ask[4];
            for (int b = 0; b < 4; b++) {
                ask[b] = classify_feedback_ask(n, equations, *cands[b], guess, solved[b]);
            }

            for (int b = 0; b < 4; b++) {
                const int beq = b + 1;
                if (solved[b]) {
                    f[b] = nerdle::compute_feedback_string(guess, equations[(*cands[b])[0]], n);
                    std::cout << "  Eq " << beq
                              << ": (no input) already solved — feedback vs this guess is automatic (not for "
                                 "narrowing).\n";
                } else if (ask[b] == FeedbackAsk::KnownMustBeThisGuess) {
                    f[b] = std::string(static_cast<size_t>(n), 'G');
                    std::cout << "  Eq " << beq
                              << ": (no input) only remaining candidate is your guess — must be all green "
                                 "(correct).\n";
                } else if (ask[b] == FeedbackAsk::KnownAnswerInfer) {
                    f[b] = nerdle::compute_feedback_string(guess, equations[(*cands[b])[0]], n);
                    std::cout << "  Eq " << beq
                              << ": (no input) only candidate left is known; feedback for this guess is implied and "
                                 "does not narrow the search (you are staging other boards).\n";
                } else if (fset[b]) {
                } else {
                    char qbuf[80];
                    std::snprintf(qbuf, sizeof(qbuf), "  Feedback eq %d (G/P/B, or y if correct): ", beq);
                    if (!read_feedback_line(qbuf, n, f[b])) return 0;
                }
            }

            for (int b = 0; b < 4; b++) {
                for (char& ch : f[b]) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }

            for (int b = 0; b < 4; b++) {
                if (f[b] == std::string(static_cast<size_t>(n), 'G')) solved[b] = true;
            }
            if (solved[0] && solved[1] && solved[2] && solved[3]) {
                std::cout << "\n✓ All four solved in " << turn << " guess(es)!\n";
                if (!prompt_new_game_or_quit()) return 0;
                goto next_game;
            }

            std::vector<size_t> n1, n2, n3, n4;
            std::unordered_set<size_t> s1, s2, s3, s4;
            if (!solved[0]) filter_candidates(equations, c1, guess, f[0], n, n1, s1);
            else {
                n1 = c1;
                for (size_t i : c1) s1.insert(i);
            }
            if (!solved[1]) filter_candidates(equations, c2, guess, f[1], n, n2, s2);
            else {
                n2 = c2;
                for (size_t i : c2) s2.insert(i);
            }
            if (!solved[2]) filter_candidates(equations, c3, guess, f[2], n, n3, s3);
            else {
                n3 = c3;
                for (size_t i : c3) s3.insert(i);
            }
            if (!solved[3]) filter_candidates(equations, c4, guess, f[3], n, n4, s4);
            else {
                n4 = c4;
                for (size_t i : c4) s4.insert(i);
            }

            c1 = std::move(n1);
            c2 = std::move(n2);
            c3 = std::move(n3);
            c4 = std::move(n4);

            if ((!solved[0] && c1.empty()) || (!solved[1] && c2.empty()) || (!solved[2] && c3.empty()) ||
                (!solved[3] && c4.empty())) {
                std::cout << "\nNo candidates remain. Check your feedback.\n";
                if (!prompt_new_game_or_quit()) return 0;
                goto next_game;
            }

            guess = best_guess_quads(equations, c1, c2, c3, c4, n, solved, use_partition, MAX_TRIES - turn);
            std::cout << "\n";
        }

        std::cout << "Out of guesses. Last suggestions per board: " << equations[c1[0]] << " | " << equations[c2[0]]
                  << " | " << equations[c3[0]] << " | " << equations[c4[0]] << "\n";
        if (!prompt_new_game_or_quit()) return 0;
    }
}
