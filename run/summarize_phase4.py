#!/usr/bin/env python3
"""Summarize Phase 4 ablation and hybrid experiments."""

from pathlib import Path

import pandas as pd


ABLATION_SOCIAL_RAW = Path("results/raw/07_ablation_social_raw.csv")
ABLATION_SOCIAL_SUMMARY = Path("results/summary/08_ablation_social_summary.csv")
ABLATION_RIFFLE_RAW = Path("results/raw/09_ablation_riffle_raw.csv")
ABLATION_RIFFLE_SUMMARY = Path("results/summary/10_ablation_riffle_summary.csv")
HYBRID_RAW = Path("results/raw/11_hybrid_raw.csv")
HYBRID_SUMMARY = Path("results/summary/12_hybrid_summary.csv")

ABLATION_SUMMARY_COLUMNS = [
    "dataset",
    "n",
    "variant",
    "count",
    "mean_sec",
    "median_sec",
    "std_sec",
    "min_sec",
    "max_sec",
    "mean_k",
    "median_k",
    "relative_time_mean",
    "relative_time_median",
    "sorted_ok",
]

HYBRID_SUMMARY_COLUMNS = [
    "dataset",
    "n",
    "algo",
    "count",
    "mean_sec",
    "median_sec",
    "std_sec",
    "min_sec",
    "max_sec",
    "mean_k",
    "median_k",
    "fallback_rate",
    "speedup_vs_raw_ladder_median",
    "speedup_vs_timsort_median",
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


def summarize_ablation(raw_path: Path, out_path: Path) -> pd.DataFrame:
    df = pd.read_csv(raw_path)
    df["sorted_ok"] = normalize_boolish(df["sorted_ok"])

    summary = (
        df.groupby(["dataset", "n", "variant"], as_index=False)
        .agg(
            count=("time_sec", "count"),
            mean_sec=("time_sec", "mean"),
            median_sec=("time_sec", "median"),
            std_sec=("time_sec", "std"),
            min_sec=("time_sec", "min"),
            max_sec=("time_sec", "max"),
            mean_k=("k_final", "mean"),
            median_k=("k_final", "median"),
            sorted_ok=("sorted_ok", "min"),
        )
    )

    ladder = (
        summary[summary["variant"] == "LadderFull"]
        [["dataset", "n", "mean_sec", "median_sec"]]
        .rename(
            columns={
                "mean_sec": "ladder_full_mean_sec",
                "median_sec": "ladder_full_median_sec",
            }
        )
    )

    summary = summary.merge(ladder, on=["dataset", "n"], how="left")
    summary["relative_time_mean"] = (
        summary["mean_sec"] / summary["ladder_full_mean_sec"]
    )
    summary["relative_time_median"] = (
        summary["median_sec"] / summary["ladder_full_median_sec"]
    )

    summary = summary[ABLATION_SUMMARY_COLUMNS].sort_values(
        ["dataset", "n", "variant"]
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    summary.to_csv(out_path, index=False)
    return summary


def summarize_hybrid(raw_path: Path, out_path: Path) -> pd.DataFrame:
    df = pd.read_csv(raw_path)
    df["sorted_ok"] = normalize_boolish(df["sorted_ok"])
    df["used_fallback"] = normalize_boolish(df["used_fallback"])

    summary = (
        df.groupby(["dataset", "n", "algo"], as_index=False)
        .agg(
            count=("time_sec", "count"),
            mean_sec=("time_sec", "mean"),
            median_sec=("time_sec", "median"),
            std_sec=("time_sec", "std"),
            min_sec=("time_sec", "min"),
            max_sec=("time_sec", "max"),
            mean_k=("k_final", "mean"),
            median_k=("k_final", "median"),
            fallback_rate=("used_fallback", "mean"),
            sorted_ok=("sorted_ok", "min"),
        )
    )

    raw_ladder = (
        summary[summary["algo"] == "RawLadderSort"]
        [["dataset", "n", "median_sec"]]
        .rename(columns={"median_sec": "raw_ladder_median_sec"})
    )
    timsort = (
        summary[summary["algo"] == "TimSort"]
        [["dataset", "n", "median_sec"]]
        .rename(columns={"median_sec": "timsort_median_sec"})
    )

    summary = summary.merge(raw_ladder, on=["dataset", "n"], how="left")
    summary = summary.merge(timsort, on=["dataset", "n"], how="left")
    summary["speedup_vs_raw_ladder_median"] = (
        summary["raw_ladder_median_sec"] / summary["median_sec"]
    )
    summary["speedup_vs_timsort_median"] = (
        summary["timsort_median_sec"] / summary["median_sec"]
    )

    summary = summary[HYBRID_SUMMARY_COLUMNS].sort_values(["dataset", "n", "algo"])
    out_path.parent.mkdir(parents=True, exist_ok=True)
    summary.to_csv(out_path, index=False)
    return summary


def report(name: str, out_path: Path, summary: pd.DataFrame, ratio_cols: list[str]) -> None:
    sorted_failures = int((summary["sorted_ok"] != 1).sum())
    nan_ratios = int(summary[ratio_cols].isna().sum().sum())
    print(f"{name}: wrote {len(summary)} rows to {out_path}")
    print(f"{name}: sorted_ok failures = {sorted_failures}")
    print(f"{name}: NaN relative times/speedups = {nan_ratios}")


def main() -> None:
    social = summarize_ablation(ABLATION_SOCIAL_RAW, ABLATION_SOCIAL_SUMMARY)
    report(
        "ablation_social",
        ABLATION_SOCIAL_SUMMARY,
        social,
        ["relative_time_mean", "relative_time_median"],
    )

    riffle = summarize_ablation(ABLATION_RIFFLE_RAW, ABLATION_RIFFLE_SUMMARY)
    report(
        "ablation_riffle",
        ABLATION_RIFFLE_SUMMARY,
        riffle,
        ["relative_time_mean", "relative_time_median"],
    )

    hybrid = summarize_hybrid(HYBRID_RAW, HYBRID_SUMMARY)
    report(
        "hybrid",
        HYBRID_SUMMARY,
        hybrid,
        ["speedup_vs_raw_ladder_median", "speedup_vs_timsort_median"],
    )


if __name__ == "__main__":
    main()
