# Nerdle Solver

Entropy-based solver for [Nerdle](https://www.nerdlegame.com/) and [Binerdle](https://www.nerdlegame.com/binerdle.html). Supports Micro (5-tile), Mini (6), Midi (7), Classic (8-tile) Nerdle, plus Binerdle Mini (6) and Normal (8).

---

## Project structure

```
nerdle_solver/
├── src/                  # C++ and Python source
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
```

---

## Common use cases

| Goal | Command |
|------|---------|
| Generate equations | `./generate --len N` or `./generate_maxi` |
| Best first guess | `./solve data/equations_N.txt` |
| Nerdle benchmark | `./bench_nerdle data/equations_N.txt` |
| Interactive Nerdle | `./nerdle --len 5\|6\|7\|8\|10` |
| Binerdle optimal 1st | `./solve_binerdle data/equations_6.txt` |
| Binerdle interactive | `./binerdle --len 6` or `--len 8` |
| Binerdle benchmark | `./bench_binerdle data/equations_6.txt` |

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
```

Press Enter to use the suggested guess, or type your own. Type `y` when you solve it. Requires `data/equations_N.txt` (run `./generate --len N` or `./generate_maxi` first).

---

### 4. Binerdle (`binerdle`)

Guess 2 Nerdle equations in 7 tries. **One guess per turn** applies to both; you get separate feedback for each equation.

```bash
./binerdle --len 6     # mini (6-tile equations)
./binerdle --len 8     # normal (8-tile equations)
```

Enter feedback for each equation (G/P/B or `y` if correct). You can override the suggested guess by typing your own (same length); press Enter to use the suggestion.

---

### 5. Benchmark (`bench_nerdle`)

```bash
./bench_nerdle data/equations_8.txt
./bench_nerdle data/equations_10.txt --sample 5000   # Maxi: sample 5k (full ~1.8M takes hours)
```

Simulates the solver against all equations and reports mean/max guesses and distribution.

---

### 6. Binerdle benchmark (`bench_binerdle`)

```bash
./bench_binerdle data/equations_6.txt   # mini
./bench_binerdle data/equations_8.txt   # normal
```

Simulates Binerdle with one shared guess per turn. Reports mean guesses and distribution. Turns = when both equations are identified (effectively max of the two "virtual" identification times).

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
| 5 (micro) | 4-1=3 | 4.54 bits |
| 6 (mini) | 4*7=28 | 5.82 bits |
| 7 (midi) | 6+18=24 | 8.15 bits |
| 8 (classic) | 48-32=16 | 9.78 bits |
| 10 (maxi) | 76+1-23=54 | 12.82 bits |

**Binerdle (pair space):**

| Length | First guess | Notes |
|--------|-------------|------|
| 6 (mini) | 4*7=28 | Same as single |
| 8 (normal) | 43-27=16 | Slightly better than 48-32=16 for pairs |

**Benchmark stats (lengths 5–10):**

| Length | EV guesses | 2 guesses | 3 guesses | 4 guesses | 5 guesses |
|--------|------------|-----------|-----------|-----------|-----------|
| 5 (micro) | 3.00 | 19.7% | 61.4% | 15.0% | 3.1% |
| 6 (mini) | 2.64 | 35.4% | 63.6% | 0.5% | 0% |
| 7 (midi) | 3.20 | 3.7% | 72.7% | 23.4% | 0.2% |
| 8 (classic) | 3.17 | 3.5% | 76.6% | 19.9% | 0.1% |
| 10 (maxi) | 3.65 | 0.2% | 37.6% | 59.0% | 3.2% |

*Maxi stats from 5k-sample benchmark (`./bench_nerdle data/equations_10.txt --sample 5000`). Full benchmark ~1.8M equations would take hours.*

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
