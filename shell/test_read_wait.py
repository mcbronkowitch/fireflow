"""Guard for read_wait.py's parser, its shift arithmetic and G9.

Runs as a plain script; pytest is not installed on this machine (memory
`fireflow-tools-guards-have-no-runner`: the guards that had no runner stood
red for 23 days).

build_block() writes a whole block in the exact shape wait_probe.cpp's
PrintLine calls produce -- transcribed from the firmware, not from the spec.
A fixture written by the same hand as the parser agrees with it by
construction, so section H also parses the first complete block the board
printed (testdata/wait-block-e22a628.txt, lines 244-520 of
docs/hardware/captures/wait-capture-e22a628.txt), the way test_read_tone.py's
section B does.
"""
import io
import os
import sys
from contextlib import redirect_stderr

import pot_round
from read_wait import (parse_block, shifts, g9, tie_faults, report,
                       format_csv, WAIT_GRID_US, CODEC_GRID_US, G9_LO, G9_HI,
                       CSV_FIELDS, pot_readings, victims, format_meta_csv)

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


# kXtalkVictimTable's order and impedances (xtalk_plan.h), and the rung each
# arm reads it at, in tenths: 5150 ohm -> 2.5 cycles, 650/150 -> 1.5 cycles,
# arm L -> 387.5.
VICTIMS = ((0, 8, 5150, 25), (1, 6, 5150, 25), (0, 9, 650, 15),
           (0, 10, 150, 15), (1, 3, 150, 15))
BASE = {(0, 8): 31700, (0, 9): 32750, (1, 6): 31950, (0, 10): 0, (1, 3): 0}
POT_VICTIMS = ((0, 2, 2650, 15), (0, 6, 5150, 25), (1, 2, 2650, 15))


def build_block(g9_shift=-852, gates_ok=1, sweep_dir=0, tie_shift=0,
                drop_point=None, dup_point=False, overflow=False, n_at=None,
                pots=False, pot_base=32768):
    """One complete block. Every arm's shift is a simple function of W so a
    test can predict it; arm C REF_A at 10 ms is exactly `g9_shift`."""
    lines = ["SHELL_WAIT_CFG adc_khz=6146 repeats=64 points=15 block_size=96 "
             "sr=48000 sweep_dir=%d git=%s"
             % (sweep_dir, "deadbee$$" if overflow else "deadbee"),
             "SHELL_WAIT_CLK span_short_cyc=1300 span_long_cyc=30300 "
             "smp_short_tenths=165 smp_long_tenths=3875",
             "SHELL_WAIT_CAL lat_mean_ns=690 lat_min_ns=640 lat_max_ns=770 "
             "b0=17 timeouts=0"]
    if pots:
        lines[1:1] = pot_round.sample_lines()
    victims_used = VICTIMS + (POT_VICTIMS if pots else ())
    base = dict(BASE)
    if pots:
        base.update({(0, 2): pot_base, (0, 6): pot_base, (1, 2): pot_base})
    for arm in range(4):
        grid = CODEC_GRID_US if arm == 3 else WAIT_GRID_US
        order = list(grid) if sweep_dir == 0 else list(reversed(grid))
        for v, (g, ch, r_src, rung) in enumerate(victims_used):
            case = arm * len(victims_used) + v
            lines.append("SHELL_WAIT_CASE case=%d arm=%d victim_group=%d "
                         "victim_ch=%d r_src=%d rung_tenths=%d codec=%d"
                         % (case, arm, g, ch, r_src,
                            3875 if arm == 2 else rung, 1 if arm == 3 else 0))
            for w in order:
                if drop_point == (case, w):
                    continue
                if r_src == 150:
                    shift = tie_shift if w else 0
                elif arm == 3 and (g, ch) == (0, 8) and w == 10000:
                    shift = g9_shift
                else:
                    shift = -(w // 20)   # any monotone law will do
                mean = base[(g, ch)] + shift
                if n_at == (case, w):
                    line = ("SHELL_WAIT case=%d w_us=%d n=0 mean=-1 min=-1 "
                            "max=-1" % (case, w))
                else:
                    line = ("SHELL_WAIT case=%d w_us=%d n=64 mean=%d min=%d "
                            "max=%d" % (case, w, mean, mean - 5, mean + 5))
                lines.append(line)
                if dup_point and case == 0 and w == 0:
                    lines.append(line)
    lines.append("SHELL_WAIT_SPAN zero=0 rail=65532 hi_spread=0 lo_spread=0 "
                 "valid=1")
    for (g, ch, _, _) in victims_used:
        lines.append("SHELL_WAIT_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=%d ok=1" % (g, ch, base[(g, ch)]))
    lines.append("SHELL_WAIT_GATES g2=1 g4=1 g5=1 g7=1 g9=-1 gates_ok=%d"
                 % gates_ok)
    lines.append("SHELL_WAIT_HEALTH missed_blocks=0 timeouts=0 block_ms=90123")
    lines.append("SHELL_WAIT_END")
    return lines


def run_report(block):
    buf = io.StringIO()
    with redirect_stderr(buf):
        code = report(block)
    return code, buf.getvalue()


# --- A. A healthy block parses, and the shift is what the fixture put in ---
b = parse_block(build_block())
check("A1 a complete block parses", b is not None)
rows = shifts(b)
check("A2 one row per grid point: 15 x 15 + 4 x 5",
      len(rows) == 15 * 15 + 4 * 5)
check("A3 every W=0 shift is exactly 0",
      all(r["shift"] == 0 for r in rows if r["w_us"] == 0))
check("A4 a W=1000 shift on arm A REF_A is -50 (mean(W) - mean(0))",
      [r["shift"] for r in rows
       if r["arm"] == 0 and r["victim"] == (0, 8) and r["w_us"] == 1000]
      == [-50])
check("A5 G9 passes at -852", g9(b) == (True, -852))
check("A6 the healthy block exits 0", run_report(b)[0] == 0)
check("A7 the CSV has a header and one line per row",
      format_csv(b).count("\n") == len(rows) + 1
      and format_csv(b).startswith(",".join(CSV_FIELDS)))

# --- B. G9's four boundaries: inclusive at -800 and -900, red one past each ---
for shift, want in ((-800, True), (-900, True), (-799, False), (-901, False)):
    bb = parse_block(build_block(g9_shift=shift))
    check("B g9 at %d is %s" % (shift, want), g9(bb) == (want, shift))
    check("B exit code at %d" % shift,
          run_report(bb)[0] == (0 if want else 1))
check("B bounds are the spec's", (G9_LO, G9_HI) == (-900, -800))

# --- C. The firmware's gates refuse on their own ---
code, text = run_report(parse_block(build_block(gates_ok=0)))
check("C1 gates_ok=0 exits 1", code == 1)
check("C2 and says why", "gates_ok is 0" in text)

# --- D. Descending sweeps parse to the same table ---
check("D1 sweep_dir=1 parses",
      parse_block(build_block(sweep_dir=1)) is not None)
check("D2 and yields the same shifts",
      [r["shift"] for r in shifts(parse_block(build_block(sweep_dir=1)))]
      == [r["shift"] for r in rows])

# --- E. Incomplete or corrupted blocks are refused, never half-read ---
check("E1 a missing point refuses the block",
      parse_block(build_block(drop_point=(15, 10000))) is None)
check("E2 a missing G9 reference (arm C REF_A, W=0) refuses the block",
      parse_block(build_block(drop_point=(15, 0))) is None)
check("E2b a missing G9 point (arm C REF_A, W=10 ms) refuses the block",
      parse_block(build_block(drop_point=(15, 10000))) is None)
check("E3 a duplicated point refuses the block",
      parse_block(build_block(dup_point=True)) is None)
check("E4 a $$ overflow marker refuses the block",
      parse_block(build_block(overflow=True)) is None)
truncated = build_block()
truncated = [l for l in truncated if not l.startswith("SHELL_WAIT_HEALTH")]
check("E5 a block without its HEALTH line is refused",
      parse_block(truncated) is None)
check("E6 a block that never reaches END is refused",
      parse_block(build_block()[:-1]) is None)
off_grid = [l.replace("w_us=50000 ", "w_us=40000 ") for l in build_block()]
check("E7 a point off the grid refuses the block",
      parse_block(off_grid) is None)

cut = [l.replace(" block_ms=90123", "") for l in build_block()]
check("E8 a line cut short without a $$ marker refuses the block",
      parse_block(cut) is None)
g5_dup = build_block()
i = [k for k, l in enumerate(g5_dup) if l.startswith("SHELL_WAIT_G5")]
g5_dup[i[1]] = g5_dup[i[0]]
check("E9 five G5 lines naming only four victims refuse the block",
      parse_block(g5_dup) is None)

# --- G. A timed-out point is not a reference, and G9 then refuses ---
bn = parse_block(build_block(n_at=(15, 10000)))
check("G1 an n=0 point parses", bn is not None)
check("G2 and its shift is None", g9(bn) == (None, None))
code, text = run_report(bn)
check("G3 an uncomputable G9 exits 1", code == 1 and "NOT COMPUTABLE" in text)
bz = parse_block(build_block(n_at=(15, 0)))
check("G4 an n=0 reference voids every shift of its case",
      all(r["shift"] is None for r in shifts(bz) if r["case"] == 15))
code, text = run_report(parse_block(build_block(gates_ok=0, g9_shift=-700)))
check("G5 both refusals are named", "gates_ok is 0; G9" in text)

# --- F. A moving tie is reported, and does not change the exit code ---
bt = parse_block(build_block(tie_shift=-3))
check("F1 tie faults are found", len(tie_faults(bt)) > 0)
code, text = run_report(bt)
check("F2 and reported", "TIE MOVED" in text)
check("F3 but not gated", code == 0)
check("F4 a clean block has no tie faults", tie_faults(b) == [])
check("F5 a tie at +-1 is inside the measured tolerance",
      tie_faults(parse_block(build_block(tie_shift=1))) == []
      and tie_faults(parse_block(build_block(tie_shift=-1))) == [])
check("F6 a tie at 2 is reported",
      len(tie_faults(parse_block(build_block(tie_shift=2)))) > 0)

# --- P. a pot-round block (spec 2026-09-28 sections 5, 6, 7) ---
pb = parse_block(build_block(pots=True))
check("P1 an eight-victim pot block parses", pb is not None)
check("P2 it counts eight victims and 32 cases",
      pb is not None and victims(pb) == 8 and len(pb["cases"]) == 32)
check("P3 the old five-victim block still counts five",
      victims(parse_block(build_block())) == 5)
check("P4 one reading per pot, from arm L at W=0",
      pb is not None and pot_readings(pb) == [("RV2", 32768), ("RV4", 32768),
                                              ("RV6", 32768)])
check("P5 a pot block exits 0 at mid travel", run_report(pb)[0] == 0)
check("P6 a pot at 37356 fails PG1 and the exit code",
      run_report(parse_block(build_block(pots=True, pot_base=37356)))[0] == 1)
check("P7 a pot at 37355 passes",
      run_report(parse_block(build_block(pots=True, pot_base=37355)))[0] == 0)
no_ids = [l for l in build_block(pots=True) if not l.startswith("SHELL_POT_ID idx=2")]
check("P8 a pot block missing an ID line is refused", parse_block(no_ids) is None)
meta = {tuple(r.split(",")[:3]): r.split(",")[3]
        for r in format_meta_csv(pb).strip().split("\n")[1:]}
check("P9 the metadata carries the gate, G9, PG1 and the pots",
      meta.get(("gates", "", "gates_ok")) == "1"
      and meta.get(("host", "", "g9_pass")) == "1"
      and meta.get(("host", "", "pg1_pass")) == "1"
      and meta.get(("pot_id", "2", "name")) == "RV6")
check("P10 a block with no pot lines has no readings and pg1_pass=1",
      pot_readings(parse_block(build_block())) == []
      and ("host", "", "pg1_pass") in
      {tuple(r.split(",")[:3]) for r in
       format_meta_csv(parse_block(build_block())).strip().split("\n")[1:]})

# --- H. The first real block the board printed parses, and says what the
# write-up says. docs/hardware/wait-measured.md quotes these numbers; a
# firmware or reader change that moved them would leave the document
# asserting something the code no longer produces. ---
REAL_BLOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "testdata", "wait-block-e22a628.txt")
with io.open(REAL_BLOCK, encoding="utf-8") as fh:
    rb = parse_block(fh)
check("H1 the vendored board block parses", rb is not None)
rrows = shifts(rb)
code, text = run_report(rb)
check("H2 it passes every gate and G9, and exits 0", code == 0)
check("H3 G9 reads -865 on it", g9(rb) == (True, -865))
check("H4 no tie moves beyond tolerance", tie_faults(rb) == [])


def real(arm, victim, w):
    return [r["shift"] for r in rrows
            if r["arm"] == arm and r["victim"] == victim and r["w_us"] == w][0]


check("H5 arm A REF_A saturates: -804 at 5 ms, -856 at 10 ms, -855 at 50 ms",
      (real(0, (0, 8), 5000), real(0, (0, 8), 10000), real(0, (0, 8), 50000))
      == (-804, -856, -855))
check("H6 arm L is flat within 2 counts on every victim and W",
      all(abs(r["shift"]) <= 2 for r in rrows if r["arm"] == 2))
check("H7 arm B leaves a residue: REF_A between -30 and -45 from 1 ms up",
      all(-45 <= real(1, (0, 8), w) <= -30 for w in (1000, 5000, 10000, 50000)))

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_wait guard: ok")
