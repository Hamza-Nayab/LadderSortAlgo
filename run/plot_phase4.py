#!/usr/bin/env python3
"""Generate Phase 4 plots without external plotting dependencies."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape

import pandas as pd


PLOTS_DIR = Path("results/plots")
SOCIAL_SUMMARY = Path("results/summary/08_ablation_social_summary.csv")
RIFFLE_SUMMARY = Path("results/summary/10_ablation_riffle_summary.csv")
HYBRID_SUMMARY = Path("results/summary/12_hybrid_summary.csv")

WIDTH = 980
HEIGHT = 560
MARGIN_LEFT = 78
MARGIN_RIGHT = 28
MARGIN_TOP = 64
MARGIN_BOTTOM = 118

COLORS = [
    (55, 126, 184),
    (77, 175, 74),
    (228, 26, 28),
    (152, 78, 163),
    (255, 127, 0),
]

# ---------- B&W-safe hatch patterns (SVG pattern ids) ----------
# Each series gets a distinct geometric fill so bars are distinguishable in grayscale.
# Patterns: diagonal lines, dots, crosshatch, vertical lines, horizontal lines.
HATCH_PATTERNS = [
    "hatch_diag",
    "hatch_dots",
    "hatch_cross",
    "hatch_vert",
    "hatch_horiz",
]


@dataclass
class Bar:
    x: float
    y: float
    width: float
    height: float
    color: tuple[int, int, int]
    series_index: int = 0
    label: str | None = None


@dataclass
class Text:
    x: float
    y: float
    text: str
    size: int = 12
    anchor: str = "middle"
    bold: bool = False


@dataclass
class Line:
    x1: float
    y1: float
    x2: float
    y2: float
    stroke: tuple[int, int, int] = (120, 120, 120)
    width: float = 1.0
    dash: bool = False


def fmt_n(n: int) -> str:
    if n >= 1_000_000:
        return f"{n // 1_000_000}M"
    if n >= 1_000:
        return f"{n // 1_000}K"
    return str(n)


def nice_ticks(max_value: float, count: int = 5) -> list[float]:
    if max_value <= 0:
        return [0.0, 1.0]
    top = max_value * 1.08
    step = top / count
    return [i * step for i in range(count + 1)]


def rgb(color: tuple[int, int, int]) -> str:
    return f"rgb({color[0]},{color[1]},{color[2]})"


def build_grouped_bars(
    data: pd.DataFrame,
    group_col: str,
    series_col: str,
    value_col: str,
    group_order: list,
    series_order: list,
    title: str,
    ylabel: str,
    reference_line: float | None,
) -> tuple[list[Bar], list[Text], list[Line]]:
    chart_w = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
    chart_h = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
    max_value = float(data[value_col].max())
    ticks = nice_ticks(max(max_value, reference_line or 0.0))
    y_max = ticks[-1]

    bars: list[Bar] = []
    texts: list[Text] = [
        Text(WIDTH / 2, 30, title, 18, bold=True),
        Text(24, MARGIN_TOP + chart_h / 2, ylabel, 13, anchor="middle"),
    ]
    lines: list[Line] = []

    plot_x0 = MARGIN_LEFT
    plot_y0 = MARGIN_TOP
    plot_y1 = MARGIN_TOP + chart_h

    for tick in ticks:
        y = plot_y1 - (tick / y_max) * chart_h
        lines.append(Line(plot_x0, y, plot_x0 + chart_w, y, (222, 222, 222), 0.7))
        texts.append(Text(plot_x0 - 10, y + 4, f"{tick:.2f}", 11, anchor="end"))

    lines.append(Line(plot_x0, plot_y0, plot_x0, plot_y1, (70, 70, 70), 1.2))
    lines.append(Line(plot_x0, plot_y1, plot_x0 + chart_w, plot_y1, (70, 70, 70), 1.2))

    if reference_line is not None and 0 <= reference_line <= y_max:
        y = plot_y1 - (reference_line / y_max) * chart_h
        lines.append(Line(plot_x0, y, plot_x0 + chart_w, y, (70, 70, 70), 1.0, True))

    group_w = chart_w / len(group_order)
    inner_gap = group_w * 0.18
    available = group_w - inner_gap
    bar_gap = 4
    bar_w = max(8, (available - bar_gap * (len(series_order) - 1)) / len(series_order))

    lookup = {
        (row[group_col], row[series_col]): float(row[value_col])
        for _, row in data.iterrows()
    }
    for gi, group in enumerate(group_order):
        group_left = plot_x0 + gi * group_w + inner_gap / 2
        center = plot_x0 + gi * group_w + group_w / 2
        for si, series in enumerate(series_order):
            value = lookup.get((group, series))
            if value is None:
                continue
            h = (value / y_max) * chart_h
            x = group_left + si * (bar_w + bar_gap)
            y = plot_y1 - h
            bars.append(Bar(x, y, bar_w, h, COLORS[si % len(COLORS)], si))
        label = str(group)
        texts.append(Text(center, HEIGHT - 74, label, 11))

    legend_x = MARGIN_LEFT
    legend_y = HEIGHT - 35
    for si, series in enumerate(series_order):
        x = legend_x + si * 160
        bars.append(Bar(x, legend_y - 12, 14, 14, COLORS[si % len(COLORS)], si))
        texts.append(Text(x + 20, legend_y, str(series), 11, anchor="start"))

    return bars, texts, lines


def _svg_hatch_defs() -> str:
    """Return SVG <defs> block with geometric hatch patterns for B&W printing."""
    return (
        '<defs>'
        # diagonal lines (45°)
        '<pattern id="hatch_diag" width="6" height="6" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">'
        '<line x1="0" y1="0" x2="0" y2="6" stroke="#000" stroke-width="1.2" stroke-opacity="0.45"/>'
        '</pattern>'
        # dots
        '<pattern id="hatch_dots" width="6" height="6" patternUnits="userSpaceOnUse">'
        '<circle cx="3" cy="3" r="1.2" fill="#000" fill-opacity="0.45"/>'
        '</pattern>'
        # crosshatch
        '<pattern id="hatch_cross" width="6" height="6" patternUnits="userSpaceOnUse">'
        '<line x1="0" y1="0" x2="6" y2="6" stroke="#000" stroke-width="0.9" stroke-opacity="0.40"/>'
        '<line x1="6" y1="0" x2="0" y2="6" stroke="#000" stroke-width="0.9" stroke-opacity="0.40"/>'
        '</pattern>'
        # vertical lines
        '<pattern id="hatch_vert" width="5" height="5" patternUnits="userSpaceOnUse">'
        '<line x1="2.5" y1="0" x2="2.5" y2="5" stroke="#000" stroke-width="1.0" stroke-opacity="0.45"/>'
        '</pattern>'
        # horizontal lines
        '<pattern id="hatch_horiz" width="5" height="5" patternUnits="userSpaceOnUse">'
        '<line x1="0" y1="2.5" x2="5" y2="2.5" stroke="#000" stroke-width="1.0" stroke-opacity="0.45"/>'
        '</pattern>'
        '</defs>'
    )


def write_svg(path: Path, bars: list[Bar], texts: list[Text], lines: list[Line]) -> None:
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="white"/>',
        _svg_hatch_defs(),
    ]
    for line in lines:
        dash = ' stroke-dasharray="5 4"' if line.dash else ""
        parts.append(
            f'<line x1="{line.x1:.2f}" y1="{line.y1:.2f}" x2="{line.x2:.2f}" y2="{line.y2:.2f}" '
            f'stroke="{rgb(line.stroke)}" stroke-width="{line.width}"{dash}/>'
        )
    for bar in bars:
        # color fill
        parts.append(
            f'<rect x="{bar.x:.2f}" y="{bar.y:.2f}" width="{bar.width:.2f}" height="{bar.height:.2f}" '
            f'fill="{rgb(bar.color)}"/>'
        )
        # hatch pattern overlay for B&W readability
        hatch_id = HATCH_PATTERNS[bar.series_index % len(HATCH_PATTERNS)]
        parts.append(
            f'<rect x="{bar.x:.2f}" y="{bar.y:.2f}" width="{bar.width:.2f}" height="{bar.height:.2f}" '
            f'fill="url(#{hatch_id})"/>'
        )
    for text in texts:
        weight = "700" if text.bold else "400"
        parts.append(
            f'<text x="{text.x:.2f}" y="{text.y:.2f}" font-family="Helvetica,Arial,sans-serif" '
            f'font-size="{text.size}" font-weight="{weight}" text-anchor="{text.anchor}" fill="#222">'
            f"{escape(text.text)}</text>"
        )
    parts.append("</svg>\n")
    path.write_text("\n".join(parts), encoding="utf-8")


def pdf_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def pdf_y(y: float) -> float:
    return HEIGHT - y


def write_pdf(path: Path, bars: list[Bar], texts: list[Text], lines: list[Line]) -> None:
    commands: list[str] = ["1 1 1 rg 0 0 980 560 re f"]
    for line in lines:
        r, g, b = [v / 255 for v in line.stroke]
        dash = "[5 4] 0 d " if line.dash else "[] 0 d "
        commands.append(
            f"{dash}{r:.3f} {g:.3f} {b:.3f} RG {line.width:.2f} w "
            f"{line.x1:.2f} {pdf_y(line.y1):.2f} m {line.x2:.2f} {pdf_y(line.y2):.2f} l S"
        )
    for bar in bars:
        r, g, b = [v / 255 for v in bar.color]
        commands.append(
            f"{r:.3f} {g:.3f} {b:.3f} rg {bar.x:.2f} {pdf_y(bar.y + bar.height):.2f} "
            f"{bar.width:.2f} {bar.height:.2f} re f"
        )
    for text in texts:
        font = "/F1"
        x = text.x
        if text.anchor == "middle":
            x -= len(text.text) * text.size * 0.25
        elif text.anchor == "end":
            x -= len(text.text) * text.size * 0.5
        commands.append(
            f"0.133 0.133 0.133 rg BT {font} {text.size} Tf {x:.2f} {pdf_y(text.y):.2f} Td "
            f"({pdf_escape(text.text)}) Tj ET"
        )

    stream = "\n".join(commands).encode("latin-1", errors="replace")
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 980 560] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Length " + str(len(stream)).encode() + b" >>\nstream\n" + stream + b"\nendstream",
    ]

    out = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(out))
        out.extend(f"{i} 0 obj\n".encode())
        out.extend(obj)
        out.extend(b"\nendobj\n")
    xref = len(out)
    out.extend(f"xref\n0 {len(objects) + 1}\n".encode())
    out.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        out.extend(f"{offset:010d} 00000 n \n".encode())
    out.extend(
        f"trailer << /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    )
    path.write_bytes(out)


def save_plot(stem: str, bars: list[Bar], texts: list[Text], lines: list[Line]) -> None:
    write_svg(PLOTS_DIR / f"{stem}.svg", bars, texts, lines)
    write_pdf(PLOTS_DIR / f"{stem}.pdf", bars, texts, lines)
    print(f"Wrote results/plots/{stem}.pdf")
    print(f"Wrote results/plots/{stem}.svg")


def plot_ablation(path: Path, stem: str, title: str) -> None:
    df = pd.read_csv(path)
    variant_order = ["LadderFull", "NoHint", "NoGallop", "HeapMerge", "StableMode"]
    sizes = sorted(df["n"].unique())
    df = df.copy()
    df["N"] = df["n"].map(lambda n: fmt_n(int(n)))
    bars, texts, lines = build_grouped_bars(
        df,
        "variant",
        "N",
        "relative_time_median",
        variant_order,
        [fmt_n(int(n)) for n in sizes],
        title,
        "Median relative time",
        1.0,
    )
    save_plot(stem, bars, texts, lines)


def plot_hybrid(path: Path) -> None:
    df = pd.read_csv(path)
    algo_order = ["RawLadderSort", "HybridLadderSort", "TimSort", "StdSort"]
    df = df.copy()
    df["dataset_n"] = df["dataset"] + "\n" + df["n"].map(lambda n: fmt_n(int(n)))
    group_order = [
        f"{dataset}\n{fmt_n(int(n))}"
        for dataset in ["descending", "random", "block_cyclic", "social_feed"]
        for n in sorted(df[df["dataset"] == dataset]["n"].unique())
    ]
    group_order = [group for group in group_order if group in set(df["dataset_n"])]
    bars, texts, lines = build_grouped_bars(
        df,
        "dataset_n",
        "algo",
        "speedup_vs_timsort_median",
        group_order,
        algo_order,
        "Phase 4 Hybrid Speedup vs TimSort",
        "Speedup vs TimSort",
        1.0,
    )
    save_plot("hybrid_speedup", bars, texts, lines)


def main() -> None:
    PLOTS_DIR.mkdir(parents=True, exist_ok=True)
    plot_ablation(
        SOCIAL_SUMMARY,
        "ablation_social_relative",
        "Social Feed Ablation: Median Relative Time",
    )
    plot_ablation(
        RIFFLE_SUMMARY,
        "ablation_riffle_relative",
        "Two-Run Riffle Ablation: Median Relative Time",
    )
    plot_hybrid(HYBRID_SUMMARY)


if __name__ == "__main__":
    main()
