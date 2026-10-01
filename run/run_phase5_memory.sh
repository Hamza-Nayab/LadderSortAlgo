#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

RAW_PATH="results/raw/13_memory_raw.csv"
HEADER="dataset,n,algo,seed,round,time_sec,max_rss_bytes,k_final,k_over_n,used_fallback,sorted_ok,git_hash"
GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

datasets=(random descending social_feed)
sizes=(10000000)
seeds=(1 2 3)
algorithms=(RawLadderSort HybridLadderSort TimSort StdSort StableSort QuickSort)

g++ -O3 -std=c++20 -march=native bench_memory_phase5.cpp -o bin/bench_memory_phase5

printf '%s\n' "$HEADER" > "$RAW_PATH"

for dataset in "${datasets[@]}"; do
  for n in "${sizes[@]}"; do
    for seed in "${seeds[@]}"; do
      for algo in "${algorithms[@]}"; do
        echo "dataset=$dataset n=$n seed=$seed algo=$algo"

        row_file="$(mktemp)"
        time_file="$(mktemp)"

        /usr/bin/time -l \
          ./bin/bench_memory_phase5 \
            --dataset "$dataset" \
            --algo "$algo" \
            --n "$n" \
            --seed "$seed" \
            --round 1 \
            --git-hash "$GIT_HASH" \
            >"$row_file" 2>"$time_file"

        max_rss_bytes="$(awk '/maximum resident set size/ {print $1}' "$time_file")"
        if [[ -z "$max_rss_bytes" ]]; then
          echo "ERROR: failed to parse maximum resident set size" >&2
          cat "$time_file" >&2
          rm -f "$row_file" "$time_file"
          exit 1
        fi

        if ! [[ "$max_rss_bytes" =~ ^[0-9]+$ ]] || (( max_rss_bytes <= 0 )); then
          echo "ERROR: invalid max RSS: $max_rss_bytes" >&2
          rm -f "$row_file" "$time_file"
          exit 1
        fi

        full_row="$(awk -F, -v OFS=, -v rss="$max_rss_bytes" '{print $1,$2,$3,$4,$5,$6,rss,$7,$8,$9,$10,$11}' "$row_file")"
        sorted_ok="$(printf '%s\n' "$full_row" | awk -F, '{print $11}')"
        if [[ "$sorted_ok" != "1" ]]; then
          echo "ERROR: sorted_ok != 1: $full_row" >&2
          rm -f "$row_file" "$time_file"
          exit 1
        fi

        printf '%s\n' "$full_row" >> "$RAW_PATH"
        rm -f "$row_file" "$time_file"
      done
    done
  done
done

echo "Phase 5 memory run complete: $RAW_PATH"
