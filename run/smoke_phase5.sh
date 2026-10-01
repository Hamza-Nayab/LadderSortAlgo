#!/usr/bin/env bash
set -euo pipefail

mkdir -p bin results/raw results/summary

GIT_HASH="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT="results/raw/smoke_phase5_memory.csv"
HEADER="dataset,n,algo,seed,round,time_sec,max_rss_bytes,k_final,k_over_n,used_fallback,sorted_ok,git_hash"

g++ -O3 -std=c++20 -march=native bench_memory_phase5.cpp -o bin/bench_memory_phase5

printf '%s\n' "$HEADER" > "$OUT"

for algo in RawLadderSort HybridLadderSort TimSort StdSort StableSort; do
  row_file="$(mktemp)"
  time_file="$(mktemp)"

  /usr/bin/time -l \
    ./bin/bench_memory_phase5 \
      --dataset random \
      --algo "$algo" \
      --n 10000 \
      --seed 1 \
      --round 1 \
      --git-hash "$GIT_HASH" \
      >"$row_file" 2>"$time_file"

  max_rss_bytes="$(awk '/maximum resident set size/ {print $1}' "$time_file")"
  if [[ -z "$max_rss_bytes" ]]; then
    echo "ERROR: failed to parse maximum resident set size for $algo" >&2
    cat "$time_file" >&2
    rm -f "$row_file" "$time_file"
    exit 1
  fi

  if ! [[ "$max_rss_bytes" =~ ^[0-9]+$ ]] || (( max_rss_bytes <= 0 )); then
    echo "ERROR: invalid max RSS for $algo: $max_rss_bytes" >&2
    rm -f "$row_file" "$time_file"
    exit 1
  fi

  row="$(tr -d '\r\n' < "$row_file")"
  if [[ -z "$row" ]]; then
    echo "ERROR: missing stdout row for $algo" >&2
    rm -f "$row_file" "$time_file"
    exit 1
  fi

  full_row="$(awk -F, -v OFS=, -v rss="$max_rss_bytes" '{print $1,$2,$3,$4,$5,$6,rss,$7,$8,$9,$10,$11}' "$row_file")"
  sorted_ok="$(printf '%s\n' "$full_row" | awk -F, '{print $11}')"
  if [[ "$sorted_ok" != "1" ]]; then
    echo "ERROR: sorted_ok != 1 for $algo: $full_row" >&2
    rm -f "$row_file" "$time_file"
    exit 1
  fi

  printf '%s\n' "$full_row" >> "$OUT"
  rm -f "$row_file" "$time_file"
done

echo "Phase 5 memory smoke test complete."

COMPARISONS_OUT="results/raw/smoke_phase5_comparisons.csv"
COMPARISONS_HEADER="dataset,n,algo,seed,round,time_sec,total_comparisons,hint_comparisons,merge_comparisons,k_final,k_over_n,used_fallback,sorted_ok,git_hash"

g++ -O3 -std=c++20 -march=native bench_comparisons_phase5.cpp -o bin/bench_comparisons_phase5

printf '%s\n' "$COMPARISONS_HEADER" > "$COMPARISONS_OUT"

for dataset in random descending block_cyclic two_run_riffle social_feed; do
  for algo in RawLadderSort HybridLadderSort TimSort StdSort StableSort; do
    row="$(
      ./bin/bench_comparisons_phase5 \
        --dataset "$dataset" \
        --algo "$algo" \
        --n 10000 \
        --seed 1 \
        --round 1 \
        --git-hash "$GIT_HASH"
    )"

    if [[ -z "$row" ]]; then
      echo "ERROR: missing comparison stdout row for $dataset/$algo" >&2
      exit 1
    fi

    sorted_ok="$(printf '%s\n' "$row" | awk -F, '{print $13}')"
    if [[ "$sorted_ok" != "1" ]]; then
      echo "ERROR: comparison sorted_ok != 1 for $dataset/$algo: $row" >&2
      exit 1
    fi

    total_comparisons="$(printf '%s\n' "$row" | awk -F, '{print $7}')"
    if ! [[ "$total_comparisons" =~ ^[0-9]+$ ]] || (( total_comparisons <= 0 )); then
      echo "ERROR: invalid total comparisons for $dataset/$algo: $row" >&2
      exit 1
    fi

    printf '%s\n' "$row" >> "$COMPARISONS_OUT"
  done
done

echo "Phase 5 comparison-count smoke test complete."
