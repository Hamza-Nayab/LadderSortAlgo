#!/usr/bin/env bash
set -euo pipefail

BIN="./bin/bench_main_runtime"
OUT="results/raw/01_main_runtime_raw.csv"
SEEDS="results/seeds.txt"

mkdir -p results/raw results/summary

# Fresh Phase 2 output
rm -f "$OUT"

DATASETS=(
  random
  ascending
  descending
  band_limited
  block_cyclic
  two_run_riffle
  social_feed
  partial_index
)

SIZES=(
  1000000
  10000000
  100000000
)

ALGOS=(
  laddersort_raw
  laddersort_hybrid
  timsort
  std_sort
  std_stable_sort
  quicksort
  mergesort
)

for dataset in "${DATASETS[@]}"; do
  for n in "${SIZES[@]}"; do
    for algo in "${ALGOS[@]}"; do

      echo "============================================================"
      echo "Phase 2: dataset=${dataset}, n=${n}, algo=${algo}"
      echo "============================================================"

      "$BIN" \
        --dataset "$dataset" \
        --n "$n" \
        --algo "$algo" \
        --rounds 10 \
        --warmups 1 \
        --seeds "$SEEDS" \
        --out "$OUT"

    done
  done
done

echo "DONE: wrote $OUT"