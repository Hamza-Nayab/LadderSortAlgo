#!/usr/bin/env python3
"""Prepare the Phase 6 GH Archive real-trace workload."""

import argparse
import csv
import gzip
import glob
import json
import random
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_INPUT = Path("data/raw/gharchive_2015-01-01-15.json.gz")
DEFAULT_OUTPUT = Path("data/processed/real_trace_github_events.csv")
DEFAULT_NOTES = Path("results/real_trace_notes.txt")
SOURCE_URL = "https://data.gharchive.org/2015-01-01-15.json.gz"
DATE_HOUR = "2015-01-01 hour 15"
SOURCE_URL_PREFIX = "https://data.gharchive.org/"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default=str(DEFAULT_INPUT), help="Input GH Archive .json.gz file")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT), help="Prepared CSV output path")
    parser.add_argument("--notes", default=str(DEFAULT_NOTES), help="Trace notes output path")
    parser.add_argument("--seed", type=int, default=1, help="Deterministic interleaving seed")
    parser.add_argument("--max-records", type=int, default=None, help="Optional cap on emitted records")
    return parser.parse_args()


def timestamp_to_ms(value: str) -> int | None:
    if not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return int(parsed.timestamp() * 1000)


def extract_source_id(event: dict[str, Any]) -> str | None:
    repo = event.get("repo")
    if not isinstance(repo, dict):
        return None
    source_id = repo.get("id")
    if source_id is None:
        source_id = repo.get("name")
    if source_id is None or source_id == "":
        return None
    return str(source_id)


def resolve_input_paths(input_arg: str) -> list[Path]:
    has_glob = any(char in input_arg for char in "*?[")
    if has_glob:
        paths = [Path(match) for match in glob.glob(input_arg)]
    else:
        paths = [Path(input_arg)]
    paths = sorted(paths)
    if not paths:
        print(f"ERROR: no input files matched: {input_arg}", file=sys.stderr)
        raise SystemExit(1)
    missing_or_empty = [path for path in paths if not path.exists() or path.stat().st_size <= 0]
    if missing_or_empty:
        joined = ", ".join(str(path) for path in missing_or_empty)
        print(f"ERROR: input file is missing or empty: {joined}", file=sys.stderr)
        raise SystemExit(1)
    return paths


def source_url_for(path: Path) -> str:
    name = path.name
    if name.startswith("gharchive_"):
        name = name.removeprefix("gharchive_")
    return f"{SOURCE_URL_PREFIX}{name}"


def read_events(paths: list[Path]) -> tuple[dict[str, list[dict[str, Any]]], int, int]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    parsed_rows = 0
    dropped_rows = 0
    sequential_id = 0

    for path in paths:
        with gzip.open(path, "rt", encoding="utf-8") as fh:
            for line_no, line in enumerate(fh, start=1):
                line = line.strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as exc:
                    print(f"ERROR: invalid JSON in {path} on line {line_no}: {exc}", file=sys.stderr)
                    raise SystemExit(1) from exc

                record_id = event.get("id")
                if record_id is None or record_id == "":
                    record_id = sequential_id
                    sequential_id += 1
                timestamp = event.get("created_at")
                source_id = extract_source_id(event)
                timestamp_ms = timestamp_to_ms(timestamp) if isinstance(timestamp, str) else None

                if source_id is None or timestamp_ms is None:
                    dropped_rows += 1
                    continue

                grouped[source_id].append(
                    {
                        "record_id": str(record_id),
                        "source_id": source_id,
                        "timestamp_ms": timestamp_ms,
                    }
                )
                parsed_rows += 1

    return grouped, parsed_rows, dropped_rows


def build_interleaving(
    grouped: dict[str, list[dict[str, Any]]], seed: int, max_records: int | None
) -> tuple[list[dict[str, Any]], int, int]:
    streams: dict[str, list[dict[str, Any]]] = {}
    for source_id, rows in grouped.items():
        rows.sort(key=lambda row: (row["timestamp_ms"], row["record_id"]))
        if len(rows) >= 2:
            streams[source_id] = rows

    rng = random.Random(seed)
    active = sorted(streams)
    positions = {source_id: 0 for source_id in active}
    output: list[dict[str, Any]] = []

    while active and (max_records is None or len(output) < max_records):
        active_index = rng.randrange(len(active))
        source_id = active[active_index]
        stream = streams[source_id]
        pos = positions[source_id]
        burst_size = rng.randint(1, 16)
        if max_records is not None:
            burst_size = min(burst_size, max_records - len(output))

        for row in stream[pos : pos + burst_size]:
            output.append(
                {
                    "record_id": row["record_id"],
                    "source_id": row["source_id"],
                    "timestamp_ms": row["timestamp_ms"],
                    "original_index": len(output),
                }
            )

        positions[source_id] += min(burst_size, len(stream) - pos)
        if positions[source_id] >= len(stream):
            active.pop(active_index)

    return output, len(streams), sum(len(rows) for rows in streams.values())


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(
            fh, fieldnames=["record_id", "source_id", "timestamp_ms", "original_index"]
        )
        writer.writeheader()
        writer.writerows(rows)


def write_notes(
    path: Path,
    input_paths: list[Path],
    seed: int,
    max_records: int | None,
    parsed_rows: int,
    dropped_rows: int,
    eligible_rows: int,
    emitted_rows: int,
    unique_sources: int,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    max_records_text = "none" if max_records is None else str(max_records)
    raw_file_names = "\n".join(f"- {input_path.name}" for input_path in input_paths)
    source_urls = "\n".join(f"- {source_url_for(input_path)}" for input_path in input_paths)
    text = f"""Phase 6 Real Trace Notes

source: GH Archive
source URLs:
{source_urls}
date/hour collected: 2015-01-01, matched hours from input files
number of raw files parsed: {len(input_paths)}
raw file names:
{raw_file_names}

preprocessing steps:
1. Parsed GH Archive JSON lines from the downloaded gzip file(s).
2. Kept record_id from event["id"] when present, otherwise assigned a sequential id.
3. Kept source_id from event["repo"]["id"] when present, else event["repo"]["name"].
4. Parsed event["created_at"] as a UTC timestamp in milliseconds.
5. Dropped rows missing timestamp or source_id.
6. Grouped events by source_id.
7. Within each source_id, sorted by timestamp_ms and then record_id.
8. Kept only sources with at least 2 events.
9. Built a deterministic sticky-burst interleaving with seed {seed}; burst sizes are 1..16 events.
10. Preserved local timestamp order inside each source stream and did not globally sort the final output.

parsed rows before source-size filtering: {parsed_rows}
dropped rows missing timestamp or source_id: {dropped_rows}
eligible rows after preprocessing: {eligible_rows}
total rows after preprocessing: {emitted_rows}
total unique sources: {unique_sources}
max records limit: {max_records_text}
measured K: placeholder, not measured yet

reason this is an interleaved-stream workload:
public GitHub events are produced by many repositories; each repository stream is locally ordered by event time; the benchmark sequence is a deterministic interleaving of those repository streams.
"""
    path.write_text(text, encoding="utf-8")


def main() -> None:
    args = parse_args()
    input_paths = resolve_input_paths(args.input)
    output_path = Path(args.output)
    notes_path = Path(args.notes)

    if args.max_records is not None and args.max_records <= 0:
        print("ERROR: --max-records must be positive when provided", file=sys.stderr)
        raise SystemExit(2)

    grouped, parsed_rows, dropped_rows = read_events(input_paths)
    rows, unique_sources, eligible_rows = build_interleaving(grouped, args.seed, args.max_records)
    if not rows:
        print("ERROR: no rows remained after preprocessing", file=sys.stderr)
        raise SystemExit(1)

    write_csv(output_path, rows)
    write_notes(
        notes_path,
        input_paths,
        args.seed,
        args.max_records,
        parsed_rows,
        dropped_rows,
        eligible_rows,
        len(rows),
        unique_sources,
    )

    print(f"Inputs: {len(input_paths)} file(s)")
    for input_path in input_paths:
        print(f"  {input_path}")
    print(f"Output: {output_path}")
    print(f"Notes: {notes_path}")
    print(f"Total rows: {len(rows)}")
    print(f"Unique sources: {unique_sources}")


if __name__ == "__main__":
    main()
