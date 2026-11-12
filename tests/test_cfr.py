import csv
import itertools
import json
import random
import struct
from pathlib import Path

import pytest

from hu_aof_cfr import CFRSolver, _core
from hu_aof_cfr.solver import _checked_table


def test_terminal_payoffs():
    expected = [(-0.5, 0.5), (1, -1), (8, -8), (0, 0), (-8, 8)]
    for outcome, payoffs in enumerate(expected):
        actual = _core._terminal_payoff(outcome)
        assert tuple(actual) == payoffs
        assert sum(actual) == 0


def test_card_legality_and_suit_symmetry():
    deal = [48, 44, 41, 43]
    key = _core._canonical_key(deal)
    for permutation in itertools.permutations(range(4)):
        swapped = [4 * (card // 4) + permutation[card % 4] for card in deal]
        assert _core._canonical_key(swapped) == key
        assert _core._canonical_key(swapped[2:] + swapped[:2]) != key
    assert _core._canonical_key([44, 48, 43, 41]) == key
    # A shared suit and a disjoint suit are distinct relative configurations.
    assert _core._canonical_key([48, 44, 40, 36]) != _core._canonical_key([48, 44, 41, 37])
    for invalid in ([48, 44, 48, 43], [52, 44, 41, 43], [-1, 44, 41, 43]):
        with pytest.raises(ValueError, match="four distinct cards"):
            _core._canonical_key(invalid)


def test_concrete_oracle_all_legal_deals():
    solver = CFRSolver()
    rng = random.Random(7)
    sb, bb = ([rng.random() for _ in range(169)] for _ in range(2))
    assert solver._engine._verify_concrete(sb, bb) < 2e-12


def test_equity_validation():
    table = _checked_table()
    assert len(table) == 1_125_248
    with pytest.raises(ValueError, match="size or header"):
        _core.Engine(table[:-1])
    changed = bytearray(table)
    struct.pack_into("<I", changed, 24, 1_712_305)
    with pytest.raises(ValueError, match="record"):
        _core.Engine(bytes(changed))
    changed = bytearray(table)
    first_win = struct.unpack_from("<I", table, 24)[0]
    struct.pack_into("<I", changed, 24, first_win + 1)
    with pytest.raises(ValueError, match="seat reversal"):
        _core.Engine(bytes(changed))


@pytest.mark.parametrize("regret,probability", [
    ((0, 0), 0.5), ((-3, -1), 0.5), ((-3, 2), 1), ((2, -3), 0), ((2, 3), 0.6),
])
def test_regret_matching(regret, probability):
    assert _core._regret_match(regret) == probability


def test_article_7_ten_round_trace():
    path = Path(__file__).with_name("article7_selected.csv")
    with path.open(newline="", encoding="utf-8") as stream:
        expected = list(csv.DictReader(stream))
    assert len(expected) == 40
    solver = CFRSolver()
    for t in range(1, 11):
        before = solver._engine._state()
        solver.train(1, evaluate_every=1)
        after = solver._engine._state()
        average = solver.average_strategy()
        for row in expected[4 * (t - 1):4 * t]:
            assert int(row["iteration"]) == t
            index = int(row["id"])
            seat, hand = divmod(index, 169)
            assert average[index]["hand"] == row["hand"]
            p = before[seat][hand]
            fold, active = before[4 + seat][hand]
            value = (1 - p) * fold + p * active
            actual = {
                "p_before": p,
                "cf_fold": fold,
                "cf_active": active,
                "cf_value": value,
                "regret_fold": fold - value,
                "regret_active": active - value,
                "cumulative_fold_before": before[2 + seat][hand][0],
                "cumulative_active_before": before[2 + seat][hand][1],
                "cumulative_fold": after[2 + seat][hand][0],
                "cumulative_active": after[2 + seat][hand][1],
                "p_after": after[seat][hand],
                "p_average": average[index]["allin" if seat == 0 else "call"],
            }
            for field, measurement in actual.items():
                assert measurement == pytest.approx(float(row[field]), abs=2e-12), (t, index, field)


def test_best_response_and_metrics():
    solver = CFRSolver()
    uniform = solver.evaluate()
    assert uniform["sb_ev"] == pytest.approx(0)
    assert uniform["bb_ev"] == pytest.approx(0)
    engine = solver._engine
    zeros, ones = [0.0] * 169, [1.0] * 169
    # When both always fold, SB can gain 1.5 by shoving, BB has no decision.
    fold = engine._evaluate_profile(zeros, zeros)
    assert fold["sb_ev"] == pytest.approx(-0.5)
    assert fold["bb_ev"] == pytest.approx(0.5)
    assert fold["sb_br_ev"] == pytest.approx(1)
    assert fold["bb_br_ev"] == pytest.approx(0.5)
    assert fold["nash_conv"] == pytest.approx(1.5)
    assert fold["exploitability"] == pytest.approx(0.75)
    assert engine._evaluate_profile(ones, ones)["sb_ev"] == pytest.approx(0)

    solver.train(100, evaluate_every=10)
    sb, bb = engine.average()
    response = solver.best_response()
    sb_br = [row["allin"] for row in response[:169]]
    bb_br = [row["call"] for row in response[169:]]
    metrics = solver.evaluate()
    assert metrics["sb_ev"] + metrics["bb_ev"] == pytest.approx(0, abs=2e-12)
    sb_deviation = engine._evaluate_profile(sb_br, bb)["sb_ev"]
    bb_deviation = engine._evaluate_profile(sb, bb_br)["bb_ev"]
    assert metrics["sb_br_ev"] == pytest.approx(sb_deviation)
    assert metrics["bb_br_ev"] == pytest.approx(bb_deviation)
    assert metrics["nash_conv"] == pytest.approx(sb_deviation + bb_deviation)
    assert metrics["exploitability"] == pytest.approx(metrics["nash_conv"] / 2)
    assert metrics["exploitability"] < uniform["exploitability"]
    assert solver.evaluate(per_100=True)["sb_ev"] == 100 * metrics["sb_ev"]


def test_python_training_and_csv(tmp_path):
    solver = CFRSolver()
    assert solver.iterations == 0
    initial = solver.evaluate()
    assert solver.convergence() == [{"iteration": 0, **initial}]
    assert all(row["fold"] == 0.5 for row in solver.average_strategy())
    with pytest.raises(ValueError):
        solver.train(0)
    with pytest.raises(ValueError):
        solver.train(10, evaluate_every=0)
    solver.train(3, evaluate_every=2)
    solver.train(3, evaluate_every=2)
    whole = CFRSolver()
    whole.train(6, evaluate_every=2)
    assert solver.average_strategy() == whole.average_strategy()
    assert solver.convergence() == whole.convergence()

    out = tmp_path / "nested" / "results"
    solver.save_strategy(out / "strategy.csv")
    solver.save_convergence(out / "convergence.csv")
    with (out / "strategy.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 338
    for row, expected in zip(rows, solver.average_strategy()):
        active, unused = ("allin", "call") if row["position"] == "SB" else ("call", "allin")
        assert row["hand"] == expected["hand"]
        assert row[unused] == ""
        assert float(row[active]) == expected[active]
        assert float(row["fold"]) + float(row[active]) == 1
    with (out / "convergence.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert [int(row["iteration"]) for row in rows] == [0, 2, 4, 6]
    assert float(rows[0]["nash_conv"]) == initial["nash_conv"]
    assert float(rows[-1]["nash_conv"]) == solver.evaluate()["nash_conv"]
    metadata = json.loads((out / "metadata.json").read_text())
    assert metadata["iterations"] == 6
    assert metadata["equity_table_version"] == "HUAOFEQ1"

    first = solver.save_run(out)
    solver.train(2, evaluate_every=2)
    second = solver.save_run(out)
    assert first != second
    for directory, iterations in ((first, 6), (second, 8)):
        assert directory.parent == out
        assert (directory / "strategy.csv").is_file()
        assert (directory / "convergence.csv").is_file()
        assert json.loads((directory / "metadata.json").read_text())["iterations"] == iterations
