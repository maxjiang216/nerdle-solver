/**
 * Try to generate data/optimal_policy_{5,6}.bin via `make micro_policy` / `make mini_policy`.
 */
#ifndef OPTIMAL_POLICY_BUILD_HPP
#define OPTIMAL_POLICY_BUILD_HPP

#include <cstdlib>
#include <ostream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace nerdle {

/** Runs `make micro_policy` (N=5) or `make mini_policy` (N=6). Returns true on success. */
inline bool try_build_optimal_policy_bin(int N, std::ostream& log) {
    if (N != 5 && N != 6)
        return false;
    const char* target = (N == 5) ? "micro_policy" : "mini_policy";
    log << "Policy file missing — running `make " << target << "` ...\n" << std::flush;
    std::string cmd = std::string("make -s ") + target;
    int st = std::system(cmd.c_str());
#if defined(_WIN32)
    (void)st;
    log << "Automatic policy build is not supported on this platform.\n";
    return false;
#else
    if (st == -1) {
        log << "Could not run `make " << target << "` (system() failed).\n";
        return false;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        log << "`make " << target << "` failed"
            << (WIFEXITED(st) ? " (exit " + std::to_string(WEXITSTATUS(st)) + ")" : "") << ".\n";
        return false;
    }
    return true;
#endif
}

} // namespace nerdle

#endif
