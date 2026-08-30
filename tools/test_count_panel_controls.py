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


# The inventory as gen_panel.py prints it today. This is a tripwire, not a
# spec: any commit that adds, removes or re-sides a control turns it red on
# purpose, and re-basing it is part of that commit's work.
#
# When it goes red, in the SAME commit:
#   1. python tools/count_panel_controls.py   -- the generator is the authority,
#      never a count quoted from a spec, a plan or this file
#   2. copy what it printed into BASELINE below, and move BASELINE_ROUND on
#   3. pull docs/hardware/io-budget.md sec. 1 along -- its two command dumps, the
#      by-Bauform table and the physical-position count all quote these numbers
# Do NOT weaken or delete the check instead. It was left red through five
# panel rounds (2.21.1 regrouping, 2.21.2 redistribution, 2.21.3 plate round
# 2a, the MOD latch layer, PAN); io-budget.md drifted right back out of date
# behind it, because nothing was pushing back.
#
# History, so retiring the old marker loses nothing: the baseline was 82
# runtime / 23 part_a / 16 shared on 2026-08-07 (Phase-0 planning, remeasured
# unchanged on 08-08). The hw-control-reduction round of 2026-08-09 took that
# inventory down on purpose; the number it was guarding is a finished job.
BASELINE_ROUND = "2026-08-30, after PAN (spec 2026-08-30-pan)"
BASELINE = {
    "panel": 68,
    "appended": 6,
    "runtime": 74,
    "part_a": 20,
    "shared": 10,
}


def test_current_baseline():
    counts = c.count_controls()
    for key, expected in sorted(BASELINE.items()):
        check(counts[key] == expected,
              "%s baseline moved: %d, expected %d (baseline pins %s) -- "
              "re-base BASELINE and docs/hardware/io-budget.md sec. 1 together"
              % (key, counts[key], expected, BASELINE_ROUND))


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
