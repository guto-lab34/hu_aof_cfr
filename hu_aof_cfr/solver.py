"""Python interface and export helpers for the C++ solver."""

import csv
import hashlib
import json
from datetime import datetime, timedelta, timezone
from importlib.resources import files
from pathlib import Path
from tempfile import NamedTemporaryFile
from threading import RLock

from . import _core

EQUITY_SHA256 = "7753a849640a97521e573d744bf36455c4d3eef31a2575b7ae38c9c71af5d733"
STRATEGY_FIELDS = ("position", "hand", "fold", "allin", "call")
CONVERGENCE_FIELDS = ("iteration", "nash_conv", "exploitability", "sb_ev", "bb_ev")
HANDS = tuple(_core.hand_name(i) for i in range(169))


def _checked_table():
    data = files("hu_aof_cfr").joinpath("data", "hu_equity.bin").read_bytes()
    if hashlib.sha256(data).hexdigest() != EQUITY_SHA256:
        raise ValueError("Bundled HU equity table has an unexpected SHA-256")
    return data


def _strategy_rows(profile):
    sb, bb = profile
    return [
        {"position": "SB", "hand": hand, "fold": 1 - p, "allin": p, "call": None}
        for hand, p in zip(HANDS, sb)
    ] + [
        {"position": "BB", "hand": hand, "fold": 1 - p, "allin": None, "call": p}
        for hand, p in zip(HANDS, bb)
    ]


def _write(path, write):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="", dir=path.parent, delete=False
        ) as stream:
            temp = Path(stream.name)
            write(stream)
        temp.replace(path)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)


def _write_csv(path, fields, rows):
    def write(stream):
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    _write(path, write)


class CFRSolver:
    """Vanilla CFR for fixed 8BB HU AoF, initialized at uniform play."""

    def __init__(self):
        self._lock = RLock()
        self._engine = _core.Engine(_checked_table())

    @property
    def iterations(self):
        """Number of completed simultaneous updates."""
        with self._lock:
            return self._engine.iterations

    def train(self, iterations, *, evaluate_every=1000):
        """Add iterations and record metrics at global multiples of evaluate_every."""
        for name, value in (("iterations", iterations), ("evaluate_every", evaluate_every)):
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"{name} must be an integer")
            if not 0 < value < 2**64:
                raise ValueError(f"{name} must be a positive 64-bit integer")
        with self._lock:
            self._engine.train(iterations, evaluate_every)

    def average_strategy(self):
        """Return 338 rows of average action probabilities in CSV order."""
        with self._lock:
            return _strategy_rows(self._engine.average())

    def best_response(self):
        """Return each seat's best response to the other seat's average strategy."""
        with self._lock:
            return _strategy_rows(self._engine.best_response())

    def evaluate(self, *, per_100=False):
        """Evaluate the average strategy. Values use BB/hand by default."""
        with self._lock:
            metrics = self._engine.evaluate()
        return {key: value * 100 for key, value in metrics.items()} if per_100 else metrics

    def convergence(self):
        """Return the recorded evaluations of the average strategy in BB/hand."""
        with self._lock:
            return self._engine.history()

    def metadata(self):
        with self._lock:
            return {
                "game": "Heads-Up No-Limit Hold'em All-in or Fold",
                "stack_bb": 8,
                "small_blind_bb": 0.5,
                "big_blind_bb": 1,
                "ante_bb": 0,
                "rake": False,
                "jackpot": False,
                "fortune": False,
                "utility_reference": "net from before blinds",
                "algorithm": "full-tree vanilla CFR",
                "updates": "simultaneous",
                "average": "uniform time weight, including the initial strategy",
                "iterations": self.iterations,
                "metric_unit": "BB/hand",
                "equity_table_version": "HUAOFEQ1",
                "equity_sha256": EQUITY_SHA256,
            }

    def save_metadata(self, path):
        metadata = self.metadata()
        _write(Path(path), lambda stream: json.dump(metadata, stream, indent=2))

    def save_strategy(self, path):
        self._save_export(path, STRATEGY_FIELDS, self.average_strategy)

    def save_convergence(self, path):
        self._save_export(path, CONVERGENCE_FIELDS, self.convergence)

    def save_run(self, output="results"):
        """Save one snapshot in a new timestamped folder and return its path."""
        parent = Path(output)
        parent.mkdir(parents=True, exist_ok=True)
        jst = timezone(timedelta(hours=9))
        stamp = datetime.now(jst).strftime("run_%Y-%m-%d_%H%M%S_%f_JST")
        directory = parent / stamp
        suffix = 0
        while True:
            try:
                directory.mkdir()
                break
            except FileExistsError:
                suffix += 1
                directory = parent / f"{stamp}_{suffix:04d}"
        with self._lock:
            _write_csv(directory / "strategy.csv", STRATEGY_FIELDS, self.average_strategy())
            _write_csv(directory / "convergence.csv", CONVERGENCE_FIELDS, self.convergence())
            self.save_metadata(directory / "metadata.json")
        return directory

    def _save_export(self, path, fields, get_rows):
        path = Path(path)
        metadata_path = path.parent / "metadata.json"
        if path.resolve() == metadata_path.resolve():
            raise ValueError("CSV path conflicts with metadata.json")
        with self._lock:
            _write_csv(path, fields, get_rows())
            self.save_metadata(metadata_path)
