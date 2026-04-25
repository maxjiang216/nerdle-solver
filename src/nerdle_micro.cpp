/**
 * Nerdle Micro (5-tile) interactive assistant only.
 *
 * Same suggestion engine as `./nerdle --len 5`; defaults to Bellman when
 * data/optimal_policy_5.bin is present (see docs/MICRO_STRATEGY.md).
 */

#include "bench_solve.hpp"
#include "nerdle_interactive.hpp"

#include <iostream>
#include <optional>
#include <string>

using PlayStrategy = nerdle_bench::PlayStrategy;

static void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--strategy bellman|partition]\n";
    std::cerr << "  Default: Bellman (min E[guesses]) if data/optimal_policy_5.bin exists;\n";
    std::cerr << "           otherwise partition policy.\n";
    std::cerr << "  Run ./generate --len 5 && make micro_policy first.\n";
}

int main(int argc, char** argv) {
    std::optional<PlayStrategy> strategy_opt;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--strategy" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "bellman")
                strategy_opt = PlayStrategy::Bellman;
            else if (v == "partition")
                strategy_opt = PlayStrategy::Partition;
            else {
                std::cerr << "--strategy must be bellman or partition.\n";
                usage(argv[0]);
                return 1;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    return nerdle_interactive::run({5, strategy_opt});
}
