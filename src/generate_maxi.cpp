/**
 * Generate all valid 10-character Maxi Nerdle equations.
 * 10 tiles: 0-9, +, -, *, /, (, ), ², ³, =
 * Rules: no standalone 0, no negative result, no adjacent operators.
 *
 * Compile: g++ -O3 -std=c++17 -fopenmp -o generate_maxi generate_maxi.cpp
 * Run:     ./generate_maxi [--no-pointless-brackets]
 *
 * --no-pointless-brackets: exclude (n), ((e)), (a+b)+c, (a*b)*c, (2)², etc.
 */

#include "equation_canonical.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

static bool no_pointless_brackets = false;

// Placeholders during generation; ² and ³ are 2 bytes UTF-8
static const char PLACE_SQ = '\x01';
static const char PLACE_CB = '\x02';

static inline int int_len(long long n) {
    if (n < 10) return 1;
    if (n < 100) return 2;
    if (n < 1000) return 3;
    if (n < 10000) return 4;
    if (n < 100000) return 5;
    if (n < 1000000) return 6;
    return 7;
}

// ---------------------------------------------------------------------------
// Parser: expr = term (+ term | - term)*, term = expon (* expon | / expon)*,
// expon = factor (²|³)*, factor = number | (expr)
// ---------------------------------------------------------------------------
struct MaxiParser {
    const char* s;
    const char* end;

    MaxiParser(const char* buf, int len) : s(buf), end(buf + len) {}

    bool eof() const { return s >= end; }
    char peek() const { return eof() ? 0 : *s; }
    char get() { return eof() ? 0 : *s++; }

    long long parse_number() {
        if (eof() || !std::isdigit(peek())) return -1;
        long long n = get() - '0';
        while (!eof() && std::isdigit(peek())) {
            n = n * 10 + (get() - '0');
            if (n > 999999999) return -1;
        }
        return n;
    }

    long long parse_factor() {
        if (peek() >= '0' && peek() <= '9')
            return parse_number();
        if (peek() == '(') {
            get();
            long long r = parse_expr();
            if (peek() != ')') return -1;
            get();
            return r;
        }
        return -1;
    }

    long long parse_expon() {
        long long r = parse_factor();
        if (r < 0) return -1;
        while (peek() == PLACE_SQ || peek() == PLACE_CB) {
            char c = get();
            if (c == PLACE_SQ)
                r = r * r;
            else
                r = r * r * r;
            if (r < 0 || r > 999999999) return -1;
        }
        return r;
    }

    long long parse_term() {
        long long r = parse_expon();
        if (r < 0) return -1;
        while (peek() == '*' || peek() == '/') {
            char op = get();
            long long u = parse_expon();
            if (u < 0) return -1;
            if (op == '*')
                r *= u;
            else {
                if (u == 0) return -1;
                if (r % u != 0) return -1;
                r /= u;
            }
        }
        return r;
    }

    long long parse_expr() {
        long long r = parse_term();
        if (r < 0) return -1;
        while (peek() == '+' || peek() == '-') {
            char op = get();
            long long u = parse_term();
            if (u < 0) return -1;
            r = (op == '+') ? (r + u) : (r - u);
            if (r < 0) return -1;
        }
        return r;
    }

    long long eval() {
        long long r = parse_expr();
        return (!eof() || r < 0) ? -1 : r;
    }
};

// Returns true if any brackets are pointless.
// Definition: brackets are pointless iff removing them yields the same evaluation.
// O(n) per paren pair: try removing each ( ) pair, re-parse, compare eval.
static bool has_pointless_brackets(const char* buf, int len, long long original_result) {
    for (int i = 0; i < len; i++) {
        if (buf[i] != '(') continue;
        int depth = 1, j = i + 1;
        while (j < len && depth > 0) {
            if (buf[j] == '(') depth++;
            else if (buf[j] == ')') depth--;
            j++;
        }
        if (depth != 0) continue;
        int close = j - 1;
        /* Build lhs without this ( ) pair */
        std::string stripped;
        stripped.reserve(len - 2);
        for (int k = 0; k < len; k++) {
            if (k == i || k == close) continue;
            stripped += buf[k];
        }
        MaxiParser p(stripped.data(), static_cast<int>(stripped.size()));
        long long ev = p.eval();
        if (ev == original_result) return true;  /* Removing these parens changes nothing */
    }
    return false;
}

// Convert placeholder string to final UTF-8 equation
static void to_equation(const char* buf, int len, long long rhs, std::string& out) {
    out.clear();
    out.reserve(len + 2 + 7);  // lhs + '=' + rhs_str
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == PLACE_SQ)
            out += "\xc2\xb2";  // ²
        else if (c == PLACE_CB)
            out += "\xc2\xb3";  // ³
        else
            out += c;
    }
    out += '=';
    out += std::to_string(rhs);
}

// ---------------------------------------------------------------------------
// Recursive generator (based on C solution). LHS max 8 chars; total 10.
// buf[0..level) is built; we need level + 1 + rhs_len == 10.
// ---------------------------------------------------------------------------
struct Generator {
    std::vector<std::string>& results;
    char buf[24];
    int br_level;

    explicit Generator(std::vector<std::string>& r) : results(r), br_level(0) { buf[0] = '\0'; }

    void gen_equals(int level);
    void gen_close(int level);
    void gen_squared(int level);
    void gen_cubed(int level);
    void gen_oper(int level);
    void gen_open(int level);
    void gen_digit(int level, int ndigits);
    void gen_nz_digit(int level);
};

// Returns true if LHS contains at least one operator (no bare number)
static bool has_operator(const char* buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] == '+' || buf[i] == '-' || buf[i] == '*' || buf[i] == '/')
            return true;
    }
    return false;
}

void Generator::gen_equals(int level) {
    if (br_level != 0) return;
    buf[level] = '\0';
    if (!has_operator(buf, level)) return;  // LHS must have at least one operator
    MaxiParser p(buf, level);
    long long result = p.eval();
    if (result >= 0) {
        int rhs_len = int_len(result);
        if (level + 1 + rhs_len == 10) {
            if (no_pointless_brackets && has_pointless_brackets(buf, level, result)) return;
            std::string eq;
            to_equation(buf, level, result, eq);
            results.push_back(eq);
        }
    }
}

void Generator::gen_close(int level) {
    if (level >= 9 || br_level <= 0) return;
    buf[level] = ')';
    br_level--;
    gen_equals(level + 1);
    gen_oper(level + 1);
    gen_squared(level + 1);
    gen_cubed(level + 1);
    if (br_level > 0) gen_close(level + 1);
    br_level++;
}

void Generator::gen_squared(int level) {
    if (level >= 9) return;
    buf[level] = PLACE_SQ;
    if (level >= 3) gen_equals(level + 1);
    gen_oper(level + 1);
    if (br_level > 0) gen_close(level + 1);
}

void Generator::gen_cubed(int level) {
    if (level >= 9) return;
    buf[level] = PLACE_CB;
    if (level >= 2) gen_equals(level + 1);
    gen_oper(level + 1);
    if (br_level > 0) gen_close(level + 1);
}

void Generator::gen_oper(int level) {
    if (level >= 8) return;
    for (char op : {'+', '-', '*', '/'}) {
        buf[level] = op;
        gen_nz_digit(level + 1);
        gen_open(level + 1);
    }
}

void Generator::gen_open(int level) {
    if (level >= 8) return;
    buf[level] = '(';
    br_level++;
    gen_nz_digit(level + 1);
    gen_open(level + 1);
    br_level--;
}

void Generator::gen_digit(int level, int ndigits) {
    if (level >= 9) return;
    if (ndigits >= 4) return;

    for (int d = 1; d <= 9; d++) {
        buf[level] = '0' + d;
        gen_equals(level + 1);
        gen_digit(level + 1, ndigits + 1);
        gen_oper(level + 1);
        gen_squared(level + 1);
        gen_cubed(level + 1);
        if (br_level > 0) gen_close(level + 1);
    }
    buf[level] = '0';
    gen_equals(level + 1);
    gen_digit(level + 1, ndigits + 1);
    gen_oper(level + 1);
    gen_squared(level + 1);
    gen_cubed(level + 1);
    if (br_level > 0) gen_close(level + 1);
}

void Generator::gen_nz_digit(int level) {
    if (level >= 9) return;
    for (int d = 1; d <= 9; d++) {
        buf[level] = '0' + d;
        gen_equals(level + 1);
        gen_digit(level + 1, 0);
        gen_oper(level + 1);
        gen_squared(level + 1);
        gen_cubed(level + 1);
        if (br_level > 0) gen_close(level + 1);
    }
}

// Partition for parallelism: first branch determines disjoint subsets
void run_branch(int first_digit, std::vector<std::string>& out) {
    Generator gen(out);
    gen.buf[0] = '0' + first_digit;
    gen.gen_equals(1);
    gen.gen_digit(1, 0);
    gen.gen_oper(1);
    gen.gen_squared(1);
    gen.gen_cubed(1);
}

/* Maxi Nerdle solutions never start with ( - skip that branch */

int main(int argc, char** argv) {
    std::string out_dir = "data";
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--no-pointless-brackets") {
            no_pointless_brackets = true;
        } else if ((std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output-dir") && i + 1 < argc) {
            out_dir = argv[++i];
        }
    }
    std::cout << "Generating Maxi Nerdle equations (10 tiles)";
    if (no_pointless_brackets) std::cout << " [no pointless brackets]";
    std::cout << "...\n";

    std::vector<std::vector<std::string>> all_results;

#ifdef _OPENMP
#pragma omp parallel
    {
        std::vector<std::string> local;
        local.reserve(300000);  // ~2.2M total, ~275k per thread

#pragma omp for schedule(static)
        for (int first = 1; first <= 9; first++) {
            run_branch(first, local);
        }

#pragma omp critical
        all_results.push_back(std::move(local));
    }
#else
    std::vector<std::string> local;
    local.reserve(2500000);
    for (int d = 1; d <= 9; d++) run_branch(d, local);
    all_results.push_back(std::move(local));
#endif

    size_t total = 0;
    for (const auto& v : all_results) total += v.size();

    std::vector<std::string> combined;
    combined.reserve(total);
    for (auto& v : all_results) {
        for (auto& s : v) combined.push_back(std::move(s));
    }
    all_results.clear();
    all_results.shrink_to_fit();

    std::sort(combined.begin(), combined.end());
    auto last = std::unique(combined.begin(), combined.end());
    combined.erase(last, combined.end());
    nerdle::sort_equations_canonical(combined);

    std::string out_path = out_dir + "/equations_10.txt";
    std::ofstream f(out_path);
    if (!f) {
        std::cerr << "Cannot write " << out_path << "\n";
        return 1;
    }
    for (const auto& eq : combined) {
        f << eq << '\n';
    }
    f.close();

    std::cout << "Found " << combined.size() << " valid equations.\n";
    std::cout << "Saved to " << out_path << "\n";
    return 0;
}
