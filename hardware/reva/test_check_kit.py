#!/usr/bin/env python3
"""Guard for hardware/reva/check_kit.py (P4-2). Plain script, no pcbnew."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_kit as CK  # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


REPORT = """** Drc report for x.kicad_pcb **
** Created on 2026-09-30T11:08:04 **
** Report includes: Errors, Warnings **

** Found 2 DRC violations **
[clearance]: Clearance violation
    Rule: x; error
    @(105.1014 mm, 69.6471 mm): Pad 1 [SM_3V3] of C11 on B.Cu
    @(214.3000 mm, 118.9200 mm): PTH pad T [MOD2_B] of J13
[shorting_items]: Items shorting two nets
    Local override; error
    @(258.1300 mm, 21.7080 mm): PTH pad 1 [GND] of D16
    @(256.8000 mm, 22.0000 mm): PTH pad 2 [M4_CH0] of RV56

** Found 1 unconnected pads **
[unconnected_items]: Missing connection between items
    Local override; error
    @(1.0 mm, 2.0 mm): Pad 1 [LED3] of R12 on B.Cu
    @(3.0 mm, 4.0 mm): PTH pad 2 [LED3] of D4

** Found 0 Footprint errors **

** End of Report **
"""


def main():
    blocks = CK.drc_blocks(REPORT)
    check([(c, r) for c, r, _f in blocks] == [("clearance", ["C11", "J13"]),
                                             ("shorting_items", ["D16", "RV56"]),
                                             ("unconnected_items", ["D4", "R12"])],
          "drc_blocks: classes and refs, SMD pad refs included")
    s = CK.report_summary(REPORT)
    check(s == {"violations": 2, "unconnected": 1, "footprint_errors": 0, "complete": True},
          "report_summary: %r" % s)
    cut = REPORT.split("** Found 1 unconnected")[0]
    check(CK.report_summary(cut)["complete"] is False, "report_summary: a report cut before its counts is incomplete")
    # every count present, only the end marker gone: the counts alone cannot
    # tell that this report was cut short
    no_end = REPORT.replace("** End of Report **\n", "")
    ns = CK.report_summary(no_end)
    check(ns["complete"] is False and ns["violations"] == 2,
          "report_summary: a report with all counts but no end marker is incomplete")
    ok, details, nk = CK.judge({"a"}, {"a": "m", "b": "n"})
    check(not ok and nk == 1 and any("[NEW]" in d for d in details), "judge: a NEW key is red")
    ok, details, _ = CK.judge({"a", "z"}, {"a": "m"})
    check(not ok and any("no longer fails" in d for d in details), "judge: a stale key is red")
    ok, _d, _n = CK.judge({"a"}, {"a": "m"})
    check(ok, "judge: known only is green")
    cw = {"body", "clearance"}
    pairs = ({"GATE_A_L", "SOURCE_A"},)
    allowed = {"CLOCK", "SONG_A", "GATE_A_L", "SOURCE_A"}
    check(CK.key_allowed("clearance GATE_A_L/SOURCE_A", allowed, pairs, cw), "key: pair allowed")
    check(not CK.key_allowed("clearance GATE_A_L/CLOCK", allowed, pairs, cw), "key: pair with a stranger refused")
    check(not CK.key_allowed("NOT_A_PART CLOCK", allowed, pairs, cw), "key: unknown class word refused")
    check(abs(CK.mst([(0, 0), (3, 4), (3, 0)]) - 7.0) < 1e-9, "mst: 3 + 4")
    print("FAILED: %d" % len(FAILS) if FAILS else "all check_kit checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
