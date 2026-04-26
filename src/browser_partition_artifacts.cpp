/**
 * Emit static JSON artifacts for the browser-only partition solver under web/data/partition/.
 *
 * Usage:
 *   ./browser_partition_artifacts --pool data/equations_8.txt --kind classic --out web/data/partition
 *   ./browser_partition_artifacts --pool data/equations_6.txt --kind binerdle --out web/data/partition
 *   ./browser_partition_artifacts --pool data/equations_10.txt --kind classic --out web/data/partition --opening-buckets-only
 *   ./browser_partition_artifacts --write-maxi-manifest-only --out web/data/partition
 *
 * For each (pool, opening from partition_fixed_opening_tie6), writes:
 *   {out}/{kind}_n{n}/manifest.json
 *   {out}/{kind}_n{n}/b/{feedback_code}.json   (nonempty buckets only)
 *   {out}/{kind}_n{n}/pool_full.json           (optional, n <= 8 only)
 */

#include "equation_canonical.hpp"
#include "nerdle_core.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr unsigned char PLACE_SQ = '\x01';
constexpr unsigned char PLACE_CB = '\x02';

std::string normalize_maxi(std::string s) {
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2) {
            if ((unsigned char)s[i + 1] == 0xB2) {
                out += (char)PLACE_SQ;
                i++;
                continue;
            }
            if ((unsigned char)s[i + 1] == 0xB3) {
                out += (char)PLACE_CB;
                i++;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string maxi_to_display(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == PLACE_SQ)
            out += "\xc2\xb2";
        else if (c == PLACE_CB)
            out += "\xc2\xb3";
        else
            out += c;
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    o.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\b':
            o += "\\b";
            break;
        case '\f':
            o += "\\f";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else
                o += (char)c;
        }
    }
    o.push_back('"');
    return o;
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("cannot write " + path);
    f << content;
}

void ensure_dir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        throw std::runtime_error("mkdir failed: " + dir + ": " + ec.message());
}

} // namespace

int main(int argc, char** argv) {
    std::string pool_path;
    std::string kind = "classic";
    std::string out_root = "web/data/partition";
    bool manifest_only = false;
    bool opening_buckets_only = false;
    bool write_maxi_manifest_only = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--pool" && i + 1 < argc)
            pool_path = argv[++i];
        else if (a == "--kind" && i + 1 < argc)
            kind = argv[++i];
        else if (a == "--out" && i + 1 < argc)
            out_root = argv[++i];
        else if (a == "--manifest-only")
            manifest_only = true;
        else if (a == "--opening-buckets-only")
            opening_buckets_only = true;
        else if (a == "--write-maxi-manifest-only")
            write_maxi_manifest_only = true;
        else if (a == "-h" || a == "--help") {
            std::cerr << "Usage: browser_partition_artifacts --pool data/equations_N.txt [--kind classic|binerdle|quad] "
                         "[--out web/data/partition] [--manifest-only|--opening-buckets-only]\n"
                         "       browser_partition_artifacts --write-maxi-manifest-only [--out web/data/partition]\n";
            return 1;
        }
    }

    if (write_maxi_manifest_only) {
        constexpr int N = 10;
        constexpr const char* kPoolSizeStr = "2102375";
        const char* opening_c = nerdle::partition_fixed_opening_tie6(N);
        if (!opening_c) {
            std::cerr << "internal: no partition opening for N=" << N << "\n";
            return 1;
        }
        std::string opening(opening_c);
        std::string opening_display = maxi_to_display(opening);
        std::string subdir = out_root + "/classic_n" + std::to_string(N);
        ensure_dir(subdir);
        std::error_code ec;
        std::filesystem::remove_all(subdir + "/b", ec);
        std::filesystem::remove(subdir + "/pool_full.json", ec);

        std::ostringstream man;
        man << "{\n";
        man << "  \"version\": 1,\n";
        man << "  \"kind\": \"classic\",\n";
        man << "  \"n\": " << N << ",\n";
        man << "  \"opening\": " << json_escape(opening_display) << ",\n";
        man << "  \"poolSize\": " << kPoolSizeStr << ",\n";
        man << "  \"bucketDir\": \"b\",\n";
        man << "  \"hasPoolFull\": false,\n";
        man << "  \"hasOpeningBuckets\": false\n";
        man << "}\n";
        write_file(subdir + "/manifest.json", man.str());

        std::cout << "Wrote static Maxi manifest under " << subdir << " (opening " << opening_display << ", pool "
                  << kPoolSizeStr << ")\n";
        return 0;
    }

    if (pool_path.empty()) {
        std::cerr << "missing --pool\n";
        return 1;
    }
    if (kind != "classic" && kind != "binerdle" && kind != "quad") {
        std::cerr << "kind must be classic, binerdle, or quad\n";
        return 1;
    }

    std::ifstream pf(pool_path);
    if (!pf) {
        std::cerr << "cannot open " << pool_path << "\n";
        return 1;
    }

    std::vector<std::string> equations;
    std::string line;
    while (std::getline(pf, line)) {
        if (!line.empty())
            equations.push_back(line);
    }
    pf.close();

    if (equations.empty()) {
        std::cerr << "empty pool\n";
        return 1;
    }

    /* UTF-8 ²/³ are two bytes each; normalize before length checks so maxi pools agree on N=10. */
    for (auto& e : equations)
        e = normalize_maxi(e);

    const int N = static_cast<int>(equations[0].size());
    for (const auto& e : equations) {
        if (static_cast<int>(e.size()) != N) {
            std::cerr << "mixed lengths in pool\n";
            return 1;
        }
    }

    const bool is_maxi = (N == 10);

    const char* opening_c = nerdle::partition_fixed_opening_tie6(N);
    if (!opening_c) {
        std::cerr << "no partition opening for N=" << N << "\n";
        return 1;
    }
    std::string opening(opening_c);
    if (static_cast<int>(opening.size()) != N) {
        std::cerr << "opening length mismatch\n";
        return 1;
    }

    /* Map opening string to index (for sanity). */
    size_t opening_idx = SIZE_MAX;
    for (size_t i = 0; i < equations.size(); i++) {
        if (equations[i] == opening) {
            opening_idx = i;
            break;
        }
    }
    if (opening_idx == SIZE_MAX) {
        std::cerr << "opening \"" << opening << "\" not in pool " << pool_path << "\n";
        return 1;
    }

    std::string subdir = out_root + "/" + kind + "_n" + std::to_string(N);
    ensure_dir(subdir);

    std::string opening_display = is_maxi ? maxi_to_display(opening) : opening;

    if (manifest_only) {
        std::error_code ec;
        std::filesystem::remove_all(subdir + "/b", ec);
        std::filesystem::remove(subdir + "/pool_full.json", ec);

        std::ostringstream man;
        man << "{\n";
        man << "  \"version\": 1,\n";
        man << "  \"kind\": " << json_escape(kind) << ",\n";
        man << "  \"n\": " << N << ",\n";
        man << "  \"opening\": " << json_escape(opening_display) << ",\n";
        man << "  \"poolSize\": " << equations.size() << ",\n";
        man << "  \"bucketDir\": \"b\",\n";
        man << "  \"hasPoolFull\": false,\n";
        man << "  \"hasOpeningBuckets\": false\n";
        man << "}\n";
        write_file(subdir + "/manifest.json", man.str());

        std::cout << "Wrote manifest-only artifact under " << subdir << " (opening "
                  << opening_display << ", pool " << equations.size() << ")\n";
        return 0;
    }

    if (opening_buckets_only) {
        std::filesystem::remove(subdir + "/pool_full.json");
        ensure_dir(subdir + "/b");

        std::unordered_map<uint32_t, std::vector<size_t>> buckets;
        for (size_t si = 0; si < equations.size(); si++) {
            uint32_t code = nerdle::compute_feedback_packed(opening, equations[si], N);
            buckets[code].push_back(si);
        }

        for (auto& kv : buckets) {
            uint32_t code = kv.first;
            auto& vec = kv.second;
            std::sort(vec.begin(), vec.end());

            std::ostringstream body;
            body << "[\n";
            for (size_t i = 0; i < vec.size(); i++) {
                size_t id = vec[i];
                std::string disp = is_maxi ? maxi_to_display(equations[id]) : equations[id];
                body << "  [" << id << "," << id << "," << json_escape(disp) << "]";
                if (i + 1 < vec.size())
                    body << ",";
                body << "\n";
            }
            body << "]";
            write_file(subdir + "/b/" + std::to_string(code) + ".json", body.str());
        }

        std::ostringstream man;
        man << "{\n";
        man << "  \"version\": 1,\n";
        man << "  \"kind\": " << json_escape(kind) << ",\n";
        man << "  \"n\": " << N << ",\n";
        man << "  \"opening\": " << json_escape(opening_display) << ",\n";
        man << "  \"poolSize\": " << equations.size() << ",\n";
        man << "  \"bucketDir\": \"b\",\n";
        man << "  \"hasPoolFull\": false,\n";
        man << "  \"hasOpeningBuckets\": true\n";
        man << "}\n";
        write_file(subdir + "/manifest.json", man.str());

        std::cout << "Wrote " << buckets.size() << " opening buckets under " << subdir << " (opening "
                  << opening_display << ")\n";
        return 0;
    }

    const std::vector<nerdle::CanonicalEqKey>& ckeys = nerdle::canonical_keys_for_pool(equations);
    std::vector<size_t> ord(equations.size());
    for (size_t i = 0; i < ord.size(); i++)
        ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        return nerdle::canonical_less(a, b, equations, ckeys);
    });
    std::vector<int> rank(equations.size());
    for (size_t k = 0; k < ord.size(); k++)
        rank[static_cast<size_t>(ord[k])] = static_cast<int>(k);

    std::unordered_map<uint32_t, std::vector<size_t>> buckets;
    for (size_t si = 0; si < equations.size(); si++) {
        uint32_t code = nerdle::compute_feedback_packed(opening, equations[si], N);
        buckets[code].push_back(si);
    }

    ensure_dir(subdir + "/b");

    for (auto& kv : buckets) {
        uint32_t code = kv.first;
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end());

        std::ostringstream body;
        body << "[\n";
        for (size_t i = 0; i < vec.size(); i++) {
            size_t id = vec[i];
            std::string disp = is_maxi ? maxi_to_display(equations[id]) : equations[id];
            body << "  [" << id << "," << rank[id] << "," << json_escape(disp) << "]";
            if (i + 1 < vec.size())
                body << ",";
            body << "\n";
        }
        body << "]";
        write_file(subdir + "/b/" + std::to_string(code) + ".json", body.str());
    }

    bool has_full = (N <= 8);
    {
        std::ostringstream man;
        man << "{\n";
        man << "  \"version\": 1,\n";
        man << "  \"kind\": " << json_escape(kind) << ",\n";
        man << "  \"n\": " << N << ",\n";
        man << "  \"opening\": " << json_escape(opening_display) << ",\n";
        man << "  \"poolSize\": " << equations.size() << ",\n";
        man << "  \"bucketDir\": \"b\",\n";
        man << "  \"hasPoolFull\": " << (has_full ? "true" : "false") << ",\n";
        man << "  \"hasOpeningBuckets\": true\n";
        man << "}\n";
        write_file(subdir + "/manifest.json", man.str());
    }

    if (has_full) {
        std::ostringstream full;
        full << "[\n";
        for (size_t id = 0; id < equations.size(); id++) {
            std::string disp = is_maxi ? maxi_to_display(equations[id]) : equations[id];
            full << "  [" << id << "," << rank[id] << "," << json_escape(disp) << "]";
            if (id + 1 < equations.size())
                full << ",";
            full << "\n";
        }
        full << "]";
        write_file(subdir + "/pool_full.json", full.str());
    }

    std::cout << "Wrote " << buckets.size() << " buckets under " << subdir << " (opening "
              << opening_display << ")\n";
    return 0;
}
