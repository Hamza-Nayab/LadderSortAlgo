import pandas as pd
from pathlib import Path

RAW = Path("results/raw/23_encroaching_baseline_raw.csv")
OUT = Path("results/summary/24_encroaching_baseline_summary.csv")

df = pd.read_csv(RAW)

if "is_warmup" in df.columns:
    df = df[df["is_warmup"] == False].copy()

df["sorted_ok"] = df["sorted_ok"].astype(str).str.lower()
bad = df[df["sorted_ok"] != "true"]
if len(bad):
    print(bad.head())
    raise SystemExit("ERROR: found rows with sorted_ok != true")

summary = (
    df.groupby(["dataset", "n"], as_index=False)
      .agg(
          count=("time_sec", "count"),
          mean_sec=("time_sec", "mean"),
          std_sec=("time_sec", "std"),
          mean_k=("k_final", "mean"),
          all_ok=("sorted_ok", lambda s: int((s == "true").all()))
      )
)

summary = summary.sort_values(["dataset", "n"])
OUT.parent.mkdir(parents=True, exist_ok=True)
summary.to_csv(OUT, index=False)
print(f"Wrote {OUT}")
print(summary)
