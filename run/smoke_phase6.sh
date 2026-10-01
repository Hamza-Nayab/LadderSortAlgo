#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw data/processed

INPUT="data/processed/real_trace_github_events.csv"
SMOKE_INPUT="data/processed/real_trace_github_events_smoke.csv"
RAW_OUT="results/raw/smoke_phase6_real_trace.csv"
GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

if [[ ! -s "$INPUT" ]]; then
  echo "missing processed real trace: $INPUT" >&2
  exit 1
fi

g++ -O3 -std=c++20 -march=native bench_real_trace_phase6.cpp -o bin/bench_real_trace_phase6

head -n 10001 "$INPUT" > "$SMOKE_INPUT"

printf '%s\n' 'dataset,n,algo,seed,round,time_sec,k_final,k_over_n,used_fallback,sorted_ok,git_hash' > "$RAW_OUT"

for algo in RawLadderSort HybridLadderSort TimSort StdSort StableSort; do
  ./bin/bench_real_trace_phase6 \
    --input "$SMOKE_INPUT" \
    --algo "$algo" \
    --seed 1 \
    --round 1 \
    --git-hash "$GIT_HASH" >> "$RAW_OUT"
done

awk -F, 'NR>1 && $10 != 1 {print; bad=1} END {exit bad ? 1 : 0}' "$RAW_OUT"
awk -F, 'NR>1 && $7 <= 0 {print; bad=1} END {exit bad ? 1 : 0}' "$RAW_OUT"

echo "Phase 6 smoke test complete."
