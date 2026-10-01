#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

g++ -O3 -std=c++20 -march=native bench_hybrid_phase4.cpp -o bin/bench_hybrid_phase4

./bin/bench_hybrid_phase4 \
  --datasets descending,random,block_cyclic \
  --sizes 1000000,10000000,100000000 \
  --rounds 1 \
  --seeds results/seeds.txt \
  --out results/raw/11_hybrid_raw.csv \
  --git-hash "$GIT_HASH"
