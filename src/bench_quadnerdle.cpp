/**
 * Quad Nerdle benchmark - simulates solver over SAMPLED equation quadruples.
 * Full enumeration (n^4) is infeasible, so we sample quadruples and report stats.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o bench_quadnerdle bench_quadnerdle.cpp
 * Run:     ./bench_quadnerdle data/equations_8.txt
 *          ./bench_quadnerdle data/equations_8.txt --sample 10000
 */

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int SEARCH_CAP = 400;
constexpr size_t MC_QUAD_THRESHOLD = 50000;
constexpr size_t MC_SAMPLE_SIZE = 12000;
constexpr int MAX_TRIES = 10;
constexpr size_t DEFAULT_BENCH_SAMPLES = 5000;

std::string compute_feedback(const std::string& guess, const std::string& solution, int N) {
    std::string result(N, 'B');
    int sol_count[256] = {0};
    for (char c : solution) sol_count[static_cast<unsigned char>(c)]++;

    for (int i = 0; i < N; i++) {
        if (guess[i] == solution[i]) {
            result[i] = 'G';
            sol_count[static_cast<unsigned char>(guess[i])]--;
        }
    }
    for (int i = 0; i < N; i++) {
        if (result[i] == 'G') continue;
        char c = guess[i];
        if (sol_count[static_cast<unsigned char>(c)] > 0) {
            result[i] = 'P';
            sol_count[static_cast<unsigned char>(c)]--;
        }
    }
    return result;
}

bool is_consistent(const std::string& candidate, const std::string& guess,
                   const std::string& feedback, int N) {
    return compute_feedback(guess, candidate, N) == feedback;
}

/** Entropy of guess over QUAD space. Monte Carlo when huge. */
double entropy_of_guess_quads(const std::string& guess,
                             const std::vector<std::string>& all_eqs,
                             const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                             const std::vector<size_t>& c3, const std::vector<size_t>& c4,
                             int N) {
    size_t n1 = c1.size(), n2 = c2.size(), n3 = c3.size(), n4 = c4.size();
    size_t quad_count = n1 * n2 * n3 * n4;

    std::unordered_map<std::string, int> pattern_count;

    if (quad_count <= MC_QUAD_THRESHOLD) {
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
        size_t accepted = 0;
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

std::string best_guess_quads(const std::vector<std::string>& all_eqs,
                            const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                            const std::vector<size_t>& c3, const std::vector<size_t>& c4,
                            int N) {
    if (c1.size() == 1 && c2.size() == 1 && c3.size() == 1 && c4.size() == 1) {
        return all_eqs[c1[0]];
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

/** Solve Quad Nerdle for one quadruple. */
int solve_quadnerdle_quad(size_t s1, size_t s2, size_t s3, size_t s4,
                         const std::vector<std::string>& all_eqs,
                         const std::string& first_guess, int N) {
    std::vector<size_t> c1, c2, c3, c4;
    for (size_t i = 0; i < all_eqs.size(); i++) {
        c1.push_back(i);
        c2.push_back(i);
        c3.push_back(i);
        c4.push_back(i);
    }

    std::string guess = first_guess;

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        std::string fb1 = compute_feedback(guess, all_eqs[s1], N);
        std::string fb2 = compute_feedback(guess, all_eqs[s2], N);
        std::string fb3 = compute_feedback(guess, all_eqs[s3], N);
        std::string fb4 = compute_feedback(guess, all_eqs[s4], N);

        std::vector<size_t> next_c1, next_c2, next_c3, next_c4;
        for (size_t idx : c1) {
            if (is_consistent(all_eqs[idx], guess, fb1, N)) next_c1.push_back(idx);
        }
        for (size_t idx : c2) {
            if (is_consistent(all_eqs[idx], guess, fb2, N)) next_c2.push_back(idx);
        }
        for (size_t idx : c3) {
            if (is_consistent(all_eqs[idx], guess, fb3, N)) next_c3.push_back(idx);
        }
        for (size_t idx : c4) {
            if (is_consistent(all_eqs[idx], guess, fb4, N)) next_c4.push_back(idx);
        }

        c1 = std::move(next_c1);
        c2 = std::move(next_c2);
        c3 = std::move(next_c3);
        c4 = std::move(next_c4);

        if (c1.empty() || c2.empty() || c3.empty() || c4.empty()) {
            return MAX_TRIES + 1;
        }
        if (c1.size() == 1 && c2.size() == 1 && c3.size() == 1 && c4.size() == 1) {
            return turn;
        }

        guess = best_guess_quads(all_eqs, c1, c2, c3, c4, N);
    }
    return MAX_TRIES + 1;
}

int main(int argc, char** argv) {
    size_t sample_count = DEFAULT_BENCH_SAMPLES;
    std::string path = "data/equations_8.txt";
    std::string first_guess_override;

    for (int a = 1; a < argc; ) {
        std::string arg = argv[a];
        if (arg == "--sample" && a + 1 < argc) {
            sample_count = static_cast<size_t>(std::atol(argv[a + 1]));
            a += 2;
        } else if (arg == "--single") {
            first_guess_override = "48-32=16";
            a++;
        } else if (arg == "--binerdle") {
            first_guess_override = "43-27=16";
            a++;
        } else if (arg[0] != '-') {
            path = arg;
            a++;
        } else {
            a++;
        }
    }

    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    if (equations.empty()) {
        std::cerr << "No equations loaded.\n";
        return 1;
    }

    int N = static_cast<int>(equations[0].size());
    if (N != 8) {
        std::cerr << "Quad Nerdle: use equations_8.txt\n";
        return 1;
    }

    std::string first_guess = first_guess_override.empty() ? "43-27=16" : first_guess_override;
    size_t n = equations.size();

    std::cout << "Quad Nerdle benchmark (" << n << " equations, "
              << sample_count << " sampled quadruples";
    if (!first_guess_override.empty()) {
        std::cout << ", first guess: " << first_guess;
    }
    std::cout << ")\n\n";

    std::vector<int> turn_dist(MAX_TRIES + 3, 0);
    double sum_turns = 0;

    /* Generate sample indices: DISTINCT quadruples (i,j,k,l) - all 4 equations different */
    std::vector<std::array<size_t,4>> samples;
    samples.reserve(sample_count);
    for (size_t t = 0; samples.size() < sample_count && t < sample_count * 5; t++) {
        size_t r = (t * 2654435761ULL);
        size_t i = (r) % n; r /= n;
        size_t j = (r) % n; r /= n;
        size_t k = (r) % n; r /= n;
        size_t l = (r) % n;
        if (i != j && i != k && i != l && j != k && j != l && k != l) {
            samples.push_back({i, j, k, l});
        }
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) reduction(+:sum_turns)
#endif
    for (size_t t = 0; t < samples.size(); t++) {
        size_t i = samples[t][0], j = samples[t][1], k = samples[t][2], l = samples[t][3];
        int turns = solve_quadnerdle_quad(i, j, k, l, equations, first_guess, N);
        sum_turns += turns;
#ifdef _OPENMP
#pragma omp critical
#endif
        turn_dist[turns]++;
    }

    size_t num_samples = samples.size();
    std::cout << "Quad Nerdle (one guess for all 4, 10 tries, distinct quads):\n";
    std::cout << "  Mean guesses: " << (num_samples > 0 ? sum_turns / num_samples : 0) << "\n";
    std::cout << "  Distribution: ";
    for (int k = 1; k <= MAX_TRIES + 1; k++) {
        if (turn_dist[k] > 0) std::cout << k << ":" << turn_dist[k] << " ";
    }
    std::cout << "\n";
    std::cout << "  Failures (11+): " << turn_dist[MAX_TRIES + 1] << "\n";

    return 0;
}
