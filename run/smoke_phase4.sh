#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

g++ -O3 -std=c++20 -march=native bench_ablation_phase4.cpp -o bin/bench_ablation_phase4

./bin/bench_ablation_phase4 \
  --dataset social_feed \
  --sizes 10000 \
  --rounds 1 \
  --out results/raw/smoke_phase4_ablation_social.csv \
  --git-hash "$GIT_HASH"

./bin/bench_ablation_phase4 \
  --dataset two_run_riffle \
  --sizes 10000 \
  --rounds 1 \
  --out results/raw/smoke_phase4_ablation_riffle.csv \
  --git-hash "$GIT_HASH"

g++ -O3 -std=c++20 -march=native bench_hybrid_phase4.cpp -o bin/bench_hybrid_phase4

./bin/bench_hybrid_phase4 \
  --datasets descending,random,block_cyclic \
  --sizes 10000 \
  --rounds 1 \
  --out results/raw/smoke_phase4_hybrid.csv \
  --git-hash smoke

echo "Phase 4 smoke test complete."
