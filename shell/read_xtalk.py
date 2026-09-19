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
(_CASE, points, _STAT) for a grid case -- plus _G6 behind the _STAT when that
case is the victim's control -- or (_CASE, _STATIC) for a static one, or
_CASE alone for a skipped one, or (_CASE, _NOVICTIM) for a case naming no
victim; then _SPAN, one _G5 per victim, _GATES, _END.
(SHELL_XTALK_WARMUP is printed once at boot, outside the repeating block, and
is simply not one of the lines this parser looks for.)

**Three lines and two fields arrived with Task 7's round-one fixes.** A block
that carries them is read strictly; a block whose `_SPAN` line predates them
is read as a PRE-FIX BLOCK -- accepted, labelled in the metadata, and told
plainly what its G5 is missing. Refusing an old capture would not be the more
rigorous choice: every published number in
`docs/hardware/crosstalk-measured.md` came from a pre-fix capture, and a
reader that cannot re-read that capture destroys the ability to check the
earlier work. The `_G6` and `_NOVICTIM` lines are not in that position -- the
firmware emits them from paths every block runs, so their absence from a
block that has `_SPAN`'s new fields is transit loss and is refused.

- `_SPAN` carries `lost=` and `n_min=`: the repeats the four 0 ohm tie reads
  lost, and the smallest surviving count on any one tie. The firmware refuses
  the span outright when either says a repeat went missing, because G5 is
  judged against that span and -- unlike G6 -- this reader does not recompute
  it. Before the fix a timed-out repeat on an AGND tie was completely
  invisible: the true reading is already ~0, so a zeroed repeat looked
  identical to a good one and could pass a broken tie as the yardstick.
- `_G6` carries `pairs=` and `compared=` per control case: how many
  control-vs-silent point pairs actually survived, and whether that was enough
  to call the victim compared. The firmware's gate used to count VICTIMS here
  and not comparisons, so a victim whose every pair was excluded still counted
  and handed G6 a worst delta of 0 -- a perfect score computed from nothing.
- `_NOVICTIM` names a case whose (group, channel) is in no victim table row.
  That used to print nothing whatsoever; the block came up short, this reader
  discarded it, and the log said only that a block was short.

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


def min_control_pairs(block):
    """How many surviving control-vs-silent pairs a victim needs before its
    G6 recomputation below may be called a comparison: a majority of the grid.

    THE SAME RULE AS THE FIRMWARE'S kMinControlPairs (xtalk_probe.cpp), and
    derived from the block's own `points` field rather than copying the 33
    that rule yields for a 65-point grid -- a reader carrying its own constant
    would refuse or accept the wrong blocks the day the grid changes length,
    which is the mistake _is_complete()'s rule 1 already avoids for the case
    count.

    Why both sides need it: G6 is an envelope over d, not a statistic, so a
    few surviving pairs can all sit in a quiet stretch of the transient and
    report 0 counts. `compared > 0` was the old test on this side and it has
    exactly the weakness the firmware's `++controls_compared` had -- it cannot
    tell a victim compared at 2 of 65 points from one compared at 65. The
    firmware's own argument for the number is at kMinControlPairs and is not
    repeated here; it is a majority because below half, "over the grid" stops
    being an honest description of what was measured."""
    return block["cfg"]["points"] // 2 + 1

# Spec section 4's table rows that are not aggressors: row 1 is the floor and
# row 2 is the control every delta is measured against. Row 8 is Static and
# has no grid at all. Everything else -- 3, 4, 5, 6, 7, 9 and 10 -- gets a
# verdict, row 10 included: its aggressor is the probe's own USB-CDC traffic,
# which spec section 2 lists as an aggressor like any other.
#
# Row 10 is Silent, though -- no chain access, no bit change, no event at
# all -- so its "delta" is not measuring what deltas()'s docstring below
# claims for the other rows. See deltas()'s docstring for what it actually
# isolates.
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
            "span": None, "g5": [], "g6": [], "novictim": [], "cases": [],
            "points": [], "stats": [], "statics": [], "gates": None}


def _victim_key(entry):
    return (entry["victim_group"], entry["victim_ch"])


# What a pre-fix block's G5 is missing, in the file where its numbers are.
# COMMA-FREE BY NECESSITY: the metadata file is four bare columns and nothing
# quotes a value.
_SPAN_G5_RISK = ("G5 rests on a span whose four tie reads could lose a repeat "
                 "unnoticed -- on an AGND tie a timed-out repeat reads 0 and "
                 "so does a good one so a broken tie can pass as the "
                 "yardstick -- closed in firmware by task 7 step 1")


def span_is_prefix(block):
    """True when the `_SPAN` line carries no tie-loss fields: a capture from
    an image whose four 0 ohm tie reads still went through
    `probe_adc::mean_of_repeats()`, which folds a timed-out repeat's 0 into
    the mean with nothing in the return value to say so.

    Such a block is read, not refused -- see `_is_complete()` rule 0a -- and
    everything it says is as good as it ever was EXCEPT the span, and
    therefore G5. `lost=0` was never printed by that image, so "no loss
    reported" there does not mean "no loss": on an AGND tie, whose true
    reading is already ~0, a zeroed repeat is indistinguishable from a good
    one and could pass a broken tie as the yardstick every victim is judged
    against. G5 is the one gate this reader does not recompute, so nothing
    further down catches it either. That is the whole cost, and it is stated
    in the metadata and on stderr rather than implied by a refusal."""
    return "lost" not in block["span"]


def _is_complete(block):
    """Every completeness check the block format calls for at
    SHELL_XTALK_END. Anything short of this must be dropped rather than
    returned -- see the module docstring. Each rule exists because a
    different corruption gets past the other four."""
    # 0. Every block-level line the module docstring lists as part of the
    #    block. _CLK, _CAL and _SPAN were missing from this check while the
    #    docstring claimed them: a block that had lost _CAL was ACCEPTED,
    #    both files were written, and main() then raised a TypeError on
    #    block["cal"] with the output already on disk. A lost _SPAN was
    #    quieter and worse -- accepted, and the metadata file silently
    #    without the span G5 was judged against, which is the one row this
    #    reader's own guard asserts must be there.
    for key in ("cfg", "rate", "clk", "cal", "span", "gates"):
        if block[key] is None:
            return False
    # 0a. _SPAN's two Task 7 fields: BOTH, or NEITHER.
    #
    #     NEITHER is a pre-fix block -- an image from before the four tie
    #     reads got their timeout exclusion -- and it is ACCEPTED, labelled
    #     and warned about, not refused. Refusing it would not be more
    #     rigorous, it would only destroy the ability to check the earlier
    #     work: every published number in docs/hardware/crosstalk-measured.md
    #     came from a pre-fix capture, and a reader that cannot re-read that
    #     capture makes the document unreproducible. What such a block gives
    #     up is stated where its numbers are -- see span_is_prefix() and the
    #     span_format / g5_risk rows in the metadata file -- rather than
    #     hidden behind a refusal.
    #
    #     ONE of the two is neither shape. It is a line that arrived damaged,
    #     which is what the rest of these rules exist to catch, and no image
    #     has ever printed it: refused.
    present = [k for k in ("lost", "n_min") if k in block["span"]]
    if len(present) == 1:
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
    # 2. Every grid case, and only a grid case, has exactly the grid the
    #    firmware said it would sweep. PER CASE: a serial timeout loses one
    #    case's row while its neighbours keep theirs, and a total count
    #    reports that as complete.
    #
    #    BY d_ns AND NOT BY COUNT: counting alone accepts a case with two
    #    d_ns=0 rows and no d_ns=200 -- the count is right and the grid is
    #    not -- after which deltas() differences one point fewer than it
    #    reports and nothing says so. The grid is the firmware's own
    #    (cfg's `points` and `grid_ns`), not this reader's.
    grid = [i * block["cfg"]["grid_ns"] for i in range(block["cfg"]["points"])]
    grid_cases = {c["case"] for c in block["cases"]
                  if not c["skipped"] and c["kind"] != KIND_STATIC}
    want = Counter((case, d) for case in grid_cases for d in grid)
    if Counter((p["case"], p["d_ns"]) for p in block["points"]) != want:
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
    if {s["case"] for s in block["stats"]} != grid_cases:
        return False
    # 5. Every victim has its addressing evidence. None of rules 1-4 can see
    #    a lost SHELL_XTALK_G5 line: every case, point, static and statistic
    #    is still there, and what is missing is the proof that the mux was
    #    where the table says it was -- without which every delta is a
    #    measurement of nothing (spec section 7, G5).
    if len({_victim_key(g) for g in block["g5"]}) != block["rate"]["victims"]:
        return False
    # 6. Every victim that HAS a control case has its G6 evidence, and no
    #    other victim does. Rules 1-5 cannot see a lost SHELL_XTALK_G6 line
    #    either: every case, point, static, statistic and G5 line is still
    #    there, and what is gone is the count of point pairs the gate rests
    #    on -- the number that tells a victim compared at 65 points from one
    #    compared at 2, which the gate itself could not tell apart before
    #    Task 7 and this reader would silently stop cross-checking.
    #
    #    AGAINST THE CONTROL CASES AND NOT AGAINST `victims=`, which rule 5
    #    uses: the firmware prints this line from its row-2 branch, so a
    #    victim with no control case has no G6 line and is not missing one.
    #    A victim that appears only in a skipped case is exactly that shape,
    #    and it must still parse -- it is reported, per-victim, as compared
    #    at no points.
    want_g6 = {_victim_key(c) for c in block["cases"]
               if not c["skipped"] and c["kind"] != KIND_STATIC
               and c["row"] == ROW_CONTROL}
    if {_victim_key(g) for g in block["g6"]} != want_g6:
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
            elif line.startswith("SHELL_XTALK_G6"):
                block["g6"].append(_fields(line, "SHELL_XTALK_G6"))
            elif line.startswith("SHELL_XTALK_NOVICTIM"):
                block["novictim"].append(_fields(line, "SHELL_XTALK_NOVICTIM"))
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

    That "isolates the bit change" reading holds for rows 3-7: each of them
    changes exactly one thing relative to the control. It does NOT hold for
    row 9 or row 10.

    Row 9 (ShiftOnly) is spec section 10's own comparison, deliberately: 16
    bits shifted with RCLK never pulsed, differenced against the control to
    show the shift alone, without the latch. That one is load-bearing and
    correct as documented.

    Row 10 is Silent -- no chain access, no bit change, no event at all --
    so delta(row10) is not "isolating" anything: it is the NEGATED
    control-vs-silent offset (mean_control(d) - mean_silent(d), i.e. -1 *
    what G6 computes) plus whatever printing inside the grid does. The 2026-
    09-18 capture shows this directly: row 10 reports worst_delta=6 on both
    REF_A and REF_C, matching G6's own recomputed offset for those victims
    (6-7 counts and 5-6 counts respectively -- see xtalk.csv.meta.csv scope
    g6), while task-5-report.md measured the printing effect itself at -3 to
    +5 counts with no consistent sign. A "row 10 PASS at 6 counts" today is
    G6's offset wearing row 10's label, and a worse day could push it past
    CRITERION_COUNTS and report a USB-CDC failure that is really the
    control's own offset from silence -- or a printing effect that happens
    to cancel the offset could report 0 and hide a real one. Spec section 5
    mandates this arithmetic for row 10 as for every other row, so the
    computation below is unchanged; this is a reading caveat, not a bug.

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

    THE BASIS IS A PROPERTY OF THE GRID, NOT OF THE ROWS THAT SURVIVED. It
    is (points - 1) * grid_ns against scan_settle_ns, both the firmware's own
    numbers, which is the condition the plan states. Deciding it from the
    surviving delta rows instead reads the same in every case but one, and
    that one is the dangerous one: a boundary that IS inside the grid, whose
    only points at or past it were never examined, would fall through to the
    envelope, be labelled "over the WHOLE grid", and PASS. The points that
    count being unusable is not an envelope. It is no data, and it is a
    refusal.

    A run whose gates did not pass yields NO verdicts at all: the differences
    stop being interpretable, which is the whole point of G6.
    """
    if not block["gates"]["gates_ok"]:
        return []
    scan_settle_ns = block["cfg"]["scan_settle_ns"]
    grid_end_ns = (block["cfg"]["points"] - 1) * block["cfg"]["grid_ns"]
    basis = "criterion" if scan_settle_ns <= grid_end_ns else "envelope"
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
        if basis == "criterion":
            considered = [row for row in rows if row["d_ns"] >= scan_settle_ns]
        else:
            considered = rows
        if not considered:
            # Either no point in this case yielded a difference at all, or
            # the boundary is inside the grid and every point at or past it
            # was unusable. Not a pass, and not an envelope either: it is a
            # case that was never examined, and saying so is the whole
            # reason this function has a basis column.
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
    g6_by_victim = {_victim_key(g): g for g in block["g6"]}
    cases = _case_by_id(block)
    g6 = block["gates"].get("g6", 0)
    min_pairs = min_control_pairs(block)

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
        # `compared >= min_pairs`, NOT `compared > 0`, and Task 7 changed it.
        # The old test is the same defect the firmware's own gate carried:
        # it cannot tell a victim compared at 2 of 65 points from one
        # compared at 65, and G6 is an envelope over d, so a couple of
        # surviving pairs can all sit in a quiet stretch and report 0
        # counts -- a pass out of almost no data. See min_control_pairs().
        within = compared >= min_pairs and worst <= CRITERION_COUNTS
        # What the FIRMWARE counted for the same victim, carried beside this
        # reader's own recomputation rather than into a scope of its own: the
        # two run the same exclusion over the same curves, so they must agree
        # exactly, and a mismatch means one of them is not reading the curves
        # the other is. Absent only if the block has no G6 line for this
        # victim, which _is_complete() rule 6 already refuses.
        fw = g6_by_victim.get(key, {})
        fw_pairs = fw.get("pairs", -1)
        # Off ANY case carrying this victim, skipped and Static included, not
        # only its floor and control cases: a victim whose row-1 and row-2
        # cases were both skipped would otherwise leave this None, and
        # main() formats it with %d. -1 only if the victim has a G5 line and
        # no case at all, which is a block this reader should still print
        # rather than crash on.
        r_src = -1
        for c in block["cases"]:
            if _victim_key(c) == key:
                r_src = c["r_src"]
                break
        out.append({"victim_index": index,
                    "victim_group": key[0],
                    "victim_ch": key[1],
                    "r_src": r_src,
                    "expect": g5_by_victim.get(key, {}).get("expect"),
                    "silent_case": silent_id,
                    "control_case": control_id,
                    "points_compared": compared,
                    "min_points_required": min_pairs,
                    "fw_pairs": fw_pairs,
                    "fw_worst": fw.get("worst", -1),
                    "fw_compared": fw.get("compared", -1),
                    "fw_pairs_agree": fw_pairs == compared,
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
        # `span_format` on EVERY block and not only the pre-fix ones. A label
        # that appears only in the bad case is a label a reader has to know
        # to look for, and -- the reason it is written both ways -- one that
        # quietly stopped being written would be indistinguishable from a
        # good block. Both values are asserted in test_read_xtalk.py, so the
        # label cannot rot in either direction.
        if scope == "span":
            if span_is_prefix(block):
                rows.append("span,,span_format,pre-fix")
                rows.append("span,,g5_risk,%s" % _SPAN_G5_RISK)
            else:
                rows.append("span,,span_format,task7-exclusion")

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
    # A no-victim case is a CASE line with skipped=1 and nothing else, which
    # is indistinguishable in the metadata from an rv4 skip. Its own scope is
    # the only thing that tells the two apart in the file, and they are
    # different events: one is a configuration, the other is a defect in the
    # firmware's tables.
    for nv in sorted(block["novictim"], key=lambda e: e["case"]):
        rows.extend(_meta_rows_of("novictim", nv["case"], nv, skip=("case",)))
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
        meta = format_meta_csv(block)
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
        meta_out = out + ".meta.csv"
        with open(meta_out, "w", encoding="utf-8", newline="") as fh:
            fh.write(meta)
        print("wrote %s (%d grid points) and %s (%d metadata rows: cfg, rate, "
              "clk, cal, span, gates, G5, the G6 recomputation, every case, "
              "every curve statistic and the static means)"
              % (out, len(block["points"]), meta_out,
                 len(meta.strip().split("\n")) - 1),
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

    # The tie reads behind the span, which G5 is judged against and which
    # nothing here recomputes. The firmware refuses the span itself when a
    # repeat went missing; this says so in words, because `valid=0` on its
    # own does not distinguish a collapsed rail from an ADC that stopped
    # answering on a 0 ohm tie, and those call for different next steps.
    span = block["span"]
    if span_is_prefix(block):
        print("PRE-FIX BLOCK: this capture's _SPAN line has no lost= or "
              "n_min= field, so it came from an image whose four 0 ohm tie "
              "reads folded a timed-out repeat's 0 into the mean. %s "
              "Everything else in this block is unaffected."
              % _SPAN_G5_RISK, file=sys.stderr)
    elif span["lost"]:
        print("WARNING: the span pass lost %d of %d tie repeats (worst tie "
              "converted %d) -- the four 0 ohm ties are the quietest channels "
              "on the board, so this is the instrument failing to respond and "
              "not a difficult node. The span is refused (valid=%d) and G5 "
              "rests on nothing this run."
              % (span["lost"], 4 * block["cfg"]["repeats"], span["n_min"],
                 span["valid"]), file=sys.stderr)

    # G6, recomputed. Printed for every run, pass or fail: the bit alone
    # cannot tell a healthy gate from one that passed with a single count of
    # margin, and this run's own margin is the first thing a reader of the
    # numbers below needs.
    for row in control_deltas(block):
        # points_compared AND the minimum it had to clear, on every line: a
        # gate that rested on 2 of 65 point pairs and one that rested on 65
        # are not the same claim, and until Task 7 neither the firmware nor
        # this reader could tell them apart.
        print("G6 victim (%d,%d) r_src=%d: |control - silent| worst %d counts "
              "at d_ns=%d over %d points (need %d, bound %d)"
              % (row["victim_group"], row["victim_ch"], row["r_src"],
                 row["worst_control_delta"], row["worst_d_ns"],
                 row["points_compared"], row["min_points_required"],
                 CRITERION_COUNTS), file=sys.stderr)
        if not row["fw_pairs_agree"]:
            print("WARNING: the firmware counted %d surviving pairs for "
                  "victim (%d,%d) and this reader counts %d. Both apply the "
                  "same exclusion to the same two curves, so they cannot "
                  "honestly differ -- one of them is not reading the curves "
                  "the other is."
                  % (row["fw_pairs"], row["victim_group"], row["victim_ch"],
                     row["points_compared"]), file=sys.stderr)

    # A case whose (group, channel) is in no victim table row. It is never a
    # transport fault -- the firmware prints this line deliberately -- so it
    # is a defect in kXtalkPlan or kXtalkVictimTable, and the case was not
    # measured: it carries skipped=1, so verdicts() drops it, and the run
    # reports fewer aggressor verdicts than the table has aggressors with
    # nothing else saying why. Fewer verdicts out than aggressors in is the
    # silent omission spec section 4 refuses by name, so this run's verdicts
    # are not quotable and the exit code says so.
    for nv in block["novictim"]:
        print("INSTRUMENT DEFECT: case %d (row %d) names victim (%d,%d), "
              "which is in no row of the victim table. The case was skipped "
              "and never measured. This is a fault in the firmware's own "
              "tables, not in the board or the capture."
              % (nv["case"], nv["row"], nv["victim_group"], nv["victim_ch"]),
              file=sys.stderr)

    disagreement = g6_disagreement(block)
    if disagreement is not None:
        print(disagreement, file=sys.stderr)
        print("The firmware and this reader do not agree about the gate that "
              "licenses every aggressor delta in this run. No verdict below "
              "may be quoted until that is resolved.", file=sys.stderr)

    if block["novictim"]:
        print("No crosstalk verdict from this run may be quoted: %d case(s) "
              "named a victim the table does not have."
              % len(block["novictim"]), file=sys.stderr)
        return 1

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
        # points_considered on EVERY line, not only the failed ones: a
        # verdict taken over three surviving points and one taken over 65
        # are not the same claim, and a line that does not say which leaves
        # the reader to assume the grid was whole.
        print("row=%-2d victim (%d,%d) r_src=%-4d worst_delta=%-4d at d_ns=%-6d "
              "basis=%-9s pts=%-3d %s"
              % (row["row"], row["victim_group"], row["victim_ch"],
                 row["r_src"], row["worst_delta"], row["worst_d_ns"],
                 row["verdict_basis"], row["points_considered"],
                 "PASS" if row["pass"] else "FAIL"),
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
