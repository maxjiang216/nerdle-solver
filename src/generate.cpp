/**
 * Generate all valid N-character Nerdle equations.
 * Uses OpenMP for parallel evaluation. No standalone 0 on LHS by default.
 *
 * Evaluation matches official Nerdle for Micro…Classic (no ²/³, no parens in this generator):
 * - Standard precedence: * and / before + and -, left-associative at each level.
 * - True division for / (8/3*6=16 is valid; integer division is not used).
 * - After each + or - step, a negative running value is allowed (1-3+11=9).
 * - The final evaluated LHS value must be a non-negative integer (the RHS in the file).
 * - No unary + or -: the LHS is built only from number tokens and binary operators, so
 *   expressions do not start with a sign; the evaluator also rejects a leading - or +.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o generate generate.cpp
 * Run:     ./generate              # classic 8-char (LHS has ≥1 op, no standalone 0)
 *          ./generate --len 5     # micro 5-char
 *          ./generate --allow-standalone-zero   # include 0+1=1, etc.
 *          ./generate --allow-bare              # include bare LHS like 18=18
 */

#include "equation_canonical.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

static bool allow_standalone_zero = false;
static bool exclude_zero_result = false;
static bool allow_bare = false;  // If false, LHS must have at least one operator
static std::string out_dir = "data";

// ---------------------------------------------------------------------------
// Expression evaluator (operator precedence: * / before + -)
// Uses true float division. Returns -1 on error or if the final value is not a non-negative
// integer. Intermediate values after + or - may be negative; intermediate terms may be fractional.
// ---------------------------------------------------------------------------

struct Parser {
    const std::string& s;
    size_t i = 0;

    explicit Parser(const std::string& s_) : s(s_) {}

    bool eof() const { return i >= s.size(); }
    char peek() const { return eof() ? 0 : s[i]; }
    char get() { return eof() ? 0 : s[i++]; }

    long long parse_number() {
        if (eof() || !std::isdigit(peek())) return -1;
        long long n = 0;
        while (!eof() && std::isdigit(peek())) {
            n = n * 10 + (get() - '0');
            if (n > 999999999) return -1;
        }
        return n;
    }

    // term = number (* number | / number)*  (use double for division to match Python)
    double parse_term() {
        long long a = parse_number();
        if (a < 0) return -1.0;  // signal error
        double result = static_cast<double>(a);
        while (!eof()) {
            char op = peek();
            if (op == '*' || op == '/') {
                get();
                long long b = parse_number();
                if (b < 0) return -1;
                if (op == '*') result *= b;
                else {
                    if (b == 0) return -1.0;
                    result /= b;
                }
            } else break;
        }
        return result;
    }

    // expr = term (+ term | - term)*  (allow negative intermediate, check final only)
    double parse_expr() {
        double a = parse_term();
        if (a < 0) return -1.0;
        while (!eof()) {
            char op = peek();
            if (op == '+' || op == '-') {
                get();
                double b = parse_term();
                if (b < 0) return -1.0;
                a = (op == '+') ? (a + b) : (a - b);
            } else break;
        }
        return eof() ? a : -1.0;
    }
};

// Returns result if valid (non-negative integer), else -1
long long safe_eval(const std::string& expr) {
    if (expr.empty() || !std::isdigit(static_cast<unsigned char>(expr[0])))
        return -1; // no unary + or - (matches Nerdle: no leading sign without parens; we have no parens)
    Parser p(expr);
    double r = p.parse_expr();
    if (r < 0 || !p.eof() || r != r) return -1;  // NaN check, negative result
    long long ir = static_cast<long long>(std::round(r));
    if (std::abs(static_cast<double>(ir) - r) > 1e-9) return -1;  // non-integer
    return ir;
}

// ---------------------------------------------------------------------------
// Valid number generators
// ---------------------------------------------------------------------------

void collect_numbers(int length, bool allow_zero, std::vector<std::string>& out) {
    if (length == 1) {
        int start = allow_zero ? 0 : 1;
        for (int d = start; d <= 9; d++) {
            out.push_back(std::string(1, '0' + d));
        }
    } else if (length >= 2) {
        long long pow10 = 1;
        for (int j = 0; j < length - 1; j++) pow10 *= 10;
        for (int first = 1; first <= 9; first++) {
            std::string prefix(1, '0' + first);
            int rest_len = length - 1;
            for (long long i = 0; i < pow10; i++) {
                std::string n = prefix;
                for (int j = rest_len - 1; j >= 0; j--) {
                    long long div = 1;
                    for (int k = 0; k < j; k++) div *= 10;
                    n += '0' + (int)((i / div) % 10);
                }
                out.push_back(n);
            }
        }
    }
}

void collect_expressions(int length, bool no_standalone_zero,
                         bool require_op, std::vector<std::string>& out) {
    bool allow_zero = !no_standalone_zero;

    if (!require_op) {
        std::vector<std::string> nums;
        collect_numbers(length, allow_zero, nums);
        for (const auto& n : nums) out.push_back(n);
    }

    for (int num_len = 1; num_len < length - 1; num_len++) {
        int sub_len = length - num_len - 1;
        if (sub_len < 1) continue;
        std::vector<std::string> nums_sub;
        collect_numbers(num_len, allow_zero, nums_sub);
        std::vector<std::string> sub_exprs;
        collect_expressions(sub_len, no_standalone_zero, false, sub_exprs);
        for (const auto& num : nums_sub) {
            for (const char* op : {"+", "-", "*", "/"}) {
                for (const auto& sub : sub_exprs) {
                    out.push_back(num + op + sub);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main generation
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    int eq_len = 8;
    allow_standalone_zero = false;
    exclude_zero_result = false;
    allow_bare = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--len" && i + 1 < argc) {
            eq_len = std::atoi(argv[++i]);
        } else if (arg == "--allow-standalone-zero") {
            allow_standalone_zero = true;
        } else if (arg == "--allow-bare") {
            allow_bare = true;
        } else if (arg == "--no-zero") {
            exclude_zero_result = true;
        } else if ((arg == "-o" || arg == "--output-dir") && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }

    if (eq_len < 5 || eq_len > 8) {
        std::cerr << "Length must be 5, 6, 7, or 8.\n";
        return 1;
    }

    std::cout << "Generating " << eq_len << "-character Nerdle equations";
    if (!allow_bare) std::cout << " (LHS has ≥1 operator)";
    if (!allow_standalone_zero) std::cout << " (no standalone 0 on LHS)";
    std::cout << "...\n";

    // Collect all (lhs, rhs_len) work items
    struct WorkItem {
        std::string lhs;
        int rhs_len;
    };
    std::vector<WorkItem> work;

    for (int eq_pos = 1; eq_pos < eq_len - 1; eq_pos++) {
        int lhs_len = eq_pos;
        int rhs_len = eq_len - eq_pos - 1;
        std::vector<std::string> lhs_exprs;
        collect_expressions(lhs_len, !allow_standalone_zero, !allow_bare, lhs_exprs);
        for (const auto& lhs : lhs_exprs) {
            work.push_back({lhs, rhs_len});
        }
    }

    std::vector<std::string> results;
    results.reserve(work.size() / 4);  // rough estimate

#ifdef _OPENMP
#pragma omp parallel
    {
        std::vector<std::string> local;
#pragma omp for schedule(dynamic, 256) nowait
        for (size_t i = 0; i < work.size(); i++) {
            const auto& item = work[i];
            long long val = safe_eval(item.lhs);
            if (val < 0) continue;
            if (exclude_zero_result && val == 0) continue;
            std::string rhs = std::to_string(val);
            if ((int)rhs.size() == item.rhs_len) {
                local.push_back(item.lhs + "=" + rhs);
            }
        }
#pragma omp critical
        for (const auto& eq : local) results.push_back(eq);
    }
#else
    for (const auto& item : work) {
        long long val = safe_eval(item.lhs);
        if (val < 0) continue;
        if (exclude_zero_result && val == 0) continue;
        std::string rhs = std::to_string(val);
        if ((int)rhs.size() == item.rhs_len) {
            results.push_back(item.lhs + "=" + rhs);
        }
    }
#endif

    // Deduplicate and sort (canonical pool order; see equation_canonical.hpp)
    std::set<std::string> unique(results.begin(), results.end());
    std::vector<std::string> eqs(unique.begin(), unique.end());
    nerdle::sort_equations_canonical(eqs);

    std::string out_path = out_dir + "/equations_" + std::to_string(eq_len) + ".txt";
    std::ofstream f(out_path);
    if (!f) {
        std::cerr << "Cannot write " << out_path << "\n";
        return 1;
    }
    for (const auto& eq : eqs) {
        f << eq << '\n';
    }
    f.close();

    std::cout << "Found " << eqs.size() << " valid equations.\n";
    std::cout << "Saved to " << out_path << "\n";

    return 0;
}
