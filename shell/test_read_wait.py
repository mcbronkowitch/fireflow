"""Guard for read_wait.py's parser, its shift arithmetic and G9.

Runs as a plain script; pytest is not installed on this machine (memory
`fireflow-tools-guards-have-no-runner`: the guards that had no runner stood
red for 23 days).

build_block() writes a whole block in the exact shape wait_probe.cpp's
PrintLine calls produce -- transcribed from the firmware, not from the spec.
There is no board capture yet to vendor beside it, which is the one gap this
guard cannot close: a fixture written by the same hand as the parser agrees
with it by construction. **The first real block should be vendored under
testdata/ and parsed here**, the way test_read_tone.py's section B does.
"""
import io
import sys
from contextlib import redirect_stderr

from read_wait import (parse_block, shifts, g9, tie_faults, report,
                       format_csv, WAIT_GRID_US, CODEC_GRID_US, G9_LO, G9_HI,
                       CSV_FIELDS)

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


def build_block(g9_shift=-852, gates_ok=1, sweep_dir=0, tie_shift=0,
                drop_point=None, dup_point=False, overflow=False, n_at=None):
    """One complete block. Every arm's shift is a simple function of W so a
    test can predict it; arm C REF_A at 10 ms is exactly `g9_shift`."""
    lines = ["SHELL_WAIT_CFG adc_khz=6146 repeats=64 points=15 block_size=96 "
             "sr=48000 sweep_dir=%d git=%s"
             % (sweep_dir, "deadbee$$" if overflow else "deadbee"),
             "SHELL_WAIT_CLK span_short_cyc=1300 span_long_cyc=30300 "
             "smp_short_tenths=165 smp_long_tenths=3875",
             "SHELL_WAIT_CAL lat_mean_ns=690 lat_min_ns=640 lat_max_ns=770 "
             "b0=17 timeouts=0"]
    for arm in range(4):
        grid = CODEC_GRID_US if arm == 3 else WAIT_GRID_US
        order = list(grid) if sweep_dir == 0 else list(reversed(grid))
        for v, (g, ch, r_src, rung) in enumerate(VICTIMS):
            case = arm * 5 + v
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
                mean = BASE[(g, ch)] + shift
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
    for (g, ch, _, _) in VICTIMS:
        lines.append("SHELL_WAIT_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=%d ok=1" % (g, ch, BASE[(g, ch)]))
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

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_wait guard: ok")
