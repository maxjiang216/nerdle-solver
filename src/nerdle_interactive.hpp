/**
 * Shared interactive Nerdle play loop (CLI assistant).
 * Used by nerdle (all lengths) and nerdle_micro (5-tile only).
 */
#ifndef NERDLE_INTERACTIVE_HPP
#define NERDLE_INTERACTIVE_HPP

#include "bench_solve.hpp"

#include <optional>

namespace nerdle_interactive {

struct Config {
    int N = 8;
    /** If set, user chose --strategy; otherwise defaults match ./nerdle. */
    std::optional<nerdle_bench::PlayStrategy> strategy;
};

/** 0 = normal quit/solve; 1 = bad feedback / missing data. */
int run(const Config& cfg);

} // namespace nerdle_interactive

#endif
