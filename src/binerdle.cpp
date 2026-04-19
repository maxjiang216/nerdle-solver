/**
 * Binerdle solver - guess 2 Nerdles in 7 tries.
 * Supports Mini (6-tile) and Normal (8-tile).
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o binerdle binerdle.cpp
 * Run:     ./binerdle --len 6     # mini
 *          ./binerdle --len 8     # normal
 */

#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nerdle_core.hpp"

constexpr int MAX_TRIES = 7;

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "3+2=5"}, {6, "4*7=28"}, {7, "4+27=31"},
    {8, "43-27=16"}, /* Binerdle-optimal (single uses 48-32=16) */
};

/** Best guess: sum of independent board entropies + v2 candidate bonus + 2-ply (see nerdle_core). */
static std::string best_guess_pairs(const std::vector<std::string>& all_eqs,
                                    const std::vector<size_t>& c1, const std::vector<size_t>& c2, int N,
                                    bool solved1 = false, bool solved2 = false) {
    if (c1.size() == 1 && c2.size() == 1) {
        if (solved1 && !solved2) return all_eqs[c2[0]];
        if (solved2 && !solved1) return all_eqs[c1[0]];
        return all_eqs[c1[0]];
    }
    std::vector<int> hist;
    if (c1.size() == 1 && c2.size() > 1) {
        double h2 = nerdle::entropy_of_guess_packed(all_eqs[c1[0]].c_str(), all_eqs, c2, N, hist);
        if (h2 > 0) return all_eqs[c1[0]];
    }
    if (c1.size() > 1 && c2.size() == 1) {
        double h1 = nerdle::entropy_of_guess_packed(all_eqs[c2[0]].c_str(), all_eqs, c1, N, hist);
        if (h1 > 0) return all_eqs[c2[0]];
    }

    std::vector<std::vector<size_t>> boards = {c1, c2};
    std::mt19937 rng(std::random_device{}());
    return nerdle::best_guess_v2_multi(all_eqs, boards, N, hist, rng);
}

static void filter_candidates(const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& in, const std::string& guess,
                              const std::string& feedback, int N, std::vector<size_t>& out,
                              std::unordered_set<size_t>& out_set) {
    out.clear();
    out_set.clear();
    for (size_t idx : in) {
        if (nerdle::is_consistent_feedback_string(all_eqs[idx], guess, feedback.c_str(), N)) {
            out.push_back(idx);
            out_set.insert(idx);
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

    if (N != 6 && N != 8) {
        std::cerr << "Binerdle: use --len 6 (mini) or --len 8 (normal)\n";
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

    std::string mode = (N == 6) ? "Mini" : "Normal";
    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   B I N E R D L E   " << mode << "  ║\n";
    std::cout << "║   2 equations · 7 tries       ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Feedback: G=Green  P=Purple  B=Black\n";
    std::cout << "Loaded " << equations.size() << " equations.\n\n";

    std::vector<size_t> c1, c2;
    std::unordered_set<size_t> s1, s2;
    for (size_t i = 0; i < equations.size(); i++) {
        c1.push_back(i);
        c2.push_back(i);
        s1.insert(i);
        s2.insert(i);
    }

    std::string guess = FIRST_GUESS.at(N);
    bool solved1 = false, solved2 = false;

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        std::cout << "Guess " << turn << "/7\n";
        std::cout << "  Eq 1: " << (solved1 ? "✓ done" : std::to_string(c1.size()) + " candidates")
                  << " | Eq 2: " << (solved2 ? "✓ done" : std::to_string(c2.size()) + " candidates")
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

        std::string f1, f2;

        if (solved1) {
            f1 = nerdle::compute_feedback_string(guess, equations[c1[0]], N);
        } else {
            std::cout << "  Feedback eq 1 (" << N << " chars G/P/B, or 'y' if correct): ";
            std::getline(std::cin, f1);
            if (f1 == "q" || f1 == "quit") return 0;
            if (f1 == "y" || f1 == "Y") f1 = std::string(N, 'G');
            while ((int)f1.size() != N) {
                std::cout << "  Enter " << N << " characters: ";
                std::getline(std::cin, f1);
            }
            for (char& c : f1) c = (char)std::toupper(c);
        }

        if (solved2) {
            f2 = nerdle::compute_feedback_string(guess, equations[c2[0]], N);
        } else {
            std::cout << "  Feedback eq 2 (" << N << " chars G/P/B, or 'y' if correct): ";
            std::getline(std::cin, f2);
            if (f2 == "q" || f2 == "quit") return 0;
            if (f2 == "y" || f2 == "Y") f2 = std::string(N, 'G');
            while ((int)f2.size() != N) {
                std::cout << "  Enter " << N << " characters: ";
                std::getline(std::cin, f2);
            }
            for (char& c : f2) c = (char)std::toupper(c);
        }

        if (f1 == std::string(N, 'G')) solved1 = true;
        if (f2 == std::string(N, 'G')) solved2 = true;
        if (solved1 && solved2) {
            std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
            return 0;
        }

        std::vector<size_t> next_c1, next_c2;
        std::unordered_set<size_t> next_s1, next_s2;
        filter_candidates(equations, c1, guess, f1, N, next_c1, next_s1);
        filter_candidates(equations, c2, guess, f2, N, next_c2, next_s2);

        c1 = std::move(next_c1);
        c2 = std::move(next_c2);
        s1 = std::move(next_s1);
        s2 = std::move(next_s2);

        if (c1.empty() || c2.empty()) {
            std::cout << "\nNo candidates remain. Check your feedback.\n";
            return 1;
        }

        guess = best_guess_pairs(equations, c1, c2, N, solved1, solved2);
        std::cout << "\n";
    }

    std::cout << "Out of guesses. Solutions might be: " << equations[c1[0]] << ", " << equations[c2[0]]
              << "\n";
    return 0;
}
