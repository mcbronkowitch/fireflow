#!/usr/bin/env python3
"""Guard rails for the panel control counter.

No pytest in this environment -- plain asserts, exit code says it all,
same shape as host/vcv/res/test_flow_panel.py.
Run from tools/:  python test_count_panel_controls.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import count_panel_controls as c

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_runtime_is_panel_plus_appended():
    counts = c.count_controls()
    check(counts["runtime"] == counts["panel"] + counts["appended"],
          "runtime %d != panel %d + appended %d"
          % (counts["runtime"], counts["panel"], counts["appended"]))


def test_parts_are_symmetric():
    counts = c.count_controls()
    check(counts["part_a"] == counts["part_b"],
          "the two parts are no longer symmetric: A=%d B=%d"
          % (counts["part_a"], counts["part_b"]))


def test_known_baseline_2026_08_07():
    # Baseline am Tag der Phase-0-Planung, am 8. August unveraendert
    # nachgemessen. Wenn diese Zeile rot wird, hat sich das Panel geaendert
    # -- dann docs/hardware/io-budget.md nachziehen, nicht den Test
    # aufweichen. Die Hardware-Reduktion haengt an genau dieser Zahl.
    counts = c.count_controls()
    check(counts["runtime"] == 82, "runtime baseline moved: %d, expected 82"
          % counts["runtime"])
    check(counts["part_a"] == 23, "part_a baseline moved: %d, expected 23"
          % counts["part_a"])
    check(counts["shared"] == 16, "shared baseline moved: %d, expected 16"
          % counts["shared"])


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("control count OK")
