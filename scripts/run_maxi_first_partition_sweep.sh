#!/usr/bin/env bash
set -euo pipefail

# One-command launcher for the exact Maxi first-guess partition sweep.
#
# Defaults:
#   - Build the exact sweeper.
#   - Generate data/equations_10.txt if it is missing.
#   - Sweep only all-unique (10 distinct-symbol) candidates.
#   - Run under nohup in the background so an SSH disconnect does not kill it.
#
# Useful overrides:
#   THREADS=64 ./scripts/run_maxi_first_partition_sweep.sh
#   MIN_DISTINCT=9 MAX_DISTINCT=10 ./scripts/run_maxi_first_partition_sweep.sh
#   FOREGROUND=1 ./scripts/run_maxi_first_partition_sweep.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

POOL="${POOL:-data/equations_10.txt}"
OUT_DIR="${OUT_DIR:-data/maxi_first_partition}"
MIN_DISTINCT="${MIN_DISTINCT:-10}"
MAX_DISTINCT="${MAX_DISTINCT:-10}"
PROGRESS_EVERY="${PROGRESS_EVERY:-500}"
THREADS="${THREADS:-$(nproc)}"
FOREGROUND="${FOREGROUND:-0}"
GENERATE_ARGS="${GENERATE_ARGS:-}"

mkdir -p "$(dirname "$POOL")" "$OUT_DIR"

echo "Building maxi_first_partition_sweep..."
make maxi_first_partition_sweep

if [[ ! -s "$POOL" ]]; then
  echo "$POOL is missing; generating Maxi equations with: ./generate_maxi $GENERATE_ARGS"
  make generate_maxi
  # shellcheck disable=SC2086
  ./generate_maxi $GENERATE_ARGS
fi

export OMP_NUM_THREADS="$THREADS"

cmd=(
  "$ROOT/maxi_first_partition_sweep"
  --pool "$POOL"
  --out-dir "$OUT_DIR"
  --min-distinct "$MIN_DISTINCT"
  --max-distinct "$MAX_DISTINCT"
  --progress-every "$PROGRESS_EVERY"
)

log="$OUT_DIR/sweep.log"
pidfile="$OUT_DIR/sweep.pid"

echo "Pool: $POOL"
echo "Output dir: $OUT_DIR"
echo "Distinct range: [$MIN_DISTINCT, $MAX_DISTINCT]"
echo "OMP_NUM_THREADS=$OMP_NUM_THREADS"
echo "Log: $log"

if [[ "$FOREGROUND" == "1" ]]; then
  "${cmd[@]}" 2>&1 | tee "$log"
else
  nohup "${cmd[@]}" >"$log" 2>&1 &
  pid="$!"
  echo "$pid" > "$pidfile"
  echo "Started background sweep: pid=$pid"
  echo "Follow progress with: tail -f $log"
  echo "When finished, read:"
  echo "  $OUT_DIR/winners.txt"
  echo "  $OUT_DIR/summary.txt"
  echo "  $OUT_DIR/all_candidates.tsv"
fi
