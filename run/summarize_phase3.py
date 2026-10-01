#!/usr/bin/env python3

import pandas as pd
from pathlib import Path

Path("results/summary").mkdir(parents=True, exist_ok=True)

kstats_path = Path("results/raw/03_k_stats_raw.csv")
sweep_path = Path("results/raw/05_k_sweep_raw.csv")

if kstats_path.exists():
    df = pd.read_csv(kstats_path)

    summary = (
        df.groupby(["dataset", "n"])
          .agg(
              count=("k_final", "count"),
              mean_k=("k_final", "mean"),
              median_k=("k_final", "median"),
              min_k=("k_final", "min"),
              max_k=("k_final", "max"),
              mean_k_over_n=("k_over_n", "mean"),
          )
          .reset_index()
    )

    summary.to_csv("results/summary/04_k_stats_summary.csv", index=False)
    print("Wrote results/summary/04_k_stats_summary.csv")
    print(summary)
else:
    print("Skipping K stats summary: results/raw/03_k_stats_raw.csv not found")

if sweep_path.exists():
    df = pd.read_csv(sweep_path)

    # Build unfiltered summary with extended statistics
    summary = (
        df.groupby(["n", "target_k", "algo", "variant"])
          .agg(
              measured_k_mean=("measured_k", "mean"),
              measured_k_min=("measured_k", "min"),
              measured_k_max=("measured_k", "max"),
              mean_sec=("time_sec", "mean"),
              median_sec=("time_sec", "median"),
              std_sec=("time_sec", "std"),
              min_sec=("time_sec", "min"),
              max_sec=("time_sec", "max"),
              p25_sec=("time_sec", lambda x: x.quantile(0.25)),
              p75_sec=("time_sec", lambda x: x.quantile(0.75)),
              count=("time_sec", "count"),
              sorted_ok=("sorted_ok", "min"),
              fallback_rate=("used_fallback", "mean"),
          )
          .reset_index()
    )

    timsort = (
        summary[summary["algo"] == "TimSort"]
        [["n", "target_k", "median_sec"]]
        .rename(columns={"median_sec": "timsort_median_sec"})
    )

    summary = summary.merge(timsort, on=["n", "target_k"], how="left")
    summary["speedup_vs_timsort_median"] = summary["timsort_median_sec"] / summary["median_sec"]

    summary.to_csv("results/summary/06_k_sweep_summary.csv", index=False)
    print("Wrote results/summary/06_k_sweep_summary.csv")
    print(summary)
    
    # Build filtered summary (outlier removal for controlled sweep)
    df_filtered = df[df["time_sec"] <= 10.0].copy()
    
    removed_count = len(df) - len(df_filtered)
    if removed_count > 0:
        print(f"\n⚠️  WARNING: Removed {removed_count} outlier rows (time_sec > 10.0s)")
        removed_rows = df[df["time_sec"] > 10.0]
        print("Removed rows:")
        print(removed_rows[["n", "target_k", "algo", "round", "time_sec"]].to_string(index=False))
    
    summary_filtered = (
        df_filtered.groupby(["n", "target_k", "algo", "variant"])
          .agg(
              measured_k_mean=("measured_k", "mean"),
              measured_k_min=("measured_k", "min"),
              measured_k_max=("measured_k", "max"),
              mean_sec=("time_sec", "mean"),
              median_sec=("time_sec", "median"),
              std_sec=("time_sec", "std"),
              min_sec=("time_sec", "min"),
              max_sec=("time_sec", "max"),
              p25_sec=("time_sec", lambda x: x.quantile(0.25)),
              p75_sec=("time_sec", lambda x: x.quantile(0.75)),
              count=("time_sec", "count"),
              sorted_ok=("sorted_ok", "min"),
              fallback_rate=("used_fallback", "mean"),
          )
          .reset_index()
    )

    timsort_filtered = (
        summary_filtered[summary_filtered["algo"] == "TimSort"]
        [["n", "target_k", "median_sec"]]
        .rename(columns={"median_sec": "timsort_median_sec"})
    )

    summary_filtered = summary_filtered.merge(timsort_filtered, on=["n", "target_k"], how="left")
    summary_filtered["speedup_vs_timsort_median"] = summary_filtered["timsort_median_sec"] / summary_filtered["median_sec"]

    summary_filtered.to_csv("results/summary/06_k_sweep_summary_filtered.csv", index=False)
    print("\nWrote results/summary/06_k_sweep_summary_filtered.csv")
    print(summary_filtered)
else:
    print("Skipping K sweep summary: results/raw/05_k_sweep_raw.csv not found")
