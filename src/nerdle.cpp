/**
 * Interactive Nerdle player - solver-assisted play for lengths 5-8 and Maxi (10).
 * Play on nerdlegame.com, enter your guess and feedback; we suggest the next guess.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o nerdle nerdle.cpp
 * Run:     ./nerdle --len 6     # mini
 *          ./nerdle --len 8     # classic
 *          ./nerdle --len 10    # maxi
 */

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr int SEARCH_CAP = 300;
constexpr size_t ENTROPY_SAMPLE = 30000;  /* Subsample candidates when larger for speed */
constexpr int MAX_TRIES = 6;

static const unsigned char PLACE_SQ = '\x01';
static const unsigned char PLACE_CB = '\x02';

static const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "4-1=3"},
    {6, "4*7=28"},
    {7, "6+18=24"},
    {8, "48-32=16"},
    {10, "76+1-23=54"},
};

static std::string normalize_maxi(std::string s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2) {
            if ((unsigned char)s[i + 1] == 0xB2) { out += (char)PLACE_SQ; i++; continue; }
            if ((unsigned char)s[i + 1] == 0xB3) { out += (char)PLACE_CB; i++; continue; }
        }
        out += s[i];
    }
    return out;
}

static std::string maxi_to_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == PLACE_SQ) out += "\xc2\xb2";
        else if (c == PLACE_CB) out += "\xc2\xb3";
        else out += c;
    }
    return out;
}

/* Normalize user input for Maxi (² ³) */
static std::string normalize_input(const std::string& s, bool is_maxi) {
    if (!is_maxi) return s;
    return normalize_maxi(s);
}

/* Feedback rules: G = right char right place. For each char, N = remaining in solution after greens;
   the leftmost N non-green occurrences in guess get P, the rest get B. */
std::string compute_feedback(const std::string& guess, const std::string& solution, int N) {
    std::string result(N, 'B');
    int remaining[256] = {0};
    for (char c : solution) remaining[static_cast<unsigned char>(c)]++;

    /* Greens first: exact matches consume from remaining */
    for (int i = 0; i < N; i++) {
        if (guess[i] == solution[i]) {
            result[i] = 'G';
            remaining[static_cast<unsigned char>(guess[i])]--;
        }
    }

    /* Leftmost N non-green occurrences of each char get P, rest stay B */
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

/* Entropy over indices; subsample if large for interactive speed */
double entropy_of_guess(const std::string& guess,
                       const std::vector<std::string>& all_eqs,
                       const std::vector<size_t>& indices, int N) {
    std::vector<size_t> use;
    if (indices.size() <= ENTROPY_SAMPLE) {
        use = indices;
    } else {
        size_t step = indices.size() / ENTROPY_SAMPLE;
        if (step < 1) step = 1;
        for (size_t i = 0; i < indices.size() && use.size() < ENTROPY_SAMPLE; i += step)
            use.push_back(indices[i]);
    }
    std::unordered_map<std::string, int> pattern_count;
    for (size_t idx : use) {
        std::string fb = compute_feedback(guess, all_eqs[idx], N);
        pattern_count[fb]++;
    }
    double total = static_cast<double>(use.size());
    double h = 0.0;
    for (const auto& kv : pattern_count) {
        double p = kv.second / total;
        h -= p * std::log2(p);
    }
    return h;
}

std::string best_guess(const std::vector<std::string>& all_eqs,
                       const std::vector<size_t>& candidate_indices,
                       const std::unordered_set<size_t>& candidate_set,
                       int N) {
    if (candidate_indices.size() == 0) return "";
    if (candidate_indices.size() == 1) return all_eqs[candidate_indices[0]];

    std::vector<size_t> pool_indices;
    size_t n = all_eqs.size();
    if (n <= (size_t)SEARCH_CAP) {
        for (size_t i = 0; i < n; i++) pool_indices.push_back(i);
    } else {
        /* Prefer candidates in pool when they're few enough */
        if (candidate_indices.size() <= (size_t)SEARCH_CAP * 2) {
            pool_indices = candidate_indices;
            if (pool_indices.size() > (size_t)SEARCH_CAP) {
                size_t step = pool_indices.size() / SEARCH_CAP;
                std::vector<size_t> sub;
                for (size_t i = 0; i < pool_indices.size(); i += step) {
                    sub.push_back(pool_indices[i]);
                    if (sub.size() >= (size_t)SEARCH_CAP) break;
                }
                pool_indices = std::move(sub);
            }
        } else {
            size_t step = n / SEARCH_CAP;
            for (size_t i = 0; i < n && pool_indices.size() < (size_t)SEARCH_CAP; i += step)
                pool_indices.push_back(i);
        }
    }

    size_t best_idx = pool_indices[0];
    double best_h = -1.0;
    for (size_t idx : pool_indices) {
        double h = entropy_of_guess(all_eqs[idx], all_eqs, candidate_indices, N);
        bool is_cand = candidate_set.count(idx) > 0;
        bool best_is_cand = candidate_set.count(best_idx) > 0;
        if (h > best_h || (h == best_h && is_cand && !best_is_cand)) {
            best_h = h;
            best_idx = idx;
        }
    }
    return all_eqs[best_idx];
}

int main(int argc, char** argv) {
    int N = 8;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--len" && i + 1 < argc) {
            N = std::atoi(argv[++i]);
        }
    }

    if (N != 5 && N != 6 && N != 7 && N != 8 && N != 10) {
        std::cerr << "Usage: ./nerdle --len 5|6|7|8|10\n";
        std::cerr << "  5=micro, 6=mini, 7=midi, 8=classic, 10=maxi\n";
        return 1;
    }

    std::string path = "data/equations_" + std::to_string(N) + ".txt";
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << ". Run ./generate";
        if (N == 10) std::cerr << "_maxi";
        else std::cerr << " --len " << N;
        std::cerr << " first.\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    bool is_maxi = (N == 10);
    if (is_maxi) {
        for (auto& eq : equations) eq = normalize_maxi(eq);
    }

    std::string mode = (N == 5) ? "Micro" : (N == 6) ? "Mini" : (N == 7) ? "Midi" : (N == 8) ? "Classic" : "Maxi";
    std::cout << "\n╔═══════════════════════════════╗\n";
    std::cout << "║   N E R D L E   " << mode << std::string(7 - mode.size(), ' ') << "║\n";
    std::cout << "║   " << N << " tiles · " << MAX_TRIES << " tries              ║\n";
    std::cout << "╚═══════════════════════════════╝\n";
    std::cout << "Play on nerdlegame.com. Enter your guess and feedback (G/P/B).\n";
    std::cout << "Type 'y' when correct. Loaded " << equations.size() << " equations.\n\n";

    std::vector<size_t> candidates;
    for (size_t i = 0; i < equations.size(); i++) candidates.push_back(i);
    std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());

    std::string guess = FIRST_GUESS.at(N);
    auto display = [is_maxi](const std::string& s) {
        return is_maxi ? maxi_to_display(s) : s;
    };

    for (int turn = 1; turn <= MAX_TRIES; turn++) {
        std::cout << "Guess " << turn << "/" << MAX_TRIES << "  (" << candidates.size() << " candidates)\n";
        std::cout << "  Suggested: " << display(guess) << "\n";
        std::cout << "  Your guess (Enter=suggested): ";
        std::string user_guess;
        std::getline(std::cin, user_guess);
        if (user_guess == "q" || user_guess == "quit") return 0;
        if (!user_guess.empty() && static_cast<int>(user_guess.size()) >= N) {
            std::string norm = normalize_input(user_guess, is_maxi);
            if (static_cast<int>(norm.size()) == N) guess = norm;
        }
        std::cout << "  Using: " << display(guess) << "\n\n";

        std::cout << "  Feedback (" << N << " chars G/P/B, or 'y' if correct): ";
        std::string feedback;
        std::getline(std::cin, feedback);
        if (feedback == "q" || feedback == "quit") return 0;
        if (feedback == "y" || feedback == "Y") {
            std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
            return 0;
        }
        while (static_cast<int>(feedback.size()) != N) {
            std::cout << "  Enter " << N << " characters: ";
            std::getline(std::cin, feedback);
            if (feedback == "q" || feedback == "quit") return 0;
        }
        for (char& c : feedback) c = (char)std::toupper(c);

        if (feedback == std::string(N, 'G')) {
            std::cout << "\n✓ Solved in " << turn << " guess(es)!\n";
            return 0;
        }

        std::vector<size_t> next_candidates;
        std::unordered_set<size_t> next_set;
        for (size_t idx : candidates) {
            if (is_consistent(equations[idx], guess, feedback, N)) {
                next_candidates.push_back(idx);
                next_set.insert(idx);
            }
        }
        candidates = std::move(next_candidates);
        candidate_set = std::move(next_set);

        if (candidates.empty()) {
            std::cout << "\nNo candidates remain. Check your feedback.\n";
            return 1;
        }

        guess = best_guess(equations, candidates, candidate_set, N);
        std::cout << "\n";
    }

    std::cout << "Out of tries. Possible: " << display(equations[candidates[0]]) << "\n";
    return 0;
}
