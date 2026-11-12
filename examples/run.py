"""Train the solver and save a strategy and convergence history."""

import argparse
import json
from pathlib import Path

from hu_aof_cfr import CFRSolver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=int, default=100_000)
    parser.add_argument("--evaluate-every", type=int, default=1000)
    parser.add_argument("--output", type=Path, default=Path("results"))
    args = parser.parse_args()

    solver = CFRSolver()
    solver.train(args.iterations, evaluate_every=args.evaluate_every)
    directory = solver.save_run(args.output)
    print(f"Saved to {directory.resolve()}")
    print(json.dumps(solver.evaluate(), indent=2))


if __name__ == "__main__":
    main()
