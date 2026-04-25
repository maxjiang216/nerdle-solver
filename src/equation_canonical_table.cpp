/**
 * Emit a TSV table of equations in canonical order with tuple keys (see equation_canonical.hpp):
 *   distinct, purple, green, partition; plus lexicographic.
 *
 * Usage: ./equation_canonical_table data/equations_5.txt [--out path] [--max N]
 *        --max 0 means no limit (default 0).
 */

#include "equation_canonical.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string in_path;
    std::string out_path;
    size_t max_rows = 0;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--max" && i + 1 < argc) {
            max_rows = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            return 1;
        } else if (in_path.empty()) {
            in_path = a;
        } else {
            std::cerr << "Unexpected argument: " << a << "\n";
            return 1;
        }
    }

    if (in_path.empty()) {
        std::cerr << "Usage: equation_canonical_table <equations.txt> [--out file.tsv] [--max N]\n";
        return 1;
    }

    std::ifstream fin(in_path);
    if (!fin) {
        std::cerr << "Cannot open " << in_path << "\n";
        return 1;
    }

    std::vector<std::string> eqs;
    std::string line;
    while (std::getline(fin, line)) {
        if (!line.empty())
            eqs.push_back(line);
    }
    fin.close();

    if (eqs.empty()) {
        std::cerr << "No equations.\n";
        return 1;
    }

    std::vector<nerdle::CanonicalEqKey> keys = nerdle::compute_canonical_keys(eqs);
    std::vector<size_t> ord(eqs.size());
    for (size_t i = 0; i < ord.size(); i++)
        ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        return nerdle::canonical_less(a, b, eqs, keys);
    });

    std::ostream* out = &std::cout;
    std::ofstream fout;
    if (!out_path.empty()) {
        fout.open(out_path);
        if (!fout) {
            std::cerr << "Cannot write " << out_path << "\n";
            return 1;
        }
        out = &fout;
    }

    *out << std::setprecision(17);
    *out << "equation\tdistinct_symbols\tpurple\tgreen\tpartition\n";

    size_t n = ord.size();
    if (max_rows > 0 && max_rows < n)
        n = max_rows;

    for (size_t r = 0; r < n; r++) {
        size_t idx = ord[r];
        const nerdle::CanonicalEqKey& k = keys[idx];
        *out << eqs[idx] << '\t' << k.distinct << '\t' << k.purple << '\t' << k.green << '\t' << k.partition
             << '\n';
    }

    if (!out_path.empty())
        std::cout << "Wrote " << n << " rows to " << out_path << "\n";
    return 0;
}
