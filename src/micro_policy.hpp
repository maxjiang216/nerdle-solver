/**
 * Precomputed Bellman-optimal policy (min E[guesses], uniform prior).
 * Binary format written by ./optimal_expected --write-policy (see optimal_expected.cpp).
 *
 * Format v1: 16-byte bitmask (up to 128 equations). v2: 32-byte bitmask (up to 256).
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
constexpr uint32_t kPolicyFormatVer1 = 1;           /* 16-byte masks */
constexpr uint32_t kPolicyFormatVer2 = 2;            /* 32-byte masks */

/** Candidate set as a bitset over equation indices (max 256). */
struct PolicyMask {
    uint64_t w[4] = {};
};

inline bool operator==(PolicyMask a, PolicyMask b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}

struct PolicyMaskHash {
    size_t operator()(PolicyMask m) const noexcept {
        return static_cast<size_t>(m.w[0] ^ (m.w[1] * 0x9e3779b97f4a7c15ULL) ^ (m.w[2] << 1) ^
                                   (m.w[3] << 2));
    }
};

/** Legacy typedef — same as PolicyMask (256-bit capable). */
using MicroMask128 = PolicyMask;
using MicroMask128Hash = PolicyMaskHash;

inline int popcount(PolicyMask m) {
    return __builtin_popcountll(m.w[0]) + __builtin_popcountll(m.w[1]) + __builtin_popcountll(m.w[2]) +
           __builtin_popcountll(m.w[3]);
}

inline PolicyMask set_bit(PolicyMask m, int i) {
    if (i >= 0 && i < 256)
        m.w[i >> 6] |= (1ULL << (i & 63));
    return m;
}

inline bool eq_mask(PolicyMask a, PolicyMask b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3];
}

inline PolicyMask full_policy_mask(int n) {
    PolicyMask m{};
    for (int i = 0; i < n; i++)
        m = set_bit(m, i);
    return m;
}

template <typename F>
void for_each_bit(PolicyMask m, F&& fn) {
    for (int limb = 0; limb < 4; limb++) {
        uint64_t x = m.w[limb];
        while (x) {
            int b = __builtin_ctzll(x);
            fn((limb << 6) + b);
            x &= x - 1;
        }
    }
}

inline PolicyMask mask_from_candidates(const std::vector<size_t>& cand) {
    PolicyMask m{};
    for (size_t idx : cand) {
        if (idx < 256)
            m.w[idx >> 6] |= (1ULL << (idx & 63));
    }
    return m;
}

/** Returns false if missing, corrupt, or n_eq mismatch. */
inline bool load_micro_policy(const std::string& path, int expected_n,
                              std::unordered_map<PolicyMask, uint8_t, PolicyMaskHash>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    uint32_t magic = 0, ver = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&ver), 4);
    if (magic != kMicroPolicyMagic || (ver != kPolicyFormatVer1 && ver != kPolicyFormatVer2))
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
        PolicyMask m{};
        if (ver == kPolicyFormatVer1) {
            uint64_t lo = 0, hi = 0;
            f.read(reinterpret_cast<char*>(&lo), 8);
            f.read(reinterpret_cast<char*>(&hi), 8);
            m.w[0] = lo;
            m.w[1] = hi;
        } else {
            f.read(reinterpret_cast<char*>(m.w), 32);
        }
        uint8_t g = 0;
        f.read(reinterpret_cast<char*>(&g), 1);
        out[m] = g;
    }
    return static_cast<bool>(f) && f.peek() == std::ifstream::traits_type::eof();
}

/**
 * Next guess from precomputed policy. Empty string if lookup fails (caller should fall back).
 * Singleton candidate returns that equation.
 */
inline std::string guess_from_micro_policy(const std::unordered_map<PolicyMask, uint8_t, PolicyMaskHash>& policy,
                                           const std::vector<std::string>& eqs,
                                           const std::vector<size_t>& candidates) {
    if (candidates.size() == 1)
        return eqs[candidates[0]];
    PolicyMask m = mask_from_candidates(candidates);
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
