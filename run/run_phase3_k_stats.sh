#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary results/plots

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
ROUNDS="${ROUNDS:-10}"
DATASETS_STR="${DATASETS:-random ascending descending band_limited block_cyclic two_run_riffle social_feed partial_index}"
SIZES_STR="${SIZES:-1000000 10000000 100000000}"

read -r -a DATASET_ARRAY <<< "$DATASETS_STR"
read -r -a SIZE_ARRAY <<< "$SIZES_STR"

g++ -O3 -std=c++17 -march=native bench_k_stats.cpp -o bin/bench_k_stats

./bin/bench_k_stats \
  --rounds "$ROUNDS" \
  --seeds results/seeds.txt \
  --out results/raw/03_k_stats_raw.csv \
  --git-hash "$GIT_HASH"
