#!/usr/bin/env bash
set -euo pipefail

mkdir -p results/raw
GIT_HASH=$(./run/get_git_hash.sh)

echo "=== Running Phase 3 K smoke test ==="
./bin/bench_k_smoke

echo "=== Running Phase 2 benchmark smoke test ==="
./bin/bench_postinsert_multi \
  --dataset social_feed \
  --n 10000 \
  --algo laddersort_raw \
  --rounds 1 \
  --warmups 1 \
  --seeds results/seeds.txt \
  --out results/raw/smoke_postinsert.csv \
  --git-hash "$GIT_HASH"

echo "Smoke tests completed successfully."