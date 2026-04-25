/**
 * Interactive Nerdle player - solver-assisted play for lengths 5-8 and Maxi (10).
 * Play on nerdlegame.com, enter your guess and feedback; we suggest the next guess.
 *
 * Micro (5-tile)-only entry point: ./nerdle_micro (same engine, simpler CLI).
 */

#include "bench_solve.hpp"
#include "nerdle_interactive.hpp"

#include <iostream>
#include <optional>
#include <string>

using PlayStrategy = nerdle_bench::PlayStrategy;

int main(int argc, char** argv) {
    int N = 8;
    bool strategy_set = false;
    std::string strat_arg;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--len" || arg == "--length") && i + 1 < argc) {
            N = std::atoi(argv[++i]);
        } else if (arg == "--strategy" && i + 1 < argc) {
            strat_arg = argv[++i];
            strategy_set = true;
        }
    }

    std::optional<PlayStrategy> strategy_opt;
    if (strategy_set) {
        if (N == 5) {
            if (strat_arg == "bellman")
                strategy_opt = PlayStrategy::Bellman;
            else if (strat_arg == "partition")
                strategy_opt = PlayStrategy::Partition;
            else {
                std::cerr << "Micro (--len 5): --strategy must be bellman or partition.\n";
                return 1;
            }
        } else if (N == 6) {
            if (strat_arg == "optimal")
                strategy_opt = PlayStrategy::Optimal;
            else if (strat_arg == "partition")
                strategy_opt = PlayStrategy::Partition;
            else {
                std::cerr << "Mini (--len 6): --strategy must be optimal or partition.\n";
                return 1;
            }
        } else if (N == 7 || N == 8 || N == 10) {
            if (strat_arg == "partition")
                strategy_opt = PlayStrategy::Partition;
            else if (strat_arg == "entropy" || strat_arg == "v2")
                strategy_opt = PlayStrategy::Entropy;
            else {
                std::cerr << "For --len " << N << ", --strategy must be partition or entropy.\n";
                return 1;
            }
        } else {
            std::cerr << "Invalid --len for --strategy.\n";
            return 1;
        }
    }

    if (N != 5 && N != 6 && N != 7 && N != 8 && N != 10) {
        std::cerr << "Usage: ./nerdle --len 5|6|7|8|10 [--strategy ...]  (alias: --length)\n";
        std::cerr << "  --len 5 (Micro):  bellman | partition\n";
        std::cerr << "  --len 6 (Mini):   optimal | partition\n";
        std::cerr << "  --len 7/8/10:     partition | entropy\n";
        return 1;
    }

    return nerdle_interactive::run({N, strategy_opt});
}
