#!/usr/bin/env python3
"""Summarize Phase 5 memory and comparison-overhead experiments."""

from pathlib import Path
import math

import pandas as pd


MEMORY_RAW = Path("results/raw/13_memory_raw.csv")
MEMORY_SUMMARY = Path("results/summary/14_memory_summary.csv")
COMPARISONS_RAW = Path("results/raw/15_comparisons_raw.csv")
COMPARISONS_SUMMARY = Path("results/summary/16_comparisons_summary.csv")

MEMORY_SUMMARY_COLUMNS = [
    "dataset",
    "n",
    "algo",
    "count",
    "mean_sec",
    "median_sec",
    "mean_max_rss_bytes",
    "median_max_rss_bytes",
    "min_max_rss_bytes",
    "max_max_rss_bytes",
    "mean_k",
    "median_k",
    "fallback_rate",
    "sorted_ok",
]

COMPARISONS_SUMMARY_COLUMNS = [
    "dataset",
    "n",
    "algo",
    "count",
    "mean_sec",
    "median_sec",
    "mean_comparisons",
    "median_comparisons",
    "comparisons_per_n",
    "comparisons_per_n_log2_n",
    "mean_hint_comparisons",
    "mean_merge_comparisons",
    "mean_k",
    "median_k",
    "fallback_rate",
    "sorted_ok",
]


def normalize_boolish(series: pd.Series) -> pd.Series:
    if pd.api.types.is_bool_dtype(series):
        return series.astype(int)
    if pd.api.types.is_numeric_dtype(series):
        return series.astype(int)
    return (
        series.astype(str)
        .str.strip()
        .str.lower()
        .map({"1": 1, "true": 1, "yes": 1, "0": 0, "false": 0, "no": 0})
        .fillna(0)
        .astype(int)
    )


def require_input(path: Path) -> bool:
    if not path.exists():
        print(f"missing input: {path}")
        return False
    if path.stat().st_size == 0:
        print(f"empty input: {path}")
        return False
    return True


def summarize_memory() -> pd.DataFrame | None:
    if not require_input(MEMORY_RAW):
        return None

    df = pd.read_csv(MEMORY_RAW)
    df["sorted_ok"] = normalize_boolish(df["sorted_ok"])
    df["used_fallback"] = normalize_boolish(df["used_fallback"])

    summary = (
        df.groupby(["dataset", "n", "algo"], as_index=False)
        .agg(
            count=("time_sec", "count"),
            mean_sec=("time_sec", "mean"),
            median_sec=("time_sec", "median"),
            mean_max_rss_bytes=("max_rss_bytes", "mean"),
            median_max_rss_bytes=("max_rss_bytes", "median"),
            min_max_rss_bytes=("max_rss_bytes", "min"),
            max_max_rss_bytes=("max_rss_bytes", "max"),
            mean_k=("k_final", "mean"),
            median_k=("k_final", "median"),
            fallback_rate=("used_fallback", "mean"),
            sorted_ok=("sorted_ok", "min"),
        )
    )

    summary = summary[MEMORY_SUMMARY_COLUMNS].sort_values(["dataset", "n", "algo"])
    MEMORY_SUMMARY.parent.mkdir(parents=True, exist_ok=True)
    summary.to_csv(MEMORY_SUMMARY, index=False)
    return summary


def summarize_comparisons() -> pd.DataFrame | None:
    if not require_input(COMPARISONS_RAW):
        return None

    df = pd.read_csv(COMPARISONS_RAW)
    df["sorted_ok"] = normalize_boolish(df["sorted_ok"])
    df["used_fallback"] = normalize_boolish(df["used_fallback"])

    summary = (
        df.groupby(["dataset", "n", "algo"], as_index=False)
        .agg(
            count=("time_sec", "count"),
            mean_sec=("time_sec", "mean"),
            median_sec=("time_sec", "median"),
            mean_comparisons=("total_comparisons", "mean"),
            median_comparisons=("total_comparisons", "median"),
            mean_hint_comparisons=("hint_comparisons", "mean"),
            mean_merge_comparisons=("merge_comparisons", "mean"),
            mean_k=("k_final", "mean"),
            median_k=("k_final", "median"),
            fallback_rate=("used_fallback", "mean"),
            sorted_ok=("sorted_ok", "min"),
        )
    )

    summary["comparisons_per_n"] = summary["median_comparisons"] / summary["n"]
    summary["comparisons_per_n_log2_n"] = summary.apply(
        lambda row: row["median_comparisons"] / (row["n"] * math.log2(row["n"])),
        axis=1,
    )

    summary = summary[COMPARISONS_SUMMARY_COLUMNS].sort_values(
        ["dataset", "n", "algo"]
    )
    COMPARISONS_SUMMARY.parent.mkdir(parents=True, exist_ok=True)
    summary.to_csv(COMPARISONS_SUMMARY, index=False)
    return summary


def report(name: str, path: Path, summary: pd.DataFrame | None) -> bool:
    if summary is None:
        print(f"{name}: skipped")
        return False

    sorted_failures = int((summary["sorted_ok"] != 1).sum())
    nan_count = int(summary.isna().sum().sum())
    print(f"{name}: wrote {len(summary)} rows to {path}")
    print(f"{name}: sorted failures = {sorted_failures}")
    print(f"{name}: NaN values = {nan_count}")
    return sorted_failures == 0 and nan_count == 0


def main() -> None:
    ok = True
    memory = summarize_memory()
    ok = report("memory", MEMORY_SUMMARY, memory) and ok

    comparisons = summarize_comparisons()
    ok = report("comparisons", COMPARISONS_SUMMARY, comparisons) and ok

    print()
    print("FINAL:", "PASS" if ok else "FAIL")


if __name__ == "__main__":
    main()
