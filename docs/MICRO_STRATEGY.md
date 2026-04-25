# Nerdle Micro (5 tiles): how we chose the strategy

Micro Nerdle uses a **short equation pool** under the same feedback rules as classic Nerdle (green / purple / black, multiset-aware). That small pool makes **exact dynamic programming** feasible, so we do not need a hand-tuned heuristic for “best” play under a clear objective.

## Objective

We optimize **expected number of guesses until the secret is found**, with a **uniform prior** over all valid equations in `data/equations_5.txt`. Each submitted guess counts; the round ends when the guess equals the secret (all green). The DP in `optimal_expected.cpp` is the Bellman value for that objective.

**Note:** The official game caps you at six tries. The Bellman solution above minimizes unconstrained expected length (infinite horizon). It usually stays within six guesses on Micro, but the objective is not “maximize probability of winning in six.” For that, you would solve a different finite-horizon MDP. The repo’s **partition** policy (see `bench_solve.hpp`) explicitly reasons about tries remaining; use `./nerdle --len 5 --strategy partition` or `./nerdle_micro --strategy partition` when you want that tie-breaking.

## State space

A game state is **which equations are still possible** after the feedback seen so far. For Micro, there are on the order of **100 equations**, so a state is represented as a **bitmask over equation indices** (`PolicyMask` in `micro_policy.hpp`). Transitions: pick any pool equation as the next guess; partition the mask by the packed feedback code against each remaining secret.

The recursion V(S) for candidate set S (|S|>1) is:

- Try every guess g in the pool that **refines** S (more than one feedback cell, unless g \in S so a hit can end the game).
- For uniform s \in S, the one-step contribution is 1 plus the value of the child mask consistent with g and s, unless g is the secret (then 1).
- V(S) is the minimum over g of that average.

Base cases: |S|=0 → 0; |S|=1 → 1 (play the only equation).

## Tie-breaking

Several first guesses can share the same optimal **E[guesses]** (within numerical tolerance). Among those, `optimal_expected.cpp` picks the guess with **smallest index in canonical equation order** (`canonical_less` over precomputed keys). For the current pool, that yields `**3+2=5`** as the printed “policy” first guess (tied with others such as `5-1=4` at the same expectation).

The same tie-break is applied at **every** DP state when writing `data/optimal_policy_5.bin`, so the stored policy is **deterministic** and reproducible.

## Artifact: `data/optimal_policy_5.bin`

`./optimal_expected data/equations_5.txt --write-policy data/optimal_policy_5.bin` enumerates all **non-singleton** masks visited by the DP and records the chosen guess index per mask. The interactive tools (`./nerdle_micro`, `./nerdle --len 5`, `solver_json` for classic n=5) **look up** the next guess from this table. If a mask is missing (should not happen for Micro), they fall back to entropy v2.

Regenerate after changing the equation generator, the Micro pool file, or the Bellman tie-break.

```bash
./generate --len 5
make micro_policy   # runs optimal_expected with --write-policy
```

## Alternatives we compared

- **Entropy / v2** (`nerdle_core.hpp`): fast heuristic; not guaranteed to match Bellman on tiny pools.
- **Partition policy** (`best_guess_partition_policy`): maximizes the number of distinct feedback patterns, then optimizes win probability within remaining tries, then nested criteria — good when you care about the **six-try cap**, but not the same as min **E[guesses]** with an infinite horizon.

`compare_bellman.cpp` audits where Bellman and partition (or entropy) **disagree** on the next move along simulated paths; that tool is useful when validating that the precomputed table matches the DP definition.

## Where in the code


| Piece                         | Location                                              |
| ----------------------------- | ----------------------------------------------------- |
| Bellman DP + policy export    | `src/optimal_expected.cpp`                            |
| Binary policy format + lookup | `src/micro_policy.hpp`                                |
| Canonical tie-break keys      | `src/equation_canonical.hpp`                          |
| Interactive Micro-only binary | `src/nerdle_micro.cpp` + `src/nerdle_interactive.cpp` |
| Build policy file             | `make micro_policy` in `Makefile`                     |


## One-line summary

**Micro strategy:** exact **Bellman-optimal** play for **minimum expected guesses** under a **uniform prior**, with **canonical-order** tie-breaking, precomputed into `**data/optimal_policy_5.bin`** because the 5-tile pool is small enough to solve completely.