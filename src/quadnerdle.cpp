/**
 * Quad Nerdle solver - guess 4 Nerdles in 10 tries.
 * Supports len 8 (classic). Same principle as Binerdle but 4 games at once.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o quadnerdle quadnerdle.cpp
 * Run:     ./quadnerdle --len 8
 */

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nerdle_core.hpp"

constexpr int MAX_TRIES = 10;

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {8, "43-27=16"}, /* Start with Binerdle optimal; run solve_quadnerdle for true optimal */
};

static std::string best_guess_quads(const std::vector<std::string>& all_eqs,
                                    const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                                    const std::vector<size_t>& c3, const std::vector<size_t>& c4, int N,
                                    bool solved[4]) {
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
            hsum += nerdle::entropy_of_guess_packed(all_eqs[guess_idx].c_str(), all_eqs, Sb, N, hist);
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
    return nerdle::best_guess_v2_multi(all_eqs, boards, N, hist, rng);
}

static void filter_candidates(const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& in, const std::string& guess,
                              const std::string& feedback, int N, std::vector<size_t>& out) {
    out.clear();
    for (size_t idx : in) {
        if (nerdle::is_consistent_feedback_string(all_eqs[idx], guess, feedback.c_str(), N)) {
            out.push_back(idx);
        }
    }
}

int main(int argc, char** argv) {
    int N = 8;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--len" && i + 1 < argc) {
            N = std::atoi(argv[++i]);
        }
    }

    if (N != 8) {
        std::cerr << "Quad Nerdle: use --len 8 (classic)\n";
        return 1;
    }

    std::string path = "data/equations_" + std::to_string(N) + ".txt";
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << ". Run ./generate --len " << N << " first.\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   Q U A D   N E R D L E       ║\n";
    std::cout << "║   4 equations · 10 tries      ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Feedback: G=Green  P=Purple  B=Black\n";
    std::cout << "Loaded " << equations.size() << " equations.\n\n";

    std::vector<size_t> c1, c2, c3, c4;
    for (size_t i = 0; i < equations.size(); i++) {
        c1.push_back(i);
        c2.push_back(i);
        c3.push_back(i);
        c4.push_back(i);
    }

    std::string guess = FIRST_GUESS.at(N);
    bool solved[4] = {false, false, false, false};

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        std::cout << "Guess " << turn << "/" << MAX_TRIES << "\n";
        std::cout << "  Eq 1: " << (solved[0] ? "✓" : std::to_string(c1.size()) + " candidates")
                  << " | Eq 2: " << (solved[1] ? "✓" : std::to_string(c2.size()) + " candidates")
                  << " | Eq 3: " << (solved[2] ? "✓" : std::to_string(c3.size()) + " candidates")
                  << " | Eq 4: " << (solved[3] ? "✓" : std::to_string(c4.size()) + " candidates")
                  << "\n";
        std::cout << "  Suggested: " << guess << "\n";
        std::cout << "  Your guess (Enter=suggested): ";
        std::string user_guess;
        std::getline(std::cin, user_guess);
        if (user_guess == "q" || user_guess == "quit") return 0;
        if (!user_guess.empty() && (int)user_guess.size() == N) {
            guess = user_guess;
        }
        std::cout << "  Using: " << guess << "\n\n";

        std::string f1, f2, f3, f4;

        auto read_feedback = [&](int eq_num, bool is_solved, std::vector<size_t>& cand) {
            if (is_solved) {
                return nerdle::compute_feedback_string(guess, equations[cand[0]], N);
            }
            std::cout << "  Feedback eq " << eq_num << " (" << N << " chars G/P/B, or 'y' if correct): ";
            std::string fb;
            std::getline(std::cin, fb);
            if (fb == "q" || fb == "quit") exit(0);
            if (fb == "y" || fb == "Y") fb = std::string(N, 'G');
            while ((int)fb.size() != N) {
                std::cout << "  Enter " << N << " characters: ";
                std::getline(std::cin, fb);
            }
            for (char& c : fb) c = (char)std::toupper(c);
            return fb;
        };

        f1 = read_feedback(1, solved[0], c1);
        f2 = read_feedback(2, solved[1], c2);
        f3 = read_feedback(3, solved[2], c3);
        f4 = read_feedback(4, solved[3], c4);

        if (!solved[0]) solved[0] = (f1 == std::string(N, 'G'));
        if (!solved[1]) solved[1] = (f2 == std::string(N, 'G'));
        if (!solved[2]) solved[2] = (f3 == std::string(N, 'G'));
        if (!solved[3]) solved[3] = (f4 == std::string(N, 'G'));

        if (solved[0] && solved[1] && solved[2] && solved[3]) {
            std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
            return 0;
        }

        std::vector<size_t> next_c1, next_c2, next_c3, next_c4;
        filter_candidates(equations, c1, guess, f1, N, next_c1);
        filter_candidates(equations, c2, guess, f2, N, next_c2);
        filter_candidates(equations, c3, guess, f3, N, next_c3);
        filter_candidates(equations, c4, guess, f4, N, next_c4);

        c1 = std::move(next_c1);
        c2 = std::move(next_c2);
        c3 = std::move(next_c3);
        c4 = std::move(next_c4);

        if (c1.empty() || c2.empty() || c3.empty() || c4.empty()) {
            std::cout << "\nNo candidates remain. Check your feedback.\n";
            return 1;
        }

        guess = best_guess_quads(equations, c1, c2, c3, c4, N, solved);
        std::cout << "\n";
    }

    std::cout << "Out of guesses.\n";
    return 0;
}
