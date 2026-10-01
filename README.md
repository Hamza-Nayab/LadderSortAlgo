# LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![IEEE Access](https://img.shields.io/badge/IEEE%20Access-2026-orange.svg)](https://ieeeaccess.ieee.org/)

Official source code, benchmark suites, and raw experimental reproduction logs for the paper:
> **LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences**  
> *IEEE Access* (2026).

---

## Overview

**LadderSort** is an adaptive sorting algorithm designed specifically for data streams composed of interleaved sorted runs. While classical adaptive sorters (such as TimSort and PowerSort) excel when inputs consist of contiguous sorted blocks, their run-detection mechanisms fragment when multiple sorted sources are interleaved, degrading to $O(N \log N)$ complexity.

LadderSort overcomes this limitation:
1. **Optimal Greedy Online Partitioning:** Partitions the input of size $N$ into $K$ interleaved nondecreasing subsequences (*ladders*) using a hinted exponential search on ladder tops. By Dilworth's Theorem, this greedy placement rule strictly attains the minimum possible cover count $K^*(A) = \mathrm{LDS}(A)$ online without offline preprocessing.
2. **$K$-Adaptive Merge Routing:**
   - **$K = 1$:** The input is already nondecreasing; returns in $O(N)$ time with zero merge overhead.
   - **$K = 2$:** Routes to a specialized two-way galloping merge with exponential skip search ($O(N)$ time).
   - **$K > 2$:** Routes to an in-place multiway **Loser Tree** merge, requiring only $\lceil \log_2 K \rceil$ comparisons per extracted element with a deterministic single-path winner update.
3. **Hybrid Fallback Guarantee:** An optional hybrid variant monitors $K$ dynamically during the online decomposition. If $K$ exceeds an adaptive threshold $K_{\text{thresh}} = \lceil\sqrt{N}\rceil$, it falls back to introsort/`std::sort`, guaranteeing $O(N \log N)$ worst-case time complexity even on adversarially constructed reverse-sorted sequences.

---

## Header-Only C++ Library Usage

LadderSort is provided as a self-contained, header-only C++20 library in [`include/laddersort.hpp`](include/laddersort.hpp). It requires no external dependencies.

```cpp
#include <iostream>
#include <vector>
#include "laddersort.hpp"

int main() {
    std::vector<int> data = {1, 10, 2, 11, 3, 12, 4, 13}; // Interleaved runs

    // Standard LadderSort:
    laddersort::sort(data);

    // Or with automatic hybrid fallback (worst-case O(N log N)):
    // laddersort::hybrid_sort(data);

    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";
    return 0;
}
```

---

## Repository Structure

```text
.
├── include/
│   ├── laddersort.hpp            # Standalone header-only LadderSort implementation
│   └── bench_csv.hpp             # Benchmark CSV logging utilities
├── third_party/
│   └── cpp-TimSort/              # Fuji/Morwenn C++ TimSort baseline (commit 131cf68)
├── data/
│   ├── processed/                # Preprocessed real-world GitHub Archive event traces
│   └── raw/                      # Downloaded gzip-compressed GH Archive event records
├── results/
│   ├── raw/                      # Raw per-run benchmark logs (seeds, runtimes, comparisons)
│   ├── summary/                  # Aggregated statistical summaries (means, medians, std)
│   ├── plots/                    # Generated high-resolution reproduction figures
│   ├── metadata.txt              # Execution environment and hardware specifications
│   ├── seeds.txt                 # Deterministic pseudo-random generator seeds (1–10)
│   └── benchmark_manifest.txt    # Complete dataset manifest
├── run/
│   ├── compile_all.sh            # Automated compilation script for all benchmarks
│   ├── run_phase2_main_runtime.sh# Main runtime benchmark suite across all synthetic generators
│   ├── run_phase3_k_sweep.sh     # K-sweep benchmarks varying target K from 1 to N
│   ├── run_phase3_k_stats.sh     # K-distribution measurements across datasets
│   ├── run_phase4_ablation_riffle.sh # Ablation studies on two-run riffle data
│   ├── run_phase4_ablation_social.sh # Ablation studies on social-feed data
│   ├── run_phase4_hybrid.sh      # Hybrid threshold sensitivity benchmark
│   ├── run_phase5_comparisons.sh # Hardware comparison counter benchmarks
│   ├── run_phase5_memory.sh      # Memory usage and peak RSS tracking
│   ├── run_phase6_real_trace.sh  # Real-world GH Archive workload execution
│   └── summarize_*.py            # Statistical aggregation scripts
├── bench_*.cpp                   # Phase-specific C++ benchmark executables
├── Makefile                      # Portable build automation
├── LICENSE                       # MIT Open Source License
└── README.md                     # Documentation and reproduction guide
```

---

## Building the Benchmarks

### Prerequisites
- **C++ Compiler:** A C++20 compliant compiler (`g++` >= 11 or `clang++` >= 13).
- **Make:** GNU Make or compatible.
- **Python 3:** (Optional, for aggregating summaries and plotting: `pandas`, `matplotlib`, `numpy`).

### Quick Build
To compile all benchmark executables into the `bin/` directory:

```bash
make -j4
```

Alternatively, use the shell compilation script:
```bash
./run/compile_all.sh
```

To clean compiled binaries:
```bash
make clean
```

---

## Reproducing Paper Results

All synthetic benchmarks execute across 10 deterministic seed repetitions (seeds 1–10).

### 1. Main Runtime Performance (Tables 1 & 2)
Evaluates Raw LadderSort, Hybrid LadderSort, TimSort, std::sort, std::stable_sort, and QuickSort across sizes $N \in [10^6, 10^8]$:
```bash
./run/run_phase2_main_runtime.sh
python3 run/summarize_phase2.py
```

### 2. $K$-Sensitivity and $K$-Sweep (Table 3)
Measures runtime and decomposition count $K$ across varying interleaving degrees:
```bash
./run/run_phase3_k_sweep.sh
./run/run_phase3_k_stats.sh
python3 run/summarize_phase3.py
```

### 3. Component Ablation Studies (Table 4)
Isolates the performance impact of (i) hinted search vs. unhinted search, (ii) galloping merge vs. standard merge, (iii) Loser Tree vs. binary heap, and (iv) stable tie-breaking:
```bash
./run/run_phase4_ablation_riffle.sh
./run/run_phase4_ablation_social.sh
python3 run/summarize_phase4.py
```

### 4. Comparison Counts and Memory Overhead (Tables 5 & 6)
Tracks exact key comparison counts and peak resident set size (RSS):
```bash
./run/run_phase5_comparisons.sh
./run/run_phase5_memory.sh
python3 run/summarize_phase5.py
```

### 5. Real-World GitHub Archive Trace (Table 7)
Evaluates sorting latency on 55,364 public GitHub event records:
```bash
./run/run_phase6_real_trace.sh
python3 run/summarize_phase6.py
```

---

## Hardware Environment

As documented in [`results/metadata.txt`](results/metadata.txt), baseline paper benchmarks were performed on:
- **Processor:** Apple M1 (8 cores: 4 performance + 4 efficiency)
- **Memory:** 8 GB unified memory
- **Operating System:** macOS 15.6.1 (Darwin 24.6.0)
- **Compiler Flags:** `-O3 -march=native -std=c++20`
- **Execution Policy:** Single-threaded, deterministic seeds, no manual SIMD.

---

## Citation

If you use LadderSort or this benchmark repository in your research, please cite our IEEE Access paper:

```bibtex
@article{nayab2026laddersort,
  author    = {Nayab, Hamza and Authors, Co},
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
The TimSort baseline in `third_party/cpp-TimSort` is licensed under the MIT License by Fuji, Goro and Morwenn.
