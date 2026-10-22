# Hand-off: what the cloud session could not do

Work on branch `claude/funny-mendel-4pwus6` (PR #23) ran in a sandbox with restricted network
access and 4 CPU cores. This lists what was blocked or cut short, so it can be picked up locally.

## Blocked by the network

The sandbox could not reach `www.nerdlegame.com` or `faqs.nerdlegame.com` (also not Try Hard
Guides or the Wayback Machine). Only web *search* snippets and `raw.githubusercontent.com` worked.

- **Official answer history.** Every daily answer has a page:
  `https://www.nerdlegame.com/answers/maxi/maxi-nerdle-answer-YYYY-MM-DD.html` (also `classic/`,
  `mini/`, `midi/`; archive at `https://www.nerdlegame.com/answers.html`, ~1,350 Maxi answers).
  Only 32 Maxi answers were collected (from search snippets): `docs/known_maxi_answers.tsv`.
  All 32 are in `data/equations_10.txt` and in the cleaned pools below.
- **Rules page and settings.** Not read directly. Search snippets of the FAQ say: no leading or
  lone zeros, no negative number at the start or after `=`, one `=` with a single number on the
  right, standard order of operations. They also mention a **"commutative answers" setting** (on
  by default): `20+10=30` accepted for `10+20=30`. Some answer pages are marked "order
  important". The exact rule decides the Maxi-in-4 question (see `docs/MAXI_FOUR_GUESSES.md`).
- **Replay mode.** `nerdlegame.com/replay` (or `nerdlegame.com/YYYYMMDD`) replays the original
  daily answer; it does not generate puzzles. The page's script may contain the answer list,
  which would give the full history in one download.

What to do with the history once scraped:
1. Check every answer is in the pool, and which pool rules they never break (brackets at the
   start, bracketed negatives `(-5)`, pointless brackets like `(2)`, square/cube-only left sides).
2. Compare feature frequencies with the pool. With 30 dated answers: brackets 43% vs 0.5% of the
   cleaned pool, `²`/`³` 40% vs 31%, `=0` 10% vs 3%. Answers look curated, not uniform.
3. Use the frequencies as a prior for average-case strategies.

## Pool questions found but not resolved

From comparing `data/equations_10.txt` with
[pedrokkrause/Nerdle-Equations](https://github.com/pedrokkrause/Nerdle-Equations):

- **Classic (8 tiles): identical** (17,723 equations both).
- **Maxi: they have 2,197,321, we have 2,102,375.**
  - 95,060 of theirs start with `(`. `generate_maxi.cpp` skips that branch with the comment
    "Maxi Nerdle solutions never start with (" (unverified). These are at least valid *guesses*.
  - The rest (185 only theirs, 299 only ours) are mostly `-5²` cases. Our parser binds unary minus before `²`/`³`: `-5²` = 25, so
    `7+(-2²)=11` is in our pool. Standard maths (and theirs) gives -25. Fix in the parser's
    `factor`/`expon` grammar, then regenerate.
- **The committed Maxi pool was generated without `--no-pointless-brackets`.** It holds 49,001
  equations with brackets around a bare number (`6+9-(2)=13`), 742 with double brackets,
  10,660 with a bracketed negative and 270 with a square/cube-only left side. The README's pool
  sizes (~1.81M / ~1.86M) are stale: with the flag the generator now gives 2,036,124.

## Cut short by compute or time

| Question | Status | Notes |
|---|---|---|
| Binerdle (8) in 5 | open | `binerdle_guarantee --tries 5 --opening 52-34=18 --width 0` timed out at 50 min. Needs proven-impossible caching per bucket pair and largest-first fail-fast. |
| Maxi in 4, other openings | open | One bucket proof ~70 min on one thread (`maxi_guarantee --cores`). Parallelizing the cover loop within a bucket is the next speedup. Best candidates by largest bucket: `35-8+67=94`, `37-8+65=94` (2,400). |
| Maxi in 4, commutative wins | not started | Needs the exact reordering rule. |
| Maxi max-three over the full pool | estimated ~1.8 h | Only 2k and 20k pool samples were run (57.39%, 59.03% solved by guess 3). |
| Full 10-symbol opener sweep on `data/equations_10.txt` | not run | ~5 min with batched feedback. |

## Not tested on other hardware

- The batched feedback (`src/feedback_batch.hpp`) runs only on x86 with AVX2 (checked at run
  time). ARM (Apple Silicon) uses the per-pair fallback; a NEON version was not written.
  AVX-512 (64 lanes) is not used.
- Clang builds need libomp (`libomp-dev`, `brew install libomp`); the Makefile falls back to
  `g++` otherwise. macOS builds were not tried.

## Not done

- The interactive CLI and web UI do not read the override files (`data/maxi_*_overrides.tsv`).
- No guarantee search for Quad Nerdle.
