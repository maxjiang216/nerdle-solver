/**
 * JSON API for the web UI: read one request object from stdin, write one JSON object to stdout.
 *
 * Request:
 * {
 *   "kind": "classic" | "binerdle" | "quad",
 *   "n": 5-10 (classic: 5,6,7,8,10; binerdle: 6 or 8; quad: 8),
 *   "strategy": "partition" | "ev",
 *   "history": [ { "guess": "...", "feedback": "GPB..." } ] // classic
 *            or [ { "guess": "...", "feedback": ["...","..."] } ]      // binerdle
 *            or [ { "guess": "...", "feedback": ["...","...","...","..."] } ]  // quad
 * }
 *
 * Response:
 * { "ok": true, "suggestion": "...", "remaining": <int> | { "boards": [...] },
 *   "strategy_resolved": "...", ... }
 */

#include "bench_solve.hpp"
#include "binerdle_partition.hpp"
#include "micro_policy.hpp"
#include "nerdle_core.hpp"
#include "quad_partition.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using PlayStrategy = nerdle_bench::PlayStrategy;

namespace detail {

constexpr unsigned char PLACE_SQ = '\x01';
constexpr unsigned char PLACE_CB = '\x02';

const std::unordered_map<int, std::string> FIRST_GUESS = {
    {5, "3+2=5"},   {6, "4*7=28"}, {7, "4+27=31"}, {8, "48-32=16"}, {10, "56+4-21=39"},
};

const std::unordered_map<int, std::string> FIRST_GUESS_BINERDLE = {
    {6, "4*7=28"}, {8, "43-27=16"},
};

constexpr int MAX_TRIES_CLASSIC = 6;
constexpr int MAX_TRIES_BINERDLE = 7;
constexpr int MAX_TRIES_QUAD = 10;

static std::string normalize_maxi(std::string s) {
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

static std::string maxi_to_display(const std::string& s) {
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

static std::string normalize_guess_input(const std::string& s, bool is_maxi) {
    if (!is_maxi) return s;
    return normalize_maxi(s);
}

static std::string json_escape(const std::string& s) {
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

// ---------- minimal JSON parse (subset) ----------

struct JsonParseError {};

const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

std::string parse_string(const char*& p) {
    if (*p != '"') throw JsonParseError();
    p++;
    std::string s;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) throw JsonParseError();
            switch (*p) {
            case '"':
            case '\\':
            case '/':
                s += *p++;
                break;
            case 'b':
                s += '\b';
                p++;
                break;
            case 'f':
                s += '\f';
                p++;
                break;
            case 'n':
                s += '\n';
                p++;
                break;
            case 'r':
                s += '\r';
                p++;
                break;
            case 't':
                s += '\t';
                p++;
                break;
            case 'u': {
                p++;
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    if (!std::isxdigit((unsigned char)*p)) throw JsonParseError();
                    int v = *p <= '9' ? *p - '0' :10 + std::tolower(*p) - 'a';
                    cp = (cp << 4) | v;
                    p++;
                }
                if (cp < 0x80)
                    s += (char)cp;
                else if (cp < 0x800) {
                    s += (char)(0xC0 | (cp >> 6));
                    s += (char)(0x80 | (cp & 0x3F));
                } else {
                    s += (char)(0xE0 | (cp >> 12));
                    s += (char)(0x80 | ((cp >> 6) & 0x3F));
                    s += (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                throw JsonParseError();
            }
        } else {
            s += *p++;
        }
    }
    if (*p != '"') throw JsonParseError();
    p++;
    return s;
}

double parse_number(const char*& p) {
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) throw JsonParseError();
    p = end;
    return v;
}

void expect(const char*& p, char c) {
    p = skip_ws(p);
    if (*p != c) throw JsonParseError();
    p++;
}

using JsonObj = std::unordered_map<std::string, std::string>;
using JsonArr = std::vector<std::string>;

JsonObj parse_object(const char*& p) {
    p = skip_ws(p);
    if (*p != '{') throw JsonParseError();
    p++;
    JsonObj m;
    p = skip_ws(p);
    if (*p == '}') {
        p++;
        return m;
    }
    for (;;) {
        p = skip_ws(p);
        std::string key = parse_string(p);
        expect(p, ':');
        p = skip_ws(p);
        if (*p == '"') {
            m[key] = parse_string(p);
        } else
            throw JsonParseError();
        p = skip_ws(p);
        if (*p == '}') {
            p++;
            break;
        }
        if (*p != ',') throw JsonParseError();
        p++;
    }
    return m;
}

JsonArr parse_string_array(const char*& p) {
    p = skip_ws(p);
    if (*p != '[') throw JsonParseError();
    p++;
    JsonArr a;
    p = skip_ws(p);
    if (*p == ']') {
        p++;
        return a;
    }
    for (;;) {
        a.push_back(parse_string(p));
        p = skip_ws(p);
        if (*p == ']') {
            p++;
            break;
        }
        if (*p != ',') throw JsonParseError();
        p++;
    }
    return a;
}

/** Parse top-level object allowing "history" array of objects with nested feedback array or string. */
struct HistEntry {
    std::string guess;
    std::vector<std::string> feedbacks;
};

struct ParsedRequest {
    std::string kind;
    int n = 0;
    std::string strategy_ui;
    std::vector<HistEntry> history;
};

ParsedRequest parse_request_body(const std::string& raw) {
    const char* p = raw.c_str();
    p = skip_ws(p);
    if (*p != '{') throw JsonParseError();
    p++;
    ParsedRequest req;
    p = skip_ws(p);
    while (*p != '}') {
        std::string key = parse_string(p);
        expect(p, ':');
        p = skip_ws(p);
        if (key == "kind") {
            if (*p != '"') throw JsonParseError();
            req.kind = parse_string(p);
        } else if (key == "strategy") {
            if (*p != '"') throw JsonParseError();
            req.strategy_ui = parse_string(p);
        } else if (key == "n") {
            req.n = (int)parse_number(p);
        } else if (key == "history") {
            if (*p != '[') throw JsonParseError();
            p++;
            p = skip_ws(p);
            while (*p != ']') {
                if (*p != '{') throw JsonParseError();
                p++;
                HistEntry e;
                p = skip_ws(p);
                while (*p != '}') {
                    std::string hk = parse_string(p);
                    expect(p, ':');
                    p = skip_ws(p);
                    if (hk == "guess") {
                        if (*p != '"') throw JsonParseError();
                        e.guess = parse_string(p);
                    } else if (hk == "feedback") {
                        if (*p == '"') {
                            e.feedbacks.push_back(parse_string(p));
                        } else if (*p == '[') {
                            p++;
                            p = skip_ws(p);
                            while (*p != ']') {
                                if (*p != '"') throw JsonParseError();
                                e.feedbacks.push_back(parse_string(p));
                                p = skip_ws(p);
                                if (*p == ']') break;
                                if (*p != ',') throw JsonParseError();
                                p++;
                                p = skip_ws(p);
                            }
                            if (*p != ']') throw JsonParseError();
                            p++;
                        } else
                            throw JsonParseError();
                    } else {
                        if (*p == '"')
                            parse_string(p);
                        else if (*p == '{')
                            parse_object(p);
                        else if (*p == '[')
                            parse_string_array(p);
                        else
                            parse_number(p);
                    }
                    p = skip_ws(p);
                    if (*p == '}') break;
                    if (*p != ',') throw JsonParseError();
                    p++;
                    p = skip_ws(p);
                }
                if (*p != '}') throw JsonParseError();
                p++;
                req.history.push_back(std::move(e));
                p = skip_ws(p);
                if (*p == ']') break;
                if (*p != ',') throw JsonParseError();
                p++;
                p = skip_ws(p);
            }
            if (*p != ']') throw JsonParseError();
            p++;
        } else {
            if (*p == '"')
                parse_string(p);
            else if (*p == '{')
                parse_object(p);
            else if (*p == '[') {
                p++;
                int depth = 1;
                while (depth > 0 && *p) {
                    if (*p == '[') depth++;
                    else if (*p == ']') depth--;
                    else if (*p == '"') {
                        p++;
                        while (*p && *p != '"') {
                            if (*p == '\\') p++;
                            p++;
                        }
                    }
                    p++;
                }
            } else
                parse_number(p);
        }
        p = skip_ws(p);
        if (*p == '}') break;
        if (*p != ',') throw JsonParseError();
        p++;
        p = skip_ws(p);
    }
    if (*p != '}') throw JsonParseError();
    return req;
}

void fail_json(const std::string& msg) {
    std::cout << "{\"ok\":false,\"error\":" << json_escape(msg) << "}\n";
}

} // namespace detail

static std::string data_path(const std::string& root, const std::string& rel) {
    if (root.empty() || root == ".")
        return rel;
    if (root.back() == '/')
        return root + rel;
    return root + "/" + rel;
}

static void uppercase_feedback(std::string& f) {
    for (char& c : f) c = (char)std::toupper((unsigned char)c);
}

static bool valid_feedback(const std::string& f, int N) {
    if ((int)f.size() != N) return false;
    for (char c : f) {
        if (c != 'G' && c != 'P' && c != 'B') return false;
    }
    return true;
}

static void filter_board(const std::vector<std::string>& all_eqs, const std::vector<size_t>& in,
                         const std::string& guess, const std::string& feedback, int N,
                         std::vector<size_t>& out) {
    out.clear();
    for (size_t idx : in) {
        if (nerdle::is_consistent_feedback_string(all_eqs[idx], guess, feedback.c_str(), N))
            out.push_back(idx);
    }
}

static std::string best_guess_pairs_api(const std::vector<std::string>& all_eqs,
                                        const std::vector<size_t>& c1, const std::vector<size_t>& c2, int N,
                                        bool solved1, bool solved2, bool use_partition,
                                        int tries_remaining) {
    if (use_partition) {
        return nerdle::best_guess_binerdle_partition(
            all_eqs, c1, c2, N, std::max(1, tries_remaining), solved1, solved2);
    }
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

static std::string best_guess_quads_api(const std::vector<std::string>& all_eqs,
                                        const std::vector<size_t>& c1, const std::vector<size_t>& c2,
                                        const std::vector<size_t>& c3, const std::vector<size_t>& c4, int N,
                                        bool solved[4], bool use_partition, int tries_remaining) {
    if (use_partition) {
        return nerdle::best_guess_quad_partition(all_eqs, c1, c2, c3, c4, N, tries_remaining, solved);
    }

    int num_identified = (c1.size() == 1 ? 1 : 0) + (c2.size() == 1 ? 1 : 0) + (c3.size() == 1 ? 1 : 0) +
                         (c4.size() == 1 ? 1 : 0);

    if (num_identified == 4) {
        if (!solved[0]) return all_eqs[c1[0]];
        if (!solved[1]) return all_eqs[c2[0]];
        if (!solved[2]) return all_eqs[c3[0]];
        return all_eqs[c4[0]];
    }

    std::vector<int> hist;
    const std::vector<size_t>* B[4] = {&c1, &c2, &c3, &c4};
    auto singleton_positive = [&](int skip_bi, size_t guess_idx) -> bool {
        double hsum = 0.0;
        for (int bi = 0; bi < 4; bi++) {
            if (bi == skip_bi) continue;
            const std::vector<size_t>& Sb = *B[bi];
            if (Sb.size() <= 1) continue;
            hsum += nerdle::entropy_of_guess_packed(all_eqs[guess_idx].c_str(), all_eqs, Sb, N, hist);
        }
        return hsum > 0.0;
    };
    if (c1.size() == 1 && (c2.size() > 1 || c3.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(0, c1[0])) return all_eqs[c1[0]];
    }
    if (c2.size() == 1 && (c1.size() > 1 || c3.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(1, c2[0])) return all_eqs[c2[0]];
    }
    if (c3.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c4.size() > 1)) {
        if (singleton_positive(2, c3[0])) return all_eqs[c3[0]];
    }
    if (c4.size() == 1 && (c1.size() > 1 || c2.size() > 1 || c3.size() > 1)) {
        if (singleton_positive(3, c4[0])) return all_eqs[c4[0]];
    }

    std::vector<std::vector<size_t>> boards = {c1, c2, c3, c4};
    std::mt19937 rng(std::random_device{}());
    return nerdle::best_guess_v2_multi(all_eqs, boards, N, hist, rng);
}

static PlayStrategy resolve_strategy(int N, const std::string& strat_ui, bool micro_policy_ok,
                                     std::string& strat_resolved) {
    const bool want_ev = (strat_ui == "ev" || strat_ui == "entropy" || strat_ui == "optimal");
    const bool want_part = (strat_ui == "partition");

    if (!want_ev && !want_part) {
        strat_resolved = "error";
        return PlayStrategy::Entropy;
    }

    if (N == 5) {
        if (want_part) {
            strat_resolved = "partition";
            return PlayStrategy::Partition;
        }
        if (micro_policy_ok) {
            strat_resolved = "bellman";
            return PlayStrategy::Bellman;
        }
        strat_resolved = "partition";
        return PlayStrategy::Partition;
    }
    if (N == 6) {
        if (want_part) {
            strat_resolved = "partition";
            return PlayStrategy::Partition;
        }
        if (micro_policy_ok) {
            strat_resolved = "optimal";
            return PlayStrategy::Optimal;
        }
        strat_resolved = "error";
        return PlayStrategy::Optimal;
    }
    if (N == 7 || N == 8 || N == 10) {
        if (want_part) {
            strat_resolved = "partition";
            return PlayStrategy::Partition;
        }
        strat_resolved = "entropy_v2";
        return PlayStrategy::Entropy;
    }
    strat_resolved = "error";
    return PlayStrategy::Entropy;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::string raw((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

    detail::ParsedRequest req;
    try {
        req = detail::parse_request_body(raw);
    } catch (...) {
        detail::fail_json("invalid JSON or unsupported shape");
        return 0;
    }

    const char* droot = std::getenv("NERDLE_DATA_DIR");
    std::string root = droot ? droot : ".";

    if (req.kind != "classic" && req.kind != "binerdle" && req.kind != "quad") {
        detail::fail_json("kind must be classic, binerdle, or quad");
        return 0;
    }
    if (req.strategy_ui.empty()) {
        detail::fail_json("strategy required (partition or ev)");
        return 0;
    }

    int N = req.n;
    if (req.kind == "classic") {
        if (N != 5 && N != 6 && N != 7 && N != 8 && N != 10) {
            detail::fail_json("classic n must be 5, 6, 7, 8, or 10");
            return 0;
        }
    } else if (req.kind == "binerdle") {
        if (N != 6 && N != 8) {
            detail::fail_json("binerdle n must be 6 or 8");
            return 0;
        }
    } else {
        if (N != 8) {
            detail::fail_json("quad requires n=8");
            return 0;
        }
    }

    std::string eq_path = data_path(root, "data/equations_" + std::to_string(N) + ".txt");
    std::ifstream f(eq_path);
    if (!f) {
        detail::fail_json("cannot open equation pool: " + eq_path);
        return 0;
    }
    std::vector<std::string> equations;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) equations.push_back(line);
    }
    f.close();

    const bool is_maxi = (N == 10);
    if (is_maxi) {
        for (auto& eq : equations) eq = detail::normalize_maxi(eq);
    }

    PlayStrategy strategy = PlayStrategy::Entropy;
    std::string strat_resolved;
    std::unordered_map<nerdle::PolicyMask, uint8_t, nerdle::PolicyMaskHash> micro_policy;
    bool micro_policy_ok = false;

    if (req.kind == "classic") {
        const bool want_part = (req.strategy_ui == "partition");
        if ((N == 5 || N == 6) && !(N == 5 && want_part)) {
            std::string pol_path =
                data_path(root, (N == 5) ? "data/optimal_policy_5.bin" : "data/optimal_policy_6.bin");
            const int neq = static_cast<int>(equations.size());
            micro_policy_ok = nerdle::load_micro_policy(pol_path, neq, micro_policy);
        }
        strategy = resolve_strategy(N, req.strategy_ui, micro_policy_ok, strat_resolved);
        if (strat_resolved == "error") {
            if (N == 6 && (req.strategy_ui == "ev" || req.strategy_ui == "entropy" || req.strategy_ui == "optimal"))
                detail::fail_json("Mini EV requires data/optimal_policy_6.bin (run make mini_policy)");
            else
                detail::fail_json("invalid strategy for this mode");
            return 0;
        }
    } else if (req.kind == "binerdle") {
        const bool want_part = (req.strategy_ui == "partition");
        const bool want_ev = (req.strategy_ui == "ev" || req.strategy_ui == "entropy" ||
                              req.strategy_ui == "optimal");
        if (!want_part && !want_ev) {
            detail::fail_json("invalid strategy for binerdle");
            return 0;
        }
        strat_resolved = want_part ? "binerdle_partition" : "joint_entropy_v2";
    } else {
        const bool want_part = (req.strategy_ui == "partition");
        const bool want_ev =
            (req.strategy_ui == "ev" || req.strategy_ui == "entropy" || req.strategy_ui == "optimal");
        if (!want_part && !want_ev) {
            detail::fail_json("invalid strategy for quad");
            return 0;
        }
        strat_resolved = want_part ? "quad_partition" : "joint_entropy_v2";
    }

    std::mt19937 rng(std::random_device{}());
    std::vector<int> hist;

    if (req.kind == "classic") {
        std::vector<size_t> candidates;
        for (size_t i = 0; i < equations.size(); i++) candidates.push_back(i);
        std::unordered_set<size_t> candidate_set(candidates.begin(), candidates.end());

        for (const detail::HistEntry& he : req.history) {
            std::string g = detail::normalize_guess_input(he.guess, is_maxi);
            if ((int)g.size() != N || he.feedbacks.size() != 1) {
                detail::fail_json("each classic history row needs guess length n and one feedback string");
                return 0;
            }
            std::string fb = he.feedbacks[0];
            uppercase_feedback(fb);
            if (!valid_feedback(fb, N)) {
                detail::fail_json("feedback must be length n with only G, P, B");
                return 0;
            }
            if (fb == std::string(N, 'G')) {
                std::cout << "{\"ok\":true,\"solved\":true,\"remaining\":1,\"suggestion\":"
                          << detail::json_escape(is_maxi ? detail::maxi_to_display(g) : g)
                          << ",\"strategy_resolved\":" << detail::json_escape(strat_resolved) << ",\"kind\":\"classic\",\"n\":"
                          << N << "}\n";
                return 0;
            }
            std::vector<size_t> next_c;
            std::unordered_set<size_t> next_s;
            for (size_t idx : candidates) {
                if (nerdle::is_consistent_feedback_string(equations[idx], g, fb.c_str(), N)) {
                    next_c.push_back(idx);
                    next_s.insert(idx);
                }
            }
            candidates = std::move(next_c);
            candidate_set = std::move(next_s);
            if (candidates.empty()) {
                detail::fail_json("no candidates remain — check guess and feedback");
                return 0;
            }
        }

        const int turn = static_cast<int>(req.history.size());
        const int tries_left = detail::MAX_TRIES_CLASSIC - turn;
        std::string guess;
        if (strategy == PlayStrategy::Partition) {
            const int ptd = nerdle::kPartitionInteractiveTieDepth;
            if (req.history.empty()) {
                guess.clear();
                if (const char* fo = nerdle::partition_fixed_opening_tie6(N)) {
                    for (size_t idx : candidates) {
                        if (equations[idx] == fo) {
                            guess = equations[idx];
                            break;
                        }
                    }
                }
                if (guess.empty())
                    guess = nerdle::best_guess_partition_policy(
                        equations, candidates, N, std::max(1, tries_left), ptd);
            } else {
                guess = nerdle::best_guess_partition_policy(
                    equations, candidates, N, std::max(1, tries_left), ptd);
            }
        } else if (((strategy == PlayStrategy::Bellman && N == 5) ||
                    (strategy == PlayStrategy::Optimal && N == 6)) &&
                   micro_policy_ok) {
            std::string pg = nerdle::guess_from_micro_policy(micro_policy, equations, candidates);
            guess = pg.empty() ? detail::FIRST_GUESS.at(N) : pg;
        } else {
            guess = nerdle::best_guess_v2(equations, candidates, candidate_set, N, hist, rng);
        }
        if (guess.empty() && candidates.size() == 1) guess = equations[candidates[0]];

        std::string sugg_out = is_maxi ? detail::maxi_to_display(guess) : guess;
        std::cout << "{\"ok\":true,\"remaining\":" << candidates.size() << ",\"suggestion\":"
                  << detail::json_escape(sugg_out) << ",\"strategy_resolved\":" << detail::json_escape(strat_resolved)
                  << ",\"kind\":\"classic\",\"n\":" << N << "}\n";
        return 0;
    }

    if (req.kind == "binerdle") {
        const int nboards = 2;
        std::vector<size_t> c1, c2;
        for (size_t i = 0; i < equations.size(); i++) {
            c1.push_back(i);
            c2.push_back(i);
        }
        bool solved1 = false, solved2 = false;

        for (const detail::HistEntry& he : req.history) {
            std::string g = he.guess;
            if ((int)g.size() != N || (int)he.feedbacks.size() != nboards) {
                detail::fail_json("each binerdle history row needs guess length n and two feedback strings");
                return 0;
            }
            std::string f1 = he.feedbacks[0];
            std::string f2 = he.feedbacks[1];
            uppercase_feedback(f1);
            uppercase_feedback(f2);
            if (!valid_feedback(f1, N) || !valid_feedback(f2, N)) {
                detail::fail_json("feedback must be length n with only G, P, B");
                return 0;
            }
            if (f1 == std::string(N, 'G')) solved1 = true;
            if (f2 == std::string(N, 'G')) solved2 = true;
            if (solved1 && solved2) {
                std::cout << "{\"ok\":true,\"solved\":true,\"remaining\":{\"boards\":[1,1]},"
                             "\"suggestion\":"
                          << detail::json_escape(g) << ",\"strategy_resolved\":" << detail::json_escape(strat_resolved)
                          << ",\"kind\":\"binerdle\",\"n\":" << N << "}\n";
                return 0;
            }
            std::vector<size_t> n1, n2;
            filter_board(equations, c1, g, f1, N, n1);
            filter_board(equations, c2, g, f2, N, n2);
            c1 = std::move(n1);
            c2 = std::move(n2);
            if (c1.empty() || c2.empty()) {
                detail::fail_json("no candidates remain on one board — check feedback");
                return 0;
            }
        }

        const int turn = static_cast<int>(req.history.size());
        const int tries_left = detail::MAX_TRIES_BINERDLE - turn;
        const bool use_partition = (strat_resolved == "binerdle_partition");
        std::string guess = best_guess_pairs_api(equations, c1, c2, N, solved1, solved2,
                                                 use_partition, tries_left);
        if (guess.empty()) {
            if (!solved1 && c1.size() == 1) guess = equations[c1[0]];
            else if (!solved2 && c2.size() == 1) guess = equations[c2[0]];
        }
        if (guess.empty() && req.history.empty())
            guess = detail::FIRST_GUESS_BINERDLE.at(N);

        uint64_t prod = (uint64_t)c1.size() * (uint64_t)c2.size();
        std::cout << "{\"ok\":true,\"remaining\":{\"boards\":[" << c1.size() << "," << c2.size()
                  << "]},\"remaining_product\":" << prod << ",\"suggestion\":" << detail::json_escape(guess)
                  << ",\"strategy_resolved\":" << detail::json_escape(strat_resolved) << ",\"kind\":\"binerdle\",\"n\":"
                  << N << "}\n";
        return 0;
    }

    /* quad */
    std::vector<size_t> c1, c2, c3, c4;
    for (size_t i = 0; i < equations.size(); i++) {
        c1.push_back(i);
        c2.push_back(i);
        c3.push_back(i);
        c4.push_back(i);
    }
    bool solved[4] = {false, false, false, false};

    for (const detail::HistEntry& he : req.history) {
        std::string g = he.guess;
        if ((int)g.size() != N || (int)he.feedbacks.size() != 4) {
            detail::fail_json("each quad history row needs guess length n and four feedback strings");
            return 0;
        }
        std::string f[4];
        for (int i = 0; i < 4; i++) {
            f[i] = he.feedbacks[i];
            uppercase_feedback(f[i]);
            if (!valid_feedback(f[i], N)) {
                detail::fail_json("feedback must be length n with only G, P, B");
                return 0;
            }
        }
        for (int i = 0; i < 4; i++) {
            if (f[i] == std::string(N, 'G')) solved[i] = true;
        }
        if (solved[0] && solved[1] && solved[2] && solved[3]) {
            std::cout << "{\"ok\":true,\"solved\":true,\"remaining\":{\"boards\":[1,1,1,1]},"
                         "\"suggestion\":"
                      << detail::json_escape(g) << ",\"strategy_resolved\":" << detail::json_escape(strat_resolved)
                      << ",\"kind\":\"quad\",\"n\":" << N << "}\n";
            return 0;
        }
        std::vector<size_t> n1, n2, n3, n4;
        filter_board(equations, c1, g, f[0], N, n1);
        filter_board(equations, c2, g, f[1], N, n2);
        filter_board(equations, c3, g, f[2], N, n3);
        filter_board(equations, c4, g, f[3], N, n4);
        c1 = std::move(n1);
        c2 = std::move(n2);
        c3 = std::move(n3);
        c4 = std::move(n4);
        if (c1.empty() || c2.empty() || c3.empty() || c4.empty()) {
            detail::fail_json("no candidates remain on one board — check feedback");
            return 0;
        }
    }

    const int turn = static_cast<int>(req.history.size());
    const int tries_left = detail::MAX_TRIES_QUAD - turn;
    const bool use_partition = (strat_resolved == "quad_partition");
    std::string guess = best_guess_quads_api(equations, c1, c2, c3, c4, N, solved, use_partition,
                                             tries_left);
    if (guess.empty() && req.history.empty()) {
        if (use_partition) {
            guess = nerdle::quad_full_pool_partition_opening(equations, N, detail::MAX_TRIES_QUAD,
                                                             nerdle::kPartitionInteractiveTieDepth);
        } else
            guess = detail::FIRST_GUESS.at(N);
    }

    uint64_t prod = (uint64_t)c1.size() * (uint64_t)c2.size() * (uint64_t)c3.size() * (uint64_t)c4.size();
    std::cout << "{\"ok\":true,\"remaining\":{\"boards\":[" << c1.size() << "," << c2.size() << ","
              << c3.size() << "," << c4.size() << "]},\"remaining_product\":" << prod
              << ",\"suggestion\":" << detail::json_escape(guess) << ",\"strategy_resolved\":"
              << detail::json_escape(strat_resolved) << ",\"kind\":\"quad\",\"n\":" << N << "}\n";
    return 0;
}
