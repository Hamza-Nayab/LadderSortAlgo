#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

g++ -O3 -std=c++20 -march=native bench_ablation_phase4.cpp -o bin/bench_ablation_phase4

./bin/bench_ablation_phase4 \
  --dataset social_feed \
  --sizes 1000000,10000000,100000000 \
  --rounds 1 \
  --seeds results/seeds.txt \
  --out results/raw/07_ablation_social_raw.csv \
  --git-hash "$GIT_HASH"
