#!/usr/bin/env python3
"""Download the fixed GH Archive trace used by Phase 6."""

import argparse
import sys
import urllib.error
import urllib.request
from pathlib import Path


RAW_DATA_DIR = Path("data/raw")
NOTES_PATH = Path("results/real_trace_notes.txt")
DEFAULT_DATE = "2015-01-01"
DEFAULT_HOUR = 15
URL_TEMPLATE = "https://data.gharchive.org/{date}-{hour}.json.gz"

# TODO(Phase 6): Record source URLs and timestamps in NOTES_PATH.


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--date", default=DEFAULT_DATE, help="GH Archive date as YYYY-MM-DD")
    parser.add_argument("--hour", type=int, default=DEFAULT_HOUR, help="GH Archive hour, 0-23")
    parser.add_argument("--out-dir", default=str(RAW_DATA_DIR), help="Output directory")
    parser.add_argument("--force", action="store_true", help="Redownload even if output exists")
    return parser.parse_args()


def build_url(date: str, hour: int) -> str:
    if hour < 0 or hour > 23:
        raise ValueError("--hour must be between 0 and 23")
    return URL_TEMPLATE.format(date=date, hour=hour)


def download(url: str, out_path: Path) -> None:
    tmp_path = out_path.with_suffix(out_path.suffix + ".tmp")
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            with tmp_path.open("wb") as out:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    out.write(chunk)
        tmp_path.replace(out_path)
    except Exception:
        tmp_path.unlink(missing_ok=True)
        raise


def main() -> None:
    args = parse_args()
    try:
        url = build_url(args.date, args.hour)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"gharchive_{args.date}-{args.hour}.json.gz"

    print(f"Source URL: {url}")
    print(f"Output path: {out_path}")

    if out_path.exists() and out_path.stat().st_size > 0 and not args.force:
        print("File already exists; use --force to redownload.")
    else:
        try:
            download(url, out_path)
        except urllib.error.HTTPError as exc:
            print(f"ERROR: download failed: HTTP {exc.code} {exc.reason}", file=sys.stderr)
            raise SystemExit(1) from exc
        except urllib.error.URLError as exc:
            print(f"ERROR: download failed: {exc.reason}", file=sys.stderr)
            raise SystemExit(1) from exc
        except Exception as exc:
            print(f"ERROR: download failed: {exc}", file=sys.stderr)
            raise SystemExit(1) from exc

    size = out_path.stat().st_size if out_path.exists() else 0
    print(f"File size: {size} bytes")
    if size <= 0:
        print("ERROR: downloaded file is missing or empty", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
