"""Round four's host-side constants and arithmetic, in one module so
read_settle.py, read_wait.py and read_pots.py cannot disagree about a bound.

What lives here: the pot table (shell/pot_plan.h's twin -- test_pot_round.py
parses the header and fails when they drift), the two position gates PG1 and
PG2, the derived source impedance, and the parser for the two lines both
probes print in a SHELL_POT_ROUND=1 image (shell/pot_report.cpp).

Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""

# pot_plan.h's kPots, in order: (name, group, channel, hi_ch, lo_ch, r_track).
POTS = (("RV2", 0, 2, 1, 3, 10000),
        ("RV4", 0, 6, 5, 7, 20000),
        ("RV6", 1, 2, 1, 3, 10000))

RON_OHM = 150
FULL_SCALE = 65535

# PG1, spec section 7: inclusive, in counts, symmetric about 32767.5
# (+-4587.5), i.e. x ~ 0.43..0.57, where x(1-x) is at least 98 % of its peak.
PG1_LO = 28180
PG1_HI = 37355

# PG2, spec section 7: inclusive. 1024 counts is dx = 0.0156, which moves
# x(1-x) by at most 0.1 % around mid travel; a knocked pot moves thousands.
PG2_MAX = 1024

# settle_plan.cpp kSettlePotPlan: the pots' pairs start at P6, two per pot in
# POTS order -- from the high neighbour, then from the low one.
SETTLE_POT_PAIR0 = 6
# wait_plan.h wait_victim(): the pots are victims 5..7 in POTS order.
WAIT_POT_VICTIM0 = 5

_STR_FIELDS = ("name", "git")
_KEYS = {
    "SHELL_POT_CFG": {"round", "pots", "git"},
    "SHELL_POT_ID": {"idx", "name", "group", "ch", "r_track", "hi", "lo"},
}


def parse_pot_line(line):
    """("cfg", fields) or ("id", fields) for a SHELL_POT_* line, None for any
    other line. Raises ValueError on libDaisy's "$$" overflow marker, on a
    non-integer where an integer belongs, or on a wrong field set -- a line
    cut short without a marker parses as valid ints and simply lacks its
    tail, and only the key-set check turns that into a refusal."""
    line = line.strip()
    for tag, kind in (("SHELL_POT_CFG", "cfg"), ("SHELL_POT_ID", "id")):
        if line.startswith(tag + " "):
            if "$$" in line:
                raise ValueError("libDaisy overflow marker")
            out = {}
            for token in line[len(tag):].split():
                key, _, value = token.partition("=")
                out[key] = value if key in _STR_FIELDS else int(value)
            if set(out) != _KEYS[tag]:
                raise ValueError("%s carries the wrong field set" % tag)
            return kind, out
    return None


def ids_match(cfg, ids):
    """True iff a block's SHELL_POT_CFG and SHELL_POT_ID lines describe POTS
    exactly -- the image measured the pots this reader is about to name."""
    if cfg is None or cfg.get("round") != 1 or cfg.get("pots") != len(POTS):
        return False
    want = [{"idx": i, "name": n, "group": g, "ch": c, "r_track": r,
             "hi": h, "lo": l}
            for i, (n, g, c, h, l, r) in enumerate(POTS)]
    return sorted(ids, key=lambda d: d["idx"]) == want


def r_src(reading, r_track):
    """The wiper's source impedance at `reading`, DERIVED: x = reading / full
    scale, R_track * x * (1 - x) + Ron. R_track is nominal (spec section 3)."""
    x = reading / FULL_SCALE
    return r_track * x * (1.0 - x) + RON_OHM


def pg1(reading):
    return reading is not None and PG1_LO <= reading <= PG1_HI


def pg2(a, b):
    return a is not None and b is not None and abs(a - b) <= PG2_MAX


def sample_lines(git="deadbee"):
    """The two line kinds exactly as pot_report.cpp prints them, for the
    guards' fixtures. Transcribed from its format strings, not the spec."""
    out = ["SHELL_POT_CFG round=1 pots=%d git=%s" % (len(POTS), git)]
    for i, (n, g, c, h, l, r) in enumerate(POTS):
        out.append("SHELL_POT_ID idx=%d name=%s group=%d ch=%d r_track=%d "
                   "hi=%d lo=%d" % (i, n, g, c, r, h, l))
    return out
