"""Guard for pot_round.py: the two position gates at their edges, the
derived impedance, the pot-line parser, and the pot table held against
shell/pot_plan.h so the host copy cannot drift from the firmware's.

Runs as a plain script; pytest is not installed on this machine.
Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""
import os
import re
import sys

import pot_round as pr

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def raises(fn):
    try:
        fn()
    except ValueError:
        return True
    return False


# --- A. PG1 edges, inclusive, in counts (spec section 7) ---
check("A1 PG1 admits its low edge", pr.pg1(28180))
check("A2 PG1 admits its high edge", pr.pg1(37355))
check("A3 PG1 refuses one below", not pr.pg1(28179))
check("A4 PG1 refuses one above", not pr.pg1(37356))
check("A5 PG1 refuses a missing reading", not pr.pg1(None))
check("A6 PG1 is symmetric about 32767.5",
      32767.5 - pr.PG1_LO == pr.PG1_HI - 32767.5)

# --- B. PG2 edges, inclusive ---
check("B1 PG2 admits 1024", pr.pg2(32000, 33024))
check("B2 PG2 refuses 1025", not pr.pg2(32000, 33025))
check("B3 PG2 is symmetric", pr.pg2(33024, 32000) and not pr.pg2(33025, 32000))
check("B4 PG2 refuses a missing reading", not pr.pg2(None, 32000))

# --- C. the derived impedance ---
check("C1 a 10k pot at exact mid is 2650 ohm",
      abs(pr.r_src(32767.5, 10000) - 2650.0) < 1e-6)
check("C2 a 20k pot at exact mid is 5150 ohm",
      abs(pr.r_src(32767.5, 20000) - 5150.0) < 1e-6)
check("C3 at PG1's edge the impedance is within 2 % of its peak",
      pr.r_src(pr.PG1_LO, 20000) - pr.RON_OHM > 0.98 * 5000)

# --- D. the parser ---
lines = pr.sample_lines()
parsed = [pr.parse_pot_line(l) for l in lines]
check("D1 the sample CFG line parses",
      parsed[0] == ("cfg", {"round": 1, "pots": 3, "git": "deadbee"}))
check("D2 three ID lines parse", [p[0] for p in parsed[1:]] == ["id"] * 3)
check("D3 an ID carries its name as a string",
      parsed[2][1]["name"] == "RV4" and parsed[2][1]["r_track"] == 20000)
check("D4 a foreign line is None",
      pr.parse_pot_line("SHELL_WAIT_CFG adc_khz=6146") is None)
check("D5 the overflow marker is refused",
      raises(lambda: pr.parse_pot_line(lines[1] + "$$")))
check("D6 a line short of a field is refused",
      raises(lambda: pr.parse_pot_line(lines[1].rsplit(" ", 1)[0])))
check("D7 ids_match accepts the sample",
      pr.ids_match(parsed[0][1], [p[1] for p in parsed[1:]]))
wrong = [dict(p[1]) for p in parsed[1:]]
wrong[1]["ch"] = 7
check("D8 ids_match refuses a pot on the wrong channel",
      not pr.ids_match(parsed[0][1], wrong))
check("D9 ids_match refuses a missing CFG line",
      not pr.ids_match(None, [p[1] for p in parsed[1:]]))

# --- E. the table against the firmware's ---
header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pot_plan.h")
with open(header, encoding="utf-8") as fh:
    text = fh.read()
rows = re.findall(r'\{"(RV\d)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}',
                  text)
from_header = tuple((n, int(g), int(c), int(h), int(l), int(r))
                    for n, g, c, h, l, r in rows)
check("E1 pot_plan.h's kPots parses to three rows", len(from_header) == 3)
check("E2 POTS is pot_plan.h's kPots, row for row", from_header == pr.POTS)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("pot_round guard: ok")
