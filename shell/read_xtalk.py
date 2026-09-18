"""Reads one SHELL_XTALK_CFG..SHELL_XTALK_END block from the board's USB-CDC
port, writes it out as two CSV files, and computes the verdict.

`out.csv` carries the grid points, one row per measured point, joined to the
attributes of the case that produced it (row, kind, victim, r_src, the two
chain words) and to this reader's own `delta`. Everything else the block says
-- the configuration, the block period's inputs, the clock spans, the
calibration pass, the span G5 was judged against, the per-victim addressing
evidence, every case's own line, each curve's two statistics and the static
cases' means -- goes to `out.csv.meta.csv` beside it, as `scope,case,key,value`
rows. Both files are needed to reproduce anything, and a skipped case exists
in the metadata file alone: it has no points, no statistic and no static
measurement, and spec section 4 is explicit that a skipped case is a line in
the output and never a silently shorter table.

**THE VERDICT IS THIS READER'S, NOT THE FIRMWARE'S.** Spec section 5 says so
in as many words: the firmware prints curves and gates. `deltas()` and
`verdicts()` below are that computation, and `verdict_basis` on every verdict
row says which of the two bases it rests on.

The block format below is `xtalk_probe.cpp`'s `hw.PrintLine()` calls, read
directly rather than assumed. Printed once per pass, in this order:
SHELL_XTALK_CFG, _RATE, _CLK, _CAL, then per case either
(_CASE, points, _STAT) for a grid case or (_CASE, _STATIC) for a static one or
_CASE alone for a skipped one, then _SPAN, one _G5 per victim, _GATES, _END.
(SHELL_XTALK_WARMUP is printed once at boot, outside the repeating block, and
is simply not one of the lines this parser looks for.)

**_STAT is printed for every grid case, not only the Silent ones** --
`reduce_and_emit()` emits it unconditionally, and Task 6's capture counts 44
of them against 44 grid cases. Its two numbers mean different things depending
on whose curve they describe, so they are filed under two scopes: `silent` for
a Silent case, which is the floor the codec-tone plan's G8 reads by case out
of this file, and `curve` for a control's or an aggressor's, which is not a
floor and must not be read as one.

The firmware repeats the block forever with no inter-block delay and there is
no handshake, so this listens until a whole block has arrived. It may NOT
return early: a partial block would report a clean board because the failing
tail never came.

**Default timeout: 40 s.** Task 6 MEASURED the block at 9.07 s on 2026-09-18
-- 9.074 / 9.066 / 9.067 / 9.068 s, `_END` to `_END`, over four consecutive
blocks, with an inter-block gap under 2 ms. Attaching lands in the middle of a
block, so a complete one can need two block periods; 40 s is that doubled and
rounded up generously. (The plan's ~10 s was derived arithmetic and is not
what this is built on.)

libDaisy's logger writes "$$" into `tx_buff_[126..127]` only after an
`impl_.Transmit()` the host did not drain, and it does **not** reset `tx_ptr_`
when that happens (`lib/libDaisy/src/hid/logger.cpp:63-71` and `:78-87`), so
the next `PrintLine` accumulates into the same 128-byte buffer and overflows.
The lever is total bytes printed while the host is not draining, not line
length -- so a fused or truncated line can land **anywhere** in a block, not
only on the first line after connect. A corrupted field drops the whole block
and the reader keeps listening; `_is_complete()`'s five rules are what stop a
block with a line missing from being accepted as whole. This is also why the
read loop only re-parses at an `_END` marker: re-parsing the accumulated list
once per line is quadratic in a 2 984-line block, and a host busy doing that
is a host that is not draining the port.

Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Call:
    python read_xtalk.py COM4 [out.csv] [timeout_seconds]

Exit code is the verdict: 1 when the run's gates did not pass, when any
aggressor case fails the criterion, or when this reader's own recomputation of
G6 disagrees with the bit the firmware printed.
"""
import sys
import time
from collections import Counter, OrderedDict

# Mirrors XtalkKind in shell/xtalk_plan.h:23-29. The numbering is printed as
# `kind=%d` and is part of the output format: it may not be reordered on
# either side.
KIND_SILENT     = 0
KIND_LATCH      = 1
KIND_SHIFT_ONLY = 2
KIND_STATIC     = 3

# Spec section 5's criterion, and shell/settle_plan.h:103's kSettleCounts,
# which xtalk_gates() also judges G6 by. Half an LSB of 12 bit in the 16-bit
# counts the ADC reports.
CRITERION_COUNTS = 8

# Spec section 4's table rows that are not aggressors: row 1 is the floor and
# row 2 is the control every delta is measured against. Row 8 is Static and
# has no grid at all. Everything else -- 3, 4, 5, 6, 7, 9 and 10 -- gets a
# verdict, row 10 included: its aggressor is the probe's own USB-CDC traffic,
# which spec section 2 lists as an aggressor like any other.
ROW_FLOOR   = 1
ROW_CONTROL = 2

# The one field in this format whose value is not an integer. Everything else
# is int()d, which is what makes a "$$"-truncated field raise instead of
# quietly parsing as a string.
_STR_FIELDS = ("git",)

# One row per measured point. The case attributes are repeated on every row on
# purpose: a point line carries only `case`, and a CSV whose rows cannot say
# which victim or which edge direction they came from cannot reproduce a
# single line of the spec's section 4 table.
FIELDS = ("case", "row", "kind", "victim_group", "victim_ch", "r_src",
          "word_a", "word_b", "d_ns", "n", "mean", "min", "max", "delta")

META_FIELDS = ("scope", "case", "key", "value")
# Block-level lines, one per block; the case column stays empty for these.
_META_BLOCK_SCOPES = (("cfg", "cfg"), ("rate", "rate"), ("clk", "clk"),
                      ("cal", "cal"), ("span", "span"), ("gates", "gates"))

_GATE_NAMES = OrderedDict((("g2", "instrument floor"),
                           ("g4", "instrument jitter"),
                           ("g5", "victim addressing"),
                           ("g6", "the control curve")))


def _fields(line, prefix):
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = value if key in _STR_FIELDS else int(value)
    return out


def _new_block():
    return {"cfg": None, "rate": None, "clk": None, "cal": None,
            "span": None, "g5": [], "cases": [], "points": [], "stats": [],
            "statics": [], "gates": None}


def _victim_key(entry):
    return (entry["victim_group"], entry["victim_ch"])


def _is_complete(block):
    """Every completeness check the block format calls for at
    SHELL_XTALK_END. Anything short of this must be dropped rather than
    returned -- see the module docstring. Each rule exists because a
    different corruption gets past the other four."""
    if block["cfg"] is None or block["rate"] is None or block["gates"] is None:
        return False
    # 1. Every case the firmware said it would print, printed. The count is
    #    the firmware's own (SHELL_XTALK_RATE's `cases=`), not this reader's
    #    idea of how big the table is -- a reader that carried its own 58
    #    would refuse every capture the day a row is added.
    if len(block["cases"]) != block["rate"]["cases"]:
        return False
    if sorted(c["case"] for c in block["cases"]) != list(
            range(block["rate"]["cases"])):
        return False
    # 2. Every grid case, and only a grid case, has exactly grid_points
    #    points. PER CASE: a serial timeout loses one case's row while its
    #    neighbours keep theirs, and a total count reports that as complete.
    want = Counter()
    for c in block["cases"]:
        if c["skipped"]:
            continue
        if c["kind"] == KIND_STATIC:
            continue
        want[c["case"]] = block["cfg"]["points"]
    if Counter(p["case"] for p in block["points"]) != want:
        return False
    # 3. Every Static case has its one measurement.
    want_static = {c["case"] for c in block["cases"]
                   if c["kind"] == KIND_STATIC and not c["skipped"]}
    if {s["case"] for s in block["statics"]} != want_static:
        return False
    # 4. Every grid case has its two reported statistics -- they are what
    #    settle-measured.md section 5 could not attribute, and round two's G8
    #    reads the Silent ones out of the metadata file. The firmware emits
    #    one per grid case (reduce_and_emit()), not one per Silent case, and a
    #    rule that wanted only the Silent ones would accept a block that had
    #    lost an aggressor's.
    if {s["case"] for s in block["stats"]} != set(want):
        return False
    # 5. Every victim has its addressing evidence. None of rules 1-4 can see
    #    a lost SHELL_XTALK_G5 line: every case, point, static and statistic
    #    is still there, and what is missing is the proof that the mux was
    #    where the table says it was -- without which every delta is a
    #    measurement of nothing (spec section 7, G5).
    if len({_victim_key(g) for g in block["g5"]}) != block["rate"]["victims"]:
        return False
    return True


def parse_block(lines):
    """The first complete block in `lines`, or None if there is not one."""
    block = None
    for raw in lines:
        line = raw.strip()
        try:
            if line.startswith("SHELL_XTALK_CFG"):
                block = _new_block()
                block["cfg"] = _fields(line, "SHELL_XTALK_CFG")
            elif block is None:
                continue
            elif line.startswith("SHELL_XTALK_RATE"):
                block["rate"] = _fields(line, "SHELL_XTALK_RATE")
            elif line.startswith("SHELL_XTALK_CLK"):
                block["clk"] = _fields(line, "SHELL_XTALK_CLK")
            elif line.startswith("SHELL_XTALK_CAL"):
                block["cal"] = _fields(line, "SHELL_XTALK_CAL")
            elif line.startswith("SHELL_XTALK_CASE"):
                block["cases"].append(_fields(line, "SHELL_XTALK_CASE"))
            # STATIC before STAT: "SHELL_XTALK_STATIC".startswith(
            # "SHELL_XTALK_STAT") is True, and the other order files every
            # static measurement as a curve statistic.
            elif line.startswith("SHELL_XTALK_STATIC"):
                block["statics"].append(_fields(line, "SHELL_XTALK_STATIC"))
            elif line.startswith("SHELL_XTALK_STAT"):
                block["stats"].append(_fields(line, "SHELL_XTALK_STAT"))
            elif line.startswith("SHELL_XTALK_SPAN"):
                block["span"] = _fields(line, "SHELL_XTALK_SPAN")
            elif line.startswith("SHELL_XTALK_G5"):
                block["g5"].append(_fields(line, "SHELL_XTALK_G5"))
            elif line.startswith("SHELL_XTALK_GATES"):
                block["gates"] = _fields(line, "SHELL_XTALK_GATES")
            elif line.startswith("SHELL_XTALK_END"):
                if not _is_complete(block):
                    block = None
                    continue
                return block
            elif line.startswith("SHELL_XTALK "):
                block["points"].append(_fields(line, "SHELL_XTALK "))
        except (ValueError, KeyError):
            # A line the serial read timeout cut in half, or one libDaisy's
            # own logger truncated and stamped with its "$$" overflow marker.
            # Drop the block and keep listening rather than crash on the
            # board's next breath; a later SHELL_XTALK_CFG still gets its own
            # chance.
            block = None
    return None


# --- what the block means -------------------------------------------------

def _points_by_case(block):
    out = {}
    for p in block["points"]:
        out.setdefault(p["case"], []).append(p)
    return out


def _case_by_id(block):
    return {c["case"]: c for c in block["cases"]}


def _row_case_by_victim(block, row):
    """The case id of `row`'s case for each victim, keyed (group, channel).

    Rows 1 and 2 are one case per victim by construction; if a table ever
    made them more than one, the last wins and the caller's arithmetic is
    still against a real curve rather than against nothing."""
    out = {}
    for c in block["cases"]:
        if c["skipped"] or c["kind"] == KIND_STATIC:
            continue
        if c["row"] == row:
            out[_victim_key(c)] = c["case"]
    return out


def _victim_order(block):
    """The victims in the order the firmware printed their G5 lines, which is
    kXtalkVictimTable's order. The index is what the metadata file's `g5` and
    `g6` scopes are keyed by, so both are keyed the same way."""
    seen = []
    for g in block["g5"]:
        key = _victim_key(g)
        if key not in seen:
            seen.append(key)
    return seen


def deltas(block):
    """delta(d) = mean_aggressor(d) - mean_control(d), per aggressor case.

    Against the CONTROL and not the silent curve, which is spec section 5's
    choice and not a detail: the control carries the same 16-bit shift and
    the same RCLK pulse as the aggressor case, so the difference isolates the
    bit change. Against the silent curve it would isolate the bit change plus
    the pulse plus the shift, which is three findings folded into one number.

    The silent curve is reported beside both, and its own two statistics
    (settled_mean_spread, widest_sample_band) are findings in their own
    right -- they are what settle-measured.md section 5 could not attribute.

    A point at which no repeat converted is excluded on either side. `n` is
    the SURVIVING repeat count, not kRepeats -- Task 5 re-purposed the field
    when it closed an undetected-timeout path -- so n < 64 is a mean over
    fewer real conversions and is perfectly good data, while n == 0 is not a
    reading at all and its mean is not a level. That is the same exclusion
    xtalk_probe.cpp's own G6 loop makes.
    """
    by_case = _points_by_case(block)
    cases = _case_by_id(block)
    control_of = _row_case_by_victim(block, ROW_CONTROL)
    rows = []
    for case_id in sorted(by_case):
        c = cases[case_id]
        if c["row"] in (ROW_FLOOR, ROW_CONTROL):
            continue
        control_id = control_of.get(_victim_key(c))
        if control_id is None:
            continue
        control = {p["d_ns"]: p for p in by_case.get(control_id, [])}
        for p in by_case[case_id]:
            cp = control.get(p["d_ns"])
            if cp is None or p["n"] <= 0 or cp["n"] <= 0:
                continue
            rows.append({"case": case_id,
                         "row": c["row"],
                         "victim_group": c["victim_group"],
                         "victim_ch": c["victim_ch"],
                         "r_src": c["r_src"],
                         "word_a": c["word_a"],
                         "word_b": c["word_b"],
                         "control_case": control_id,
                         "d_ns": p["d_ns"],
                         "n": p["n"],
                         "delta": p["mean"] - cp["mean"]})
    return rows


def verdicts(block):
    """The per-aggressor verdict, and which of the two bases it rests on.

    Spec section 5's criterion is |delta(d)| <= 8 at every grid point at or
    past scan_settle_ns -- the delay the shipping scan gives an address
    before it reads it, which the firmware prints and does not assume.

    On this firmware that delay is the audio block period, 2 ms, and the grid
    ends at 12.8 us. So no grid point reaches the boundary, and a reader that
    applied the criterion literally would compute a maximum over an empty set
    and report a pass. It does not. It falls back to the envelope over the
    WHOLE grid and labels the row verdict_basis=envelope.

    The fallback is not a weakening. The shipping ADC free-runs a circular
    DMA (settle-measured.md section 7), so a pot read lands at an arbitrary
    phase relative to any scan event, and the envelope over d is the worst
    case such a read can meet -- which is the argument spec section 6 makes
    in as many words. It is strictly more pessimistic than the criterion
    branch, and it is never labelled as a measurement taken at 2 ms.

    A run whose gates did not pass yields NO verdicts at all: the differences
    stop being interpretable, which is the whole point of G6.
    """
    if not block["gates"]["gates_ok"]:
        return []
    scan_settle_ns = block["cfg"]["scan_settle_ns"]
    by_case = OrderedDict()
    for row in deltas(block):
        by_case.setdefault(row["case"], []).append(row)

    out = []
    # Over the CASES, not over the cases that yielded a delta: an aggressor
    # whose points all timed out produces no delta row at all, and iterating
    # the delta table would drop it out of the verdict list with nothing
    # saying so. Fewer verdicts out than aggressors in is the same shape of
    # silent omission as a short table, and spec section 4 refuses that one
    # by name.
    for c in sorted(block["cases"], key=lambda e: e["case"]):
        if c["skipped"] or c["kind"] == KIND_STATIC:
            continue
        if c["row"] in (ROW_FLOOR, ROW_CONTROL):
            continue
        case_id = c["case"]
        rows = by_case.get(case_id, [])
        at_or_past = [row for row in rows if row["d_ns"] >= scan_settle_ns]
        basis = "criterion" if at_or_past else "envelope"
        considered = at_or_past if at_or_past else rows
        if not considered:
            # No point in this case yielded a difference at all. Not a pass:
            # it is a case that was never examined, and saying so is the
            # whole reason this function has a basis column.
            out.append({"case": case_id, "row": c["row"],
                        "victim_group": c["victim_group"],
                        "victim_ch": c["victim_ch"], "r_src": c["r_src"],
                        "word_a": c["word_a"], "word_b": c["word_b"],
                        "verdict_basis": "no-data", "worst_delta": -1,
                        "worst_d_ns": -1, "points_considered": 0,
                        "pass": False})
            continue
        worst = max(considered, key=lambda row: abs(row["delta"]))
        out.append({"case": case_id, "row": c["row"],
                    "victim_group": c["victim_group"],
                    "victim_ch": c["victim_ch"], "r_src": c["r_src"],
                    "word_a": c["word_a"], "word_b": c["word_b"],
                    "verdict_basis": basis,
                    "worst_delta": abs(worst["delta"]),
                    "worst_d_ns": worst["d_ns"],
                    "points_considered": len(considered),
                    "pass": abs(worst["delta"]) <= CRITERION_COUNTS})
    return out


def control_deltas(block):
    """|mean_control(d) - mean_silent(d)| per victim: G6's own quantity,
    recomputed here from the row-1 and row-2 curves the block already prints.

    The firmware prints only the bit (`g6=0|1`), so a capture cannot tell a
    healthy gate from a marginal one -- and on the 2026-09-18 image the gate
    came in at 6-7 counts against a bound of 8. Adding a field to the
    firmware would have cost a rebuild, destroyed the measured artifact and
    changed a format spec section 8 fixes; recomputing it here costs nothing,
    is per-victim rather than the single folded maximum `XtalkSummary` holds,
    and -- unlike a firmware field -- can be made to go red on a fixture.

    Same exclusions as the firmware's own loop (xtalk_probe.cpp:768-783): a
    point where either curve has no converted repeat is not compared.
    """
    by_case = _points_by_case(block)
    silent_of = _row_case_by_victim(block, ROW_FLOOR)
    control_of = _row_case_by_victim(block, ROW_CONTROL)
    g5_by_victim = {_victim_key(g): g for g in block["g5"]}
    cases = _case_by_id(block)
    g6 = block["gates"].get("g6", 0)

    out = []
    for index, key in enumerate(_victim_order(block)):
        silent_id = silent_of.get(key)
        control_id = control_of.get(key)
        worst, worst_d_ns, compared = -1, -1, 0
        if silent_id is not None and control_id is not None:
            silent = {p["d_ns"]: p for p in by_case.get(silent_id, [])}
            for p in by_case.get(control_id, []):
                sp = silent.get(p["d_ns"])
                if sp is None or p["n"] <= 0 or sp["n"] <= 0:
                    continue
                compared += 1
                magnitude = abs(p["mean"] - sp["mean"])
                if magnitude > worst:
                    worst, worst_d_ns = magnitude, p["d_ns"]
        within = compared > 0 and worst <= CRITERION_COUNTS
        r_src = None
        for case_id in (silent_id, control_id):
            if case_id is not None:
                r_src = cases[case_id]["r_src"]
                break
        out.append({"victim_index": index,
                    "victim_group": key[0],
                    "victim_ch": key[1],
                    "r_src": r_src,
                    "expect": g5_by_victim.get(key, {}).get("expect"),
                    "silent_case": silent_id,
                    "control_case": control_id,
                    "points_compared": compared,
                    "worst_control_delta": worst,
                    "worst_d_ns": worst_d_ns,
                    "within_bound": within,
                    # The firmware's g6 is the fold over all victims, so a
                    # printed 1 is a claim about EVERY victim and any victim
                    # out of bound contradicts it. A printed 0 is a claim
                    # about at least one, which no single victim can
                    # contradict on its own -- g6_disagreement() takes that
                    # direction at block level.
                    "agrees_with_g6": (not g6) or within})
    return out


def g6_disagreement(block):
    """A one-line description of this reader and the firmware disagreeing
    about G6, or None if they agree.

    G6 is the gate that licenses every aggressor delta in the run. If the
    firmware printed a pass over curves that do not support one, every number
    downstream of it is being read as a difference when it is not; if it
    printed a failure this arithmetic cannot find, one of the two is reading
    a different curve. Either way it is not a footnote."""
    rows = control_deltas(block)
    if not rows:
        return "G6 DISAGREEMENT: the block carries no victim to recompute G6 over"
    g6 = block["gates"].get("g6", 0)
    if g6:
        bad = [r for r in rows if not r["agrees_with_g6"]]
        if bad:
            return ("G6 DISAGREEMENT: the firmware printed g6=1, but "
                    + "; ".join(
                        "victim (%d,%d) recomputes to %d counts at d_ns=%d "
                        "over %d points"
                        % (r["victim_group"], r["victim_ch"],
                           r["worst_control_delta"], r["worst_d_ns"],
                           r["points_compared"]) for r in bad)
                    + " -- against a bound of %d" % CRITERION_COUNTS)
        return None
    if all(r["within_bound"] for r in rows):
        return ("G6 DISAGREEMENT: the firmware printed g6=0, but no victim's "
                "recomputed |control - silent| exceeds %d counts (worst %d) "
                "-- the bit and the printed curves do not describe the same "
                "run" % (CRITERION_COUNTS,
                         max(r["worst_control_delta"] for r in rows)))
    return None


# --- the two files --------------------------------------------------------

def format_csv(block):
    """One row per printed grid point, with its case's attributes and this
    reader's delta beside it. The delta column is empty where there is no
    aggressor to difference -- a Silent or a Control case, or a point one of
    the two curves has no converted repeat at."""
    delta_of = {(row["case"], row["d_ns"]): row["delta"]
                for row in deltas(block)}
    cases = _case_by_id(block)
    rows = [",".join(FIELDS)]
    for point in block["points"]:
        c = cases.get(point["case"], {})
        merged = dict(c)
        merged.update(point)
        delta = delta_of.get((point["case"], point["d_ns"]))
        merged["delta"] = "" if delta is None else delta
        rows.append(",".join(str(merged.get(f, "")) for f in FIELDS))
    return "\n".join(rows) + "\n"


def _meta_rows_of(scope, case, entry, skip=()):
    out = []
    for name, value in entry.items():
        if name in skip:
            continue
        # Booleans this reader computed are written as 1/0, like every flag
        # the firmware prints: a file that mixed `1` and `True` for the same
        # kind of answer would have to be read twice.
        if isinstance(value, bool):
            value = 1 if value else 0
        out.append("%s,%s,%s,%s" % (scope, case, name, value))
    return out


def format_meta_csv(block):
    """Everything in the block that is not a grid point, as
    `scope,case,key,value` rows, plus this reader's own per-victim G6
    recomputation.

    Field order within a scope is the order the firmware printed it, so a
    reader diffing two captures sees them line up. The case column is empty
    for block-level scopes rather than 0, which is a real case number; for
    the two per-victim scopes it is the victim's index in kXtalkVictimTable,
    which is the order the G5 lines arrive in."""
    rows = [",".join(META_FIELDS)]
    for scope, key in _META_BLOCK_SCOPES:
        entry = block.get(key)
        if entry is None:
            continue
        rows.extend(_meta_rows_of(scope, "", entry))

    order = _victim_order(block)
    index_of = {key: i for i, key in enumerate(order)}
    for g in block["g5"]:
        rows.extend(_meta_rows_of("g5", index_of[_victim_key(g)], g))
    for entry in control_deltas(block):
        rows.extend(_meta_rows_of("g6", entry["victim_index"], entry,
                                  skip=("victim_index",)))

    cases = _case_by_id(block)
    for c in sorted(block["cases"], key=lambda e: e["case"]):
        rows.extend(_meta_rows_of("case", c["case"], c, skip=("case",)))
    for s in sorted(block["stats"], key=lambda e: e["case"]):
        # See the module docstring: a Silent case's spread is the floor round
        # two's G8 reads; a control's or an aggressor's is not a floor.
        kind = cases.get(s["case"], {}).get("kind")
        scope = "silent" if kind == KIND_SILENT else "curve"
        rows.extend(_meta_rows_of(scope, s["case"], s, skip=("case",)))
    for s in sorted(block["statics"], key=lambda e: e["case"]):
        rows.extend(_meta_rows_of("static", s["case"], s, skip=("case",)))
    return "\n".join(rows) + "\n"


# --- the port -------------------------------------------------------------

def _read_one_block(port, limit):
    """Listen on `port` until a whole block has arrived, or `limit` seconds
    have passed.

    Re-parsing only at an _END marker, and resetting the accumulator at every
    _CFG, is not an optimisation for its own sake: a block is 2 984 lines
    here, parsing the accumulated list once per line is quadratic, and a host
    spending that time is a host that is not draining the port -- which is
    exactly the condition libDaisy's logger fuses lines under."""
    import serial

    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        lines = []
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if line.startswith("SHELL_XTALK_CFG"):
                lines = [line]
                continue
            lines.append(line)
            if line.startswith("SHELL_XTALK_END"):
                block = parse_block(lines)
                if block is not None:
                    return block
                print("discarded an incomplete block at its _END marker; "
                      "still listening", file=sys.stderr)
                lines = []
    return None


def main() -> int:
    # serial is imported inside _read_one_block(), not at module scope:
    # parse_block()/deltas()/verdicts() are the pure computation the guard
    # exercises, and that guard must not need pyserial installed to import
    # this module.
    if len(sys.argv) not in (2, 3, 4):
        raise SystemExit("usage: read_xtalk.py PORT [out.csv] [timeout_s]")
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0

    block = _read_one_block(port, limit)
    if block is None:
        print("no complete SHELL_XTALK block within %.0f s" % limit,
              file=sys.stderr)
        return 1

    csv = format_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
        meta_out = out + ".meta.csv"
        with open(meta_out, "w", encoding="utf-8", newline="") as fh:
            fh.write(format_meta_csv(block))
        print("wrote %s (%d grid points) and %s (%d metadata rows: cfg, rate, "
              "clk, cal, span, gates, G5, the G6 recomputation, every case, "
              "every curve statistic and the static means)"
              % (out, len(block["points"]), meta_out,
                 len(format_meta_csv(block).strip().split("\n")) - 1),
              file=sys.stderr)
    else:
        sys.stdout.write(csv)
        print("no output path given -- the block metadata (gates, G5, the G6 "
              "recomputation, the case table and the curve statistics) was "
              "not written; pass one to keep it", file=sys.stderr)

    cfg, cal = block["cfg"], block["cal"]
    grid_end_ns = (cfg["points"] - 1) * cfg["grid_ns"]
    print("git=%s adc_khz=%d lat_mean_ns=%d b0=%d timeouts=%d gates_ok=%d"
          % (cfg["git"], cfg["adc_khz"], cal["lat_mean_ns"], cal["b0"],
             cal["timeouts"], block["gates"]["gates_ok"]), file=sys.stderr)
    print("scan_settle_ns=%d (block_size=%d at sr_hz=%d), grid ends at %d ns"
          % (cfg["scan_settle_ns"], block["rate"]["block_size"],
             block["rate"]["sr_hz"], grid_end_ns), file=sys.stderr)

    # Not gates, and deliberately not folded into the exit code on their own:
    # these three are the firmware's HAL return codes and the four gates are
    # the spec's, so turning one of them into a fifth verdict here would be a
    # spec change made in the reader. They are loud on stderr instead,
    # because a run whose ADC was never initialised, never calibrated or had
    # a channel configuration rejected still prints plausible counts.
    for flag, what in (("init_ok", "HAL_ADC_Init"),
                       ("cal_ok", "HAL_ADCEx_Calibration_Start"),
                       ("cfg_ok", "HAL_ADC_ConfigChannel")):
        if block["gates"].get(flag) == 0:
            print("WARNING: %s=0 -- the firmware saw %s fail on this run; "
                  "every count above came from an ADC that is not set up the "
                  "way the block header says" % (flag, what), file=sys.stderr)

    # G6, recomputed. Printed for every run, pass or fail: the bit alone
    # cannot tell a healthy gate from one that passed with a single count of
    # margin, and this run's own margin is the first thing a reader of the
    # numbers below needs.
    for row in control_deltas(block):
        print("G6 victim (%d,%d) r_src=%d: |control - silent| worst %d counts "
              "at d_ns=%d over %d points (bound %d)"
              % (row["victim_group"], row["victim_ch"], row["r_src"],
                 row["worst_control_delta"], row["worst_d_ns"],
                 row["points_compared"], CRITERION_COUNTS), file=sys.stderr)
    disagreement = g6_disagreement(block)
    if disagreement is not None:
        print(disagreement, file=sys.stderr)
        print("The firmware and this reader do not agree about the gate that "
              "licenses every aggressor delta in this run. No verdict below "
              "may be quoted until that is resolved.", file=sys.stderr)

    if not block["gates"]["gates_ok"]:
        failed = [name for key, name in _GATE_NAMES.items()
                  if not block["gates"][key]]
        print("GATES FAILED (%s) -- no crosstalk verdict from this run may be "
              "quoted" % ", ".join(failed), file=sys.stderr)
        if not block["gates"]["g6"]:
            print("G6 in particular may be a REAL RESULT and not a broken "
                  "instrument: a latch pulse with no bit change already moved "
                  "a reading past the criterion. The control curves are in "
                  "the CSV and they are this run's finding.", file=sys.stderr)
        return 1

    rows = verdicts(block)
    if not rows:
        print("no aggressor case yielded a verdict", file=sys.stderr)
        return 1

    # One line per spec section 4 row per victim, taking the worst of that
    # row's cases -- which is how the spec's table reads and how a report
    # quotes it. A failing case is named individually underneath.
    basis = sorted({row["verdict_basis"] for row in rows})
    print("verdict_basis=%s, criterion |delta| <= %d counts"
          % ("/".join(basis), CRITERION_COUNTS), file=sys.stderr)
    worst_of = OrderedDict()
    for row in rows:
        key = (row["row"], row["victim_group"], row["victim_ch"])
        if key not in worst_of or row["worst_delta"] > worst_of[key]["worst_delta"]:
            worst_of[key] = row
    for key in sorted(worst_of):
        row = worst_of[key]
        print("row=%-2d victim (%d,%d) r_src=%-4d worst_delta=%-4d at d_ns=%-6d "
              "basis=%-9s %s"
              % (row["row"], row["victim_group"], row["victim_ch"],
                 row["r_src"], row["worst_delta"], row["worst_d_ns"],
                 row["verdict_basis"], "PASS" if row["pass"] else "FAIL"),
              file=sys.stderr)

    failed_cases = [row for row in rows if not row["pass"]]
    if failed_cases:
        for row in failed_cases:
            print("FAILED case=%d row=%d victim (%d,%d) word_a=%d word_b=%d "
                  "worst_delta=%d at d_ns=%d (%s over %d points)"
                  % (row["case"], row["row"], row["victim_group"],
                     row["victim_ch"], row["word_a"], row["word_b"],
                     row["worst_delta"], row["worst_d_ns"],
                     row["verdict_basis"], row["points_considered"]),
                  file=sys.stderr)
        return 1
    if disagreement is not None:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
