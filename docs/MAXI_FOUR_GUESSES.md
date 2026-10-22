# Can Maxi be guaranteed in 4 guesses?

Status: **not with the opening `56+4-21=39`** (proof below, under the stated model). Other
openings are open.

## Model

- **Answers:** the strict cleaned pool `A2`: `./generate_maxi --no-pointless-brackets`, minus
  equations with a bracketed negative (`(-`) and square/cube-only left sides (`209²=43681`).
  2,034,954 equations; contains all 32 known official answers we collected.
- **Guesses:** any row of `data/equations_10.txt` (2,102,375, a superset of `A2`).
- **Win:** the guess must equal the answer exactly. (Nerdle's default "commutative answers"
  setting may also accept reorderings such as `20+10=30` for `10+20=30`; that would change the
  result, because the hardest sets below are mostly reorderings.)

```bash
./generate_maxi --no-pointless-brackets -o /tmp/npb
LC_ALL=C.UTF-8 grep -vP '\(-|^\d+[²³]=' /tmp/npb/equations_10.txt > /tmp/A2.txt
```

## What a 4-guess strategy needs

After the opening, each feedback bucket `B` has three guesses left: guess 2, guess 3, and the
winning guess 4. So guess 2 must split `B` into classes that guess 3 can each **separate**
(every member gets a different feedback). Call a set **unseparable** if no single guess
separates it. Two facts make the search tractable:

1. A superset of an unseparable set is unseparable. So a guess 2 that keeps a known
   unseparable set (a **core**) inside one class fails without further work.
2. A class is separable iff it contains no core. So guess 2 works iff it splits every core
   inside `B` (and the classes it leaves contain no core we have not found yet).

`maxi_guarantee --cores K` (with `--tries 4`) runs this per bucket:

- **Seed cores.** Equations with the same tiles in a different order (same multiset) are the
  usual culprits: every member lights up the same symbols, only green/purple positions
  differ. Each such family of 3–40 members inside `B` that no guess separates is shrunk to a
  minimal core. Shrinking uses one pass over the guess list that records every distinct way a
  guess partitions the set, then deletes members while every recorded partition still puts two
  survivors together.
- **Cover loop.** Walk all 2.1M guesses (best first by largest class). Skip any that keep a
  known core together. Fully check the next one. If a class is unseparable, shrink it to a new
  core and continue.
- **Proof.** If no guess in the list splits every known core, `B` has no 3-guess finish, so the
  opening has no 4-guess guarantee. Every core is verified against the whole guess list, and
  every guess is either blocked by a core or was checked and failed, so the conclusion is
  exact for this model.

## Result for `56+4-21=39`

```bash
./maxi_guarantee --pool /tmp/A2.txt --guesses data/equations_10.txt --tries 4 --largest 4 \
    --pool-scan-max 1000000 --cores 8000 --reuse-separators --verbose
```

| Bucket (size) | Cores | Guess-2 checks | Result |
|---|---|---|---|
| 3,249 | 3,359 | 5,806 | **proven impossible** |
| 3,234 | 3,089 | 6,007 | **proven impossible** |
| 3,256 | 2,044+ | 2,000+ | stopped (19,595 guesses still split every core) |
| 3,357 | 290+ | 250+ | stopped |

The guesses that still split every known core shrank steadily for the 3,249 bucket: 4,681 →
3,495 → 1,775 → 428 → 29 → 0, about one per check. Typical cores are 6–20 reorderings, such as
`2+1*1+2²=7, 2+1/1+2²=7, 2/1+1+2²=7, 1+2+1*2²=7` or `18-1³*4²=2, 1³*18-4²=2, 18*1³-4²=2`.
Each proof took about 70 minutes on one thread.

## Other openings

Sweeping every 10-distinct-symbol opening on `A2` (`maxi_first_partition_sweep`, 112,710
openings, 4.5 min) shows none has a largest bucket under 2,400 (`35-8+67=94`, `37-8+65=94`);
`56+4-21=39` has 3,357. Bucket size is not the whole story, though: what matters is how many
unseparable families a bucket contains.

## Open directions

- Run the cover proof on the largest buckets of the openings with the smallest largest bucket.
  Parallelizing the cover loop over the surviving guesses within one bucket would cut the
  ~70 min per proof.
- Re-run under "commutative answers": treat reorderings that the game accepts as the same
  answer. Needs the exact rule the game uses.
- Widen the guess list (e.g. equations starting with `(`, which the game accepts). The proof
  above only rules out guesses from `data/equations_10.txt`.
