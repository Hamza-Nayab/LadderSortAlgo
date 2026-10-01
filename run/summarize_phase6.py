#!/usr/bin/env python3
"""Summarize the Phase 6 GH Archive real-trace benchmark."""

from __future__ import annotations

import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean, median, stdev


RAW_CSV_HEADER = (
    "dataset,n,algo,seed,round,time_sec,k_final,k_over_n,used_fallback,sorted_ok,git_hash"
)
SUMMARY_CSV_HEADER = (
    "dataset,n,algo,count,mean_sec,median_sec,std_sec,min_sec,max_sec,mean_k,median_k,"
    "fallback_rate,speedup_vs_timsort_median,sorted_ok"
)

RAW_CSV_PATH = Path("results/raw/19_real_trace_raw.csv")
SUMMARY_CSV_PATH = Path("results/summary/20_real_trace_summary.csv")
NOTES_PATH = Path("results/real_trace_notes.txt")


def read_raw(path: Path) -> list[dict[str, str]]:
    if not path.exists() or path.stat().st_size <= 0:
        raise SystemExit(f"missing or empty raw CSV: {path}")
    with path.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None:
            raise SystemExit(f"missing header in raw CSV: {path}")
        expected = RAW_CSV_HEADER.split(",")
        if reader.fieldnames != expected:
            raise SystemExit(
                f"unexpected raw header in {path}: {reader.fieldnames}, expected {expected}"
            )
        return list(reader)


def fnum(value: float) -> str:
    if math.isnan(value):
        return "nan"
    return f"{value:.9f}"


def summarize(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    groups: dict[tuple[str, int, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[(row["dataset"], int(row["n"]), row["algo"])].append(row)

    summaries: list[dict[str, object]] = []
    for (dataset, n, algo), group in sorted(groups.items()):
        times = [float(row["time_sec"]) for row in group]
        ks = [float(row["k_final"]) for row in group]
        fallbacks = [float(row["used_fallback"]) for row in group]
        sorted_values = [int(row["sorted_ok"]) for row in group]
        summaries.append(
            {
                "dataset": dataset,
                "n": n,
                "algo": algo,
                "count": len(group),
                "mean_sec": mean(times),
                "median_sec": median(times),
                "std_sec": stdev(times) if len(times) > 1 else 0.0,
                "min_sec": min(times),
                "max_sec": max(times),
                "mean_k": mean(ks),
                "median_k": median(ks),
                "fallback_rate": mean(fallbacks),
                "speedup_vs_timsort_median": math.nan,
                "sorted_ok": min(sorted_values),
            }
        )

    timsort_medians = {
        (row["dataset"], row["n"]): row["median_sec"]
        for row in summaries
        if row["algo"] == "TimSort"
    }
    for row in summaries:
        baseline = timsort_medians.get((row["dataset"], row["n"]))
        median_sec = float(row["median_sec"])
        if baseline is not None and median_sec > 0:
            row["speedup_vs_timsort_median"] = float(baseline) / median_sec

    return summaries


def write_summary(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = SUMMARY_CSV_HEADER.split(",")
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            out = dict(row)
            for key in [
                "mean_sec",
                "median_sec",
                "std_sec",
                "min_sec",
                "max_sec",
                "mean_k",
                "median_k",
                "fallback_rate",
                "speedup_vs_timsort_median",
            ]:
                out[key] = fnum(float(out[key]))
            writer.writerow(out)


def update_notes(path: Path, summaries: list[dict[str, object]]) -> None:
    existing = path.read_text(encoding="utf-8") if path.exists() else ""
    marker = "\nPhase 6 Benchmark Summary\n"
    if marker in existing:
        existing = existing.split(marker, 1)[0].rstrip() + "\n"
    elif existing and not existing.endswith("\n"):
        existing += "\n"

    raw_rows = [row for row in summaries if row["algo"] == "RawLadderSort"]
    measured_k = median([float(row["median_k"]) for row in raw_rows]) if raw_rows else math.nan
    n_values = sorted({int(row["n"]) for row in summaries})
    counts = sorted({int(row["count"]) for row in summaries})
    medians = "\n".join(
        f"- {row['algo']}: {float(row['median_sec']):.9f} sec"
        for row in sorted(summaries, key=lambda item: str(item["algo"]))
    )

    section = f"""{marker}
measured K from RawLadderSort median_k: {measured_k:.0f}
benchmarked algorithms: RawLadderSort, HybridLadderSort, TimSort, StdSort, StableSort
benchmark rounds: {counts[0] if len(counts) == 1 else counts}
benchmark input rows: {n_values[0] if len(n_values) == 1 else n_values}
raw file path: {RAW_CSV_PATH}
summary file path: {SUMMARY_CSV_PATH}
median runtimes by algorithm:
{medians}
"""
    path.write_text(existing.rstrip() + "\n" + section, encoding="utf-8")


def main() -> None:
    rows = read_raw(RAW_CSV_PATH)
    summaries = summarize(rows)
    write_summary(SUMMARY_CSV_PATH, summaries)
    update_notes(NOTES_PATH, summaries)

    sorted_failures = sum(1 for row in summaries if int(row["sorted_ok"]) != 1)
    nan_speedups = sum(
        1 for row in summaries if math.isnan(float(row["speedup_vs_timsort_median"]))
    )
    print(f"wrote {len(summaries)} rows to {SUMMARY_CSV_PATH}")
    print(f"sorted failures: {sorted_failures}")
    print(f"NaN speedups: {nan_speedups}")


if __name__ == "__main__":
    main()
