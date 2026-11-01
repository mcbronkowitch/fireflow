"""Host test: read_rgrain.py against synthetic captures of the reverb grain
layer (SHELL_RGRAIN=1 and 2; spec
docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md section 5, B1
and B2).

  - test_slope_fit: per-window means of 800 000 + 18 000 n for n in
    {0, 2, 4, 8}, in the board's interleaved order over three rounds on two
    workloads, give a slope of 18 000 +- 1, and both halves agree.
  - test_slope_halves_disagree: the second half's slope is 21 000; the
    halves are 15 % apart and the verdict fails.
  - test_cap_verdict: grains off reach a worst block of 963 000; the cap's
    975 000 passes with an 18 000 slope, 990 000 does not, and one guard trip
    more than the off run fails too.
  - test_silent_layer_fails: frames with n=4 and rms_x1e6=0 fail (a layer
    that runs but reaches nothing), in B1 and, as granted_sum > 0 with no
    rms, in B2.

Usage: test_read_rgrain.py [READER_DIR]   (default: this directory; point it
at a copy of a sabotaged read_rgrain.py to see the test go red). Plain script,
the exit code is the verdict.
"""
import io
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
if len(sys.argv) > 1:
    sys.path.insert(0, os.path.abspath(sys.argv[1]))
else:
    sys.path.insert(0, here)
import read_rgrain   # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


BLOCKS = 250
# The board's B1 order (sd_schedule.cpp rgrain_n): phases of 20 frames, the
# sequence 0, 8, 2, 4, 4, 2, 8, 0 repeating; a round is four phases.
B1_SEQ = (0, 8, 2, 4, 4, 2, 8, 0)
PHASE = 20


def frame(wid, mode, n, cap, granted, mean, win, rms, tripped=0, counting=1, gen=1):
    """One report pass as panel_scan.cpp prints it: run line, LAW, WIN, RGRAIN.
    Every other pass carries the host time prefix remote_run.py writes."""
    return [
        "SHELL_REMOTE run id=%d gen=%d counting=%d void=0 tripped=%d trips=0 gest=-1 "
        "tog=00000000" % (wid, gen, counting, tripped),
        "SHELL_PLAY_LAW cyc_last=11000 cyc_max=12000 led_last=900 led_max=1000 "
        "cb_last=%d cb_max=%d" % (int(mean), win),
        "SHELL_PLAY_WIN win=%d gap=960100 over=0 n=%d sum=%d" % (win, BLOCKS, int(mean * BLOCKS)),
        "SHELL_RGRAIN mode=%d n=%d cap=%d granted_sum=%d blocks=%d swing=0 rms_x1e6=%d"
        % (mode, n, cap, granted, BLOCKS, rms),
    ]


def with_time(lines):
    return ["%d.%03d %s" % (i // 2, i % 1000, l) if i % 2 else l for i, l in enumerate(lines)]


def b1_capture(slopes, rounds=3, workloads=(90, 127), rms=lambda n: 1000 * n,
               base=800000):
    """B1 frames: per workload, `rounds` rounds of four 20-frame phases in
    the board's order. slopes(r) is the slope of round r; every frame's mean
    is base + slope * n and its worst 40 000 above."""
    lines = []
    for wid in workloads:
        lines.append("SHELL_REMOTE load id=%d gen=1 gest=-1 name=w%d input=noise settle=500 "
                     "excite=0,0 rec=0,0" % (wid, wid))
        for ph in range(4 * rounds):
            n = B1_SEQ[ph % 8]
            s = slopes(ph // 4)
            for _ in range(PHASE):
                mean = base + s * n
                lines += frame(wid, 1, n, 0, n * BLOCKS, mean, int(mean) + 40000, rms(n))
    return with_time(lines)


def test_slope_fit():
    r = read_rgrain.b1(read_rgrain.parse(b1_capture(lambda _r: 18000))[0])
    check("slope_fit: slope 18 000 +- 1 (got %s)" % r["slope"],
          r["slope"] is not None and abs(r["slope"] - 18000) <= 1)
    check("slope_fit: both halves 18 000 +- 1 (got %s, %s)" % (r["slope_a"], r["slope_b"]),
          r["slope_a"] is not None and r["slope_b"] is not None
          and abs(r["slope_a"] - 18000) <= 1 and abs(r["slope_b"] - 18000) <= 1)
    check("slope_fit: the halves agree, verdict passes (%s)" % r["why"], r["ok"])
    check("slope_fit: both workloads, four n each",
          sorted(r["workloads"]) == [90, 127]
          and all(sorted(r["workloads"][w]["n"]) == [0, 2, 4, 8] for w in r["workloads"]))
    # The rooms at the N=0 mean, p99 and worst block, against the click line:
    # (945 000 - 800 000) / 18 000 = 8.06, (945 000 - 840 000) / 18 000 = 5.8.
    rm = r["workloads"][90]["room"]
    check("slope_fit: rooms 8 / 5 / 5 grains at mean / p99 / worst (got %s)" % rm,
          rm == {"mean": 8, "p99": 5, "worst": 5})
    out = io.StringIO()
    code = read_rgrain.report(b1_capture(lambda _r: 18000), out)
    check("slope_fit: report exits 0 and prints the slope",
          code == 0 and "slope 18000" in out.getvalue())


def test_slope_halves_disagree():
    # Six rounds, so the per-n halves are exactly rounds 0-2 and 3-5.
    r = read_rgrain.b1(read_rgrain.parse(
        b1_capture(lambda rr: 18000 if rr < 3 else 21000, rounds=6))[0])
    check("halves_disagree: first half 18 000, second 21 000 (got %s, %s)"
          % (r["slope_a"], r["slope_b"]),
          r["slope_a"] is not None and r["slope_b"] is not None
          and abs(r["slope_a"] - 18000) <= 1 and abs(r["slope_b"] - 21000) <= 1)
    check("halves_disagree: verdict fails", not r["ok"])
    out = io.StringIO()
    code = read_rgrain.report(b1_capture(lambda rr: 18000 if rr < 3 else 21000, rounds=6), out)
    check("halves_disagree: report exits 1", code == 1)


def b2_capture(off_worst, cap_worst, off_trips=1, cap_trips=1, cap_granted=3, cap_rms=500):
    """B2 frames on workload 127: 60 frames with the cap off, 60 with it on,
    twice. One frame per phase reaches the phase's worst block; each phase's
    trips are spread over its frames (printed after the frame they follow,
    the second print of a trip repeating its blk as the board does while the
    guard holds)."""
    lines = ["SHELL_REMOTE load id=127 gen=3 gest=-1 name=w127 input=noise settle=500 "
             "excite=0,0 rec=0,0"]
    blk = 1000
    for ph in range(4):
        cap = ph % 2
        worst = cap_worst if cap else off_worst
        trips = cap_trips if cap else off_trips
        per_round = trips // 2 + (trips % 2 if ph < 2 else 0)
        for f in range(60):
            g = cap_granted * BLOCKS if cap else 0
            lines += frame(127, 2, -1, cap, g, 900000, worst if f == 30 else 930000,
                           cap_rms if cap else 0, gen=3)
            if f < per_round:
                blk += 777
                lines.append("SHELL_REMOTE guard tripped blk=%d by=cb" % blk)
                lines.append("SHELL_REMOTE guard tripped blk=%d by=cb" % blk)
    return with_time(lines)


def test_cap_verdict():
    for cap_worst, want in ((975000, True), (990000, False)):
        frames, trips = read_rgrain.parse(b2_capture(963000, cap_worst))
        r = read_rgrain.b2(frames, trips, 18000)
        w = r["workloads"].get(127, {})
        check("cap_verdict: off worst 963 000 seen",
              w.get(0, {}).get("worst") == 963000)
        check("cap_verdict: cap worst %d %s with an 18 000 slope (%s)"
              % (cap_worst, "passes" if want else "fails", r["why"]), r["ok"] == want)
    frames, trips = read_rgrain.parse(b2_capture(963000, 975000, off_trips=1, cap_trips=1))
    r = read_rgrain.b2(frames, trips, 18000)
    check("cap_verdict: one trip each counts once per blk (got off %s, cap %s)"
          % (r["workloads"][127][0]["trips"], r["workloads"][127][1]["trips"]),
          r["workloads"][127][0]["trips"] == 1 and r["workloads"][127][1]["trips"] == 1)
    frames, trips = read_rgrain.parse(b2_capture(963000, 975000, off_trips=1, cap_trips=2))
    r = read_rgrain.b2(frames, trips, 18000)
    check("cap_verdict: one extra trip with the cap fails", not r["ok"])
    check("cap_verdict: the granted distribution holds the cap's 3s",
          r["workloads"][127][1]["granted"].get(3) == 120)
    out = io.StringIO()
    code = read_rgrain.report(b2_capture(963000, 975000), out, slope=18000)
    check("cap_verdict: report exits 0 on a passing B2 capture", code == 0)


def test_silent_layer_fails():
    r = read_rgrain.b1(read_rgrain.parse(
        b1_capture(lambda _r: 18000, rms=lambda n: 0 if n == 4 else 1000 * n))[0])
    check("silent_layer: n=4 frames with rms_x1e6=0 fail B1 (%s)" % r["why"], not r["ok"])
    r = read_rgrain.b1(read_rgrain.parse(
        b1_capture(lambda _r: 18000, rms=lambda n: 7))[0])
    check("silent_layer: n=0 frames with rms_x1e6>0 fail B1 too", not r["ok"])
    frames, trips = read_rgrain.parse(b2_capture(963000, 975000, cap_rms=0))
    r = read_rgrain.b2(frames, trips, 18000)
    check("silent_layer: granted grains with rms_x1e6=0 fail B2", not r["ok"])


test_slope_fit()
test_slope_halves_disagree()
test_cap_verdict()
test_silent_layer_fails()
print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
