"""Guard for read_xtalk.py's parser, its delta arithmetic and its verdict.

Runs as a plain script; pytest is not installed on this machine, and the
tools/ guards that had no runner stood red for 23 days behind exactly that
gap.

build_block() generates a format-faithful block at the smallest size that
still exercises every rule -- two victims, two grid points -- rather than
pasting 58 cases. A literal fixture of this block would be unreadable and
would go stale the first time a field moved, which is what happened to the
settle plan's own SAMPLE block.

THREE DEVIATIONS FROM THE TASK BRIEF'S DRAFT OF THIS FILE, all of them
because the brief was written before Task 6's firmware existed and this file
is checked against the firmware, not against the brief:

1. The brief's generator prints SHELL_XTALK_STAT for the Silent case only.
   The shipping firmware prints it for EVERY grid case -- reduce_and_emit()
   in xtalk_probe.cpp emits it unconditionally, and Task 6's capture counts
   44 STAT lines against 44 grid cases per block. A completeness rule built
   on the brief's version would refuse every real capture. So the generator
   prints one per grid case and _is_complete()'s rule 4 wants one per grid
   case.
2. Because of 1, a STAT line's scope in the metadata file is NOT uniformly
   "silent": the floor statistic and a control's or an aggressor's spread
   are different quantities and the codec-tone plan's G8 reads the floor by
   case out of this file. Cases 8 and 9 below pin that split.
3. _is_complete() gained a fifth rule (one SHELL_XTALK_G5 per victim), with
   its own fixture at case 10. G5 is the gate that says the mux is where the
   table says it is, and a block that lost one victim's G5 line would
   otherwise be accepted with that victim's addressing evidence missing --
   the same argument read_settle.py's _one_per_pair() makes for BAND.

Cases 17 and 18 are the G6 recomputation, which is host-side because the
firmware prints only the bit: |control(d) - silent(d)| per victim, from the
row-1 and row-2 curves every block already carries, asserted against the
printed g6. Per-victim beats the firmware's single folded scalar, and unlike
a firmware field it can go red on a fixture -- which is what case 18 does.
"""
import sys

from read_xtalk import (parse_block, format_csv, format_meta_csv, deltas,
                        verdicts, control_deltas, g6_disagreement)

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def build_block(scan_settle_ns=2000000, grid_points=2, gates_ok=1,
                aggressor_means=(100, 100), control_means=(100, 100),
                silent_means=(100, 100), g6=None, aggressor_n=64,
                orphan_victim=False):
    """Two victims (one 5150 ohm divider, one 150 ohm tie), twelve cases:
    silent, control and one LED aggressor per victim, two static cases per
    victim, and one SKIPPED case per victim -- every real block has four of
    those and a fixture without one cannot test the only rule that sees them.

    `g6` overrides the G6 bit alone, so a block can print a passing gate over
    curves that do not support it -- the disagreement case 19 needs.

    `aggressor_n` is the row-7 point lines' surviving repeat count: an int
    for the whole curve, or a tuple for one per grid point, which is what a
    boundary point that no repeat converted at needs.

    `orphan_victim` adds a third victim that appears in a skipped case and in
    nothing else -- no floor curve, no control curve. G6 cannot be computed
    for it at all, and it is the one shape in which a skipped case reaches
    the per-victim arithmetic."""
    if g6 is None:
        g6 = gates_ok
    if not isinstance(aggressor_n, tuple):
        aggressor_n = (aggressor_n,) * grid_points
    n_victims = 3 if orphan_victim else 2
    n_cases = 13 if orphan_victim else 12
    lines = [
        "SHELL_XTALK_CFG adc_khz=6146 repeats=64 grid_ns=200 points=%d "
        "park_ns=20000 scan_settle_ns=%d rv4=0 git=4a7800f+"
        % (grid_points, scan_settle_ns),
        "SHELL_XTALK_RATE block_size=96 sr_hz=48000 cases=%d victims=%d"
        % (n_cases, n_victims),
        "SHELL_XTALK_CLK span_short_cyc=2000 span_long_cyc=30000 "
        "smp_short_tenths=165 smp_long_tenths=3875",
        "SHELL_XTALK_CAL lat_mean_ns=747 lat_min_ns=700 lat_max_ns=800 "
        "b0=32 timeouts=0",
        "SHELL_XTALK_SPAN zero=0 rail=63485 hi_spread=1 lo_spread=1 valid=1",
    ]
    victims = ((0, 8, 5150), (1, 3, 150))
    g5_victims = victims + (((1, 5, 650),) if orphan_victim else ())
    for g, ch, _r in g5_victims:
        lines.append("SHELL_XTALK_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=100 ok=1" % (g, ch))

    case = 0
    for vi, (g, ch, r) in enumerate(victims):
        for row, kind, wa, wb, means in (
                (1,  0, 0x28, 0x28, silent_means),
                (2,  1, 0x28, 0x28, control_means),
                (7,  1, 0x28, 0x3FE8, aggressor_means)):
            lines.append("SHELL_XTALK_CASE case=%d row=%d kind=%d "
                         "victim_group=%d victim_ch=%d r_src=%d word_a=%d "
                         "word_b=%d skipped=0"
                         % (case, row, kind, g, ch, r, wa, wb))
            # n= is the SURVIVING repeat count, not kRepeats: Task 5
            # re-purposed the field when it closed an undetected-timeout
            # path, so n < 64 is a mean over fewer real conversions and is
            # good data, while n == 0 is not a reading at all.
            for k in range(grid_points):
                n = aggressor_n[k] if row == 7 else 64
                lines.append("SHELL_XTALK case=%d d_ns=%d n=%d mean=%d "
                             "min=%d max=%d"
                             % (case, k * 200, n, means[k], means[k] - 2,
                                means[k] + 2))
            # Every grid case, not only the Silent one -- see deviation 1.
            # The floor rows get 9 and 10 so the two victims' floors are
            # distinguishable; every other curve gets a value no floor can
            # collide with, so a scope mix-up shows as a wrong number rather
            # than as a coincidence.
            spread = (9 + vi) if row == 1 else (30 + case)
            lines.append("SHELL_XTALK_STAT case=%d settled_mean_spread=%d "
                         "widest_sample_band=44 at_d_ns=0" % (case, spread))
            case += 1

    for vi, (g, ch, r) in enumerate(victims):
        for word, mean in ((0x28, 100), (0x3FE8, 104)):
            lines.append("SHELL_XTALK_CASE case=%d row=8 kind=3 "
                         "victim_group=%d victim_ch=%d r_src=%d word_a=%d "
                         "word_b=%d skipped=0" % (case, g, ch, r, word, word))
            lines.append("SHELL_XTALK_STATIC case=%d victim_group=%d "
                         "victim_ch=%d r_src=%d word=%d n=64 mean=%d min=%d "
                         "max=%d" % (case, g, ch, r, word, mean, mean - 2,
                                     mean + 2))
            case += 1

    # The skipped cases. Spec section 4: a skipped case is a line in the
    # output, never a silently shorter table -- so it has a _CASE line and
    # nothing behind it, no points, no _STAT and no _STATIC. It is also the
    # only shape in which rule 1 has work of its own: every other lost _CASE
    # line is caught by rule 2 as well, because the points behind it survive.
    for g, ch, r in victims:
        lines.append("SHELL_XTALK_CASE case=%d row=6 kind=1 victim_group=%d "
                     "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=1"
                     % (case, g, ch, r, 0x28, 0x2C))
        case += 1
    if orphan_victim:
        og, och, orr = g5_victims[2]
        lines.append("SHELL_XTALK_CASE case=%d row=6 kind=1 victim_group=%d "
                     "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=1"
                     % (case, og, och, orr, 0x28, 0x2C))
        case += 1

    # gates_ok is the FOLD, not a free field: xtalk_gates()::ok() is the AND
    # of the four, so a fixture that printed g6=0 beside gates_ok=1 would be
    # a block the firmware cannot emit.
    lines.append("SHELL_XTALK_GATES g2=%d g4=%d g5=%d g6=%d init_ok=1 "
                 "cal_ok=1 cfg_ok=1 gates_ok=%d"
                 % (gates_ok, gates_ok, gates_ok, g6,
                    1 if (gates_ok and g6) else 0))
    lines.append("SHELL_XTALK_END")
    return lines


def drop_first(lines, prefix):
    out = list(lines)
    for i, line in enumerate(out):
        if line.startswith(prefix):
            del out[i]
            return out
    raise AssertionError("no line starts with %r" % prefix)


# --- 1-4: a complete block, and what it carries ---
base = build_block()
block = parse_block(base)
check("a complete block parses", block is not None)
check("every case is kept, skipped ones included",
      block and len(block["cases"]) == 12
      and sum(c["skipped"] for c in block["cases"]) == 2)
check("every point is kept", block and len(block["points"]) == 12)
check("the verdict bits are carried",
      block and block["gates"]["gates_ok"] == 1)
# The one non-integer field the format prints. A parser that int()s every
# token refuses every real block on this line alone.
check("the git stamp survives as a string",
      block and block["cfg"]["git"] == "4a7800f+")

# --- 5: cut off before the end marker ---
check("a block truncated before SHELL_XTALK_END is refused",
      parse_block(base[:-1]) is None)

# --- 6: a missing point row (rule 2) ---
# A serial read timeout produces exactly this: the end marker arrives, a row
# does not. Only a per-case point count catches it; a total would not, because
# another case still has its points.
check("a block missing one case's point row is refused",
      parse_block(drop_first(base, "SHELL_XTALK case=1 d_ns=200")) is None)

# --- 7: a missing CASE line (rule 1) ---
check("a block missing a SHELL_XTALK_CASE line is refused",
      parse_block(drop_first(base, "SHELL_XTALK_CASE case=2 ")) is None)
# Rule 1's OWN work, which no other rule does: a skipped case has no points,
# no statistic and no static measurement, so losing its _CASE line leaves
# rules 2-5 with nothing to notice. Only the count and the 0..cases-1
# identity catch it.
check("a block missing a SKIPPED case's line is refused too",
      parse_block(drop_first(base, "SHELL_XTALK_CASE case=10 ")) is None)

# --- 8: a missing STATIC line (rule 3) ---
check("a block whose static case has no measurement is refused",
      parse_block(drop_first(base, "SHELL_XTALK_STATIC case=6 ")) is None)

# --- 9: a missing STAT line (rule 4) ---
# Rule 3 cannot see this one: the static cases all still have their
# measurements. Rule 2 cannot either: every point line is present.
check("a block missing one grid case's STAT line is refused",
      parse_block(drop_first(base, "SHELL_XTALK_STAT case=1 ")) is None)

# --- 10: a missing G5 line (rule 5) ---
# And none of rules 1-4 can see this one: every case, point, static and stat
# is present. What is missing is the evidence that victim 1 was where the
# table says it was.
check("a block missing one victim's G5 line is refused",
      parse_block(drop_first(base, "SHELL_XTALK_G5 victim_group=1")) is None)

# --- 10a: the block-level lines the module docstring promises (rule 0) ---
# Losing _CAL used to leave the block ACCEPTED, both files written, and
# main() raising a TypeError on block["cal"] with the output already on
# disk. Losing _SPAN was quieter: accepted, and the metadata file silently
# without the span row case 17 below asserts must be there.
for _line, _why in (("SHELL_XTALK_CLK", "the measured clock"),
                    ("SHELL_XTALK_CAL", "the calibration pass"),
                    ("SHELL_XTALK_SPAN", "the span G5 was judged against")):
    check("a block missing %s (%s) is refused" % (_line, _why),
          parse_block(drop_first(base, _line)) is None)

# --- 10b: the grid is checked by d_ns, not by how many rows arrived ---
# A case with two d_ns=0 rows and no d_ns=200 has the right COUNT and the
# wrong grid; accepting it makes deltas() difference one point fewer than the
# row reports, with nothing saying so.
duped = list(base)
for i, line in enumerate(duped):
    if line.startswith("SHELL_XTALK case=1 d_ns=200"):
        duped[i] = line.replace("d_ns=200", "d_ns=0")
        break
check("a block whose grid has a duplicate d_ns and a missing one is refused",
      parse_block(duped) is None)

# --- 11: a failed run still parses and is marked ---
failed = parse_block(build_block(gates_ok=0))
check("a failed run still parses", failed is not None)
check("a failed run is marked", failed and failed["gates"]["gates_ok"] == 0)

# --- 12: the delta arithmetic ---
# delta(d) = mean_aggressor(d) - mean_control(d), per victim. Not against the
# silent curve: the control carries the same shift and the same latch pulse,
# so the difference isolates the bit change.
d = deltas(parse_block(build_block(aggressor_means=(106, 103),
                                   control_means=(100, 100))))
check("delta is aggressor minus control, per grid point",
      d and [row["delta"] for row in d if row["case"] == 2] == [6, 3])
check("delta is computed for every victim's aggressor",
      d and sorted({row["case"] for row in d}) == [2, 5])
# Discriminates "aggressor minus control" from "aggressor minus silent": with
# a control that is NOT the silent curve, the two answers differ.
shifted = deltas(parse_block(build_block(aggressor_means=(106, 103),
                                         control_means=(102, 101),
                                         silent_means=(100, 100))))
check("delta is measured against the control curve, not the silent one",
      shifted and [row["delta"] for row in shifted if row["case"] == 2]
      == [4, 2])

# --- 13: the verdict, when the criterion lies past the grid ---
# The shipping scan waits a whole audio block (2 ms) and the grid ends at
# 12.8 us, so NO grid point is at or past the criterion boundary. The verdict
# must NOT be computed over an empty set and reported as a pass; it falls
# back to the whole-grid envelope and says so.
v = verdicts(parse_block(build_block(aggressor_means=(106, 103))))
check("a criterion past the end of the grid reports an envelope",
      v and all(row["verdict_basis"] == "envelope" for row in v))
check("the envelope is the largest magnitude over the whole grid",
      v and max(row["worst_delta"] for row in v) == 6)
check("the envelope verdict still applies the 8-count criterion",
      v and all(row["pass"] for row in v))
fail_v = verdicts(parse_block(build_block(aggressor_means=(109, 100))))
check("an envelope past 8 counts fails",
      fail_v and not all(row["pass"] for row in fail_v))

# --- 14: the verdict, when the criterion lies inside the grid ---
# The other branch, which a shipping firmware that waited 400 ns would take.
# The point at d_ns=0 is BEFORE the boundary and is characterisation, not a
# verdict; only d_ns=200 counts.
inside = verdicts(parse_block(build_block(scan_settle_ns=200,
                                          aggressor_means=(900, 103))))
check("a criterion inside the grid uses the criterion",
      inside and all(row["verdict_basis"] == "criterion" for row in inside))
check("points before the boundary are characterisation, not verdict",
      inside and max(row["worst_delta"] for row in inside) == 3)
check("and the verdict passes on the points that count",
      inside and all(row["pass"] for row in inside))
# The same grid with the excursion AFTER the boundary fails -- so the check
# above is about which points count, not about the criterion being toothless
# on this branch.
inside_fail = verdicts(parse_block(build_block(scan_settle_ns=200,
                                               aggressor_means=(103, 900))))
check("an excursion at or past the boundary fails the criterion",
      inside_fail and not any(row["pass"] for row in inside_fail))

# --- 14a: the basis comes from the GRID, not from the rows that survived ---
# The boundary is inside the grid (it IS the last point) and the only point
# at or past it was never converted. Choosing the basis from the surviving
# rows would find none past the boundary, fall through to the envelope,
# label the row "over the WHOLE grid" and PASS on a point that is
# characterisation -- the points that count being unusable, silently turned
# into a pass. That is not an envelope. It is no data.
starved = verdicts(parse_block(build_block(scan_settle_ns=200,
                                           aggressor_means=(106, 103),
                                           aggressor_n=(64, 0))))
check("a boundary inside the grid never falls back to the envelope",
      starved and all(row["verdict_basis"] != "envelope" for row in starved))
check("an unusable boundary point is no data, not a verdict",
      starved and all(row["verdict_basis"] == "no-data"
                      and row["worst_delta"] == -1 for row in starved))
check("and no data is never a pass",
      starved and not any(row["pass"] for row in starved))
# The same grid with the boundary point converted takes the criterion branch,
# so 14a is about the points being unusable and not about the boundary.
fed = verdicts(parse_block(build_block(scan_settle_ns=200,
                                       aggressor_means=(106, 103))))
check("the same boundary with a usable point takes the criterion",
      fed and all(row["verdict_basis"] == "criterion" for row in fed))

# --- 14b: how many points the verdict actually rests on, on every row ---
# A verdict over one surviving point and one over two are not the same
# claim, and a row that does not say which leaves a reader assuming the grid
# was whole.
check("every verdict row says how many points it rests on",
      all(row["points_considered"] == 1 for row in fed)
      and all(row["points_considered"] == 2
              for row in verdicts(parse_block(build_block()))))

# --- 15a: n is read per line, and n == 0 is not a reading ---
# Every point line in Task 6's capture reads n=64, but the field carries the
# surviving repeat count and a reader that assumed 64 would fold a timed-out
# point's mean in as a level. A mean over 30 surviving repeats is data.
thin = deltas(parse_block(build_block(aggressor_means=(106, 103),
                                      aggressor_n=30)))
check("a point over fewer surviving repeats is still data",
      [row["delta"] for row in thin if row["case"] == 2] == [6, 3]
      and all(row["n"] == 30 for row in thin))
# A case no repeat converted at yields no delta, and must NOT vanish from the
# verdict list: fewer verdicts out than aggressors in is a run nobody counted.
dead = build_block(aggressor_means=(106, 103), aggressor_n=0)
check("a case with no converted repeat yields no delta",
      deltas(parse_block(dead)) == [])
dead_v = verdicts(parse_block(dead))
check("but it still gets a verdict row, one per aggressor case",
      sorted(row["case"] for row in dead_v) == [2, 5])
check("and that row is a refusal, not a pass",
      all(row["verdict_basis"] == "no-data" and not row["pass"]
          for row in dead_v))

# --- 15b: a skipped case is carried, and is not differenced or judged ---
check("a skipped case yields no delta and no verdict",
      all(row["case"] not in (10, 11) for row in deltas(block))
      and all(row["case"] not in (10, 11)
              for row in verdicts(parse_block(base))))

# --- 15: a failed gate refuses the verdict ---
check("a run whose gates failed yields no per-aggressor verdict",
      verdicts(parse_block(build_block(gates_ok=0))) == [])

# --- 16: the CSV ---
csv = format_csv(block)
check("the CSV has a header and one row per point",
      len(csv.strip().split("\n")) == 13)
check("the CSV carries the case's row so the spec table is recoverable",
      "row" in csv.splitlines()[0])
# The computed quantity belongs in the file, not only on stderr: stderr is
# not a record, which is the argument read_settle.py's metadata file was
# added for.
delta_csv = format_csv(parse_block(build_block(aggressor_means=(106, 103))))
header = delta_csv.splitlines()[0].split(",")
rows = [dict(zip(header, r.split(","))) for r in delta_csv.splitlines()[1:]]
check("the CSV carries delta for an aggressor point",
      [r["delta"] for r in rows if r["case"] == "2"] == ["6", "3"])
check("and leaves it empty where there is no aggressor to difference",
      all(r["delta"] == "" for r in rows if r["case"] in ("0", "1")))

# --- 17: the metadata CSV ---
meta_rows = [r.split(",") for r in format_meta_csv(block).strip().split("\n")]
check("the metadata CSV has the four-column header",
      meta_rows[0] == ["scope", "case", "key", "value"])
meta = {(r[0], r[1], r[2]): r[3] for r in meta_rows[1:]}
check("the metadata carries the configuration",
      meta.get(("cfg", "", "scan_settle_ns")) == "2000000")
check("the metadata carries the git stamp of the image that printed it",
      meta.get(("cfg", "", "git")) == "4a7800f+")
check("the metadata carries the block period's two inputs",
      meta.get(("rate", "", "block_size")) == "96"
      and meta.get(("rate", "", "sr_hz")) == "48000")
check("the metadata carries the gate verdicts",
      all(meta.get(("gates", "", g)) == "1" for g in ("g2", "g4", "g5", "g6")))
check("the metadata carries the span G5 was judged against",
      meta.get(("span", "", "rail")) == "63485")
# The codec-tone probe's G8 reads exactly this, by case, off this file.
check("the metadata carries each silent case's settled mean spread",
      meta.get(("silent", "0", "settled_mean_spread")) == "9"
      and meta.get(("silent", "3", "settled_mean_spread")) == "10")
# Deviation 2. A control's spread is not a floor and G8 must not read it as
# one, so it is present under its own scope and absent from "silent".
check("a control's spread is not filed under the silent scope",
      meta.get(("silent", "1", "settled_mean_spread")) is None
      and meta.get(("curve", "1", "settled_mean_spread")) == "31")
check("the metadata carries the static cases' means",
      meta.get(("static", "6", "mean")) == "100"
      and meta.get(("static", "7", "mean")) == "104")
# A skipped case has no points, no stat and no static, so the CASE line is
# the only place it survives at all. Spec section 4: a skipped case is a line
# in the output, never a silently shorter table.
check("the metadata carries every case's own line",
      meta.get(("case", "4", "row")) == "2"
      and meta.get(("case", "4", "skipped")) == "0"
      and meta.get(("case", "9", "kind")) == "3")
check("a block-level row leaves the case column empty, not 0",
      all(r[1] == "" for r in meta_rows[1:]
          if r[0] in ("cfg", "rate", "clk", "cal", "span", "gates")))

# --- 18: G6, recomputed per victim from the printed curves ---
# The firmware prints only the bit, so a capture cannot tell a healthy gate
# from a marginal one. This is the reader's own arithmetic on numbers the
# block already carries.
agree = parse_block(build_block(silent_means=(100, 100),
                                control_means=(106, 103)))
cd = control_deltas(agree)
check("the control delta is computed for every victim",
      len(cd) == 2 and all(row["points_compared"] == 2 for row in cd))
check("the control delta is the largest magnitude over the grid",
      all(row["worst_control_delta"] == 6 for row in cd))
check("a g6=1 whose curves support it reports agreement",
      all(row["agrees_with_g6"] for row in cd)
      and g6_disagreement(agree) is None)
meta_g6 = {(r[0], r[1], r[2]): r[3]
           for r in [x.split(",") for x in
                     format_meta_csv(agree).strip().split("\n")[1:]]}
check("the per-victim control delta reaches the metadata file",
      meta_g6.get(("g6", "0", "worst_control_delta")) == "6"
      and meta_g6.get(("g6", "1", "worst_control_delta")) == "6")

# --- 19: a g6=1 the curves do NOT support ---
# The firmware and the reader disagreeing about the gate that licenses every
# aggressor delta is the loudest thing this reader can find, and it is the
# reason the recomputation is per-victim rather than a single scalar.
lying = parse_block(build_block(silent_means=(100, 100),
                                control_means=(112, 100), g6=1))
cd_bad = control_deltas(lying)
check("a g6=1 whose curves exceed the bound is reported as a disagreement",
      not all(row["agrees_with_g6"] for row in cd_bad))
check("the disagreement names the magnitude, not just the fact",
      any(row["worst_control_delta"] == 12 for row in cd_bad))
msg = g6_disagreement(lying)
check("and the disagreement has a message a reader cannot miss",
      msg is not None and "12" in msg)
# The other direction: a failed g6 nothing in the curves accounts for. Not
# the same fault, and reporting it as the same one would hide it.
quiet_fail = parse_block(build_block(gates_ok=1, g6=0,
                                     silent_means=(100, 100),
                                     control_means=(101, 100)))
check("a g6=0 no curve accounts for is also a disagreement",
      g6_disagreement(quiet_fail) is not None)

# --- 19a: a victim that appears only in a skipped case ---
# No floor curve and no control curve, so G6 cannot be computed for it at
# all. It must not read as a pass, it must not be dropped from the per-victim
# table, and -- the reason this fixture exists -- r_src must come off the
# skipped case rather than stay None, because main() formats it with %d.
orphan = parse_block(build_block(orphan_victim=True))
check("a victim with no curves still parses and is still a victim",
      orphan is not None and len(control_deltas(orphan)) == 3)
orphan_row = control_deltas(orphan)[2] if orphan else {}
check("a victim with no curves is compared at no points and is not in bound",
      orphan_row.get("points_compared") == 0
      and orphan_row.get("within_bound") is False)
check("and its r_src comes off the skipped case, not None",
      orphan_row.get("r_src") == 650)
check("a g6=1 over a victim that could not be compared is a disagreement",
      g6_disagreement(orphan) is not None)

# --- 20: libDaisy's "$$" overflow marker ---
truncated = list(base)
truncated[-2] = truncated[-2][:-2] + "$$"
check("a $$-truncated field does not crash the parser, and yields no block",
      parse_block(truncated) is None)
spliced = list(base)
spliced[-1] = "S$$SHELL_XTALK_END"
check("a spliced end marker does not crash the parser, and yields no block",
      parse_block(spliced) is None)
# logger.cpp:78-87 stamps "$$" only after a Transmit() the host did not
# drain, and tx_ptr_ is not reset -- so the fusion lands wherever the next
# PrintLine happens to be, not on the first line after connect. A block is
# tens of kilobytes; this one is in the middle of it.
mid = list(base)
for i, line in enumerate(mid):
    if line.startswith("SHELL_XTALK case=1 d_ns=0"):
        mid[i] = line[:-3] + "$$SHELL_XTALK case=1 d_ns=200 n=64 mean=100 "
        break
check("a fusion in the middle of a block does not crash it either, "
      "and yields no block", parse_block(mid) is None)
# And a second, whole block behind a corrupted one still gets its chance:
# the firmware repeats forever and the reader keeps listening.
check("a corrupted block does not poison the block that follows it",
      parse_block(mid + base) is not None)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_xtalk guard: ok")
