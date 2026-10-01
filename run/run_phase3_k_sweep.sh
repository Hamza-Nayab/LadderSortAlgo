#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary results/plots

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
g++ -O3 -std=c++20 -march=native bench_k_sweep.cpp -o bin/bench_k_sweep

./bin/bench_k_sweep \
  --n 10000000 \
  --rounds 10 \
  --out results/raw/05_k_sweep_raw.csv \
  --git-hash "$GIT_HASH"
