"""Reads one SHELL_WAIT_CFG..SHELL_WAIT_END block from the board's USB-CDC
port, writes it out as a CSV, and computes the shift table and G9.

Spec: ../docs/superpowers/specs/2026-09-27-coupon-wait-sweep-probe-design.md

Call:
    python read_wait.py PORT out.csv [timeout_s]

**THE SHIFT AND G9 ARE THIS READER'S, NOT THE FIRMWARE'S.** The firmware
prints one `SHELL_WAIT` line per grid point -- n, mean, min, max of the 64
measured conversions -- and the four gates it can compute. `shifts()` below
turns those into

    shift(arm, victim, W) = mean(arm, victim, W) - mean(arm, victim, W = 0)

same arm, same victim, same block, so a rung bias cancels inside each arm and
never enters a comparison across arms (spec section 4). `g9()` is the bridge
to round two, host-side because its bound comes from round two's published
captures (spec section 5); the firmware prints `g9=-1` as a literal
sentinel and has never printed anything else.

Exit code: 1 when the firmware's `gates_ok` is 0, when G9 fails, or when G9
cannot be computed at all. 0 otherwise. Nothing about the SHAPE of the curve
enters the exit code -- this is a characterisation, and a gate on the
answer would refuse the run that gives it.

The block format below was transcribed from `wait_probe.cpp`'s PrintLine
calls, not from the spec -- see docs/gotchas.md on a probe spec's output
section drifting from its firmware.

Default timeout: 300 s. A block is estimated at ~90 s (spec section 6,
`wait_block_estimate_ms()`); 300 s is that plus a host that starts
listening mid-block and has to wait for the next _CFG.
"""
import sys
import time

# Arm numbering, as printed on SHELL_WAIT_CASE (wait_plan.h: WaitArm).
ARM_WAIT = 0
ARM_DISCARD = 1
ARM_LONG = 2
ARM_CODEC = 3
ARM_NAMES = {ARM_WAIT: "A wait", ARM_DISCARD: "B discard",
             ARM_LONG: "L long", ARM_CODEC: "C codec"}

# wait_plan.h's two grids. The reader checks every case against these, so a
# block printed by an image with a different grid is refused rather than
# read against the wrong axis.
WAIT_GRID_US = (0, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
                10000, 20000, 50000)
CODEC_GRID_US = (0, 200, 1000, 10000)

VICTIMS = 5
CASES = 4 * VICTIMS

# --- G9, the bridge to round two -------------------------------------------
#
# Arm C, REF_A (group 0, channel 8), shift at W = 10 000 us, inclusive bounds.
# Round two measured this quantity four times across four boots: -849.7,
# -850.7, -851.4, -854.0 (codec-tone-measured.md section 6). The bound is
# NOT [min - 4, max + 4]: that would be right if the instrument were the
# same, and it is not -- round two's wait was a phase crossing from the
# previous conversion's start, this one's is a commanded idle from its EOC.
# +-50 around 850 refuses the failures that would make the curve unrelatable
# to round two (a shift near 0, or near half the size) and admits instrument
# differences of a few percent. A judgement, flagged in the spec; tighten it
# here once the first block has been read.
G9_VICTIM = (0, 8)
G9_W_US = 10000
G9_LO = -900
G9_HI = -800

# Round two's four captures of the same quantity, for the bridge table.
# REPORT ONLY -- the 1 kHz and 5 kHz columns wander 91 and 23 counts between
# boots (codec-tone-measured.md section 6), so a gate on them would be a gate
# on the boot. Keyed (victim, W in us); each tuple is the four captures in
# order: 438fd51, c5631f4, task-5, ab02aec.
ROUND_TWO = {
    ((0, 8), 10000): (-849.7, -850.7, -851.4, -854.0),
    ((0, 8), 1000): (-283.4, -280.1, -307.9, -370.7),
    ((0, 8), 200): (-69.2, -69.8, -75.2, -92.1),
    ((1, 6), 10000): (-669.2, -669.5, -670.7, -674.2),
    ((0, 9), 10000): (-16.2, -17.2, -15.9, -17.1),
}

# The two 150 ohm ties. Round two measured their shift at exactly 0 at every
# cadence; a tie that moves here is an instrument fault, and the reader says
# so -- as a report, not a gate, because a gate on it would refuse the one
# block that shows the fault.
TIE_R_SRC = 150

_STR_FIELDS = ("git",)


# --- parsing ---------------------------------------------------------------

def _fields(line, prefix):
    # libDaisy's logger stamps an overflowing line with "$$" and fuses what
    # follows onto it. int() catches almost every shape of that; the one hole
    # is `git`, a string field, and refusing any line carrying the marker
    # closes it -- the same rule read_tone.py's _fields() carries.
    if "$$" in line:
        raise ValueError("libDaisy overflow marker")
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = value if key in _STR_FIELDS else int(value)
    return out


# Every tag's field set, transcribed from wait_probe.cpp's format strings. A
# line cut short WITHOUT a "$$" marker parses as valid ints and simply lacks
# its tail; checking the key set turns that into a refused block instead of
# a KeyError halfway through report().
_KEYS = {
    "SHELL_WAIT_CFG": {"adc_khz", "repeats", "points", "block_size", "sr",
                       "sweep_dir", "git"},
    "SHELL_WAIT_CLK": {"span_short_cyc", "span_long_cyc", "smp_short_tenths",
                       "smp_long_tenths"},
    "SHELL_WAIT_CAL": {"lat_mean_ns", "lat_min_ns", "lat_max_ns", "b0",
                       "timeouts"},
    "SHELL_WAIT_CASE": {"case", "arm", "victim_group", "victim_ch", "r_src",
                        "rung_tenths", "codec"},
    "SHELL_WAIT": {"case", "w_us", "n", "mean", "min", "max"},
    "SHELL_WAIT_SPAN": {"zero", "rail", "hi_spread", "lo_spread", "valid"},
    "SHELL_WAIT_G5": {"victim_group", "victim_ch", "expect", "mean", "ok"},
    "SHELL_WAIT_GATES": {"g2", "g4", "g5", "g7", "g9", "gates_ok"},
    "SHELL_WAIT_HEALTH": {"missed_blocks", "timeouts", "block_ms"},
}


def _parse(line, prefix):
    out = _fields(line, prefix)
    if set(out) != _KEYS[prefix]:
        raise ValueError("%s carries the wrong field set" % prefix)
    return out


_SINGLE = (("SHELL_WAIT_CFG", "cfg"), ("SHELL_WAIT_CLK", "clk"),
           ("SHELL_WAIT_CAL", "cal"), ("SHELL_WAIT_SPAN", "span"),
           ("SHELL_WAIT_GATES", "gates"), ("SHELL_WAIT_HEALTH", "health"))


def _new_block():
    block = {key: None for _, key in _SINGLE}
    block.update({"cases": {}, "points": {}, "g5": []})
    return block


def _is_complete(block):
    """Everything the block format promises at SHELL_WAIT_END. A block short
    of any of it is dropped, never half-read: a missing point or a missing
    gate produces a table that is plausible in every digit."""
    for _, key in _SINGLE:
        if block[key] is None:
            return False
    if sorted(block["cases"]) != list(range(CASES)):
        return False
    if len({_victim(g) for g in block["g5"]}) != VICTIMS             or len(block["g5"]) != VICTIMS:
        return False
    for case, c in block["cases"].items():
        if c["arm"] != case // VICTIMS:
            return False
        grid = CODEC_GRID_US if c["arm"] == ARM_CODEC else WAIT_GRID_US
        pts = block["points"].get(case, {})
        # Every grid point exactly once, and nothing off the grid.
        if sorted(pts) != sorted(grid):
            return False
    return True


def parse_block(lines):
    """One block from an iterable of lines, or None.

    Starts at the first SHELL_WAIT_CFG, stops at the first SHELL_WAIT_END
    after it. A line that fails to parse, or a point that arrives twice,
    refuses the block."""
    block = None
    try:
        for raw in lines:
            line = raw.strip()
            if line.startswith("SHELL_WAIT_CFG"):
                block = _new_block()
            if block is None:
                continue
            if line.startswith("SHELL_WAIT_END"):
                return block if _is_complete(block) else None
            for prefix, key in _SINGLE:
                if line.startswith(prefix + " "):
                    block[key] = _parse(line, prefix)
                    break
            else:
                if line.startswith("SHELL_WAIT_CASE "):
                    c = _parse(line, "SHELL_WAIT_CASE")
                    if c["case"] in block["cases"]:
                        return None
                    block["cases"][c["case"]] = c
                elif line.startswith("SHELL_WAIT_G5 "):
                    block["g5"].append(_parse(line, "SHELL_WAIT_G5"))
                elif line.startswith("SHELL_WAIT "):
                    p = _parse(line, "SHELL_WAIT")
                    if p["case"] not in block["cases"]:
                        return None
                    pts = block["points"].setdefault(p["case"], {})
                    if p["w_us"] in pts:
                        return None
                    pts[p["w_us"]] = p
    except ValueError:
        return None
    return None


# --- the computation -------------------------------------------------------

def _victim(case):
    return (case["victim_group"], case["victim_ch"])


def shifts(block):
    """One row per (case, W): the shift against the same case's W = 0 mean.

    A point whose n is 0 (every measured conversion timed out, mean -1)
    carries shift None, and so does every point of a case whose W = 0 point
    has n 0 -- a reference that was never measured is not a reference."""
    rows = []
    for case_id in sorted(block["cases"]):
        c = block["cases"][case_id]
        pts = block["points"][case_id]
        ref = pts[0]
        ref_ok = ref["n"] > 0
        for w in sorted(pts):
            p = pts[w]
            ok = ref_ok and p["n"] > 0
            rows.append({
                "case": case_id, "arm": c["arm"], "victim": _victim(c),
                "r_src": c["r_src"], "rung_tenths": c["rung_tenths"],
                "codec": c["codec"], "w_us": w, "n": p["n"],
                "mean": p["mean"], "min": p["min"], "max": p["max"],
                "shift": (p["mean"] - ref["mean"]) if ok else None,
            })
    return rows


def _shift_at(rows, arm, victim, w_us):
    for r in rows:
        if r["arm"] == arm and r["victim"] == victim and r["w_us"] == w_us:
            return r["shift"]
    return None


def g9(block):
    """(passed, shift). passed is None when the shift cannot be computed --
    which the exit code treats as a failure: a bridge that was not measured
    has not been crossed."""
    s = _shift_at(shifts(block), ARM_CODEC, G9_VICTIM, G9_W_US)
    if s is None:
        return None, None
    return (G9_LO <= s <= G9_HI), s


def tie_faults(block):
    """Every (arm, victim, W) where a 150 ohm tie's shift is not 0. Report
    only (see TIE_R_SRC)."""
    return [r for r in shifts(block)
            if r["r_src"] == TIE_R_SRC and r["shift"] not in (None, 0)]


# --- output ----------------------------------------------------------------

CSV_FIELDS = ("case", "arm", "victim_group", "victim_ch", "r_src",
              "rung_tenths", "codec", "w_us", "n", "mean", "min", "max",
              "shift")


def format_csv(block):
    out = [",".join(CSV_FIELDS)]
    for r in shifts(block):
        vals = [r["case"], r["arm"], r["victim"][0], r["victim"][1],
                r["r_src"], r["rung_tenths"], r["codec"], r["w_us"], r["n"],
                r["mean"], r["min"], r["max"],
                "" if r["shift"] is None else r["shift"]]
        out.append(",".join(str(v) for v in vals))
    return "\n".join(out) + "\n"


def report(block, out=None):
    """Everything this reader says about one block, on stderr, plus the exit
    code. Separated from main() so the port and the file are the only things
    main() adds."""
    err = sys.stderr
    cfg, gates, health = block["cfg"], block["gates"], block["health"]
    print("wait block: git=%s adc_khz=%d sweep_dir=%d block_ms=%d"
          % (cfg["git"], cfg["adc_khz"], cfg["sweep_dir"],
             health["block_ms"]), file=err)
    print("gates: g2=%d g4=%d g5=%d g7=%d gates_ok=%d  missed_blocks=%d "
          "timeouts=%d" % (gates["g2"], gates["g4"], gates["g5"],
                           gates["g7"], gates["gates_ok"],
                           health["missed_blocks"], health["timeouts"]),
          file=err)

    rows = shifts(block)
    for arm in (ARM_WAIT, ARM_DISCARD, ARM_LONG, ARM_CODEC):
        grid = CODEC_GRID_US if arm == ARM_CODEC else WAIT_GRID_US
        print("\narm %s -- shift in counts against the same case's W=0"
              % ARM_NAMES[arm], file=err)
        print("  %-8s %6s  %s" % ("victim", "r_src",
                                  " ".join("%7s" % w for w in grid)),
              file=err)
        for case_id in sorted(block["cases"]):
            c = block["cases"][case_id]
            if c["arm"] != arm:
                continue
            cells = []
            for w in grid:
                s = _shift_at(rows, arm, _victim(c), w)
                cells.append("%7s" % ("-" if s is None else s))
            print("  (%d,%-2d)   %6d  %s" % (c["victim_group"], c["victim_ch"],
                                            c["r_src"], " ".join(cells)),
                  file=err)

    print("\nbridge to round two (REPORT ONLY except the G9 row):", file=err)
    for (victim, w), caps in ROUND_TWO.items():
        s = _shift_at(rows, ARM_CODEC, victim, w)
        tag = "  <- G9" if (victim, w) == (G9_VICTIM, G9_W_US) else ""
        print("  (%d,%d) W=%5d us  this block %6s   round two %s%s"
              % (victim[0], victim[1], w, "-" if s is None else s,
                 " / ".join("%.1f" % v for v in caps), tag), file=err)

    faults = tie_faults(block)
    for r in faults:
        print("TIE MOVED (instrument fault?): arm %s victim (%d,%d) W=%d us "
              "shift=%d" % (ARM_NAMES[r["arm"]], r["victim"][0],
                            r["victim"][1], r["w_us"], r["shift"]), file=err)

    passed, s = g9(block)
    if passed is None:
        print("\nG9: NOT COMPUTABLE -- arm C's REF_A has no usable W=0 or "
              "W=10000 point", file=err)
    else:
        print("\nG9: %s -- arm C REF_A shift at 10 ms = %d, bound [%d, %d]"
              % ("PASS" if passed else "FAIL", s, G9_LO, G9_HI), file=err)

    if out is not None:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(format_csv(block))

    reasons = []
    if gates["gates_ok"] != 1:
        reasons.append("the firmware's gates_ok is 0")
    if passed is not True:
        reasons.append("G9")
    if reasons:
        print("REFUSED: %s" % "; ".join(reasons), file=err)
        return 1
    return 0


# --- the port --------------------------------------------------------------

def _read_one_block(port, limit):
    """Listen on `port` until a whole block has arrived, or `limit` seconds
    have passed. Re-parses only at an _END marker and resets at every _CFG,
    for read_tone.py's reason: a host busy re-parsing is a host that is not
    draining the port, which is the condition libDaisy's logger fuses lines
    under."""
    import serial

    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        lines = []
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if line.startswith("SHELL_WAIT_CFG"):
                lines = [line]
                continue
            lines.append(line)
            if line.startswith("SHELL_WAIT_END"):
                block = parse_block(lines)
                if block is not None:
                    return block
                print("discarded an incomplete block at its _END marker; "
                      "still listening", file=sys.stderr)
                lines = []
    return None


def main() -> int:
    # serial is imported inside _read_one_block(), so the guard can import
    # this module without pyserial installed.
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: read_wait.py PORT out.csv [timeout_s]")
    port, out = sys.argv[1], sys.argv[2]
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 300.0

    block = _read_one_block(port, limit)
    if block is None:
        print("no complete SHELL_WAIT block within %.0f s" % limit,
              file=sys.stderr)
        return 1
    return report(block, out)


if __name__ == "__main__":
    raise SystemExit(main())
