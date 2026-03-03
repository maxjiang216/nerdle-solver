/**
 * Binerdle solver - guess 2 Nerdles in 7 tries.
 * Supports Mini (6-tile) and Normal (8-tile).
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o binerdle binerdle.cpp
 * Run:     ./binerdle --len 6     # mini
 *          ./binerdle --len 8     # normal
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int SEARCH_CAP = 600;  /* No subsample of union when |c1∪c2| <= this */
constexpr size_t MC_PAIR_THRESHOLD = 50000;  /* Use Monte Carlo above this */
constexpr size_t MC_SAMPLE_SIZE = 8000;
constexpr int MAX_TRIES = 7;

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "4-1=3"}, {6, "4*7=28"}, {7, "6+18=24"},
    {8, "43-27=16"},  /* Binerdle-optimal (single uses 48-32=16) */
};

/* G = right char right place. Per char: N remaining after greens → leftmost N non-greens get P. */
std::string compute_feedback(const std::string& guess, const std::string& solution, int N) {
    std::string result(N, 'B');
    int remaining[256] = {0};
    for (char c : solution) remaining[static_cast<unsigned char>(c)]++;
    for (int i = 0; i < N; i++) {
        if (guess[i] == solution[i]) {
            result[i] = 'G';
            remaining[static_cast<unsigned char>(guess[i])]--;
        }
    }
    for (int i = 0; i < N; i++) {
        if (result[i] == 'G') continue;
        unsigned char c = static_cast<unsigned char>(guess[i]);
        if (remaining[c] > 0) {
            result[i] = 'P';
            remaining[c]--;
        }
    }
    return result;
}

bool is_consistent(const std::string& candidate, const std::string& guess,
                   const std::string& feedback, int N) {
    return compute_feedback(guess, candidate, N) == feedback;
}

/** Entropy of guess over PAIR space (c1 x c2). Uses Monte Carlo when too large. */
double entropy_of_guess_pairs(const std::string& guess,
                              const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                              int N) {
    size_t pair_count = c1.size() * c2.size();
    std::unordered_map<std::string, int> pattern_count;

    if (pair_count <= MC_PAIR_THRESHOLD) {
        for (size_t i : c1) {
            std::string fb1 = compute_feedback(guess, all_eqs[i], N);
            for (size_t j : c2) {
                std::string fb2 = compute_feedback(guess, all_eqs[j], N);
                pattern_count[fb1 + "|" + fb2]++;
            }
        }
    } else {
        /* Monte Carlo: deterministic strided sample */
        size_t step = pair_count / MC_SAMPLE_SIZE;
        if (step < 1) step = 1;
        for (size_t ij = 0; ij < pair_count; ij += step) {
            size_t i = ij % c1.size(), j = ij / c1.size();
            std::string fb1 = compute_feedback(guess, all_eqs[c1[i]], N);
            std::string fb2 = compute_feedback(guess, all_eqs[c2[j]], N);
            pattern_count[fb1 + "|" + fb2]++;
        }
    }

    double total = 0;
    for (const auto& kv : pattern_count) total += kv.second;
    if (total <= 0) return 0.0;
    double h = 0.0;
    for (const auto& kv : pattern_count) {
        double p = kv.second / total;
        h -= p * std::log2(p);
    }
    return h;
}

/** Best guess that maximizes entropy. Prefer known answer when c1=1 or c2=1 and it has entropy>0. */
std::string best_guess_pairs(const std::vector<std::string>& all_eqs,
                             const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                             int N, bool solved1 = false, bool solved2 = false) {
    if (c1.size() == 1 && c2.size() == 1) {
        /* Both identified: guess the one we haven't solved yet */
        if (solved1 && !solved2) return all_eqs[c2[0]];
        if (solved2 && !solved1) return all_eqs[c1[0]];
        return all_eqs[c1[0]];
    }
    /* When one side is identified, prefer guessing it (we need that guess anyway, gives ≥0 info) */
    if (c1.size() == 1 && c2.size() > 1) {
        double h = entropy_of_guess_pairs(all_eqs[c1[0]], all_eqs, c1, c2, N);
        if (h > 0) return all_eqs[c1[0]];
    }
    if (c1.size() > 1 && c2.size() == 1) {
        double h = entropy_of_guess_pairs(all_eqs[c2[0]], all_eqs, c1, c2, N);
        if (h > 0) return all_eqs[c2[0]];
    }

    std::unordered_set<size_t> union_set(c1.begin(), c1.end());
    union_set.insert(c2.begin(), c2.end());

    std::vector<size_t> pool_indices;
    size_t n = all_eqs.size();
    if (n <= (size_t)SEARCH_CAP) {
        for (size_t i = 0; i < n; i++) pool_indices.push_back(i);
    } else {
        size_t step = n / SEARCH_CAP;
        for (size_t i = 0; i < n && pool_indices.size() < (size_t)SEARCH_CAP; i += (step < 1 ? 1 : step))
            pool_indices.push_back(i);
    }

    size_t best_idx = pool_indices[0];
    double best_h = -1.0;
    bool best_in_union = union_set.count(best_idx) > 0;
    for (size_t idx : pool_indices) {
        double h = entropy_of_guess_pairs(all_eqs[idx], all_eqs, c1, c2, N);
        bool in_union = union_set.count(idx) > 0;
        if (h > best_h || (h == best_h && in_union && !best_in_union)) {
            best_h = h;
            best_idx = idx;
            best_in_union = in_union;
        }
    }
    return all_eqs[best_idx];
}

void filter_candidates(const std::vector<std::string>& all_eqs,
                      const std::vector<size_t>& in,
                      const std::string& guess, const std::string& feedback,
                      int N, std::vector<size_t>& out, std::unordered_set<size_t>& out_set) {
    out.clear();
    out_set.clear();
    for (size_t idx : in) {
        if (is_consistent(all_eqs[idx], guess, feedback, N)) {
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
            f1 = compute_feedback(guess, equations[c1[0]], N);  /* known answer */
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
            f2 = compute_feedback(guess, equations[c2[0]], N);  /* known answer */
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

    std::cout << "Out of guesses. Solutions might be: " << equations[c1[0]] << ", " << equations[c2[0]] << "\n";
    return 0;
}
