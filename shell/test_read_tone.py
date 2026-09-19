"""Guard for read_tone.py's parser, its delta_pp arithmetic and both halves
of G8.

Runs as a plain script; pytest is not installed on this machine, and the
tools/ guards that had no runner stood red for 23 days behind exactly that
gap.

build_block() generates a format-faithful block at the smallest size that
still exercises every rule -- two victims, one tone row, one static row, four
phase points, one window case -- rather than pasting 60 cases. A literal
fixture of a whole block would be unreadable and would go stale the first
time a field moved, which is what happened to the settle plan's own SAMPLE
block.

**THE FIXTURE IS BUILT FROM SPEC SECTION 8, NOT FROM THE TASK BRIEF.** The
brief's generator was written before Tasks 3, 4 and 5 existed and does not
contain `SHELL_TONE_HEALTH`, `SHELL_TONE_WINCASE`, `SHELL_TONE_WIN`,
`audio_virgin=` on `SHELL_TONE_LEVEL` or `below_corner=` on
`SHELL_TONE_CASE`, and it still has the health counters on
`SHELL_TONE_GATES`, where they no longer are. A guard written against it
would be green while the reader failed on every real line the board prints.

**AND A GENERATED FIXTURE IS NOT ENOUGH ON ITS OWN.** A fixture written by
the same hand as the parser agrees with it by construction. Section B below
feeds the parser one complete block captured from the board
(`testdata/tone-block-86070d9.txt`, lines 898-1852 of the 2026-09-19 capture
in the plan's workspace) and asserts it parses and what it contains. That
file is vendored rather than read from `.superpowers/`, which is gitignored:
a check that silently does not run when its input is missing is not a check.
"""
import io
import os
import sys
from contextlib import redirect_stderr

from read_tone import (parse_block, format_csv, format_meta_csv, deltas,
                       static_deltas, delta_pp, g8, verdicts, block_floor,
                       level_diffs, BOOT_VIRGIN_FLOOR)

FAILURES = []

REAL_BLOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "testdata", "tone-block-86070d9.txt")


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def quiet(fn, *args, **kwargs):
    """Call `fn` with stderr captured; returns (result, stderr_text).

    verdicts() prints its refusal reason on stderr, which is part of what is
    being guarded -- see section I -- so it is captured and asserted rather
    than allowed to scroll past a passing run."""
    buf = io.StringIO()
    with redirect_stderr(buf):
        result = fn(*args, **kwargs)
    return result, buf.getvalue()


def build_xtalk_meta(spreads=(15, 0)):
    """Round one's metadata, cut to what G8's report-only half reads: each
    silent case's settled_mean_spread, and enough of its CASE rows to say
    which victim that case was."""
    rows = ["scope,case,key,value"]
    for i, (g, ch) in enumerate(((0, 8), (0, 10))):
        rows.append("case,%d,row,1" % i)
        rows.append("case,%d,victim_group,%d" % (i, g))
        rows.append("case,%d,victim_ch,%d" % (i, ch))
        rows.append("silent,%d,settled_mean_spread,%d" % (i, spreads[i]))
    return "\n".join(rows) + "\n"


# The fixture's two victims. Both are in read_tone's BOOT_VIRGIN_FLOOR, which
# is what makes G8(b) evaluable on the fixture at all:
#   (0,8)  REF_A   boots 17,16,17,14 -> bound [13-4, 17+4] = [10, 21]
#   (0,10) R_SP10  boots  1, 1, 0, 0 -> bound [ 0-4,  1+4] = [-4,  5]
VICTIMS = ((0, 8, 5150), (0, 10, 150))


def build_block(phase_points=4, gates_ok=1, missed=0,
                virgin_spreads=(17, 1),
                tone_means=(100, 106, 100, 94),
                fits=1, win_points=5, win_step=200,
                extra_victim=False):
    """One block in spec section 8's print order, field for field.

    Case numbering follows the firmware's: the boot-virgin floor lines are
    -1 downwards, the per-block Stopped/RunningSilent ladder is 0..2N-1, the
    tone cases continue that counter, and the window sweep numbers its own
    cases from 0 in a separate namespace."""
    victims = list(VICTIMS)
    if extra_victim:
        # A victim with no entry in BOOT_VIRGIN_FLOOR. Section G asserts G8
        # refuses it rather than passing it silently.
        victims.append((1, 15, 5150))

    lines = [
        "SHELL_TONE_CFG adc_khz=6146 repeats=64 phase_points=%d "
        "block_size=96 sr=48000 rv4=0 git=4a7800f+" % phase_points,
        "SHELL_TONE_CLK span_short_cyc=2311 span_long_cyc=31285 "
        "smp_short_tenths=165 smp_long_tenths=3875",
        "SHELL_TONE_CAL lat_mean_ns=689 lat_min_ns=644 lat_max_ns=773 b0=17 "
        "timeouts=0",
    ]

    # The boot-virgin floor: measured once per boot before any StartAudio(),
    # then re-emitted verbatim in every block. audio_virgin=1, negative case.
    for vi, (g, ch, r) in enumerate(victims):
        spread = virgin_spreads[vi] if vi < len(virgin_spreads) else 0
        lines.append("SHELL_TONE_LEVEL case=%d level=0 victim_group=%d "
                     "victim_ch=%d r_src=%d n=64 mean=100 min=98 max=102 "
                     "audio_virgin=1" % (-(vi + 1), g, ch, r))
        lines.append("SHELL_TONE_STAT case=%d settled_mean_spread=%d "
                     "widest_sample_band=895" % (-(vi + 1), spread))

    # The per-block ladder: Stopped then RunningSilent, per victim.
    case = 0
    for g, ch, r in victims:
        for level in (0, 1):
            lines.append("SHELL_TONE_LEVEL case=%d level=%d victim_group=%d "
                         "victim_ch=%d r_src=%d n=64 mean=100 min=98 max=102 "
                         "audio_virgin=0" % (case, level, g, ch, r))
            lines.append("SHELL_TONE_STAT case=%d settled_mean_spread=7 "
                         "widest_sample_band=208" % case)
            case += 1

    # One tone row, one case per victim, each with its phase grid.
    for g, ch, r in victims:
        lines.append("SHELL_TONE_CASE case=%d level=2 f_hz=1000 dbfs=-6 "
                     "victim_group=%d victim_ch=%d r_src=%d below_corner=0"
                     % (case, g, ch, r))
        for k in range(phase_points):
            mean = tone_means[k % len(tone_means)]
            lines.append("SHELL_TONE case=%d phase_idx=%d n=64 mean=%d "
                         "min=%d max=%d" % (case, k, mean, mean - 2, mean + 2))
        case += 1

    # The static row: a CASE line with f_hz=0, then a LEVEL/STAT pair and no
    # phase points.
    for g, ch, r in victims:
        lines.append("SHELL_TONE_CASE case=%d level=2 f_hz=0 dbfs=-6 "
                     "victim_group=%d victim_ch=%d r_src=%d below_corner=0"
                     % (case, g, ch, r))
        lines.append("SHELL_TONE_LEVEL case=%d level=2 victim_group=%d "
                     "victim_ch=%d r_src=%d n=64 mean=98 min=96 max=100 "
                     "audio_virgin=0" % (case, g, ch, r))
        lines.append("SHELL_TONE_STAT case=%d settled_mean_spread=6 "
                     "widest_sample_band=198" % case)
        case += 1

    lines.append("SHELL_TONE_SPAN zero=0 rail=65532 hi_spread=0 lo_spread=0 "
                 "valid=1")
    for g, ch, _r in victims:
        lines.append("SHELL_TONE_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=100 ok=1" % (g, ch))

    grid_end = (win_points - 1) * win_step
    lines.append("SHELL_TONE_WINDOW window_ns=63047 nominal_ns=63050 "
                 "grid_end_ns=%d fits=%d" % (grid_end, fits))
    if fits:
        lines.append("SHELL_TONE_WINCASE case=0 xtalk_case=10 victim_group=0 "
                     "victim_ch=8 r_src=5150 word_a=8 word_b=40 codec=0")
        for k in range(win_points):
            lines.append("SHELL_TONE_WIN case=0 d_before_end_ns=%d n=64 "
                         "mean=32760 min=32746 max=32782" % (k * win_step))

    # g8 is the firmware's literal -1 sentinel: G8 is host-side and the
    # firmware has never computed it. gates_ok is the AND of g2, g4, g5 and
    # g7 only.
    lines.append("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=-1 gates_ok=%d"
                 % (gates_ok, gates_ok, gates_ok, 1 if missed == 0 else 0,
                    gates_ok if missed == 0 else 0))
    lines.append("SHELL_TONE_HEALTH missed_blocks=%d phase_timeouts=0 "
                 "win_timeouts=0 block_ms=170657" % missed)
    lines.append("SHELL_TONE_END")
    return lines


def drop_first(lines, prefix):
    out = list(lines)
    for i, line in enumerate(out):
        if line.startswith(prefix):
            del out[i]
            return out
    raise AssertionError("no line starts with %r" % prefix)


# --- A: a complete block --------------------------------------------------
base = build_block()
block = parse_block(base)
check("a complete block parses", block is not None)
check("the boot-virgin floor is kept, one line per victim",
      block and len(block["levels"]) == 8)
check("every phase point is kept", block and len(block["points"]) == 8)
check("the window line is kept", block and block["window"]["fits"] == 1)
check("the window sweep's points are kept",
      block and len(block["win_points"]) == 5 and len(block["wincases"]) == 1)
check("the health counters are read off _HEALTH, not off _GATES",
      block and block["health"]["block_ms"] == 170657
      and "missed_blocks" not in block["gates"])
check("this run's own boot-virgin floor is read per victim",
      block and block_floor(block) == {(0, 8): 17, (0, 10): 1})
check("the static row is kept as a case with no phase points",
      block and len([c for c in block["cases"] if c["f_hz"] == 0]) == 2)

# --- B: the real board capture --------------------------------------------
# A fixture the implementer wrote agreeing with a parser the same implementer
# wrote proves nothing on its own. This is one complete block as the board
# printed it on 2026-09-19 at git 86070d9+, header to end marker.
check("the vendored board capture is present", os.path.exists(REAL_BLOCK))
if os.path.exists(REAL_BLOCK):
    with open(REAL_BLOCK, "r", encoding="utf-8") as fh:
        real_lines = fh.read().splitlines()
    real = parse_block(real_lines)
    check("a real block captured from the board parses", real is not None)
    if real is not None:
        check("the real block's configuration is read",
              real["cfg"]["git"] == "86070d9+"
              and real["cfg"]["phase_points"] == 16
              and real["cfg"]["repeats"] == 64)
        check("the real block's 45 tone grids are all kept",
              len(real["points"]) == 45 * 16)
        check("the real block's 60 cases are all kept",
              len(real["cases"]) + 2 * 5 == 60)
        check("the real block's 20 level/stat pairs are all kept",
              len(real["levels"]) == 20 and len(real["stats"]) == 20)
        check("the real block's five victims all have G5 evidence",
              len(real["g5"]) == 5)
        check("the real block's window sweep is kept whole",
              len(real["wincases"]) == 2 and len(real["win_points"]) == 130)
        check("the real block's boot-virgin floor is the one G8 was built on",
              block_floor(real) == {(0, 8): 14, (1, 6): 4, (0, 9): 5,
                                    (0, 10): 0, (1, 3): 0})
        check("G8(b) passes on the real block",
              g8(real, build_xtalk_meta())["pass"])
        check("the real block yields one verdict row per tone case",
              len(verdicts(real, build_xtalk_meta())) == 45)
        check("the real block's static row is reported separately",
              len(static_deltas(real)) == 5)
        check("the real block's CSV has one row per phase point",
              len(format_csv(real).strip().split("\n")) == 45 * 16 + 1)
        check("the real block's metadata CSV is writable and non-trivial",
              len(format_meta_csv(real).strip().split("\n")) > 500)

# --- C: incomplete blocks are refused -------------------------------------
check("a block truncated before SHELL_TONE_END is refused",
      parse_block(base[:-1]) is None)
check("a block missing one case's phase point is refused",
      parse_block(drop_first(base, "SHELL_TONE case=4 phase_idx=1")) is None)
check("a block missing a LEVEL line is refused",
      parse_block(drop_first(base, "SHELL_TONE_LEVEL case=1 ")) is None)
check("a block missing a STAT line is refused",
      parse_block(drop_first(base, "SHELL_TONE_STAT case=1 ")) is None)
check("a block missing a boot-virgin floor line is refused",
      parse_block(drop_first(base, "SHELL_TONE_LEVEL case=-2 ")) is None)
check("a block missing SHELL_TONE_HEALTH is refused",
      parse_block(drop_first(base, "SHELL_TONE_HEALTH")) is None)
check("a block missing SHELL_TONE_WINDOW is refused",
      parse_block(drop_first(base, "SHELL_TONE_WINDOW")) is None)
check("a block missing a victim's G5 line is refused",
      parse_block(drop_first(base, "SHELL_TONE_G5 victim_group=0 "
                                   "victim_ch=10")) is None)
check("a block missing a whole tone case's CASE line is refused",
      parse_block(drop_first(base, "SHELL_TONE_CASE case=5 ")) is None)
check("a block claiming fits=1 with no window sweep is refused",
      parse_block([l for l in base
                   if not l.startswith("SHELL_TONE_WIN")
                   or l.startswith("SHELL_TONE_WINDOW")]) is None)
check("a hole in the window grid is refused",
      parse_block(drop_first(base, "SHELL_TONE_WIN case=0 "
                                   "d_before_end_ns=200")) is None)
# fits=0 is the firmware's own refusal, and then there must be no sweep at
# all: a block that claims the grid did not fit and prints one anyway is a
# firmware that did something other than what it printed.
check("fits=0 with no sweep is a complete block",
      parse_block(build_block(fits=0)) is not None)

# --- D: delta and delta_pp ------------------------------------------------
# delta(phi) = mean_tone(phi) - mean_running_silent, and the number that
# carries the result is the PEAK-TO-PEAK across the phase grid -- a
# sinusoidal disturbance has zero mean over a period, so an average would
# report every tone case as zero.
d = deltas(block)
check("delta is the tone point minus that victim's running-silent mean",
      d and [row["delta"] for row in d if row["case"] == 4] == [0, 6, 0, -6])
check("the static row is not in the phase table",
      all(row["f_hz"] != 0 for row in d))
pp = delta_pp(block)
check("delta_pp is the peak-to-peak across the phase grid",
      pp and all(row["delta_pp"] == 12 for row in pp))
check("a mean of delta would have been zero, which is why pp is the statistic",
      sum(row["delta"] for row in d if row["case"] == 4) == 0)
check("the static row's delta is its own quantity, one per victim",
      [row["delta"] for row in static_deltas(block)] == [-2, -2])
check("running-silent minus stopped is reported per victim",
      [row["diff"] for row in level_diffs(block)] == [0, 0])

# --- E: the criterion -----------------------------------------------------
# delta_pp <= 8 on every 5150 ohm victim at every row.
v, _ = quiet(verdicts,
             parse_block(build_block(tone_means=(100, 103, 100, 97))),
             build_xtalk_meta())
check("a delta_pp of 6 passes", v and all(row["pass"] for row in v))
fail_v, _ = quiet(verdicts, block, build_xtalk_meta())
check("a delta_pp of 12 fails on the 5150 ohm victim",
      fail_v and not all(row["pass"] for row in fail_v))
check("and the 150 ohm victim is reported beside it, not gated on",
      fail_v and any(row["r_src"] == 150 and not row["gated"]
                     and row["pass"] for row in fail_v))

# --- F: the floor note ----------------------------------------------------
# This campaign measured REF_A's boot-virgin floor at 14-17 counts with the
# codec STOPPED, as a peak-to-peak of 65 means of 64 conversions -- the same
# statistic delta_pp is. A delta_pp inside that floor may be entirely floor,
# and a failing criterion that is reading the floor must not be readable as a
# tone result. The criterion itself is NOT adjusted: that is spec section 4's
# decision.
ref_a = [row for row in fail_v if (row["victim_group"], row["victim_ch"])
         == (0, 8)]
check("the verdict carries this victim's own boot-virgin floor beside it",
      ref_a and ref_a[0]["floor"] == 17)
check("a delta_pp of 12 inside a floor of 17 is flagged as possibly floor",
      ref_a and ref_a[0]["within_floor"] and not ref_a[0]["pass"])
low_floor, _ = quiet(verdicts,
                     parse_block(build_block(virgin_spreads=(11, 1))),
                     build_xtalk_meta())
low_a = [row for row in low_floor if (row["victim_group"], row["victim_ch"])
         == (0, 8)]
check("a delta_pp of 12 above a floor of 11 is not flagged",
      low_a and not low_a[0]["within_floor"])

# --- G: G8(b), the real gate ----------------------------------------------
# Like-for-like: this image's boot-virgin floor against this campaign's four
# recorded boots per victim, within 4 counts of the observed RANGE. The bound
# is the range and not a central value -- four points do not establish one,
# and a midpoint of 14..17 is a number no boot ever produced.
check("the recorded baseline is the four boots this campaign measured",
      BOOT_VIRGIN_FLOOR[(0, 8)]["boots"] == (17, 16, 17, 14)
      and BOOT_VIRGIN_FLOOR[(1, 6)]["boots"] == (3, 3, 4, 4)
      and BOOT_VIRGIN_FLOOR[(0, 9)]["boots"] == (2, 3, 4, 5)
      and BOOT_VIRGIN_FLOOR[(0, 10)]["boots"] == (1, 1, 0, 0)
      and BOOT_VIRGIN_FLOOR[(1, 3)]["boots"] == (0, 0, 0, 0))
check("G8(b) passes when the floor is inside the recorded range",
      g8(parse_block(build_block(virgin_spreads=(16, 0))),
         build_xtalk_meta())["pass"])
check("G8(b) passes at exactly 4 counts above the range",
      g8(parse_block(build_block(virgin_spreads=(21, 1))),
         build_xtalk_meta())["pass"])
check("G8(b) fails at 5 counts above the range",
      not g8(parse_block(build_block(virgin_spreads=(22, 1))),
             build_xtalk_meta())["pass"])
check("G8(b) passes at exactly 4 counts below the range",
      g8(parse_block(build_block(virgin_spreads=(10, 1))),
         build_xtalk_meta())["pass"])
check("G8(b) fails at 5 counts below the range -- a floor that COLLAPSED is "
      "as much a changed baseline as one that grew",
      not g8(parse_block(build_block(virgin_spreads=(9, 1))),
             build_xtalk_meta())["pass"])
check("G8(b) names the victim whose floor moved",
      g8(parse_block(build_block(virgin_spreads=(22, 1))),
         build_xtalk_meta())["worst_victim"] == (0, 8))
check("G8(b) reports how far outside the range it was",
      g8(parse_block(build_block(virgin_spreads=(22, 1))),
         build_xtalk_meta())["worst_delta"] == 1)
check("G8(b) refuses a victim it has no recorded baseline for, rather than "
      "passing it silently",
      not g8(parse_block(build_block(extra_victim=True,
                                     virgin_spreads=(17, 1, 5))),
             build_xtalk_meta())["pass"])

# --- H: G8(a), report only ------------------------------------------------
# Round one's settled_mean_spread is a peak-to-peak across 65 DIFFERENT
# pre-conversion delays (xtalk_probe.cpp measure_silent_point spins
# park + d before every conversion, d = 0..12800 ns); this image's is 65x64
# conversions under one identical condition. Same count, different content.
# It is printed beside the floor and it gates nothing.
g = g8(block, build_xtalk_meta(spreads=(15, 0)))
check("G8(a) reports round one's number beside this image's floor",
      [(r["boot_virgin_spread"], r["round_one_spread"])
       for r in g["round_one"]["victims"]] == [(17, 15), (1, 0)])
check("G8(a) says in the object itself that it is not like-for-like",
      g["round_one"]["like_for_like"] is False
      and "measure_silent_point" in g["round_one"]["reason"])
check("G8(a) is not folded into the gate's pass", g["pass"] is True)
# The decisive one: round one's numbers can be anything at all and the
# verdict does not move. If this ever fails, G8(a) has become a gate.
wild, _ = quiet(verdicts, block, build_xtalk_meta(spreads=(999, 999)))
sane, _ = quiet(verdicts, block, build_xtalk_meta(spreads=(15, 0)))
check("G8(a) can never block a verdict, whatever round one's numbers are",
      wild and sane and
      [r["delta_pp"] for r in wild] == [r["delta_pp"] for r in sane])

# --- I: a failed gate refuses the verdict, and says which one -------------
no_gates, msg_gates = quiet(verdicts, parse_block(build_block(gates_ok=0)),
                            build_xtalk_meta())
check("a run whose firmware gates failed yields no verdict", no_gates == [])
check("and names the failed gates distinctly on stderr",
      "GATES FAILED" in msg_gates)
no_missed, msg_missed = quiet(verdicts, parse_block(build_block(missed=3)),
                              build_xtalk_meta())
# g7 IS `missed_blocks == 0`, so a firmware-consistent block with a missed
# block also has gates_ok=0. verdicts() therefore tests the counter FIRST --
# the other order makes this branch unreachable and the operator gets the
# generic gate list instead of the count. This check is what pins that order.
check("a run that missed a block yields no verdict", no_missed == [])
check("and names the missed blocks distinctly, not the generic gate list",
      "MISSED BLOCKS" in msg_missed and "GATES FAILED" not in msg_missed)
no_g8, msg_g8 = quiet(verdicts,
                      parse_block(build_block(virgin_spreads=(22, 1))),
                      build_xtalk_meta())
check("a run whose G8(b) failed yields no verdict", no_g8 == [])
check("and says so distinctly on stderr, naming G8(b)",
      "G8(b) FAILED" in msg_g8)

# --- J: the CSV files -----------------------------------------------------
csv = format_csv(block)
check("the CSV has a header and one row per phase point",
      len(csv.strip().split("\n")) == 9)
check("the CSV's header names the delta column it carries",
      csv.split("\n")[0].endswith(",delta"))
meta_rows = [r.split(",") for r in format_meta_csv(block).strip().split("\n")]
check("the metadata CSV has the four-column header",
      meta_rows[0] == ["scope", "case", "key", "value"])
meta = {(r[0], r[1], r[2]): r[3] for r in meta_rows[1:]}
check("the metadata carries the block period's inputs",
      meta.get(("cfg", "", "block_size")) == "96"
      and meta.get(("cfg", "", "sr")) == "48000")
check("the metadata carries the firmware's g8 sentinel, unaltered",
      meta.get(("gates", "", "g8")) == "-1")
check("the metadata carries the measured window",
      meta.get(("window", "", "window_ns")) == "63047")
check("the metadata carries the health counters",
      meta.get(("health", "", "missed_blocks")) == "0"
      and meta.get(("health", "", "block_ms")) == "170657")
check("the boot-virgin floor has its own scope, because it is the one G8 "
      "reads and a per-block Stopped level is not comparable to it",
      meta.get(("virgin_stat", "-1", "settled_mean_spread")) == "17"
      and meta.get(("stat", "0", "settled_mean_spread")) == "7")
check("the metadata carries this reader's own delta_pp",
      meta.get(("delta_pp", "4", "delta_pp")) == "12")
check("the metadata carries the window sweep, which has no other home",
      meta.get(("win", "0", "200_mean")) == "32760")

# --- K: libDaisy's "$$" overflow marker -----------------------------------
truncated = list(base)
truncated[-2] = truncated[-2][:-2] + "$$"
check("a $$-truncated field does not crash the parser, and yields no block",
      parse_block(truncated) is None)
# The marker is refused outright and not only where int() happens to choke:
# a cut that lands on a space would otherwise parse as a valid line with
# extra tokens. This is the fusion shape the real capture's first two lines
# have.
fused = list(base)
for i, line in enumerate(fused):
    if line.startswith("SHELL_TONE case=4 phase_idx=0"):
        fused[i] = (line[:line.index(" min=")]
                    + "$$SHELL_TONE case=4 phase_idx=1 n=64 mean=106 min=104 "
                      "max=108")
        break
check("a fusion that lands on a token boundary is refused too",
      parse_block(fused) is None)
check("a corrupted block does not poison the block that follows it",
      parse_block(fused + base) is not None)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_tone guard: ok")
