#!/usr/bin/env bash
set -euo pipefail

BIN="./bin/bench_main_runtime"
OUT="results/raw/21_patience_baseline_raw.csv"
SEEDS="results/seeds.txt"

mkdir -p results/raw results/summary

# Fresh output file
rm -f "$OUT"

echo "============================================================"
echo "Running PatienceSort Baseline Experiments (10 runs per config)"
echo "============================================================"

# 1. block_cyclic at 10^6, 10^7, 10^8
for n in 1000000 10000000 100000000; do
  echo ">>> Workload: block_cyclic, n=$n, algo=patiencesort"
  "$BIN" \
    --dataset "block_cyclic" \
    --n "$n" \
    --algo "patiencesort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

# 2. random at 10^6, 10^7, 10^8
for n in 1000000 10000000 100000000; do
  echo ">>> Workload: random, n=$n, algo=patiencesort"
  "$BIN" \
    --dataset "random" \
    --n "$n" \
    --algo "patiencesort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

# 3. descending at 10^6, 10^7 (N=10^8 omitted due to 8.1 GB peak memory constraint on 8 GB RAM)
for n in 1000000 10000000; do
  echo ">>> Workload: descending, n=$n, algo=patiencesort"
  "$BIN" \
    --dataset "descending" \
    --n "$n" \
    --algo "patiencesort" \
    --rounds 10 \
    --warmups 1 \
    --seeds "$SEEDS" \
    --out "$OUT"
done

echo "DONE: wrote $OUT"
