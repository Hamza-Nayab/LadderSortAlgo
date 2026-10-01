# LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences

<div align="center">

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![IEEE Access](https://img.shields.io/badge/IEEE%20Access-Peer--Reviewed%20(2026)-orange.svg)](https://ieeeaccess.ieee.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](Makefile)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)]()

**An optimal adaptive sorting algorithm that exploits latent order in interleaved data streams.**

[Key Features](#key-features) •
[Architecture](#algorithmic-architecture) •
[Quick Start](#quick-start) •
[Benchmarks & Results](#experimental-highlights) •
[Reproduction](#reproducing-paper-results) •
[Citation](#citation)

</div>

---

## Overview

Modern data processing pipelines—such as distributed telemetry ingestion, multi-producer social feeds, financial ticker consolidation, and search-index posting list merging—frequently receive streams composed of multiple locally sorted sequences interleaved together.

While classical run-adaptive algorithms like **TimSort** and **PowerSort** achieve near-linear time on inputs composed of contiguous sorted blocks, **interleaving fragments contiguous order**, forcing them to detect $\Theta(N)$ short runs and degrading their performance to $\Theta(N \log N)$.

**LadderSort** is designed specifically to overcome this limitation. It models the presortedness of an input sequence $A$ by $K^*(A)$, the minimum number of nondecreasing subsequences needed to cover $A$. By Dilworth's Theorem, this quantity equals the length of the Longest strictly Decreasing Subsequence, $\mathrm{LDS}(A)$.

LadderSort achieves:
- **Optimal Online Decomposition:** Recovers the offline minimum partition count $K^*(A) = \mathrm{LDS}(A)$ in a single forward pass without any offline preprocessing.
- **Adaptive Time Complexity:** Sorts in $O(N(1 + \log(K + 1)))$ time, matching the information-theoretic lower bound for presortedness measure $K$.
- **Graceful Hybrid Guardrail:** Monitors $K$ online and automatically falls back to introsort/`std::sort` if $K > \lceil\sqrt{N}\rceil$, guaranteeing an $O(N \log N)$ worst-case ceiling.

---

## Key Features

- **Header-Only & Dependency-Free:** Clean, modern C++20 single-header library (`include/laddersort.hpp`).
- **Hinted Galloping Search:** Exploits temporal locality between successive stream arrivals using exponential search with $O(1)$ local probes.
- **$K$-Adaptive Merge Router:**
  - $K = 1$: Already sorted; $O(N)$ trivial return with zero merge allocations.
  - $K = 2$: Specialized two-way galloping merge with exponential jump search ($O(N)$ time).
  - $K > 2$: Cache-conscious multiway **Loser Tree** merge requiring only $\lceil\log_2 K\rceil$ comparisons per extracted item with deterministic winner-path updates.
- **Comprehensive Benchmark Suite:** Full synthetic workload generators, comparison counters, peak memory trackers, and a real-world 55,364-record GitHub Archive event trace.

---

## Algorithmic Architecture

```text
Input Sequence A of length N
             │
             ▼
┌──────────────────────────────────────────────────────────┐
│ Phase 1: Greedy Online Ladder Decomposition              │
│                                                          │
│   For each element x in A:                               │
│     j = HintedLowerBound(tops, x, lastIdx)               │
│     If j <= K:   Append x to Ladder L_j; tops[j] = x     │
│     If j == K+1: Create new Ladder L_{K+1}; tops[K+1] = x│
│                                                          │
│   (Dilworth's Theorem guarantee: K_LS(A) = LDS(A))       │
└────────────────────────────┬─────────────────────────────┘
                             │
            ┌────────────────┴────────────────┐
            │ Check Hybrid Fallback Condition │
            │      Is K > ceil(sqrt(N))?      │
            └────────┬───────────────┬────────┘
                     │               │
               [YES] │               │ [NO]
                     ▼               ▼
        ┌──────────────────┐  ┌───────────────────────────────────┐
        │ Hybrid Fallback  │  │ Phase 2: K-Adaptive Merge Router  │
        │                  │  ├───────────────────────────────────┤
        │ Introsort /      │  │ K = 1: Direct Return              │
        │ std::sort        │  │        O(N) time, 0 comparisons   │
        │                  │  │                                   │
        │ O(N log N) worst │  │ K = 2: Galloping Two-Way Merge    │
        │ case ceiling     │  │        O(N) time, exponential skip│
        └──────────────────┘  │                                   │
                              │ K > 2: Multiway Loser Tree Merge  │
                              │        O(N log K) time            │
                              │        ceil(log2 K) comps/extract │
                              └─────────────────┬─────────────────┘
                                                │
                                                ▼
                                      Sorted Output Array S
```

---

## Algorithm Comparison

| Algorithm | Presortedness Measure | Interleaved Streams ($K \ll N$) | Contiguous Runs | Worst-Case Time | Aux. Memory |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **LadderSort (Raw)** | $\mathrm{LDS}(A) = K$ | **$O(N \log K)$** | $O(N)$ | $O(N K)$ | $O(N)$ |
| **LadderSort (Hybrid)** | $\mathrm{LDS}(A) = K$ | **$O(N \log K)$** | $O(N)$ | **$O(N \log N)$** | $O(N)$ |
| **TimSort** [Auger et al.] | $\mathrm{Runs}(A)$ | $O(N \log N)$ | $O(N)$ | $O(N \log N)$ | $O(N)$ |
| **PowerSort** [Munro & Wild] | $\mathrm{Runs}(A)$ | $O(N \log N)$ | $O(N)$ | $O(N \log N)$ | $O(N)$ |
| **PatienceSort** [Aldous & Diaconis] | $\mathrm{LDS}(A)$ | Measures $K$ offline | $O(N \log N)$ | $O(N \log N)$ | $O(N)$ |
| **EncroachingLists** [Skiena] | Heuristic | $O(N K)$ (unhinted) | $O(N)$ | $O(N^2)$ | $O(N)$ |
| **std::sort** (Introsort) | None | $O(N \log N)$ | $O(N \log N)$ | $O(N \log N)$ | $O(\log N)$ |

---

## Quick Start

### 1. Integration (Header-Only)
Simply include [`include/laddersort.hpp`](include/laddersort.hpp) in your project:

```cpp
#include <iostream>
#include <vector>
#include "laddersort.hpp"

int main() {
    // Interleaved nondecreasing sequences (K = 2 interleaved runs)
    std::vector<int> data = {1, 10, 2, 20, 3, 30, 4, 40, 5, 50};

    // Standard LadderSort:
    laddersort::sort(data);

    // Or with automatic hybrid guardrail (falls back to std::sort if K > ceil(sqrt(N))):
    // laddersort::hybrid_sort(data);

    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";
    return 0;
}
```

### 2. Compilation
Compile with any C++20 compliant compiler:
```bash
g++ -O3 -std=c++20 -Iinclude main.cpp -o main
./main
```

---

## Experimental Highlights

*Benchmarked on Apple M1 (8 cores, 8 GB unified memory), single-threaded, seeds 1–10.*

### 1. Interleaved Workloads ($N = 10^8$ integers)
On inputs with low to moderate $K$, LadderSort demonstrates significant speedups over state-of-the-art production sorters:

| Dataset | Characteristic | Measured $K$ | TimSort | std::sort | **LadderSort (Raw)** | **Speedup vs std::sort** | **Speedup vs TimSort** |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Two-Run Riffle** | 2 interleaved runs | $K = 2$ | 1.572 s | 4.894 s | **0.724 s** | **6.76×** | **2.17×** |
| **Block-Cyclic** | 8 producers, block $B=4$ | $K = 8$ | 1.045 s | 4.965 s | **0.834 s** | **5.96×** | **1.25×** |
| **Partial Index** | 16 posting lists | $K = 16$ | 1.527 s | 2.984 s | **1.345 s** | **2.22×** | **1.14×** |
| **Social Feed** | 24 bursty producers | $K = 24$ | 1.545 s | 1.936 s | **1.260 s** | **1.54×** | **1.23×** |
| **Band-Limited** | Local jitter ($W=32$) | $K \approx 18$ | 2.503 s | 2.260 s | **2.084 s** | **1.08×** | **1.20×** |

### 2. Real-World GitHub Archive Trace
Sorting 55,364 public GitHub event records across 12,783 repository sources ($K = 368$):
- **Raw LadderSort:** **0.00203 s** (**1.45× speedup vs TimSort**)
- **Hybrid LadderSort:** **0.00160 s** (**1.84× speedup vs TimSort**)
- **TimSort:** 0.00294 s
- **std::sort:** 0.00100 s

---

## Repository Structure

```text
.
├── include/
│   ├── laddersort.hpp            # Self-contained header-only C++20 LadderSort library
│   └── bench_csv.hpp             # High-resolution benchmark CSV logging utilities
├── third_party/
│   └── cpp-TimSort/              # C++ TimSort reference baseline (commit 131cf68)
├── data/
│   ├── processed/                # Preprocessed real-world GH Archive event trace (55,364 rows)
│   └── raw/                      # Downloaded gzip-compressed GH Archive records
├── results/
│   ├── raw/                      # Raw per-run benchmark logs across 10 deterministic seeds
│   ├── summary/                  # Computed statistics (mean, median, std, speedups)
│   ├── plots/                    # High-resolution publication figures (PDF / SVG)
│   ├── metadata.txt              # Hardware environment specifications
│   ├── seeds.txt                 # Deterministic seed manifest (1–10)
│   └── benchmark_manifest.txt    # Complete dataset manifest
├── run/
│   ├── compile_all.sh            # Automated compilation for all benchmark binaries
│   ├── run_phase2_main_runtime.sh# Phase 2 runtime benchmark suite
│   ├── run_phase3_k_sweep.sh     # Phase 3 K-sweep benchmark across K in [1, N]
│   ├── run_phase3_k_stats.sh     # Phase 3 K-distribution measurements
│   ├── run_phase4_ablation_*.sh  # Phase 4 ablation study scripts (hints, gallop, loser tree)
│   ├── run_phase4_hybrid.sh      # Phase 4 hybrid threshold sensitivity benchmark
│   ├── run_phase5_comparisons.sh # Phase 5 exact key comparison counters
│   ├── run_phase5_memory.sh      # Phase 5 memory footprint and peak RSS tracking
│   ├── run_phase6_real_trace.sh  # Phase 6 real-world GitHub trace benchmark
│   ├── smoke_test.sh             # Fast integrity smoke test (< 1 second)
│   └── summarize_*.py            # Statistical processing and CSV aggregation scripts
├── bench_*.cpp                   # Phase-specific benchmark C++ implementations
├── Makefile                      # Standard build automation (build, test, clean)
├── LICENSE                       # MIT Open Source License
└── README.md                     # Documentation and reproduction guide
```

---

## Building & Smoke Testing

### Build All Benchmarks
To build all 11 benchmark executables into the `bin/` directory:

```bash
make -j4
```
*(The build system automatically detects available C++20 compilers with fallback support for `clang++` and `g++`).*

### Run Fast Smoke Test
Verify algorithmic correctness and environment integrity in under 1 second:

```bash
make smoke
```

### Clean
```bash
make clean
```

---

## Reproducing Paper Results

All benchmarks run deterministically using seeds `1` through `10`.

```bash
# 1. Main Runtime Performance (Tables 1 & 2 in manuscript)
./run/run_phase2_main_runtime.sh
python3 run/summarize_phase2.py

# 2. K-Sensitivity & K-Sweep (Table 3 & Figures in manuscript)
./run/run_phase3_k_sweep.sh
./run/run_phase3_k_stats.sh
python3 run/summarize_phase3.py

# 3. Component Ablation Studies (Table 4 in manuscript)
# Tests NoHint, NoGallop, HeapMerge, and StableMode:
./run/run_phase4_ablation_riffle.sh
./run/run_phase4_ablation_social.sh
python3 run/summarize_phase4.py

# 4. Comparison Counts & Peak Memory RSS (Tables 5 & 6 in manuscript)
./run/run_phase5_comparisons.sh
./run/run_phase5_memory.sh
python3 run/summarize_phase5.py

# 5. Real-World GitHub Archive Trace (Table 7 in manuscript)
./run/run_phase6_real_trace.sh
python3 run/summarize_phase6.py
```

---

## Citation

If you use LadderSort, its implementation, or benchmark suites in your research, please cite our IEEE Access paper:

```bibtex
@article{nayab2026laddersort,
  author    = {Nayab, Malik M. H. and Rais, Rao Naveed Bin and Khalid, Osman and Khan, Imran A.},
  title     = {LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences},
  journal   = {IEEE Access},
  volume    = {XX},
  pages     = {XXXX--XXXX},
  year      = {2026},
  publisher = {IEEE},
  doi       = {10.1109/ACCESS.2026.XXXXXXX}
}
```

---

## License

This project is licensed under the [MIT License](LICENSE).  
The TimSort baseline in [`third_party/cpp-TimSort`](third_party/cpp-TimSort) is licensed under the MIT License by Fuji, Goro and Morwenn.
