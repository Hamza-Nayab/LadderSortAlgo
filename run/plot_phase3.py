from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt

Path("results/plots").mkdir(parents=True, exist_ok=True)

# Prefer filtered summary if available (robustness against system-noise outliers)
summary_path = Path("results/summary/06_k_sweep_summary_filtered.csv")
if not summary_path.exists():
    summary_path = Path("results/summary/06_k_sweep_summary.csv")

df = pd.read_csv(summary_path)

n = df["n"].max()
df = df[df["n"] == n]

# ---------- B&W-safe marker assignments ----------
MARKER_MAP = {
    "LadderSort":       "s",   # square
    "HybridLadderSort": "D",   # diamond
    "TimSort":          "^",   # triangle up
    "StdSort":          "o",   # circle
    "StableSort":       "x",   # cross
}

plt.figure()

for algo in ["LadderSort", "HybridLadderSort", "TimSort", "StdSort", "StableSort"]:
    sub = df[df["algo"] == algo].sort_values("measured_k_mean")
    if sub.empty:
        continue

    plt.plot(
        sub["measured_k_mean"],
        sub["median_sec"],
        marker=MARKER_MAP.get(algo, "o"),
        markersize=6,
        markevery=1,
        label=algo,
    )

plt.xscale("log", base=2)
plt.xlabel("Measured ladder count K")
plt.ylabel("Median runtime (seconds)")
plt.title(f"Runtime vs measured K, randomized exact-K interleaving, N={n}")
plt.legend()
plt.tight_layout()
plt.savefig("results/plots/k_sweep_runtime_vs_k.pdf")
plt.savefig("results/plots/k_sweep_runtime_vs_k.svg")
print("Wrote runtime-vs-K plot")

plt.figure()

for algo in ["LadderSort", "HybridLadderSort", "StdSort", "StableSort"]:
    sub = df[df["algo"] == algo].sort_values("measured_k_mean")
    if sub.empty:
        continue

    plt.plot(
        sub["measured_k_mean"],
        sub["speedup_vs_timsort_median"],
        marker=MARKER_MAP.get(algo, "o"),
        markersize=6,
        markevery=1,
        label=algo,
    )

plt.axhline(1.0, linestyle="--")
plt.xscale("log", base=2)
plt.xlabel("Measured ladder count K")
plt.ylabel("Speedup vs TimSort")
plt.title(f"Speedup vs TimSort, randomized exact-K interleaving, N={n}")
plt.legend()
plt.tight_layout()
plt.savefig("results/plots/k_sweep_speedup_vs_k.pdf")
plt.savefig("results/plots/k_sweep_speedup_vs_k.svg")
print("Wrote speedup-vs-K plot")