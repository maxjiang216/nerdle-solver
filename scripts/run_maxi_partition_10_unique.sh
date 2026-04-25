#!/usr/bin/env bash
set -euo pipefail

# Exact first-guess sweep: only 10 distinct symbols (all tiles unique as bytes).
# Outputs under OUT_DIR:
#   max_partitions.txt   — single line: best partition count
#   progress.txt         — live snapshot (overwrite); safe to: cat OUT_DIR/progress.txt
#   summary.txt, winners.txt, all_candidates.tsv, sweep.log
#
#   FOREGROUND=1  — run in terminal (tee to sweep.log)
#   default       — nohup background, survives SSH drop

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

POOL="${POOL:-data/equations_10.txt}"
OUT_DIR="${OUT_DIR:-data/maxi_first_partition/10_unique}"
PROGRESS_FILE="${PROGRESS_FILE:-$OUT_DIR/progress.txt}"
PROGRESS_EVERY="${PROGRESS_EVERY:-500}"
THREADS="${THREADS:-$(nproc)}"
FOREGROUND="${FOREGROUND:-0}"
GENERATE_ARGS="${GENERATE_ARGS:-}"

mkdir -p "$OUT_DIR"

echo "Building maxi_first_partition_sweep..."
make maxi_first_partition_sweep

if [[ ! -s "$POOL" ]]; then
  echo "Missing $POOL — running ./generate_maxi"
  make generate_maxi
  # shellcheck disable=SC2086
  ./generate_maxi $GENERATE_ARGS
fi

export OMP_NUM_THREADS="$THREADS"

cmd=(
  "$ROOT/maxi_first_partition_sweep"
  --pool "$POOL"
  --out-dir "$OUT_DIR"
  --min-distinct 10
  --max-distinct 10
  --progress-every "$PROGRESS_EVERY"
  --progress-file "$PROGRESS_FILE"
)

LOG="$OUT_DIR/sweep.log"
PIDFILE="$OUT_DIR/sweep.pid"

echo "Pool: $POOL"
echo "Out:  $OUT_DIR"
echo "OMP_NUM_THREADS=$OMP_NUM_THREADS"
echo "Progress file: $PROGRESS_FILE  (cat while running)"
echo "Full log: $LOG"

if [[ "$FOREGROUND" == "1" ]]; then
  "${cmd[@]}" 2>&1 | tee "$LOG"
else
  nohup "${cmd[@]}" >"$LOG" 2>&1 &
  echo $! >"$PIDFILE"
  echo "Background pid=$(cat "$PIDFILE")"
  echo "  tail -f $LOG"
  echo "  cat $PROGRESS_FILE"
fi
