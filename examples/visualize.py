"""Plot convergence metrics from a previously saved CSV."""

import argparse
import csv
from math import ceil, floor, isfinite, log10, nan
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", nargs="?", type=Path, help="Defaults to the latest run in results")
    parser.add_argument("--output", type=Path, help="Defaults to convergence.png beside the CSV")
    parser.add_argument("--per-100", action="store_true")
    args = parser.parse_args()

    csv_path = args.csv_path
    if csv_path is None:
        root = Path("results")
        runs = [path for path in root.glob("run_*/convergence.csv")
                if (path.parent / "metadata.json").is_file()]
        csv_path = max(runs, default=root / "convergence.csv")
    output = args.output or csv_path.with_suffix(".png")
    with csv_path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        parser.error("No evaluations recorded. Train with a smaller --evaluate-every.")

    scale = 100 if args.per_100 else 1
    iterations = [int(row["iteration"]) for row in rows]
    series = {}
    for key, label in (("nash_conv", "NashConv"), ("exploitability", "Exploitability")):
        values = [scale * float(row[key]) for row in rows]
        series[label] = [value if isfinite(value) and value > 0 else nan for value in values]
    positive = [value for values in series.values() for value in values if isfinite(value)]
    if not positive:
        parser.error("A log scale requires at least one positive, finite metric.")

    fig, ax = plt.subplots(figsize=(8, 4.5), layout="constrained")
    for label, values in series.items():
        ax.plot(iterations, values, label=label, clip_on=False)
    bottom = floor(log10(min(positive)))
    top = max(ceil(log10(max(positive))), bottom + 1)
    ax.set_yscale("log", base=10)
    ax.set_ylim(10.0**bottom, 10.0**top)
    ax.yaxis.set_major_locator(LogLocator(base=10))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{log10(value):g}"))
    ax.set_xlim(left=0, right=max(iterations) or 1)
    unit = "BB/100 hands" if args.per_100 else "BB/hand"
    ax.set(xlabel="CFR iterations", ylabel=f"log₁₀(metric in {unit})")
    ax.grid(axis="y", which="major", alpha=0.25)
    ax.set_title("HU AoF average strategy")
    ax.legend()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    main()
