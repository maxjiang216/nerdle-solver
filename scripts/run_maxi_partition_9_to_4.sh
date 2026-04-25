#!/usr/bin/env bash
set -euo pipefail

# Runs distinct counts 9 → 4 sequentially. See docs/MAXI_FIRST_PARTITION_SWEEP.md
#
#   ./scripts/run_maxi_partition_9_to_4.sh
#   OUT_DIR=...  FOREGROUND=1  ./scripts/run_maxi_partition_9_to_4.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cascade_stages() {
  local d best wall top
  {
    echo "=== cascade start $(date -Is) ==="
    echo "Pool=$NPD_POOL  OMP_NUM_THREADS=$OMP_NUM_THREADS"
  } | tee -a "$NPD_LOG"

  for d in 9 8 7 6 5 4; do
    STAGE_DIR="${NPD_BASE}/${d}"
    mkdir -p "$STAGE_DIR"
    echo | tee -a "$NPD_LOG"
    echo "=== stage distinct=${d} start $(date -Is) ===" | tee -a "$NPD_LOG"

    "$ROOT/maxi_first_partition_sweep" \
      --pool "$NPD_POOL" \
      --out-dir "$STAGE_DIR" \
      --min-distinct "$d" \
      --max-distinct "$d" \
      --progress-every "$NPD_PROGRESS_EVERY" \
      --progress-file "$NPD_PROGRESS_FILE"

    best=0
    wall=""
    top="?"
    if [[ -f "$STAGE_DIR/summary.txt" ]]; then
      best=$(awk -F '\t' '/^best_partitions/ {print $2}' "$STAGE_DIR/summary.txt")
      wall=$(awk -F '\t' '/^wall_time/ {print $2}' "$STAGE_DIR/summary.txt" 2>/dev/null || true)
      top=$(awk -F '\t' '/^top_equation/ {print $2}' "$STAGE_DIR/summary.txt" 2>/dev/null || true)
    fi
    printf '%s\t%s\t%s\t%s\n' "$d" "$best" "$wall" "$top" >>"$NPD_SUMMARY"
    echo "=== stage distinct=${d} done  best=${best}  wall_s=${wall}  $(date -Is) ===" | tee -a "$NPD_LOG"
  done

  echo "=== cascade finished $(date -Is) ===" | tee -a "$NPD_LOG"
  echo "Summary table: $NPD_SUMMARY" | tee -a "$NPD_LOG"
}

if [[ "${1:-}" == "--worker" ]]; then
  cascade_stages
  exit 0
fi

POOL="${POOL:-data/equations_10.txt}"
NPD_BASE="${OUT_DIR:-data/maxi_first_partition/cascade_9_to_4}"
NPD_PROGRESS_FILE="${PROGRESS_FILE:-$NPD_BASE/current_progress.txt}"
NPD_PROGRESS_EVERY="${PROGRESS_EVERY:-500}"
NPD_THREADS="${THREADS:-$(nproc)}"
NPD_POOL="$POOL"
FOREGROUND="${FOREGROUND:-0}"
GENERATE_ARGS="${GENERATE_ARGS:-}"

mkdir -p "$NPD_BASE"
NPD_LOG="$NPD_BASE/cascade.log"
NPD_SUMMARY="$NPD_BASE/stages_summary.tsv"
if [[ ! -f "$NPD_SUMMARY" ]]; then
  echo -e "distinct\tbest_partitions\twall_s\ttop_equation" >"$NPD_SUMMARY"
fi

export NPD_BASE NPD_LOG NPD_SUMMARY NPD_POOL NPD_PROGRESS_FILE NPD_PROGRESS_EVERY

make maxi_first_partition_sweep
if [[ ! -s "$POOL" ]]; then
  make generate_maxi
  # shellcheck disable=SC2086
  ./generate_maxi $GENERATE_ARGS
fi

export OMP_NUM_THREADS="$NPD_THREADS"

if [[ "$FOREGROUND" == "1" ]]; then
  cascade_stages 2>&1 | tee "$NPD_BASE/cascade_full.log"
else
  nohup env OMP_NUM_THREADS="$NPD_THREADS" NPD_BASE="$NPD_BASE" NPD_LOG="$NPD_LOG" NPD_SUMMARY="$NPD_SUMMARY" \
    NPD_POOL="$NPD_POOL" NPD_PROGRESS_FILE="$NPD_PROGRESS_FILE" NPD_PROGRESS_EVERY="$NPD_PROGRESS_EVERY" \
    "$0" --worker >>"$NPD_BASE/cascade_full.log" 2>&1 &
  echo $! >"$NPD_BASE/cascade.pid"
  echo "Background cascade pid=$(cat "$NPD_BASE/cascade.pid")"
  echo "  tail -f $NPD_BASE/cascade_full.log"
  echo "  cat $NPD_PROGRESS_FILE"
  echo "  tail -f $NPD_LOG"
fi
