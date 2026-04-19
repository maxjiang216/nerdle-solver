# Nerdle Solver

Entropy-based solver for [Nerdle](https://www.nerdlegame.com/), [Binerdle](https://www.nerdlegame.com/binerdle.html), and [Quad Nerdle](https://www.nerdlegame.com/quadnerdle.html). Supports Micro (5-tile), Mini (6), Midi (7), Classic (8-tile) Nerdle, plus Binerdle Mini (6) and Normal (8), and Quad Nerdle (4 equations in 10 tries).

---

## Project structure

```
nerdle_solver/
├── src/                  # C++ and Python source
│   └── nerdle_core.hpp   # Shared plain-Nerdle: packed feedback, entropy, v1/v2 selectors
├── data/                 # Equation files (generated)
├── Makefile
└── README.md
```

---

## Quick reference

```bash
# First-time setup
make                           # build C++ tools
./generate --len 5             # micro (outputs to data/)
./generate --len 6             # mini
./generate --len 7             # midi
./generate --len 8             # classic
./generate_maxi [--no-pointless-brackets]  # maxi (10 tiles, ²³ brackets)

# Nerdle
./solve data/equations_5.txt   # optimal first guess
./bench_nerdle data/equations_8.txt
./nerdle --len 6               # interactive solver-assist (5,6,7,8,10)

# Binerdle (guess 2 equations in 7 tries)
./solve_binerdle data/equations_6.txt
./binerdle --len 6             # mini, interactive player
./binerdle --len 8             # normal, interactive player
./bench_binerdle data/equations_6.txt
./bench_binerdle data/equations_8.txt

# Quad Nerdle (guess 4 equations in 10 tries)
./solve_quadnerdle data/equations_8.txt   # optimal 1st guess (stratified, --no-stratify for plain)
./quadnerdle --len 8                      # interactive player
./bench_quadnerdle data/equations_8.txt   # benchmark (sampled distinct quadruples)
```

---

## Common use cases

| Goal | Command |
|------|---------|
| Generate equations | `./generate --len N` or `./generate_maxi` |
| Best first guess | `./solve data/equations_N.txt` |
| Nerdle benchmark | `./bench_nerdle data/equations_N.txt` |
| Exact min E[guesses] (uniform prior) | `./optimal_expected data/equations_5.txt` (add `--no-simulate` to skip guess-count distribution) |
| Trace optimal vs entropy+v2 for one target | `./trace_target data/equations_5.txt '9*1=9'` |
| Interactive Nerdle | `./nerdle --len 5\|6\|7\|8\|10` |
| Binerdle optimal 1st | `./solve_binerdle data/equations_6.txt` |
| Binerdle interactive | `./binerdle --len 6` or `--len 8` |
| Binerdle benchmark | `./bench_binerdle data/equations_6.txt` |
| Quad Nerdle optimal 1st | `./solve_quadnerdle data/equations_8.txt` |
| Quad Nerdle interactive | `./quadnerdle --len 8` |
| Quad Nerdle benchmark | `./bench_quadnerdle data/equations_8.txt` |

---

## Detailed usage

### 1. Generate equations

**By default**, standalone 0 on the left-hand side is excluded (official Nerdle rules).

```bash
./generate --len 5
./generate --len 6
./generate --len 7
./generate --len 8
```

**Python fallback:**
```bash
python src/generate.py --len 5
```

**Micro Bellman policy** (`nerdle --len 5`, `bench_nerdle`): after generating `data/equations_5.txt`, build the lookup table used for exact min-expected-guess play:

```bash
make micro_policy
```

Use **`--strategy bellman`** (default for Micro when the file exists), **`--strategy entropy`** for the v2 selector, or **`--strategy partition`**: among remaining candidate equations, maximize the number of distinct feedback patterns on **S**; among ties, maximize **P(solve within the tries left)** (uniform prior on **S**), then minimize **E[guesses]** under the same partition policy (computed recursively). Smallest index last. For pools larger than 128 equations, only the partition + index rule is used (no DP).

**Options:**
- `--len N` — Equation length, 5-8 (default: 8)
- `--allow-bare` — Include bare LHS (e.g. `18=18`); default: LHS must have ≥1 operator
- `--allow-standalone-zero` — Include equations with standalone 0 on LHS
- `--no-zero` — Exclude equations where the result is 0

**Maxi (10 tiles):**
```bash
./generate_maxi --no-pointless-brackets   # ~1.81M, exclude brackets that don't change eval
./generate_maxi                     # ~1.86M, include all (some redundant bracket variants)
```
Pointless brackets = removal yields same result (e.g. `(2)`, `(a+b)+c`). Necessary ones like `3*(9+4)=39` are kept.

**Solve and benchmark Maxi:**
```bash
./solve data/equations_10.txt            # optimal first guess (may take a while)
./bench_nerdle data/equations_10.txt     # 6 tries, uses first eq as guess until solve is run
```

---

### 2. Optimal first guess (`solve`)

```bash
./solve data/equations_5.txt
./solve data/equations_8.txt
```

Finds the entropy-optimal first guess. Uses OpenMP for parallel computation.

---

### 3. Interactive Nerdle (`nerdle`)

Solver-assisted play for single-equation Nerdle. Play on [nerdlegame.com](https://www.nerdlegame.com/); enter your guess and the feedback (G/P/B) you received; the tool suggests the next guess.

```bash
./nerdle --len 5     # micro
./nerdle --len 6     # mini
./nerdle --len 7     # midi
./nerdle --len 8     # classic
./nerdle --len 10    # maxi (²³ brackets)
./nerdle --len 5 --strategy partition   # greedy max-feedback-partition (any length)
```

Press Enter to use the suggested guess, or type your own. Type `y` when you solve it. Requires `data/equations_N.txt` (run `./generate --len N` or `./generate_maxi` first).

---

### 4. Binerdle (`binerdle`)

Guess 2 Nerdle equations in 7 tries. **One guess per turn** applies to both; you get separate feedback for each equation. The two equations are always distinct. Uses stratified sampling when the candidate pair space is large.

```bash
./binerdle --len 6     # mini (6-tile equations)
./binerdle --len 8     # normal (8-tile equations)
```

Enter feedback for each equation (G/P/B or `y` if correct). You can override the suggested guess by typing your own (same length); press Enter to use the suggestion.

---

### 5. Quad Nerdle (`quadnerdle`)

Guess 4 Nerdle equations in 10 tries. **One guess per turn** applies to all four; you get separate feedback for each equation. All 4 equations are always distinct. Uses stratified Monte Carlo for entropy when the candidate space is large.

```bash
./quadnerdle --len 8
```

Enter feedback for each of the 4 equations (G/P/B or `y` if correct). Recommended first guess: **1\*15-9=6** (Quad-optimal) or **43-27=16** (Binerdle optimal). Run `./solve_quadnerdle data/equations_8.txt` to recompute.

---

### 6. Benchmark (`bench_nerdle`)

```bash
./bench_nerdle data/equations_8.txt
./bench_nerdle data/equations_8.txt --selector v2   # default is v2
./bench_nerdle data/equations_10.txt --sample 5000   # Maxi: sample 5k (full ~1.8M takes hours)
./bench_nerdle data/equations_8.txt --selector v1    # legacy: 300-guess pool, 1-ply entropy only
./bench_nerdle data/equations_5.txt --strategy partition
```

Simulates the solver against all equations and reports mean/max guesses and distribution. **`--strategy`**: Micro defaults to **Bellman** when `data/optimal_policy_5.bin` exists; otherwise **entropy** with **`--selector v2`** (default): full guess pool (or candidate ∪ random sample for Maxi-sized sets), 1-ply entropy plus a small bonus when the guess is still a candidate, then a 2-ply tiebreak on the top finalists. **`v1`** reproduces the older subsampled-pool benchmark for comparison. **`--strategy partition`** uses the rule above (recursive P / EV tie-breaks when the equation pool has ≤128 entries).

---

### 7. Binerdle benchmark (`bench_binerdle`)

```bash
./bench_binerdle data/equations_6.txt   # mini
./bench_binerdle data/equations_8.txt   # normal
```

Simulates Binerdle with one shared guess per turn. Reports mean guesses and distribution. Turns = when both equations are identified (effectively max of the two "virtual" identification times).

---

### 8. Quad Nerdle benchmark (`bench_quadnerdle`)

```bash
./bench_quadnerdle data/equations_8.txt
./bench_quadnerdle data/equations_8.txt --sample 10000
./bench_quadnerdle data/equations_8.txt --single    # use 48-32=16 as first guess
./bench_quadnerdle data/equations_8.txt --binerdle  # use 43-27=16 as first guess
```

Simulates Quad Nerdle over **sampled distinct** quadruples (all 4 equations different). Default 5000 samples. Compare single vs Binerdle first guess with `--single` / `--binerdle`.

---

## Nerdle rules (summary)

- Each guess is a valid arithmetic equation
- Characters: `0-9`, `+`, `-`, `*`, `/`, `=`
- Exactly one `=`, with only a number to the right
- Standard order of operations (* and / before + and -)
- LHS must have at least one operator (no bare numbers like `18=18`)
- No standalone 0 on the LHS (0 may appear in 10, 20, etc., or on the RHS)
- Feedback: **G** = correct char, correct position; **P** = correct char, wrong position; **B** = char not in solution

---

## Dependencies

- **C++ compiler with OpenMP** — e.g. `g++` with `-fopenmp`
- **Python 3** (optional) — for `generate.py` fallback only

---

## Build

```bash
make              # build all tools
make clean        # remove binaries
```

---

## Optimal first guesses (precomputed)

**Nerdle (single):**

| Length | First guess | Entropy |
|--------|-------------|---------|
| 5 (micro) | 3+2=5 | Precomputed Bellman policy (`data/optimal_policy_5.bin`; see `make micro_policy`) |
| 6 (mini) | 4*7=28 | 5.82 bits |
| 7 (midi) | 4+27=31 | ~8.26 bits |
| 8 (classic) | 48-32=16 | 9.78 bits |
| 10 (maxi) | 76+1-23=54 | 12.82 bits |

**Binerdle (pair space, distinct equations):**

| Length | First guess | Notes |
|--------|-------------|------|
| 6 (mini) | 4*7=28 | Same as single |
| 8 (normal) | 43-27=16 | Slightly better than 48-32=16 for distinct pairs |

**Quad Nerdle (quad space, len 8):**

| First guess | Notes |
|-------------|-------|
| 1\*15-9=6 | Quad-optimal (stratified MC) |
| 43-27=16 | Binerdle optimal; very close |
| 48-32=16 | Single Nerdle optimal; very close |

Run `./solve_quadnerdle data/equations_8.txt` to recompute. **All 4 equations are always distinct** (space = P(n,4) = n×(n-1)×(n-2)×(n-3), not n⁴). Uses **adaptive strategy** with stratified sampling and a 200k-quad tiebreaker when the top 2 are within 0.02 bits. Use `--no-stratify` for plain random sampling.

**Benchmark stats (lengths 5–10):** `bench_nerdle` uses **precomputed Bellman** for Micro (len 5) when `data/optimal_policy_5.bin` is present; other lengths use **v2** (full equation pool + 2-ply; see `src/nerdle_core.hpp`).

| Length | EV guesses | 2 guesses | 3 guesses | 4 guesses | 5 guesses |
|--------|------------|-----------|-----------|-----------|-----------|
| 5 (micro) | 2.94 | 23.6% | 57.5% | 16.5% | 1.6% |
| 6 (mini) | 2.64 | 35.4% | 63.6% | 0.5% | 0% |
| 7 (midi) | 3.08 | 6.1% | 79.6% | 14.3% | 0% |
| 8 (classic) | 3.03 | 7.6% | 81.4% | 11.0% | 0.006% |
| 10 (maxi) | ~3.43 | — | — | — | — |

*Rows 5–8: full equation lists. Maxi: 500-equation random sample (`--selector v2`; sampling uses `--sample` with fixed shuffle seed 42). A 5k-sample v1 run on Maxi was ~3.65 EV; v2 is slower on Maxi—use `--sample`.*

---

## Why different games favour different first guesses

**Single Nerdle** maximizes H(fb) — how evenly the guess partitions the solution space. A guess like `48-32=16` is optimal because it creates many distinct feedback patterns with similar probability.

**Binerdle** maximizes H(fb₁, fb₂) over *pairs* of equations. This equals H(fb₁) + H(fb₂|fb₁). A guess that’s good for single might induce correlated feedback when applied to two equations (similar equations → similar feedback). `43-27=16` is slightly better than `48-32=16` for pairs because it tends to produce more *independent* feedback across the two slots — knowing fb₁ gives less information about fb₂.

**Quad Nerdle** maximizes H(fb₁, fb₂, fb₃, fb₄). We want each successive conditional entropy H(fbₖ|fb₁…fbₖ₋₁) to stay high. A guess that “spreads out” feedback across equations with different structure (operators, digits, result size) is better for Quad: it reduces correlation between the four patterns. A guess optimal for single might cluster too much feedback into a few joint patterns when applied to four diverse equations.

---

## Stratified sampling

Plain Monte Carlo can undersample certain equation families (e.g. division-heavy or small-result equations). **Stratified sampling** classifies each equation by type (operator mix + result magnitude bucket) and samples proportionally from each stratum. This yields lower variance and a more stable ranking of guesses. Used in:

- **solve_quadnerdle** — for finding the optimal first guess (use `--no-stratify` to disable)
- **quadnerdle** — for subsequent guesses when the candidate space exceeds 50k
- **binerdle** — for subsequent guesses when the pair space exceeds 50k

Implementation uses 16 strata and proportional allocation.

---

## Hardest equations (most guesses to solve)

None require 6 guesses; these take the maximum (5 for len 5/7/8/10, 4 for len 6):

| Length | Max guesses | Equations |
|-------|-------------|-----------|
| 5 (micro) | 5 | `1*5=5`, `5/5=1`, `9*1=9`, `9/1=9` |
| 6 (mini) | 4 | `12-9=3` |
| 7 (midi) | 5 | `13*1=13`, `13/1=13`, `25-25=0`, `25/25=1`, `37/37=1`, `50/50=1`, `63*1=63`, `63/1=63`, `69/69=1`, `93-91=2`, `93-92=1`, `99/99=1` |
| 8 (classic) | 5 | `12-7-4=1`, `14-7-5=2`, `14/1/7=2`, `17+40=57`, `17-7-7=3`, `39+20=59`, `40+48=88`, `66-16=50`, `7/21*9=3`, `9*1*8=72`, `9+6+8=23`, `9+7+5=21` |
| 10 (maxi) | 5 | *(from 5k sample; full list not computed)* |