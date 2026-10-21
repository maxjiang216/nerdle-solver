/**
 * Batched feedback: one guess against many secrets at once.
 *
 * Secrets are stored tile-major in blocks of 32 (tile j of 32 secrets is contiguous), so a guess
 * tile is compared against 32 secrets with one byte-vector compare. Written with GCC/Clang vector
 * extensions and compiled for AVX2 (~10x faster than per-pair feedback); used only when the CPU
 * has AVX2 (checked once at run time), otherwise each lane falls back to compute_feedback_packed,
 * since plain SSE2 lowering of this code is slower than the per-pair routine.
 * Codes match compute_feedback_packed exactly (base 3, B=0 P=1 G=2, LSB = tile 0).
 */
#ifndef FEEDBACK_BATCH_HPP
#define FEEDBACK_BATCH_HPP

#include "nerdle_core.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#define NERDLE_BATCH_AVX2 __attribute__((target("avx2")))
#else
#define NERDLE_BATCH_AVX2
#endif

namespace nerdle {

class TransposedSecrets {
  public:
    static constexpr int kLanes = 32;

    TransposedSecrets() = default;
    TransposedSecrets(const std::vector<std::string>& eqs, const std::vector<int>& idx, int N) {
        assign(eqs, idx.data(), idx.size(), N);
    }

    /** Secrets eqs[idx[0..count)]; unused lanes of the last block are zero (never match). */
    void assign(const std::vector<std::string>& eqs, const int* idx, size_t count, int N) {
        n_ = count;
        N_ = N;
        blocks_ = (count + kLanes - 1) / kLanes;
        data_.assign(blocks_ * static_cast<size_t>(N) * kLanes, 0);
        for (size_t i = 0; i < count; i++) {
            const std::string& e = eqs[static_cast<size_t>(idx[i])];
            uint8_t* blk = &data_[(i / kLanes) * static_cast<size_t>(N) * kLanes];
            for (int j = 0; j < N; j++)
                blk[static_cast<size_t>(j) * kLanes + i % kLanes] = static_cast<uint8_t>(e[static_cast<size_t>(j)]);
        }
    }

    /** All of eqs, in order. */
    void assign_all(const std::vector<std::string>& eqs, int N) {
        std::vector<int> idx(eqs.size());
        for (size_t i = 0; i < idx.size(); i++)
            idx[i] = static_cast<int>(i);
        assign(eqs, idx.data(), idx.size(), N);
    }

    size_t size() const { return n_; }
    size_t blocks() const { return blocks_; }
    int tiles() const { return N_; }
    const uint8_t* block(size_t b) const { return &data_[b * static_cast<size_t>(N_) * kLanes]; }

  private:
    size_t n_ = 0, blocks_ = 0;
    int N_ = 0;
    std::vector<uint8_t> data_;
};

namespace batch_detail {

typedef uint8_t u8x32 __attribute__((vector_size(32)));
typedef uint16_t u16x32 __attribute__((vector_size(64)));

/** codes[0..32) for one guess against one tile-major block (same rule as compute_feedback_packed_n). */
template <int N>
NERDLE_BATCH_AVX2 inline void feedback_block_n(const char* guess, const uint8_t* blk, uint16_t* codes) {
    static constexpr uint16_t p3[10] = {1, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683};
    u8x32 sol[N], avail[N], green[N];
    for (int j = 0; j < N; j++) {
        std::memcpy(&sol[j], blk + j * TransposedSecrets::kLanes, sizeof(u8x32));
        green[j] = reinterpret_cast<u8x32>(sol[j] == static_cast<uint8_t>(guess[j]));
        avail[j] = ~green[j]; /* secret tiles not yet claimed by a green or purple */
    }
    u16x32 code = {};
    for (int i = 0; i < N; i++) {
        const uint8_t gi = static_cast<uint8_t>(guess[i]);
        u8x32 open = ~green[i]; /* lanes where guess tile i may still claim a purple */
        u8x32 purple = {};
        for (int j = 0; j < N; j++) {
            u8x32 take = reinterpret_cast<u8x32>(sol[j] == gi) & avail[j] & open;
            avail[j] &= ~take;
            open &= ~take;
            purple |= take;
        }
        u8x32 trit = (green[i] & 2) | (purple & 1);
        code += __builtin_convertvector(trit, u16x32) * p3[i];
    }
    std::memcpy(codes, &code, sizeof(code));
}

}  // namespace batch_detail

/** True when the batched AVX2 routine can run on this CPU (NERDLE_NO_BATCH=1 forces the fallback). */
inline bool batch_feedback_available() {
    static const bool ok = [] {
        if (std::getenv("NERDLE_NO_BATCH"))
            return false;
#if defined(__AVX2__)
        return true;
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
        return __builtin_cpu_supports("avx2") != 0;
#else
        return false;
#endif
    }();
    return ok;
}

/**
 * codes[32*(b-first) + lane] for blocks [first, last) of T. Lanes past T.size() get meaningless
 * codes; callers ignore them.
 */
inline void feedback_blocks(const char* guess, const TransposedSecrets& T, size_t first, size_t last,
                            uint16_t* codes) {
    const int N = T.tiles();
    const bool fast = batch_feedback_available() && (N == 5 || N == 6 || N == 7 || N == 8 || N == 10);
    for (size_t b = first; b < last; b++) {
        uint16_t* out = codes + (b - first) * TransposedSecrets::kLanes;
        if (!fast) {
            const uint8_t* blk = T.block(b);
            for (int lane = 0; lane < TransposedSecrets::kLanes; lane++) {
                char s[16];
                for (int j = 0; j < N; j++)
                    s[j] = static_cast<char>(blk[j * TransposedSecrets::kLanes + lane]);
                out[lane] = static_cast<uint16_t>(compute_feedback_packed(guess, s, N));
            }
            continue;
        }
        switch (N) {
        case 5: batch_detail::feedback_block_n<5>(guess, T.block(b), out); break;
        case 6: batch_detail::feedback_block_n<6>(guess, T.block(b), out); break;
        case 7: batch_detail::feedback_block_n<7>(guess, T.block(b), out); break;
        case 8: batch_detail::feedback_block_n<8>(guess, T.block(b), out); break;
        default: batch_detail::feedback_block_n<10>(guess, T.block(b), out); break;
        }
    }
}

/** codes[0 .. 32*T.blocks()) for every secret in T. */
inline void feedback_all(const char* guess, const TransposedSecrets& T, uint16_t* codes) {
    feedback_blocks(guess, T, 0, T.blocks(), codes);
}

}  // namespace nerdle

#endif
