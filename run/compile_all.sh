#!/usr/bin/env bash
set -euo pipefail

# Portable C++20 compiler detection
if [[ -z "${CXX:-}" ]]; then
  for candidate in clang++ g++ c++ /opt/homebrew/bin/g++-15; do
    if command -v "$candidate" >/dev/null 2>&1; then
      if echo "int main(){}" | "$candidate" -std=c++20 -x c++ - -o /dev/null 2>/dev/null; then
        CXX="$candidate"
        break
      fi
    fi
  done
  CXX="${CXX:-g++}"
fi

CXXFLAGS="-O3 -march=native -std=c++20"
INCLUDES="-Ithird_party/cpp-TimSort/include -Iinclude"

echo "Using compiler: $CXX"
mkdir -p bin

echo "Compiling core benchmark executables..."
$CXX $CXXFLAGS $INCLUDES bench_laddersort_ablation.cpp -o bin/bench_laddersort_ablation
$CXX $CXXFLAGS $INCLUDES bench_postinsert_multi.cpp -o bin/bench_postinsert_multi
$CXX $CXXFLAGS $INCLUDES bench_main_runtime.cpp -o bin/bench_main_runtime
$CXX $CXXFLAGS $INCLUDES bench_k_smoke.cpp -o bin/bench_k_smoke
$CXX $CXXFLAGS $INCLUDES bench_k_stats.cpp -o bin/bench_k_stats
$CXX $CXXFLAGS $INCLUDES bench_k_sweep.cpp -o bin/bench_k_sweep
$CXX $CXXFLAGS $INCLUDES bench_ablation_phase4.cpp -o bin/bench_ablation_phase4
$CXX $CXXFLAGS $INCLUDES bench_comparisons_phase5.cpp -o bin/bench_comparisons_phase5
$CXX $CXXFLAGS $INCLUDES bench_hybrid_phase4.cpp -o bin/bench_hybrid_phase4
$CXX $CXXFLAGS $INCLUDES bench_memory_phase5.cpp -o bin/bench_memory_phase5
$CXX $CXXFLAGS $INCLUDES bench_real_trace_phase6.cpp -o bin/bench_real_trace_phase6

echo "All benchmark binaries successfully built in bin/."