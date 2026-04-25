/**
 * For a fixed first guess over data/equations_10.txt, print partition statistics:
 * how many non-empty feedback buckets, and the distribution of bucket sizes.
 */
#include "nerdle_core.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static std::string normalize_tiles(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xc2 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0xb2) {
                out.push_back('\x01');
                i++;
                continue;
            }
            if (d == 0xb3) {
                out.push_back('\x02');
                i++;
                continue;
            }
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "data/equations_10.txt";
    const char* open_str = (argc > 2) ? argv[2] : "58+2-13=47";
    int N = 10;
    const int P = nerdle::pow3_table(N);
    if (P <= 0) {
        std::cerr << "bad N\n";
        return 1;
    }
    const std::string g_str = normalize_tiles(open_str);
    if (static_cast<int>(g_str.size()) != N) {
        std::cerr << "guess must be " << N << " chars after normalize; got " << g_str.size() << "\n";
        return 1;
    }
    const char* g = g_str.c_str();

    std::vector<int> count(static_cast<size_t>(P), 0);
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::string line;
    size_t total = 0;
    while (std::getline(f, line)) {
        if (line.empty())
            continue;
        const std::string s = normalize_tiles(line);
        if (static_cast<int>(s.size()) != N)
            continue;
        const uint32_t c = nerdle::compute_feedback_packed(g, s.c_str(), N);
        count[static_cast<size_t>(c)]++;
        total++;
    }
    f.close();

    std::vector<int> sizes;
    sizes.reserve(static_cast<size_t>(P));
    long long sum_sz = 0;
    for (int c = 0; c < P; c++) {
        const int k = count[static_cast<size_t>(c)];
        if (k == 0)
            continue;
        sizes.push_back(k);
        sum_sz += k;
    }
    std::sort(sizes.begin(), sizes.end());
    const int B = static_cast<int>(sizes.size());
    if (B == 0) {
        std::cerr << "no data\n";
        return 1;
    }

    const double mean = static_cast<double>(sum_sz) / static_cast<double>(B);
    double var = 0.0;
    for (int s : sizes)
        var += (static_cast<double>(s) - mean) * (static_cast<double>(s) - mean);
    var /= static_cast<double>(B);
    const double sd = std::sqrt(std::max(0.0, var));

    auto at_pct = [&](double p) {
        if (sizes.size() == 1)
            return sizes[0];
        const double t = p * 0.01 * static_cast<double>(sizes.size() - 1);
        const size_t i = static_cast<size_t>(std::floor(t + 1e-9));
        return sizes[std::min(i, sizes.size() - 1)];
    };

    int singletons = 0, mx = 0, mn = sizes[0];
    for (int s : sizes) {
        if (s == 1)
            singletons++;
        mx = std::max(mx, s);
        mn = std::min(mn, s);
    }

    // Histogram on bucket *sizes* (how many cells have 1, 2, ... in each bin)
    auto hist = [&](int lo, int hi) {
        int n = 0;
        for (int s : sizes)
            if (s >= lo && s <= hi)
                n++;
        return n;
    };

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "First guess: " << open_str << "  (normalized len " << g_str.size() << ")\n";
    std::cout << "Pool: " << path << "  |S| = " << total << "\n";
    std::cout << "Distinct feedback patterns (partitions) B = " << B << "  (of " << P << " = 3^" << N
              << " possible)\n\n";

    std::cout << "Bucket size (cell mass) over those B non-empty cells:\n";
    std::cout << "  min  = " << mn << "\n";
    std::cout << "  max  = " << mx << "\n";
    std::cout << "  mean = " << mean << "  (E[mass] if secret uniform — same as mean for partition)\n";
    std::cout << "  s.d. = " << sd << "\n\n";

    std::cout << "Percentiles of bucket size (B sorted bucket masses):\n";
    std::cout << "  p10=" << at_pct(10) << "  p25=" << at_pct(25) << "  p50=" << at_pct(50) << "  p75=" << at_pct(75)
              << "  p90=" << at_pct(90) << "  p95=" << at_pct(95) << "  p99=" << at_pct(99) << "\n\n";

    std::cout << "Buckets of size 1 (only one secret consistent): " << singletons << "  (" << (100.0 * singletons / B)
              << " % of patterns)\n\n";

    std::cout << "Histogram: number of *patterns* whose bucket has mass in [lo,hi] (sum = B):\n";
    struct {
        int lo, hi;
        const char* lab;
    } bands[] = {
        {1, 1, "1"},
        {2, 2, "2"},
        {3, 3, "3"},
        {4, 4, "4"},
        {5, 7, "5-7"},
        {8, 16, "8-16"},
        {17, 32, "17-32"},
        {33, 64, "33-64"},
        {65, 128, "65-128"},
        {129, std::numeric_limits<int>::max(), "129+"},
    };
    for (const auto& b : bands) {
        const int n = hist(b.lo, b.hi);
        if (n == 0)
            continue;
        std::cout << "  " << std::setw(6) << b.lab << "  " << n << "  (" << (100.0 * n / B) << " % of patterns)\n";
    }
    return 0;
}
