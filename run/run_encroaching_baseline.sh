#!/usr/bin/env bash
set -euo pipefail

BIN="./bin/bench_main_runtime"
OUT="results/raw/23_encroaching_baseline_raw.csv"
SEEDS="results/seeds.txt"

mkdir -p results/raw results/summary

# Fresh output file
rm -f "$OUT"

echo "============================================================"
echo "Running EncroachingListsSort Baseline Experiments (10 runs per config)"
echo "============================================================"

# 1. block_cyclic at 10^6, 10^7, 10^8
for n in 1000000 10000000 100000000; do
  echo ">>> Workload: block_cyclic, n=$n, algo=encroaching_lists_sort"
  "$BIN" \
    --dataset "block_cyclic" \
    --n "$n" \
    --algo "encroaching_lists_sort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

# 2. descending at 10^6, 10^7, 10^8
for n in 1000000 10000000 100000000; do
  echo ">>> Workload: descending, n=$n, algo=encroaching_lists_sort"
  "$BIN" \
    --dataset "descending" \
    --n "$n" \
    --algo "encroaching_lists_sort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

# 3. random at 10^6, 10^7
for n in 1000000 10000000; do
  echo ">>> Workload: random, n=$n, algo=encroaching_lists_sort"
  "$BIN" \
    --dataset "random" \
    --n "$n" \
    --algo "encroaching_lists_sort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

echo "DONE: wrote $OUT"
