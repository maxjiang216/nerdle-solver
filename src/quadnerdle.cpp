/**
 * Quad Nerdle solver - guess 4 Nerdles in 10 tries.
 * Supports len 8 (classic). Same principle as Binerdle but 4 games at once.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o quadnerdle quadnerdle.cpp
 * Run:     ./quadnerdle --len 8
 */

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int SEARCH_CAP = 400;   /* Smaller pool for quad (entropy heavier) */
constexpr size_t MC_QUAD_THRESHOLD = 50000;  /* Always MC above this */
constexpr size_t MC_SAMPLE_SIZE = 12000;     /* Sample size for quad entropy */
static const int NUM_STRATA = 16;
constexpr int MAX_TRIES = 10;

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {8, "43-27=16"},  /* Start with Binerdle optimal; run solve_quadnerdle for true optimal */
};

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

int equation_type(const std::string& eq) {
    size_t eq_pos = eq.find('=');
    if (eq_pos == std::string::npos) return 0;
    long long rhs = 0;
    for (size_t i = eq_pos + 1; i < eq.size(); i++) {
        if (eq[i] >= '0' && eq[i] <= '9') rhs = rhs * 10 + (eq[i] - '0');
    }
    int op_mask = 0;
    for (size_t i = 0; i < eq_pos; i++) {
        if (eq[i] == '+') op_mask |= 1;
        else if (eq[i] == '-') op_mask |= 2;
        else if (eq[i] == '*') op_mask |= 4;
        else if (eq[i] == '/') op_mask |= 8;
    }
    int result_bucket = (rhs <= 9) ? 0 : (rhs <= 99) ? 1 : 2;
    return (op_mask % 8) * 3 + result_bucket;
}

/** Entropy of guess over QUAD space (c1 x c2 x c3 x c4). Uses Monte Carlo when huge.
    When using MC, stratifies by equation type of first slot for lower variance. */
double entropy_of_guess_quads(const std::string& guess,
                              const std::vector<std::string>& all_eqs,
                              const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                              const std::vector<size_t>& c3, const std::vector<size_t>& c4,
                              int N) {
    size_t n1 = c1.size(), n2 = c2.size(), n3 = c3.size(), n4 = c4.size();
    size_t quad_count = n1;
    if (quad_count > 0) quad_count *= n2;
    if (quad_count > 0 && n2 > 0) quad_count *= n3;
    if (quad_count > 0 && n3 > 0) quad_count *= n4;

    std::unordered_map<std::string, int> pattern_count;

    if (quad_count <= MC_QUAD_THRESHOLD && n2 > 0 && n3 > 0 && n4 > 0) {
        for (size_t ii : c1) {
            std::string fb1 = compute_feedback(guess, all_eqs[ii], N);
            for (size_t jj : c2) {
                if (ii == jj) continue;
                std::string fb2 = compute_feedback(guess, all_eqs[jj], N);
                for (size_t kk : c3) {
                    if (ii == kk || jj == kk) continue;
                    std::string fb3 = compute_feedback(guess, all_eqs[kk], N);
                    for (size_t ll : c4) {
                        if (ii == ll || jj == ll || kk == ll) continue;
                        std::string fb4 = compute_feedback(guess, all_eqs[ll], N);
                        pattern_count[fb1 + "|" + fb2 + "|" + fb3 + "|" + fb4]++;
                    }
                }
            }
        }
    } else {
        /* Stratified MC: partition c1 by equation type, sample proportionally */
        std::vector<std::vector<size_t>> strata(NUM_STRATA);
        for (size_t idx : c1) {
            int t = equation_type(all_eqs[idx]) % NUM_STRATA;
            strata[t].push_back(idx);
        }
        size_t accepted = 0;
        for (int h = 0; h < NUM_STRATA && accepted < MC_SAMPLE_SIZE; h++) {
            if (strata[h].empty()) continue;
            size_t n_h = (MC_SAMPLE_SIZE * strata[h].size() + n1 - 1) / n1;
            if (n_h == 0) n_h = 1;
            for (size_t sh = 0; sh < n_h && accepted < MC_SAMPLE_SIZE; sh++) {
                size_t r = (h * 7919ULL + sh * 2654435761ULL);
                size_t ei = strata[h][r % strata[h].size()];
                for (int attempt = 0; attempt < 30; attempt++) {
                    r = r * 7919 + 2654435761;
                    size_t j = r % n2; r /= n2;
                    size_t k = r % n3; r /= n3;
                    size_t l = r % n4;
                    size_t ej = c2[j], ek = c3[k], el = c4[l];
                    if (ei == ej || ei == ek || ei == el || ej == ek || ej == el || ek == el) continue;
                    std::string fb1 = compute_feedback(guess, all_eqs[ei], N);
                    std::string fb2 = compute_feedback(guess, all_eqs[ej], N);
                    std::string fb3 = compute_feedback(guess, all_eqs[ek], N);
                    std::string fb4 = compute_feedback(guess, all_eqs[el], N);
                    pattern_count[fb1 + "|" + fb2 + "|" + fb3 + "|" + fb4]++;
                    accepted++;
                    break;
                }
            }
        }
        /* Fill remainder with plain rejection */
        for (size_t s = 0; accepted < MC_SAMPLE_SIZE && s < MC_SAMPLE_SIZE * 5; s++) {
            size_t r = s * 2654435761ULL;
            size_t i = r % n1; r /= n1;
            size_t j = r % n2; r /= n2;
            size_t k = r % n3; r /= n3;
            size_t l = r % n4;
            size_t ei = c1[i], ej = c2[j], ek = c3[k], el = c4[l];
            if (ei == ej || ei == ek || ei == el || ej == ek || ej == el || ek == el) continue;
            std::string fb1 = compute_feedback(guess, all_eqs[ei], N);
            std::string fb2 = compute_feedback(guess, all_eqs[ej], N);
            std::string fb3 = compute_feedback(guess, all_eqs[ek], N);
            std::string fb4 = compute_feedback(guess, all_eqs[el], N);
            pattern_count[fb1 + "|" + fb2 + "|" + fb3 + "|" + fb4]++;
            accepted++;
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

/** Best guess that maximizes entropy over quad space. */
std::string best_guess_quads(const std::vector<std::string>& all_eqs,
                            const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                            const std::vector<size_t>& c3, const std::vector<size_t>& c4,
                            int N, bool solved[4]) {
    int num_identified = (c1.size() == 1 ? 1 : 0) + (c2.size() == 1 ? 1 : 0)
                      + (c3.size() == 1 ? 1 : 0) + (c4.size() == 1 ? 1 : 0);

    if (num_identified == 4) {
        /* All identified: guess one we haven't solved */
        if (!solved[0]) return all_eqs[c1[0]];
        if (!solved[1]) return all_eqs[c2[0]];
        if (!solved[2]) return all_eqs[c3[0]];
        return all_eqs[c4[0]];
    }

    /* When one slot is identified, prefer guessing it if entropy > 0 */
    if (c1.size() == 1 && c2.size() > 1) {
        double h = entropy_of_guess_quads(all_eqs[c1[0]], all_eqs, c1, c2, c3, c4, N);
        if (h > 0) return all_eqs[c1[0]];
    }
    if (c2.size() == 1 && (c1.size() > 1 || c3.size() > 1 || c4.size() > 1)) {
        double h = entropy_of_guess_quads(all_eqs[c2[0]], all_eqs, c1, c2, c3, c4, N);
        if (h > 0) return all_eqs[c2[0]];
    }
    if (c3.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c4.size() > 1)) {
        double h = entropy_of_guess_quads(all_eqs[c3[0]], all_eqs, c1, c2, c3, c4, N);
        if (h > 0) return all_eqs[c3[0]];
    }
    if (c4.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c3.size() > 1)) {
        double h = entropy_of_guess_quads(all_eqs[c4[0]], all_eqs, c1, c2, c3, c4, N);
        if (h > 0) return all_eqs[c4[0]];
    }

    std::unordered_set<size_t> union_set(c1.begin(), c1.end());
    union_set.insert(c2.begin(), c2.end());
    union_set.insert(c3.begin(), c3.end());
    union_set.insert(c4.begin(), c4.end());

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
        double h = entropy_of_guess_quads(all_eqs[idx], all_eqs, c1, c2, c3, c4, N);
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
                      int N, std::vector<size_t>& out) {
    out.clear();
    for (size_t idx : in) {
        if (is_consistent(all_eqs[idx], guess, feedback, N)) {
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
                return compute_feedback(guess, equations[cand[0]], N);
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

        /* Never overwrite solved: once an eq is solved, it stays solved */
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
