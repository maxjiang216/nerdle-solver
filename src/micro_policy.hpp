/**
 * Precomputed Bellman-optimal policy for Nerdle Micro (5 tiles, default equation list).
 * Binary format written by ./optimal_expected --write-policy (see optimal_expected.cpp).
 */
#ifndef MICRO_POLICY_HPP
#define MICRO_POLICY_HPP

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nerdle {

constexpr uint32_t kMicroPolicyMagic = 0x4E355042; /* "N5PB" */

struct MicroMask128 {
    uint64_t lo = 0;
    uint64_t hi = 0;
};

inline bool operator==(MicroMask128 a, MicroMask128 b) {
    return a.lo == b.lo && a.hi == b.hi;
}

struct MicroMask128Hash {
    size_t operator()(MicroMask128 m) const noexcept {
        return static_cast<size_t>(m.lo ^ (m.hi * 0x9e3779b97f4a7c15ULL));
    }
};

inline MicroMask128 mask_from_candidates(const std::vector<size_t>& cand) {
    MicroMask128 m{};
    for (size_t idx : cand) {
        if (idx < 64)
            m.lo |= (1ULL << idx);
        else
            m.hi |= (1ULL << (idx - 64));
    }
    return m;
}

/** Returns false if missing, corrupt, or n_eq mismatch. */
inline bool load_micro_policy(const std::string& path, int expected_n,
                              std::unordered_map<MicroMask128, uint8_t, MicroMask128Hash>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    uint32_t magic = 0, ver = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&ver), 4);
    if (magic != kMicroPolicyMagic || ver != 1)
        return false;
    uint8_t neq = 0;
    f.read(reinterpret_cast<char*>(&neq), 1);
    char pad[3];
    f.read(pad, 3);
    if (static_cast<int>(neq) != expected_n)
        return false;
    uint32_t nent = 0;
    f.read(reinterpret_cast<char*>(&nent), 4);
    out.clear();
    out.reserve(static_cast<size_t>(nent) * 2);
    for (uint32_t i = 0; i < nent; i++) {
        uint64_t lo = 0, hi = 0;
        uint8_t g = 0;
        f.read(reinterpret_cast<char*>(&lo), 8);
        f.read(reinterpret_cast<char*>(&hi), 8);
        f.read(reinterpret_cast<char*>(&g), 1);
        MicroMask128 m{lo, hi};
        out[m] = g;
    }
    return static_cast<bool>(f) && f.peek() == std::ifstream::traits_type::eof();
}

/**
 * Next guess from precomputed policy. Empty string if lookup fails (caller should fall back).
 * Singleton candidate returns that equation.
 */
inline std::string guess_from_micro_policy(const std::unordered_map<MicroMask128, uint8_t, MicroMask128Hash>& policy,
                                         const std::vector<std::string>& eqs,
                                         const std::vector<size_t>& candidates) {
    if (candidates.size() == 1)
        return eqs[candidates[0]];
    MicroMask128 m = mask_from_candidates(candidates);
    auto it = policy.find(m);
    if (it == policy.end())
        return "";
    uint8_t gi = it->second;
    if (static_cast<size_t>(gi) >= eqs.size())
        return "";
    return eqs[static_cast<size_t>(gi)];
}

} // namespace nerdle

#endif
