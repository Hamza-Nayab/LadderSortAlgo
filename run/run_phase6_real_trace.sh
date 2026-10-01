#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

INPUT="data/processed/real_trace_github_events.csv"
RAW_OUT="results/raw/19_real_trace_raw.csv"
GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

if [[ ! -s "$INPUT" ]]; then
  echo "missing processed real trace: $INPUT" >&2
  exit 1
fi

g++ -O3 -std=c++20 -march=native bench_real_trace_phase6.cpp -o bin/bench_real_trace_phase6

printf '%s\n' 'dataset,n,algo,seed,round,time_sec,k_final,k_over_n,used_fallback,sorted_ok,git_hash' > "$RAW_OUT"

for round in {1..10}; do
  for algo in RawLadderSort HybridLadderSort TimSort StdSort StableSort; do
    ./bin/bench_real_trace_phase6 \
      --input "$INPUT" \
      --algo "$algo" \
      --seed 1 \
      --round "$round" \
      --git-hash "$GIT_HASH" >> "$RAW_OUT"
  done
done

echo "Phase 6 real-trace benchmark complete: $RAW_OUT"
