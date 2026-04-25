# Maxi First-Guess Partition Sweep

Exact search: score each candidate first guess by how many **distinct feedback patterns** it
induces over the full `data/equations_10.txt` pool. No `n×n` table; each OpenMP thread reuses
one `3^10` counter array.

`²` / `³` in the data file (UTF-8) are normalized internally to the same one-tile encodings
as `generate_maxi`.

## 1) All 10 unique symbols (primary run)

```bash
chmod +x scripts/run_maxi_partition_10_unique.sh
THREADS=$(nproc) ./scripts/run_maxi_partition_10_unique.sh
```

- **Background by default** (`nohup`) so SSH can drop; progress is still on disk.
- **Outputs** under `data/maxi_first_partition/10_unique/` (override with `OUT_DIR=...`):
  - `max_partitions.txt` — single number (best partition count)
  - `winners.txt` — every equation tied for that max
  - `summary.txt`, `all_candidates.tsv`
  - `progress.txt` — overwritten every `--progress-every` guesses; **linear** ETA: `(left)/rate`
  - `sweep.log` — full process stdout

**Watch while it runs**

```bash
cat data/maxi_first_partition/10_unique/progress.txt
tail -f data/maxi_first_partition/10_unique/sweep.log
```

**Foreground (terminal)**: `FOREGROUND=1 ./scripts/run_maxi_partition_10_unique.sh`

## 2) After that: distinct counts 9 → 4

```bash
chmod +x scripts/run_maxi_partition_9_to_4.sh
THREADS=$(nproc) ./scripts/run_maxi_partition_9_to_4.sh
```

- Writes `OUT_DIR/9`, `OUT_DIR/8`, … each with the same file layout as above.
- **Default** `OUT_DIR` = `data/maxi_first_partition/cascade_9_to_4`.
- **Live progress** (current stage, done/total, linear ETA):  
  `cat $OUT_DIR/current_progress.txt`
- **Per-stage log**: `tail -f $OUT_DIR/cascade.log`
- **Full nohup log**: `tail -f $OUT_DIR/cascade_full.log`
- **Table when each stage finishes**: `cat $OUT_DIR/stages_summary.tsv`

## Direct binary (advanced)

```bash
make maxi_first_partition_sweep
./maxi_first_partition_sweep --pool data/equations_10.txt --out-dir /tmp/maxi \\
  --min-distinct 10 --max-distinct 10 --progress-every 500 --progress-file /tmp/maxi/progress.txt
```

## Instance notes

CPU-bound, modest RAM. Good choices: `c7i.16xlarge`, `c7a.16xlarge` (or larger for shorter wall
time). The 10-unique pass is on the order of `102,504 × 1,858,979` inner feedback evals; the
cascade 9→4 is additional heavy stages.

## Older combined script

`scripts/run_maxi_first_partition_sweep.sh` still exists as a single generic wrapper; the two
scripts above are the recommended EC2 flow.
