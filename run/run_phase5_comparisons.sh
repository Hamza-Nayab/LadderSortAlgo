#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

RAW_PATH="results/raw/15_comparisons_raw.csv"
HEADER="dataset,n,algo,seed,round,time_sec,total_comparisons,hint_comparisons,merge_comparisons,k_final,k_over_n,used_fallback,sorted_ok,git_hash"
GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

datasets=(random descending block_cyclic two_run_riffle social_feed)
sizes=(100000 1000000)
seeds=(1 2 3 4 5)
algorithms=(RawLadderSort HybridLadderSort TimSort StdSort StableSort)

g++ -O3 -std=c++20 -march=native bench_comparisons_phase5.cpp -o bin/bench_comparisons_phase5

printf '%s\n' "$HEADER" > "$RAW_PATH"

for dataset in "${datasets[@]}"; do
  for n in "${sizes[@]}"; do
    for seed in "${seeds[@]}"; do
      for algo in "${algorithms[@]}"; do
        echo "dataset=$dataset n=$n seed=$seed algo=$algo"
        row="$(
          ./bin/bench_comparisons_phase5 \
            --dataset "$dataset" \
            --algo "$algo" \
            --n "$n" \
            --seed "$seed" \
            --round 1 \
            --git-hash "$GIT_HASH"
        )"

        if [[ -z "$row" ]]; then
          echo "ERROR: missing stdout row for $dataset/$n/$seed/$algo" >&2
          exit 1
        fi

        sorted_ok="$(printf '%s\n' "$row" | awk -F, '{print $13}')"
        if [[ "$sorted_ok" != "1" ]]; then
          echo "ERROR: sorted_ok != 1: $row" >&2
          exit 1
        fi

        total_comparisons="$(printf '%s\n' "$row" | awk -F, '{print $7}')"
        if ! [[ "$total_comparisons" =~ ^[0-9]+$ ]] || (( total_comparisons <= 0 )); then
          echo "ERROR: invalid total comparisons: $row" >&2
          exit 1
        fi

        printf '%s\n' "$row" >> "$RAW_PATH"
      done
    done
  done
done

echo "Phase 5 comparisons run complete: $RAW_PATH"
