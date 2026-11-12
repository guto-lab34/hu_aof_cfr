"""Plot convergence metrics from a previously saved CSV."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", nargs="?", type=Path, default=Path("results/convergence.csv"))
    parser.add_argument("--output", type=Path, default=Path("results/convergence.png"))
    parser.add_argument("--per-100", action="store_true")
    args = parser.parse_args()

    with args.csv_path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        parser.error("No evaluations recorded. Train with a smaller --evaluate-every.")

    scale = 100 if args.per_100 else 1
    fig, ax = plt.subplots(figsize=(8, 4.5), layout="constrained")
    iterations = [int(row["iteration"]) for row in rows]
    for key, label in (("nash_conv", "NashConv"), ("exploitability", "Exploitability")):
        ax.plot(iterations, [scale * float(row[key]) for row in rows], label=label)
    ax.set(xlabel="CFR iterations", ylabel="BB/100 hands" if args.per_100 else "BB/hand")
    ax.set_title("HU AoF average strategy")
    ax.legend()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    main()
