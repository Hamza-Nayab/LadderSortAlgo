import pandas as pd
from pathlib import Path

RAW = Path("results/raw/01_main_runtime_raw.csv")
OUT = Path("results/summary/02_main_runtime_summary.csv")

df = pd.read_csv(RAW)

# If warmups are saved, remove them
if "is_warmup" in df.columns:
    df = df[df["is_warmup"] == False].copy()

# Normalize sorted_ok
df["sorted_ok"] = df["sorted_ok"].astype(str).str.lower()

bad = df[df["sorted_ok"] != "true"]
if len(bad):
    print(bad.head())
    raise SystemExit("ERROR: found rows with sorted_ok != true")

group_cols = ["dataset", "n", "algo"]

summary = (
    df.groupby(group_cols, as_index=False)
      .agg(
          mean_time_sec=("time_sec", "mean"),
          std_time_sec=("time_sec", "std"),
          min_time_sec=("time_sec", "min"),
          max_time_sec=("time_sec", "max"),
          rounds=("time_sec", "count"),
      )
)

# TimSort baseline
tim = (
    summary[summary["algo"] == "timsort"]
    [["dataset", "n", "mean_time_sec"]]
    .rename(columns={"mean_time_sec": "timsort_mean_sec"})
)

summary = summary.merge(tim, on=["dataset", "n"], how="left")

# std::sort baseline
std = (
    summary[summary["algo"] == "std_sort"]
    [["dataset", "n", "mean_time_sec"]]
    .rename(columns={"mean_time_sec": "std_sort_mean_sec"})
)

summary = summary.merge(std, on=["dataset", "n"], how="left")

summary["speedup_vs_timsort"] = (
    summary["timsort_mean_sec"] / summary["mean_time_sec"]
)

summary["speedup_vs_std_sort"] = (
    summary["std_sort_mean_sec"] / summary["mean_time_sec"]
)

summary = summary.sort_values(["dataset", "n", "algo"])

OUT.parent.mkdir(parents=True, exist_ok=True)
summary.to_csv(OUT, index=False)

print(f"Wrote {OUT}")
print(summary.head(20))
