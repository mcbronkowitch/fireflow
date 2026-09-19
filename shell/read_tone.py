"""Reads one SHELL_TONE_CFG..SHELL_TONE_END block from the board's USB-CDC
port, writes it out as two CSV files, and computes the verdict.

`out.csv` carries the tone phase points, one row per measured point, joined to
the attributes of the case that produced it (level, frequency, level in dBFS,
victim identity, `r_src`, `below_corner`) and to this reader's own `delta`.
Everything else the block says -- the configuration, the clock spans, the
calibration pass, the span G5 was judged against, the per-victim addressing
evidence, every case line, every level measurement and its statistic, the
boot-virgin floor, the measured acquisition window and the whole window sweep,
the gates and the health counters -- goes to `out.csv.meta.csv` beside it, as
`scope,case,key,value` rows.

**THE VERDICT IS THIS READER'S, NOT THE FIRMWARE'S.** The firmware prints
curves and gates; `deltas()`, `delta_pp()`, `g8()` and `verdicts()` below are
the computation. `SHELL_TONE_GATES` carries `g8=-1` as a literal sentinel and
has never carried anything else -- G8 is host-side by construction, because
its baseline is not available to the firmware.

Call:
    python read_tone.py PORT out.csv XTALK_META.csv [timeout_s]

XTALK_META.csv is round one's `xtalk.csv.meta.csv`, and it is NOT optional.
Under the G8 split below its half of the comparison is report-only, which
makes it tempting to let the argument default -- but an optional comparison
is one that silently never happens, and round one's silent block is the only
independent measurement of this node that this reader ever sees. A baseline
problem that moved the whole floor would otherwise produce tone results that
are plausible in every digit with nothing here able to notice. So the file
stays mandatory and this reader refuses to run without it.

Default timeout: 600 s. A block is dominated by the phase waits and not by
the conversions -- every repeat waits for the next crossing of its target
phase, and at 100 Hz that is up to 10 ms each. The measured block duration on
this board is `block_ms=170657` (`SHELL_TONE_HEALTH`, 2026-09-19 capture),
i.e. about 171 s; 600 s is that with room for the window sweep and for a
host that starts listening mid-block and has to wait for the next _CFG.

The block format below is `tone_probe.cpp`'s `hw.PrintLine()` calls, as spec
section 8 of `docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md`
records them after the Task 5 audit. In print order:

    SHELL_TONE_CFG     adc_khz repeats phase_points block_size sr rv4 git
    SHELL_TONE_CLK     span_short_cyc span_long_cyc smp_short_tenths smp_long_tenths
    SHELL_TONE_CAL     lat_mean_ns lat_min_ns lat_max_ns b0 timeouts
    SHELL_TONE_LEVEL   case level victim_group victim_ch r_src n mean min max audio_virgin
    SHELL_TONE_STAT    case settled_mean_spread widest_sample_band
    SHELL_TONE_CASE    case level f_hz dbfs victim_group victim_ch r_src below_corner
    SHELL_TONE         case phase_idx n mean min max
    SHELL_TONE_SPAN    zero rail hi_spread lo_spread valid
    SHELL_TONE_G5      victim_group victim_ch expect mean ok
    SHELL_TONE_WINDOW  window_ns nominal_ns grid_end_ns fits
    SHELL_TONE_WINCASE case xtalk_case victim_group victim_ch r_src word_a word_b codec
    SHELL_TONE_WIN     case d_before_end_ns n mean min max
    SHELL_TONE_GATES   g2 g4 g5 g7 g8 gates_ok
    SHELL_TONE_HEALTH  missed_blocks phase_timeouts win_timeouts block_ms
    SHELL_TONE_END

Two things about that format a reader gets wrong exactly once:

- **`case` is only meaningful with its tag.** The window sweep numbers its
  cases from 0 in its own namespace, so `case=0` exists under
  `SHELL_TONE_WINCASE` and under `SHELL_TONE_LEVEL` and means two different
  measurements. Everything here keys on the pair.
- **The boot-virgin floor lines are `SHELL_TONE_LEVEL` too**, at `case=-1`
  through `case=-5`, with `audio_virgin=1`. They are measured once per boot
  before any `StartAudio()` anywhere in the image, then cached and re-emitted
  verbatim in every block. Every `case >= 0` level line carries
  `audio_virgin=0` and is a different quantity: a per-block Stopped level is
  `StopAudio()` on a codec an earlier block already started, which is not
  "never started".
"""
import sys
import time
from collections import Counter, OrderedDict

# --- what the numbers are judged against ----------------------------------

# Spec section 4's criterion: `delta_pp <= 8` counts on every 5150 ohm victim
# at every table row. It is a spec decision and is not adjusted here for any
# reason, including the floor note below.
CRITERION_COUNTS = 8

# The criterion gates the 5150 ohm victims only. The 650 ohm victim and the
# two 150 ohm ties are the ATTRIBUTION AXIS -- they say whether a residue
# scales with source impedance, which is how spec section 10 decides whether
# it is in the node at all -- and they are reported beside the verdict and
# never gated on. A reader that gated them would fail a run for a result that
# is doing its job.
GATED_R_SRC = 5150

# G8's bound, both halves: 4 counts.
G8_BOUND_COUNTS = 4

# --- G8(b)'s baseline: this campaign's own boot-virgin floor ---------------
#
# PROVENANCE. Each figure is `settled_mean_spread` on a `SHELL_TONE_STAT` line
# at a NEGATIVE case number -- `case=-1` through `case=-5`, one per victim, the
# lines whose paired `SHELL_TONE_LEVEL` carries `audio_virgin=1`. That is the
# floor measured once per boot before any `StartAudio()` call anywhere in the
# image. The four boots, in order, and the capture each came from
# (all in `.superpowers/sdd/2026-09-18-coupon-codec-tone-probe/`):
#
#   boot 1  task-3-board-capture-1c15987.txt
#   boot 2  task-4-board-capture-438fd51.txt
#   boot 3  task-4-board-capture-c5631f4.txt
#   boot 4  task-5-board-capture.txt
#
# THE BOUND IS THE RANGE, NOT A CENTRAL VALUE. Four points do not establish a
# central value, and the midpoint of 14..17 is a number no boot ever produced.
# A reading passes when it lies within G8_BOUND_COUNTS of the OBSERVED RANGE,
# i.e. inside [min(boots) - 4, max(boots) + 4]. That uses only what was
# measured and still goes red: REF_A at 22 or at 9 fails.
#
# This is a LIKE-FOR-LIKE comparison: both sides are this image's
# `measure_level()`, 65 sub-measurements of 64 conversions each, taken under
# one identical condition with nothing varied between them. See g8()'s
# docstring for why the round-one comparison is not.
BOOT_VIRGIN_FLOOR = OrderedDict((
    ((0, 8),  {"name": "REF_A",  "r_src": 5150, "boots": (17, 16, 17, 14)}),
    ((1, 6),  {"name": "REF_C",  "r_src": 5150, "boots": (3, 3, 4, 4)}),
    ((0, 9),  {"name": "REF_B",  "r_src": 650,  "boots": (2, 3, 4, 5)}),
    ((0, 10), {"name": "R_SP10", "r_src": 150,  "boots": (1, 1, 0, 0)}),
    ((1, 3),  {"name": "R_LO3",  "r_src": 150,  "boots": (0, 0, 0, 0)}),
))

# Why G8's round-one half carries no bound. Printed with the report so the
# reason travels with the numbers rather than living in a review thread.
G8_NOT_LIKE_FOR_LIKE = (
    "NOT LIKE-FOR-LIKE: round one's settled_mean_spread "
    "(xtalk_probe.cpp measure_silent_point(d_ns):250-266, reduced in "
    "reduce_and_emit():440-468) is a peak-to-peak across 65 grid points that "
    "carry 65 DIFFERENT pre-conversion delays -- each repeat spins "
    "park_cycles + d_cycles first, and d runs 0..12800 ns. This image's "
    "(tone_probe.cpp measure_level():533-601) has no spin and varies nothing: "
    "its 65 points are 65x64 calls to sample_now() under one identical "
    "condition, so it is repeat-to-repeat noise on the mean. Same count "
    "(65x64), different content. tone_plan.h:82-85 states the requirement in "
    "its own words -- the two must be the same shape 'or the 4-count bound "
    "compares two differently-shaped spreads and means nothing' -- and it "
    "matched the count and missed the content. Reported only; never a verdict."
)

# The two files' columns.
FIELDS = ("case", "level", "f_hz", "dbfs", "victim_group", "victim_ch",
          "r_src", "below_corner", "phase_idx", "n", "mean", "min", "max",
          "delta")

META_FIELDS = ("scope", "case", "key", "value")

# Block-level lines, one per block; the case column stays empty for these.
_META_BLOCK_SCOPES = (("cfg", "cfg"), ("clk", "clk"), ("cal", "cal"),
                      ("span", "span"), ("window", "window"),
                      ("gates", "gates"), ("health", "health"))

_GATE_NAMES = OrderedDict((("g2", "instrument floor"),
                           ("g4", "instrument jitter"),
                           ("g5", "victim addressing"),
                           ("g7", "callback health")))

LEVEL_STOPPED = 0
LEVEL_RUNNING_SILENT = 1
LEVEL_TONE = 2

# The one field in this format whose value is not an integer. Everything else
# is int()d, which is what makes a "$$"-truncated field raise instead of
# quietly parsing as a string.
_STR_FIELDS = ("git",)


# --- parsing --------------------------------------------------------------

def _fields(line, prefix):
    # libDaisy's logger stamps an overflowing line with "$$" and fuses what
    # follows onto it. int() catches that whenever the cut lands inside a
    # value, but a cut that happens to land on a space would otherwise parse
    # as a valid line with extra tokens, so the marker is refused outright.
    if "$$" in line:
        raise ValueError("libDaisy overflow marker")
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = value if key in _STR_FIELDS else int(value)
    return out


def _new_block():
    return {"cfg": None, "clk": None, "cal": None, "span": None,
            "window": None, "gates": None, "health": None,
            "g5": [], "cases": [], "levels": [], "stats": [], "points": [],
            "wincases": [], "win_points": []}


def _victim_key(entry):
    return (entry["victim_group"], entry["victim_ch"])


def virgin_levels(block):
    """The boot-virgin floor measurements: the `SHELL_TONE_LEVEL` lines with
    `audio_virgin=1`. G8 reads these and only these."""
    return [lv for lv in block["levels"] if lv["audio_virgin"] == 1]


def _live_levels(block):
    return [lv for lv in block["levels"] if lv["audio_virgin"] == 0]


def _stat_by_case(block):
    return {s["case"]: s for s in block["stats"]}


def _case_by_id(block):
    return {c["case"]: c for c in block["cases"]}


def _victim_order(block):
    """The victims in the order the firmware printed their G5 lines, which is
    kXtalkVictimTable's order."""
    seen = []
    for g in block["g5"]:
        key = _victim_key(g)
        if key not in seen:
            seen.append(key)
    return seen


def _is_complete(block):
    """Every completeness check the block format calls for at
    SHELL_TONE_END. Anything short of this is dropped rather than returned:
    a partial block whose missing piece is a gate or a floor produces numbers
    that are plausible in every digit.

    Each rule exists because a different corruption gets past the others."""
    # 0. Every block-level line spec section 8 lists. _HEALTH and _WINDOW are
    #    in here because they are new in Tasks 4 and 5: missed_blocks moved
    #    off _GATES onto _HEALTH, so a block that lost _HEALTH has lost the
    #    counter verdicts() refuses on, and a block that lost _WINDOW has
    #    lost the only statement of whether the window sweep ran at all.
    for key in ("cfg", "clk", "cal", "span", "window", "gates", "health"):
        if block[key] is None:
            return False

    # 1. The boot-virgin floor: one line per victim, numbered -1 downwards
    #    with no gap. This is the set every later rule counts victims from,
    #    and it is what G8 reads.
    virgins = virgin_levels(block)
    if not virgins:
        return False
    if sorted(v["case"] for v in virgins) != list(
            range(-len(virgins), 0)):
        return False
    virgin_victims = {_victim_key(v) for v in virgins}
    if len(virgin_victims) != len(virgins):
        return False
    n_victims = len(virgins)

    # 2. Every victim has its addressing evidence, and it is the same victim
    #    set. Without G5 every reading is a measurement of an unknown channel
    #    (spec section 7), and nothing in rules 3-6 can see a lost G5 line.
    if {_victim_key(g) for g in block["g5"]} != virgin_victims:
        return False
    if len(block["g5"]) != n_victims:
        return False

    # 3. The per-block Stopped and RunningSilent pair, per victim, numbered
    #    densely from 0. PER VICTIM and by level, not by count: a serial
    #    timeout that loses one victim's RunningSilent line leaves the total
    #    right if another victim's Stopped line arrives twice.
    live = _live_levels(block)
    ladder = [lv for lv in live
              if lv["level"] in (LEVEL_STOPPED, LEVEL_RUNNING_SILENT)]
    want_ladder = Counter((v, lvl) for v in virgin_victims
                          for lvl in (LEVEL_STOPPED, LEVEL_RUNNING_SILENT))
    if Counter((_victim_key(lv), lv["level"]) for lv in ladder) != want_ladder:
        return False
    if sorted(lv["case"] for lv in ladder) != list(range(2 * n_victims)):
        return False

    # 4. Every level measurement has its statistic and every statistic has
    #    its level measurement. They are printed as a pair
    #    (tone_probe.cpp:389-391) and settled_mean_spread is what G8 reads,
    #    so a STAT without its LEVEL is a floor with no victim attached.
    if {lv["case"] for lv in block["levels"]} != {s["case"]
                                                  for s in block["stats"]}:
        return False
    if len(block["stats"]) != len(block["levels"]):
        return False

    # 5. Every case the firmware announced with a SHELL_TONE_CASE line, and
    #    every non-negative level case, together form one dense numbering
    #    from 0. That is the counter the two tags share, and a hole in it is
    #    a case whose lines were lost whole -- which no per-case rule can
    #    see, because a case that printed nothing at all leaves nothing to
    #    check.
    case_ids = {c["case"] for c in block["cases"]}
    all_ids = sorted(case_ids | {lv["case"] for lv in ladder})
    if all_ids != list(range(len(all_ids))):
        return False

    # 6. Every tone case has exactly the phase grid the firmware said it
    #    would sweep, BY phase_idx and not by count: counting alone accepts a
    #    case with two phase_idx=0 rows and no phase_idx=1, after which
    #    delta_pp is a peak-to-peak over a grid that is not the grid.
    #    The static row (f_hz == 0) has no phase and takes rule 7 instead.
    phase_points = block["cfg"]["phase_points"]
    grid_cases = {c["case"] for c in block["cases"] if c["f_hz"] != 0}
    want = Counter((case, k) for case in grid_cases
                   for k in range(phase_points))
    if Counter((p["case"], p["phase_idx"]) for p in block["points"]) != want:
        return False

    # 7. Every static case (f_hz == 0) has its one LEVEL/STAT pair and no
    #    phase points. Spec section 8: it is a tone case like any other for
    #    identity, and it has no grid to walk.
    static_ids = {c["case"] for c in block["cases"] if c["f_hz"] == 0}
    static_levels = {lv["case"] for lv in live if lv["level"] == LEVEL_TONE}
    if static_levels != static_ids:
        return False

    # 8. The window sweep. `fits` is the firmware's own refusal gate: 1 means
    #    the grid is strictly shorter than the measured acquisition window
    #    and the sweep ran, 0 means it did not and no WINCASE/WIN line
    #    follows. Both halves are checked, because a block claiming fits=1
    #    with no sweep and a block claiming fits=0 with one are each a
    #    firmware that did something other than what it printed.
    window = block["window"]
    if window["fits"] == 0:
        if block["wincases"] or block["win_points"]:
            return False
    else:
        if not block["wincases"] or not block["win_points"]:
            return False
        win_ids = sorted(c["case"] for c in block["wincases"])
        if win_ids != list(range(len(win_ids))):
            return False
        # The sweep's step is not a printed field, so the grid is checked for
        # the shape the firmware's tone_win_ns() produces: an arithmetic
        # progression from 0 to the printed grid_end_ns with no hole. A lost
        # point breaks the constant difference and is caught; a duplicated
        # one is caught by the per-case Counter below.
        #
        # THE ONE HOLE THIS CANNOT SEE: with exactly three points, losing the
        # middle one leaves two, and any two points are an arithmetic
        # progression. The step is inferred rather than printed, so nothing
        # here can do better; the real sweep is 65 points and the guard's
        # fixture is five for the same reason.
        ds = sorted({p["d_before_end_ns"] for p in block["win_points"]})
        if not ds or ds[0] != 0 or ds[-1] != window["grid_end_ns"]:
            return False
        if len(ds) > 1:
            step = ds[1] - ds[0]
            if step <= 0 or ds != list(range(0, ds[-1] + 1, step)):
                return False
        want_win = Counter((c["case"], d) for c in block["wincases"]
                           for d in ds)
        got_win = Counter((p["case"], p["d_before_end_ns"])
                          for p in block["win_points"])
        if got_win != want_win:
            return False
    return True


def parse_block(lines):
    """The first complete block in `lines`, or None if there is not one."""
    block = None
    for raw in lines:
        line = raw.strip()
        try:
            if line.startswith("SHELL_TONE_CFG"):
                block = _new_block()
                block["cfg"] = _fields(line, "SHELL_TONE_CFG")
            elif block is None:
                continue
            elif line.startswith("SHELL_TONE_CLK"):
                block["clk"] = _fields(line, "SHELL_TONE_CLK")
            # CASE before CAL is not needed -- they differ at the third
            # letter -- but WINDOW and WINCASE both start with the WIN
            # prefix, so the two longer tags are matched first and the point
            # line is matched on "SHELL_TONE_WIN " with its space.
            elif line.startswith("SHELL_TONE_CAL"):
                block["cal"] = _fields(line, "SHELL_TONE_CAL")
            elif line.startswith("SHELL_TONE_CASE"):
                block["cases"].append(_fields(line, "SHELL_TONE_CASE"))
            elif line.startswith("SHELL_TONE_LEVEL"):
                block["levels"].append(_fields(line, "SHELL_TONE_LEVEL"))
            elif line.startswith("SHELL_TONE_STAT"):
                block["stats"].append(_fields(line, "SHELL_TONE_STAT"))
            elif line.startswith("SHELL_TONE_SPAN"):
                block["span"] = _fields(line, "SHELL_TONE_SPAN")
            elif line.startswith("SHELL_TONE_G5"):
                block["g5"].append(_fields(line, "SHELL_TONE_G5"))
            elif line.startswith("SHELL_TONE_WINDOW"):
                block["window"] = _fields(line, "SHELL_TONE_WINDOW")
            elif line.startswith("SHELL_TONE_WINCASE"):
                block["wincases"].append(_fields(line, "SHELL_TONE_WINCASE"))
            elif line.startswith("SHELL_TONE_WIN "):
                block["win_points"].append(_fields(line, "SHELL_TONE_WIN "))
            elif line.startswith("SHELL_TONE_GATES"):
                block["gates"] = _fields(line, "SHELL_TONE_GATES")
            elif line.startswith("SHELL_TONE_HEALTH"):
                block["health"] = _fields(line, "SHELL_TONE_HEALTH")
            elif line.startswith("SHELL_TONE_END"):
                if not _is_complete(block):
                    block = None
                    continue
                return block
            elif line.startswith("SHELL_TONE "):
                block["points"].append(_fields(line, "SHELL_TONE "))
        except (ValueError, KeyError):
            # A line the serial read timeout cut in half, or one libDaisy's
            # own logger truncated and stamped with its "$$" overflow marker.
            # Drop the block and keep listening rather than crash on the
            # board's next breath; a later SHELL_TONE_CFG still gets its own
            # chance.
            block = None
    return None


# --- what the block means -------------------------------------------------

def running_silent_means(block):
    """The RunningSilent mean per victim: the reference every tone point is
    differenced against.

    RunningSilent and not Stopped, and not the boot-virgin floor: the tone
    cases run with the codec started, so the only difference between a tone
    case and its reference must be the tone itself. Differencing against
    Stopped would fold the codec's clocks and its DMA into every delta, which
    is the separation spec section 4's three-level ladder exists to make."""
    out = {}
    for lv in _live_levels(block):
        if lv["level"] == LEVEL_RUNNING_SILENT:
            out[_victim_key(lv)] = lv["mean"]
    return out


def level_diffs(block):
    """RunningSilent minus Stopped per victim -- the codec's own contribution
    with no tone playing, which is the DMA/clock candidate `settle-measured.md`
    section 5 names and could not separate. Reported, not gated."""
    stopped, running, attrs = {}, {}, {}
    for lv in _live_levels(block):
        key = _victim_key(lv)
        if lv["level"] == LEVEL_STOPPED:
            stopped[key] = lv
        elif lv["level"] == LEVEL_RUNNING_SILENT:
            running[key] = lv
        else:
            continue
        attrs[key] = lv
    rows = []
    for key in _victim_order(block):
        if key not in stopped or key not in running:
            continue
        rows.append({"victim_group": key[0], "victim_ch": key[1],
                     "r_src": attrs[key]["r_src"],
                     "stopped_mean": stopped[key]["mean"],
                     "running_mean": running[key]["mean"],
                     "diff": running[key]["mean"] - stopped[key]["mean"]})
    return rows


def deltas(block):
    """delta(phase) = mean_tone(phase) - mean_running_silent(victim), one row
    per phase point of every tone case that has a phase grid.

    The static row (f_hz == 0) has no phase grid and appears in
    static_deltas() instead, not here: it has one measurement, and a row in
    this table that carried one point would be read as a curve."""
    ref = running_silent_means(block)
    cases = _case_by_id(block)
    rows = []
    for p in sorted(block["points"], key=lambda e: (e["case"], e["phase_idx"])):
        c = cases.get(p["case"])
        if c is None or c["f_hz"] == 0:
            continue
        base = ref.get(_victim_key(c))
        if base is None:
            continue
        row = dict(c)
        row.update(p)
        row["delta"] = p["mean"] - base
        rows.append(row)
    return rows


def static_deltas(block):
    """The static row's one measurement minus its victim's RunningSilent
    mean, per victim. Reported and NOT gated: spec section 4's criterion is a
    peak-to-peak across a phase grid, and this row has a single point, so its
    delta_pp would be 0 by construction -- a gate that cannot go red. What it
    can do is carry a DC offset, which is what this number is."""
    ref = running_silent_means(block)
    cases = _case_by_id(block)
    rows = []
    for lv in sorted(_live_levels(block), key=lambda e: e["case"]):
        if lv["level"] != LEVEL_TONE:
            continue
        c = cases.get(lv["case"])
        if c is None or c["f_hz"] != 0:
            continue
        base = ref.get(_victim_key(lv))
        if base is None:
            continue
        row = dict(c)
        row["mean"] = lv["mean"]
        row["delta"] = lv["mean"] - base
        rows.append(row)
    return rows


def delta_pp(block):
    """The PEAK-TO-PEAK of delta across each tone case's phase grid.

    Peak-to-peak and not a mean, and this is the whole reason the abscissa is
    phase: a sinusoidal disturbance has zero mean over a period, so an
    averaged delta would report every tone case as clean no matter how large
    the coupling is. The guard asserts that directly -- the same fixture
    whose deltas sum to zero has a peak-to-peak of twelve.

    Subtracting the RunningSilent mean is a constant per case, so delta_pp is
    numerically the peak-to-peak of the raw phase means. It is computed from
    delta anyway, because the column the CSV carries and the column the
    verdict is drawn from must be the same column."""
    by_case = OrderedDict()
    for row in deltas(block):
        by_case.setdefault(row["case"], []).append(row)
    out = []
    for case, rows in by_case.items():
        lo = min(rows, key=lambda e: e["delta"])
        hi = max(rows, key=lambda e: e["delta"])
        first = rows[0]
        out.append({"case": case, "f_hz": first["f_hz"], "dbfs": first["dbfs"],
                    "victim_group": first["victim_group"],
                    "victim_ch": first["victim_ch"], "r_src": first["r_src"],
                    "below_corner": first["below_corner"],
                    "delta_pp": hi["delta"] - lo["delta"],
                    "min_delta": lo["delta"], "max_delta": hi["delta"],
                    "min_phase_idx": lo["phase_idx"],
                    "max_phase_idx": hi["phase_idx"],
                    "points": len(rows)})
    return out


def block_floor(block):
    """This run's own boot-virgin floor per victim, from its `audio_virgin=1`
    STAT lines. Override: the floor is printed beside every delta_pp, so a
    delta_pp that is inside it cannot be read as a tone result."""
    stat = _stat_by_case(block)
    out = {}
    for lv in virgin_levels(block):
        s = stat.get(lv["case"])
        if s is not None:
            out[_victim_key(lv)] = s["settled_mean_spread"]
    return out


# --- round one's file -----------------------------------------------------

def _xtalk_silent_floor(xtalk_meta):
    """Round one's silent-block `settled_mean_spread` per victim, out of its
    `xtalk.csv.meta.csv`.

    The file is `scope,case,key,value`. The `silent` scope carries each
    silent case's statistic; the `case` scope says which victim that case
    was. Neither alone is enough, which is why G8 needs both."""
    victim_of = {}
    spread_of = {}
    for line in xtalk_meta.strip().split("\n"):
        parts = line.split(",")
        if len(parts) != 4:
            continue
        scope, case, key, value = (p.strip() for p in parts)
        if scope == "case" and key in ("victim_group", "victim_ch"):
            victim_of.setdefault(case, {})[key] = int(value)
        elif scope == "silent" and key == "settled_mean_spread":
            spread_of[case] = int(value)
    out = {}
    for case, spread in spread_of.items():
        v = victim_of.get(case)
        if v is None or "victim_group" not in v or "victim_ch" not in v:
            continue
        out[(v["victim_group"], v["victim_ch"])] = spread
    return out


# --- G8 -------------------------------------------------------------------

def g8(block, xtalk_meta):
    """G8, in two halves that are not the same kind of thing.

    **(b), the gate, at the top level of the returned dict.** This image's
    boot-virgin floor against this campaign's own recorded boot-virgin floors
    (BOOT_VIRGIN_FLOOR above), per victim, within G8_BOUND_COUNTS of the
    observed RANGE. Like-for-like: both sides are `measure_level()`, 65
    sub-measurements of 64 conversions under one identical condition. This is
    what verdicts() refuses on.

    **(a), under `round_one`, REPORT ONLY.** The same floor beside round one's
    silent block, with no bound and no verdict, because the two quantities
    are not the same shape. The reason is a statement about what two pieces of
    code compute and is checkable from their source, not a claim about
    physics:

      * `shell/xtalk_probe.cpp` `measure_silent_point(d_ns)` (:250-266) spins
        `park_cycles + d_cycles` before every conversion, and its 65 grid
        points carry 65 different `d` values, 0 to 12800 ns
        (`settle_plan.h:89-93`, `grid_ns(i) = i * 200`). Its
        `settled_mean_spread` (`reduce_and_emit()` :440-468) is therefore a
        peak-to-peak ACROSS 65 DIFFERENT PRE-CONVERSION DELAYS.
      * `shell/tone_probe.cpp` `measure_level()` (:533-601) has no spin and
        varies nothing across its 65 points. Its `settled_mean_spread` is
        repeat-to-repeat noise on the mean.

    Same count, different content. `shell/tone_plan.h:82-85` states the
    requirement in its own words and then matches only the count. By the
    spec's own condition the comparison means nothing, so it is printed and
    never gated.

    WHAT IS NOT SAID, ANYWHERE: what the `d`-dependence in round one's grid
    is, and why `REF_A` happens to agree while `REF_C` and `REF_B` come out
    lower. Both are open and neither is labelled."""
    floors = block_floor(block)
    live = {_victim_key(lv): lv for lv in virgin_levels(block)}

    rows = []
    worst_excess, worst_victim = -1, None
    for key in _victim_order(block):
        spread = floors.get(key)
        entry = BOOT_VIRGIN_FLOOR.get(key)
        r_src = live.get(key, {}).get("r_src")
        if spread is None:
            # No floor measured for a victim the block announced. Not a pass:
            # G8 has nothing to judge.
            rows.append({"victim_group": key[0], "victim_ch": key[1],
                         "name": entry["name"] if entry else "?",
                         "r_src": r_src, "spread": None, "lo": None,
                         "hi": None, "excess": None, "pass": False,
                         "boots": entry["boots"] if entry else ()})
            if worst_victim is None:
                worst_victim = key
            continue
        if entry is None:
            # A victim with no recorded baseline. A silent pass here is
            # exactly the failure G8 exists to prevent, so it fails loudly
            # and the operator adds the boots to BOOT_VIRGIN_FLOOR.
            rows.append({"victim_group": key[0], "victim_ch": key[1],
                         "name": "?", "r_src": r_src, "spread": spread,
                         "lo": None, "hi": None, "excess": None,
                         "pass": False, "boots": ()})
            if worst_victim is None:
                worst_victim = key
            continue
        lo = min(entry["boots"]) - G8_BOUND_COUNTS
        hi = max(entry["boots"]) + G8_BOUND_COUNTS
        excess = max(0, lo - spread, spread - hi)
        ok = excess == 0
        # worst_victim is computed from `excess`, independently of `ok`, so
        # that it still names the right victim if the bound is ever removed.
        if excess > worst_excess:
            worst_excess, worst_victim = excess, key
        rows.append({"victim_group": key[0], "victim_ch": key[1],
                     "name": entry["name"], "r_src": r_src, "spread": spread,
                     "lo": lo, "hi": hi, "excess": excess, "pass": ok,
                     "boots": entry["boots"]})

    round_one = _xtalk_silent_floor(xtalk_meta)
    report = []
    for key in _victim_order(block):
        report.append({"victim_group": key[0], "victim_ch": key[1],
                       "name": BOOT_VIRGIN_FLOOR.get(key, {}).get("name", "?"),
                       "r_src": live.get(key, {}).get("r_src"),
                       "boot_virgin_spread": floors.get(key),
                       "round_one_spread": round_one.get(key)})

    return {"pass": bool(rows) and all(r["pass"] for r in rows),
            "worst_delta": max(worst_excess, 0),
            "worst_victim": worst_victim,
            "victims": rows,
            "round_one": {"like_for_like": False,
                          "reason": G8_NOT_LIKE_FOR_LIKE,
                          "victims": report}}


# --- the verdict ----------------------------------------------------------

def verdicts(block, xtalk_meta):
    """One row per tone case, or [] when the run is not interpretable.

    Three reasons, each printed distinctly on stderr:
      * the firmware's own gates failed (G2/G4/G5/G7),
      * `missed_blocks` is non-zero -- a starved callback outputs the DMA
        buffer's stale contents, so the tone is not the tone,
      * G8(b) failed -- this image's floor is not this campaign's floor, and
        every delta_pp in the block would be a plausible number against a
        baseline that is not the one it claims.

    G8(a), the round-one comparison, never reaches this function's decision.
    It is not the same statistic (see g8()) and cannot gate anything."""
    # MISSED BLOCKS IS TESTED FIRST, and the order is not cosmetic. `g7` IS
    # `missed_blocks == 0` -- spec section 8 says so in as many words and the
    # firmware folds it into `gates_ok` under that name -- so a block with a
    # non-zero counter ALWAYS has `gates_ok=0` too. Testing gates_ok first
    # would make this branch unreachable from any firmware-consistent block:
    # a refusal reason that cannot be reached is not a refusal reason, and
    # the operator would get "victim addressing/callback health" where the
    # specific and actionable message is the count itself.
    if block["health"]["missed_blocks"]:
        print("MISSED BLOCKS (%d) -- the audio callback was starved, so the "
              "tone on the output is not the tone the table names; no verdict"
              % block["health"]["missed_blocks"], file=sys.stderr)
        return []
    if not block["gates"]["gates_ok"]:
        failed = [name for key, name in _GATE_NAMES.items()
                  if not block["gates"].get(key)]
        print("GATES FAILED (%s) -- no tone verdict from this run may be "
              "quoted" % ", ".join(failed), file=sys.stderr)
        return []
    gate = g8(block, xtalk_meta)
    if not gate["pass"]:
        print("G8(b) FAILED -- this image's boot-virgin floor is %d counts "
              "outside the recorded range for victim %s; every delta_pp below "
              "would be measured against a baseline that is not this "
              "campaign's, and nothing in the tone block alone could notice"
              % (gate["worst_delta"], gate["worst_victim"]), file=sys.stderr)
        return []

    floors = block_floor(block)
    rows = []
    for entry in delta_pp(block):
        key = (entry["victim_group"], entry["victim_ch"])
        floor = floors.get(key)
        row = dict(entry)
        row["gated"] = entry["r_src"] == GATED_R_SRC
        row["pass"] = (entry["delta_pp"] <= CRITERION_COUNTS
                       if row["gated"] else True)
        row["floor"] = floor
        # The floor note. This campaign measured REF_A's boot-virgin floor at
        # 14-17 counts WITH THE CODEC STOPPED, as a peak-to-peak of 65 means
        # of 64 conversions -- the same statistic delta_pp is. A delta_pp at
        # or below that floor may be entirely floor. The criterion is NOT
        # adjusted for it: spec section 4's bound is a spec decision. What
        # this flag does is stop a failing criterion that is reading the
        # floor from being quoted as a tone result.
        row["within_floor"] = (floor is not None
                               and entry["delta_pp"] <= floor)
        rows.append(row)
    return rows


# --- the two files --------------------------------------------------------

def format_csv(block):
    """One row per printed phase point, with its case's attributes and this
    reader's delta beside it."""
    rows = [",".join(FIELDS)]
    for row in deltas(block):
        rows.append(",".join(str(row.get(f, "")) for f in FIELDS))
    return "\n".join(rows) + "\n"


def _meta_rows_of(scope, case, entry, skip=()):
    out = []
    for name, value in entry.items():
        if name in skip:
            continue
        if isinstance(value, bool):
            value = 1 if value else 0
        out.append("%s,%s,%s,%s" % (scope, case, name, value))
    return out


def format_meta_csv(block):
    """Everything in the block that is not a tone phase point, as
    `scope,case,key,value` rows, plus this reader's own derived numbers.

    Scopes: the block-level lines; `g5` keyed by the victim's index in the
    order its G5 line arrived; `virgin`/`virgin_stat` for the boot-virgin
    floor, split out from `level`/`stat` because they are a different
    quantity and G8 reads only them; `case` for every announced case;
    `delta_pp` for this reader's own per-case statistic; `levels` for the
    running-minus-stopped difference per victim; and `wincase`/`win` for the
    window sweep, whose 65-point grid has no other home -- its columns are
    not the tone points' columns, so it cannot go in out.csv."""
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
    for entry in level_diffs(block):
        idx = index_of.get((entry["victim_group"], entry["victim_ch"]), "")
        rows.extend(_meta_rows_of("levels", idx, entry))

    stat = _stat_by_case(block)
    for lv in sorted(block["levels"], key=lambda e: e["case"]):
        virgin = lv["audio_virgin"] == 1
        rows.extend(_meta_rows_of("virgin" if virgin else "level",
                                  lv["case"], lv, skip=("case",)))
        s = stat.get(lv["case"])
        if s is not None:
            rows.extend(_meta_rows_of(
                "virgin_stat" if virgin else "stat", s["case"], s,
                skip=("case",)))
    for c in sorted(block["cases"], key=lambda e: e["case"]):
        rows.extend(_meta_rows_of("case", c["case"], c, skip=("case",)))
    for entry in delta_pp(block):
        rows.extend(_meta_rows_of("delta_pp", entry["case"], entry,
                                  skip=("case",)))
    for entry in static_deltas(block):
        rows.extend(_meta_rows_of("static", entry["case"], entry,
                                  skip=("case",)))

    for c in sorted(block["wincases"], key=lambda e: e["case"]):
        rows.extend(_meta_rows_of("wincase", c["case"], c, skip=("case",)))
    for p in sorted(block["win_points"],
                    key=lambda e: (e["case"], e["d_before_end_ns"])):
        for name in ("n", "mean", "min", "max"):
            rows.append("win,%d,%d_%s,%d"
                        % (p["case"], p["d_before_end_ns"], name, p[name]))
    return "\n".join(rows) + "\n"


# --- the port -------------------------------------------------------------

def _read_one_block(port, limit):
    """Listen on `port` until a whole block has arrived, or `limit` seconds
    have passed.

    Re-parsing only at an _END marker, and resetting the accumulator at every
    _CFG, is not an optimisation for its own sake: a block is nearly a
    thousand lines here, parsing the accumulated list once per line is
    quadratic, and a host spending that time is a host that is not draining
    the port -- which is exactly the condition libDaisy's logger fuses lines
    under."""
    import serial

    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        lines = []
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if line.startswith("SHELL_TONE_CFG"):
                lines = [line]
                continue
            lines.append(line)
            if line.startswith("SHELL_TONE_END"):
                block = parse_block(lines)
                if block is not None:
                    return block
                print("discarded an incomplete block at its _END marker; "
                      "still listening", file=sys.stderr)
                lines = []
    return None


def report(block, xtalk_meta, out=None):
    """Everything this reader has to say about one block, on stderr, plus the
    exit code. Separated from main() so the files and the port are the only
    things main() adds."""
    csv = format_csv(block)
    meta = format_meta_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
        meta_out = out + ".meta.csv"
        with open(meta_out, "w", encoding="utf-8", newline="") as fh:
            fh.write(meta)
        print("wrote %s (%d phase points) and %s (%d metadata rows)"
              % (out, len(csv.strip().split("\n")) - 1, meta_out,
                 len(meta.strip().split("\n")) - 1), file=sys.stderr)

    cfg, cal, health = block["cfg"], block["cal"], block["health"]
    print("git=%s adc_khz=%d repeats=%d phase_points=%d block_size=%d sr=%d"
          % (cfg["git"], cfg["adc_khz"], cfg["repeats"], cfg["phase_points"],
             cfg["block_size"], cfg["sr"]), file=sys.stderr)
    print("lat_mean_ns=%d b0=%d timeouts=%d gates_ok=%d"
          % (cal["lat_mean_ns"], cal["b0"], cal["timeouts"],
             block["gates"]["gates_ok"]), file=sys.stderr)
    print("health: missed_blocks=%d phase_timeouts=%d win_timeouts=%d "
          "block_ms=%d" % (health["missed_blocks"], health["phase_timeouts"],
                           health["win_timeouts"], health["block_ms"]),
          file=sys.stderr)
    window = block["window"]
    print("window: window_ns=%d nominal_ns=%d grid_end_ns=%d fits=%d "
          "(%d window cases, %d points)"
          % (window["window_ns"], window["nominal_ns"], window["grid_end_ns"],
             window["fits"], len(block["wincases"]),
             len(block["win_points"])), file=sys.stderr)

    gate = g8(block, xtalk_meta)
    print("", file=sys.stderr)
    print("G8(b) -- like-for-like, against this campaign's recorded "
          "boot-virgin floors:", file=sys.stderr)
    for row in gate["victims"]:
        print("  %-6s (%d,%d) r_src=%-4s spread=%-4s recorded boots=%-18s "
              "bound=[%s,%s] %s"
              % (row["name"], row["victim_group"], row["victim_ch"],
                 row["r_src"], row["spread"],
                 ",".join(str(b) for b in row["boots"]) or "-",
                 row["lo"], row["hi"], "PASS" if row["pass"] else "FAIL"),
              file=sys.stderr)
    print("G8(a) -- %s" % G8_NOT_LIKE_FOR_LIKE, file=sys.stderr)
    for row in gate["round_one"]["victims"]:
        print("  %-6s (%d,%d) r_src=%-4s boot_virgin=%-4s round_one=%-4s  "
              "(report only, no bound, no verdict)"
              % (row["name"], row["victim_group"], row["victim_ch"],
                 row["r_src"], row["boot_virgin_spread"],
                 row["round_one_spread"]), file=sys.stderr)

    print("", file=sys.stderr)
    for row in level_diffs(block):
        print("levels (%d,%d) r_src=%-4d stopped=%-6d running_silent=%-6d "
              "diff=%d" % (row["victim_group"], row["victim_ch"],
                           row["r_src"], row["stopped_mean"],
                           row["running_mean"], row["diff"]), file=sys.stderr)

    rows = verdicts(block, xtalk_meta)
    if not rows:
        print("no tone case yielded a verdict", file=sys.stderr)
        return 1

    print("", file=sys.stderr)
    print("criterion: delta_pp <= %d counts on every %d ohm victim at every "
          "row (spec section 4). The floor column is THIS RUN's boot-virgin "
          "settled_mean_spread for that victim -- the same statistic, "
          "measured with the codec stopped."
          % (CRITERION_COUNTS, GATED_R_SRC), file=sys.stderr)
    for row in sorted(rows, key=lambda e: (e["f_hz"], -e["dbfs"],
                                           e["victim_group"], e["victim_ch"])):
        print("f=%-5d dbfs=%-4d victim (%d,%d) r_src=%-4d delta_pp=%-4d "
              "floor=%-4s pts=%-3d %s%s"
              % (row["f_hz"], row["dbfs"], row["victim_group"],
                 row["victim_ch"], row["r_src"], row["delta_pp"],
                 row["floor"], row["points"],
                 ("PASS" if row["pass"] else "FAIL") if row["gated"]
                 else "report",
                 "  NOTE: at or below this victim's own boot-virgin floor -- "
                 "this number may be entirely floor and must not be read as "
                 "a tone result" if row["within_floor"] else ""),
              file=sys.stderr)

    for row in static_deltas(block):
        print("static f_hz=0 dbfs=%-4d victim (%d,%d) r_src=%-4d mean=%-6d "
              "delta=%-4d  (reported, not gated: one point has no "
              "peak-to-peak)"
              % (row["dbfs"], row["victim_group"], row["victim_ch"],
                 row["r_src"], row["mean"], row["delta"]), file=sys.stderr)

    failed = [row for row in rows if row["gated"] and not row["pass"]]
    if failed:
        print("", file=sys.stderr)
        for row in failed:
            print("FAILED case=%d f_hz=%d dbfs=%d victim (%d,%d) r_src=%d "
                  "delta_pp=%d floor=%s%s"
                  % (row["case"], row["f_hz"], row["dbfs"],
                     row["victim_group"], row["victim_ch"], row["r_src"],
                     row["delta_pp"], row["floor"],
                     " -- WITHIN THE FLOOR" if row["within_floor"] else ""),
                  file=sys.stderr)
        return 1
    return 0


def main() -> int:
    # serial is imported inside _read_one_block(), not at module scope:
    # parse_block()/deltas()/delta_pp()/g8()/verdicts() are the pure
    # computation the guard exercises, and that guard must not need pyserial
    # installed to import this module.
    if len(sys.argv) not in (4, 5):
        raise SystemExit(
            "usage: read_tone.py PORT out.csv XTALK_META.csv [timeout_s]\n"
            "XTALK_META.csv is round one's xtalk.csv.meta.csv and is not "
            "optional -- see this module's docstring.")
    port, out, xtalk_path = sys.argv[1], sys.argv[2], sys.argv[3]
    limit = float(sys.argv[4]) if len(sys.argv) > 4 else 600.0

    with open(xtalk_path, "r", encoding="utf-8") as fh:
        xtalk_meta = fh.read()

    block = _read_one_block(port, limit)
    if block is None:
        print("no complete SHELL_TONE block within %.0f s" % limit,
              file=sys.stderr)
        return 1
    return report(block, xtalk_meta, out)


if __name__ == "__main__":
    raise SystemExit(main())
