#!/usr/bin/env bash
# Regenerate browser partition JSON, Micro policy copy, and the esbuild bundle.
# Run from anywhere; defaults to full rebuild. Use --web-only after TS-only edits.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_ONLY=0

usage() {
  echo "Regenerate partition JSON, Micro policy copy, and the esbuild bundle."
  echo "Usage: $0 [--web-only] [--help]"
  echo "  (default)  make browser_partition_data_web + npm ci/install + npm run build in web/"
  echo "  --web-only skip Make; only install + bundle (faster when only web/src changed)"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --web-only) WEB_ONLY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 1 ;;
  esac
done

cd "$ROOT"

if [[ "$WEB_ONLY" -eq 1 ]]; then
  echo "[prepare_web_deploy] web-only: skipping partition artifacts"
else
  pools=(
    data/equations_5.txt
    data/equations_6.txt
    data/equations_7.txt
    data/equations_8.txt
    data/equations_10.txt
  )
  for f in "${pools[@]}"; do
    if [[ ! -f "$f" ]]; then
      echo "missing pool file: $f" >&2
      echo "Generate pools first, e.g.: ./generate --len 5 && … && ./generate_maxi" >&2
      exit 1
    fi
  done

  if [[ ! -f web/data/optimal_policy_5.bin ]] && [[ ! -f data/optimal_policy_5.bin ]]; then
    echo "[prepare_web_deploy] building Micro Bellman policy (make micro_policy)…"
    make micro_policy
  fi

  echo "[prepare_web_deploy] make browser_partition_data_web…"
  make browser_partition_data_web
fi

cd "$ROOT/web"
if [[ -f package-lock.json ]]; then
  npm ci
else
  npm install
fi
npm run build

echo "[prepare_web_deploy] done — deployable assets under web/ (e.g. app.bundle.js, data/)"
