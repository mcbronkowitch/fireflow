"""Host test: read_sd.py against synthetic captures of the SD image.

Checks the card verdict (pass, and each way it fails), the stream summary,
and the 11.2 split: a window printed as `SHELL_PLAY_R w=n` was collected
while the counter stood at n - 1, so the fixture plants a wide spread on
exactly the windows the stream ran in -- including the first window of an
idle phase, which holds the last chunk of the streaming phase before it and
must not be judged. A reader that drops the n - 1 shift puts that window
among the idle ones and the idle worst comes out wide.

Usage: test_read_sd.py [READER_DIR]   (default: this directory; point it at
a copy of a sabotaged read_sd.py to see the test go red). Plain script, the
exit code is the verdict.
"""
import io
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)   # read_play.py and the panel map stay this tree's
if len(sys.argv) > 1:
    sys.path.insert(0, os.path.abspath(sys.argv[1]))
import read_play as rp   # noqa: E402
import read_sd   # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


ROWS = len(rp.row_names())
CARD = [
    "SHELL_SD mount=0 fs=3 total_mib=30436 free_mib=30420 bus=4 mhz=50",
    "SHELL_SD prev=none fr=-1 off=0",
    "SHELL_SD write kib=8192 us=1600000 kib_s=5120 worst_us=40000 fr=0",
    "SHELL_SD verify=ok kib=8192 us=800000 kib_s=10240 worst_us=9000 fr=0 off=0",
]
ROOT_OK = "SHELL_SD root=2 bin=0 names=FFSDTEST.DAT,SYSTEM~1"
NARROW, WIDE = 6, 40
COUNTERS = 41          # counter values 0..40 -> windows 1..41


def streaming(c):
    return (c // 10) % 2 == 1


def capture(card=CARD, on_override=None):
    """Card lines, then per counter value c two SHELL_SD_S lines and the
    window it fills, printed as w=c+1."""
    lines = list(card)
    for c in range(COUNTERS):
        on = streaming(c)
        if on_override is not None:
            on = on_override(c, on)
        for _ in range(2):
            lines.append("SHELL_SD_S w=%d on=%d kib=%d ms=%d kib_s=%d worst_us=%d bad=0 fr=0"
                         % (c, 1 if on else 0, 3200 if on else 0, 490 if on else 0,
                            6530 if on else 0, 12000 if on else 0))
        # Wide while streaming, and in the first idle window, which the last
        # streaming chunk spills into.
        spill = c % 10 == 0 and streaming(c - 1)
        s = WIDE if (streaming(c) or spill) else NARROW
        n = c + 1
        for r0 in range(0, ROWS, 6):
            pairs = " ".join("%d:%d" % (1000, 1000 + s) for _ in range(min(6, ROWS - r0)))
            lines.append("SHELL_PLAY_R w=%d r=%d %s" % (n, r0, pairs))
    return [l + "\n" for l in lines]


def run(lines):
    buf = io.StringIO()
    code = read_sd.report(lines, out=buf)
    return code, buf.getvalue()


# --- the good capture -----------------------------------------------------
lines = capture()
code, text = run(lines)
check("good capture exits 0", code == 0)
check("card test PASS", "card test: PASS" in text)

on, off = read_sd.classify(rp.parse_raw_windows(lines), read_sd.parse_stream(lines))
# Streaming counters 11-19 and 31-39 (10 and 30 open a phase); idle 1-9, 21-29.
check("18 streaming windows judged", len(on) == 18)
check("18 idle windows judged", len(off) == 18)
check("every streaming window is wide",
      all(w["hi"][0] - w["lo"][0] == WIDE for w in on))
check("every idle window is narrow (the spill window is not judged)",
      all(w["hi"][0] - w["lo"][0] == NARROW for w in off))
check("windows are judged by counter n - 1",
      sorted(w["w"] for w in on) == [c + 1 for c in list(range(11, 20)) + list(range(31, 40))])

st = read_sd.stream_stats(read_sd.parse_stream(lines))
check("stream: 40 streaming passes", st["passes"] == 40)
check("stream: KiB/s from the summed passes", st["kib_s"] == 3200 * 1000 // 490)
check("stream: worst f_read", st["worst_us"] == 12000)

# --- remote_run.py's capture: every line carries a host time ---------------
plain = capture(card=CARD + [ROOT_OK])
stamped = ["1000.701 " + l for l in plain]
check("test_runner_prefix_stripped: strip_host_time",
      read_sd.strip_host_time(stamped) == plain)
check("a line without a prefix is left as it is",
      read_sd.strip_host_time(["SHELL_SD mount=0", "12 apples"]) == ["SHELL_SD mount=0", "12 apples"])
check("test_runner_prefix_stripped: same card verdict",
      read_sd.card_verdict(read_sd.parse_sd(stamped)) == read_sd.card_verdict(read_sd.parse_sd(plain))
      and read_sd.card_verdict(read_sd.parse_sd(stamped))[0])
check("test_runner_prefix_stripped: same root line",
      read_sd.parse_root(stamped) == read_sd.parse_root(plain) and read_sd.parse_root(stamped) is not None)
check("test_runner_prefix_stripped: same stream stats",
      read_sd.stream_stats(read_sd.parse_stream(stamped))
      == read_sd.stream_stats(read_sd.parse_stream(plain))
      and read_sd.stream_stats(read_sd.parse_stream(stamped))["passes"] == 40)
check("test_runner_prefix_stripped: same report",
      run(stamped) == run(plain) and run(stamped)[0] == 0)

# --- the card verdict's failures -----------------------------------------
for name, card in (
        ("mount error", [CARD[0].replace("mount=0", "mount=3")] + CARD[1:]),
        ("verify bad", CARD[:3] + [CARD[3].replace("verify=ok", "verify=bad")]),
        ("verify fr", CARD[:3] + [CARD[3].replace("fr=0", "fr=1")]),
        ("short verify", CARD[:3] + [CARD[3].replace("kib=8192", "kib=4096")]),
        ("write error", CARD[:2] + [CARD[2].replace("fr=0", "fr=7")] + CARD[3:]),
        ("no card lines", [])):
    code, text = run(capture(card=card))
    check("%s exits 1" % name, code == 1)
    check("%s says FAIL" % name, "card test: FAIL" in text)

# --- the root line: no .bin file may sit beside the test file ---------------
# The bootloader scans the card's root for a .bin to start; the test file is
# FFSDTEST.DAT so it never looks like one (test_root_line_parsed,
# test_bin_in_root_fails_card).
root = read_sd.parse_root(CARD + [ROOT_OK])
check("test_root_line_parsed: bin", root is not None and root["bin"] == 0)
check("test_root_line_parsed: n", root is not None and root["n"] == 2)
check("test_root_line_parsed: names",
      root is not None and root["names"] == ["FFSDTEST.DAT", "SYSTEM~1"])
check("no root line parses to None", read_sd.parse_root(CARD) is None)
check("a root line without .bin keeps the card PASS",
      read_sd.card_verdict(read_sd.parse_sd(CARD + [ROOT_OK]))[0])

ROOT_BIN = "SHELL_SD root=2 bin=1 names=FFSDTEST.BIN,SYSTEM~1"
ok, why = read_sd.card_verdict(read_sd.parse_sd(CARD + [ROOT_BIN]))
check("test_bin_in_root_fails_card: verdict false", not ok)
check("test_bin_in_root_fails_card: the reason names the file",
      any("FFSDTEST.BIN" in w for w in why))
code, text = run(capture(card=CARD + [ROOT_BIN]))
check("a .bin in the root exits 1", code == 1)
check("a .bin in the root says FAIL and names the file",
      "card test: FAIL" in text and "FFSDTEST.BIN" in text)

# --- a stream that never ran ---------------------------------------------
code, text = run(capture(on_override=lambda c, on: False))
check("no streaming window exits 1", code == 1)

# --- the card test's window is not judged ----------------------------------
# The report loop starts at counter 3; the window it names first held the
# card test's writes and reads (wide), and only the windows after it count.
lines = [l for l in capture() if not any(l.startswith("SHELL_SD_S w=%d " % c) for c in range(3))]
lines = [l.replace("SHELL_PLAY_R w=4 r=", "SHELL_PLAY_R w=4 r=").replace(
    "1000:%d" % (1000 + NARROW), "1000:%d" % (1000 + WIDE)) if l.startswith("SHELL_PLAY_R w=4 ") else l
         for l in lines]
on, off = read_sd.classify(rp.parse_raw_windows(lines), read_sd.parse_stream(lines))
check("the card test's window (counter 3) is not judged", 4 not in [w["w"] for w in off])
check("the windows after it are", 5 in [w["w"] for w in off] and len(off) == 15)
check("so the idle windows stay narrow", all(w["hi"][0] - w["lo"][0] == NARROW for w in off))

# --- a counter value whose lines disagree with its phase is not judged ---
lines = capture(on_override=lambda c, on: False if c == 15 else on)
on, off = read_sd.classify(rp.parse_raw_windows(lines), read_sd.parse_stream(lines))
check("a phase line that says on=0 drops its window", len(on) == 17 and 16 not in [w["w"] for w in on])

# --- burst mode (SHELL_SD_PROBE=burst, spec section 5, A1) ------------------
# The same shape as above, but the phase is not a bit of w / 10: each
# SHELL_SD_S line names its own class (on, per, tail), the way
# shell/sd_schedule.cpp burst_phase() hands it out. Counters 0-9 idle, 10-19
# (83 ms, tail 0), 20-29 idle, 30-39 (83 ms, tail 1), 40 idle. Each class
# keeps its own spread, so a reader that lumps (83,0) and (83,1) together
# (keys on `on` only) shows the wider one in both.
BURST_SPREAD = {(0, 0, 0): 6, (1, 83, 0): 40, (1, 83, 1): 150}
BURST_HELD = {(0, 0, 0): 0, (1, 83, 0): 5, (1, 83, 1): 25}
SENSE_OF_ROW = rp.row_senses()


def burst_key(c):
    ph = (c // 10) % 12
    return {1: (1, 83, 0), 3: (1, 83, 1)}.get(ph, (0, 0, 0))


def burst_capture(card=CARD, fr_line=None):
    """Card lines, then per counter c two SHELL_SD_S burst lines and the
    window they fill (printed w=c+1). fr_line: the counter whose lines say
    fr=1."""
    lines = list(card)
    for c in range(COUNTERS):
        key = burst_key(c)
        on, per, tail = key
        for _ in range(2):
            lines.append("SHELL_SD_S w=%d on=%d mode=burst per=%d tail=%d kib=%d ms=%d "
                         "kib_s=%d worst_us=%d bad=0 fr=%d held=%d hmax=%d"
                         % (c, on, per, tail, 160 if on else 0, 490 if on else 0,
                            6530 if on else 0, 9000 if on else 0,
                            1 if c == fr_line else 0, BURST_HELD[key], 2 if on else 0))
        # The first window of a phase holds the previous phase's last
        # transfer and its tail: wide, and not judged.
        s = 300 if c % 10 == 0 else BURST_SPREAD[key]
        for r0 in range(0, ROWS, 6):
            pairs = " ".join("%d:%d" % (1000, 1000 + s) for _ in range(min(6, ROWS - r0)))
            lines.append("SHELL_PLAY_R w=%d r=%d %s" % (c + 1, r0, pairs))
    return [l + "\n" for l in lines]


def test_burst_classes():
    lines = burst_capture()
    stream = read_sd.parse_stream(lines)
    check("burst: the lines parse, mode kept as text",
          len(stream) == 2 * COUNTERS and stream[0]["mode"] == "burst" and stream[0]["per"] == 0
          and stream[20]["per"] == 83 and stream[60]["tail"] == 1)
    classes = read_sd.burst_classes(rp.parse_raw_windows(lines), stream)
    check("burst: three classes", sorted(classes) == sorted(BURST_SPREAD))
    # Judged: counters 1-9 and 21-29 idle, 11-19 (83,0), 31-39 (83,1).
    check("burst: window counts per class",
          [len(classes[k]["windows"]) for k in sorted(BURST_SPREAD)] == [18, 9, 9])
    for key, want in BURST_SPREAD.items():
        sm = read_sd.burst_summary(classes[key]["windows"], SENSE_OF_ROW)
        rows_by_pin = [SENSE_OF_ROW.count(p) for p in range(rp.SENSES)]
        check("burst %s: spread is the class's own, worst and median" % (key,),
              all(sm[p]["worst"] == want and sm[p]["median"] == want
                  for p in range(rp.SENSES) if rows_by_pin[p]))
        check("burst %s: pots over band" % (key,),
              all(sm[p]["over_h"] == (rows_by_pin[p] if want > rp.REVA_HYSTERESIS[p] else 0)
                  for p in range(rp.SENSES)))
        check("burst %s: pots over kPotTouch" % (key,),
              all(sm[p]["over_touch"] == (rows_by_pin[p] if want > rp.POT_TOUCH else 0)
                  for p in range(rp.SENSES)))
        check("burst %s: held share is the held blocks over 250 blocks a pass" % (key,),
              abs(classes[key]["held_share"] - BURST_HELD[key] / 250.0) < 1e-9)
        check("burst %s: no error" % (key,), classes[key]["error"] is None)
    code, text = run(lines)
    check("burst: report exits 0", code == 0)
    for key in BURST_SPREAD:
        check("burst: report names class %s" % (key,),
              "class on=%d per=%d tail=%d" % key in text)
    check("burst: the report shows the held share", "held 10.0 %" in text)


def test_burst_error_reported():
    lines = burst_capture(fr_line=35)   # inside (83,1)
    classes = read_sd.burst_classes(rp.parse_raw_windows(lines), read_sd.parse_stream(lines))
    check("burst error: the class carries it", classes[(1, 83, 1)]["error"] is not None)
    check("burst error: the other classes do not",
          classes[(1, 83, 0)]["error"] is None and classes[(0, 0, 0)]["error"] is None)
    code, text = run(lines)
    check("burst error: report says error for the class", "on=1 per=83 tail=1: error" in text)
    check("burst error: the class is not averaged",
          text.count("worst/median") == 2)   # the other two classes' tables only
    check("burst error: exits 1", code == 1)


test_burst_classes()
test_burst_error_reported()


# --- the callback by phase (SHELL_SD_PROBE=stream, test A3) ------------------
# The stream rotates three arms, sdram-16, axi-16 and sdram-32 (buf= and chunk=
# name the arm), and SHELL_PLAY_WIN prints the frame's sum / n once per 500 ms.
# A frame belongs to the latest SHELL_SD_S line before it; a frame with sum=0
# n=0 (the torn-read retry gave up) says nothing and is skipped, not counted as
# a zero.
def sd_s(w, on, buf=None, chunk=None):
    """A stream line. chunk defaults to the old image's: 16 from axi, else 32;
    chunk=0 leaves the field off (an older line has none)."""
    if chunk is None:
        chunk = 16 if buf == "axi" else 32
    return ("SHELL_SD_S w=%d on=%d mode=stream%s%s kib=%d ms=%d kib_s=%d worst_us=%d bad=0 fr=0"
            % (w, on, (" buf=" + buf) if buf else "",
               (" chunk=%d" % (chunk if on else 0)) if chunk != 0 else "",
               3200 if on else 0, 490 if on else 0, 6530 if on else 0, 12000 if on else 0))


def play_win(win, n=250, mean=500000):
    return "SHELL_PLAY_WIN win=%d gap=0 over=0 n=%d sum=%d" % (win, n, n * mean)


SPILL_MEAN, SPILL_WIN = 800000, 1100000


def cb_capture(idle=500000, sdram16=500000, axi16=500000, sdram32=500000, win=700000, extra=()):
    """Five phases, three frames each, idle / sdram-16 / axi-16 / sdram-32 /
    idle, with the window maximum `win` in every frame. The first frame of a
    phase holds the phase before's last chunk (SPILL_MEAN, SPILL_WIN) and is
    not judged."""
    lines = list(CARD)
    for w, (on, buf, chunk, mean) in enumerate([(0, None, None, idle),
                                                (1, "sdram", 16, sdram16),
                                                (1, "axi", 16, axi16),
                                                (1, "sdram", 32, sdram32),
                                                (0, None, None, idle)]):
        lines.append(sd_s(w * 10, on, buf, chunk))
        lines.append(play_win(SPILL_WIN, mean=SPILL_MEAN))
        lines.append(play_win(win, mean=mean))
        lines.append(play_win(win, mean=mean))
    lines.extend(extra)
    return [l + "\n" for l in lines]


def test_cb_by_phase():
    lines = cb_capture(idle=500000, sdram16=503000, axi16=505000, sdram32=507000, win=700000)
    by = read_sd.cb_by_phase(lines)
    check("cb: four classes", sorted(by) == ["axi16", "idle", "sdram16", "sdram32"])
    check("cb: frames go to the line before them, arm by arm",
          [len(by[k]["means"]) for k in ("idle", "sdram16", "axi16", "sdram32")] == [4, 2, 2, 2])
    # The first frame after every phase change (on/off, or one arm to the
    # next) is dropped: its mean 800 000 and win 1 100 000 appear nowhere.
    check("cb: the first frame after each phase change is dropped",
          all(SPILL_MEAN not in by[k]["means"] and SPILL_WIN not in by[k]["win"] for k in by)
          and sum(len(by[k]["means"]) for k in by) == 10)
    check("cb: per-frame means are sum / n, each frame in its own arm",
          by["idle"]["means"] == [500000.0] * 4 and by["sdram16"]["means"] == [503000.0] * 2
          and by["axi16"]["means"] == [505000.0] * 2 and by["sdram32"]["means"] == [507000.0] * 2)
    check("cb: win maxima kept per class", by["axi16"]["win"] == [700000, 700000])
    st = read_sd.cb_stats(by["axi16"])
    check("cb: stats are frames, mean of means, p99 win, max win",
          st == {"frames": 2, "mean": 505000.0, "p99": 700000, "max": 700000})
    ok, why = read_sd.cb_verdict(by)
    check("cb: +3000 / +5000 / +7000 cycles pass", ok and not why)

    # A mean 10 000 cycles up fails that class; 7 000 passes (above).
    ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(axi16=510000)))
    check("cb: a 10 000-cycle difference fails", not ok and any("axi16" in w for w in why))
    ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(sdram16=490000)))
    check("cb: the difference counts in both directions", not ok and any("sdram16" in w for w in why))
    ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(sdram16=509600)))
    check("cb: exactly 9 600 passes", ok)
    # One arm 9 601 off fails and the reason names that arm alone.
    for arm in ("sdram16", "axi16", "sdram32"):
        ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(**{arm: 509601})))
        check("cb: %s 9 601 off idle fails, naming it" % arm,
              not ok and len(why) == 1 and why[0].startswith(arm + " mean +9601"))
    # An arm with no frames fails and is named; the A3 run uses the new image.
    no_axi = [l for l in cb_capture() if "buf=axi" not in l]
    ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(no_axi))
    check("cb: no axi16 frames fails and says so", not ok and "no axi16 frames" in why)
    # The click line: any window maximum above 945 000 fails, in any class.
    ok, _ = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(win=945000)))
    check("cb: a win of exactly 945 000 passes", ok)
    ok, why = read_sd.cb_verdict(read_sd.cb_by_phase(cb_capture(win=945001)))
    check("cb: a win over 945 000 fails", not ok and any("945" in w for w in why))

    # sum=0 n=0 is skipped, not a zero in the mean.
    torn = cb_capture(extra=["SHELL_PLAY_WIN win=999999 gap=0 over=0 n=0 sum=0"])
    by = read_sd.cb_by_phase(torn)
    check("cb: a torn frame (n=0) is skipped, not counted",
          len(by["sdram32"]["means"]) == 2 and len(by["idle"]["means"]) == 4)
    check("cb: and does not drag the mean down", read_sd.cb_stats(by["idle"])["mean"] == 500000.0)
    check("cb: nor does its win reach the maximum", read_sd.cb_stats(by["idle"])["max"] == 700000)

    # Older captures: no chunk= on an on=1 line is the two-arm image's, where
    # SDRAM read 32 KB and AXI 16 KB; no buf= means SDRAM. Frames before the
    # first SHELL_SD_S line belong to no phase.
    old = [sd_s(10, 1, chunk=0), play_win(650000), play_win(650000)]   # the first is the phase's, dropped
    by = read_sd.cb_by_phase(["SHELL_PLAY_WIN win=1 gap=0 over=0 n=2 sum=2"] + old)
    check("cb: on=1 without buf= or chunk= is sdram32",
          len(by["sdram32"]["means"]) == 1 and not by["axi16"]["means"] and not by["sdram16"]["means"])
    check("cb: a frame before any SHELL_SD_S line is nobody's",
          not by["idle"]["means"] and len(by["sdram32"]["means"]) == 1)
    old = [sd_s(10, 1, "sdram", chunk=0), play_win(650000), play_win(650000)]
    by = read_sd.cb_by_phase(old)
    check("cb: buf=sdram without chunk= is sdram32", len(by["sdram32"]["means"]) == 1)
    old = [sd_s(10, 1, "axi", chunk=0), play_win(650000), play_win(650000)]
    by = read_sd.cb_by_phase(old)
    check("cb: buf=axi without chunk= is axi16",
          len(by["axi16"]["means"]) == 1 and not by["sdram32"]["means"])
    # strip_host_time on every line, as every other parser does.
    stamped = ["1000.701 " + l for l in cb_capture(axi16=505000)]
    check("cb: host-time prefixes are stripped", read_sd.cb_by_phase(stamped) ==
          read_sd.cb_by_phase(cb_capture(axi16=505000))
          and len(read_sd.cb_by_phase(stamped)["axi16"]["means"]) == 2)

    # parse_stream keeps buf as text and still reads a line without it.
    s = read_sd.parse_stream([sd_s(10, 1, "axi"), sd_s(20, 0)])
    check("cb: parse_stream keeps buf as text, absent stays absent",
          s[0]["buf"] == "axi" and "buf" not in s[1])

    # The report prints the section when the capture has frames, and does not
    # change the exit code: a miss prices the traffic, it is not a stop.
    code, text = run(cb_capture(axi16=510000))
    check("cb: the report prints each class",
          all(k in text for k in ("idle:", "sdram16:", "axi16:", "sdram32:")))
    check("cb: the report says the verdict", "callback verdict: FAIL" in text)
    # The contrasts are reported with their sign, never judged.
    _, text = run(cb_capture(sdram16=502000, axi16=505000, sdram32=499000))
    check("cb: the location contrast is axi16 - sdram16, signed",
          "location (axi16 - sdram16): +3000 cycles" in text)
    check("cb: the chunk contrast is sdram32 - sdram16, signed",
          "chunk (sdram32 - sdram16): -3000 cycles" in text)
    check("cb: a contrast does not change the verdict", "callback verdict: PASS" in text)
    _, text = run(no_axi)
    check("cb: no contrast line without both sides",
          "location" not in text and "chunk (sdram32 - sdram16)" in text)
    code_ok, text_ok = run(cb_capture())
    check("cb: a passing capture says PASS", "callback verdict: PASS" in text_ok)
    check("cb: the verdict does not change the exit code", code == code_ok)
    _, text = run(capture())
    check("cb: a capture without PLAY_WIN frames prints no callback section", "callback" not in text)


test_cb_by_phase()


# --- the latency tails (SHELL_SD_PROBE=rtail / wtail, spec section 5, C1 / C2) --
def h_stats(kind, seq, n, worst, seek=0, fr=0, bad=0, pat="b2b"):
    return ("SHELL_SD_H kind=%s seq=%d n=%d worst_us=%d seek_worst_us=%d fr=%d bad=%d pat=%s"
            % (kind, seq, n, worst, seek, fr, bad, pat))


def h_buckets(kind, seq, counts):
    """The two bucket lines the board prints for one interval: b0= is buckets
    0-7, b1= buckets 8-15."""
    assert len(counts) == 16
    return ["SHELL_SD_H kind=%s seq=%d b0=%s" % (kind, seq, ",".join(map(str, counts[:8]))),
            "SHELL_SD_H kind=%s seq=%d b1=%s" % (kind, seq, ",".join(map(str, counts[8:])))]


def h_interval(kind, seq, counts, worst, seek=0, fr=0, bad=0):
    return [h_stats(kind, seq, sum(counts), worst, seek, fr, bad)] + h_buckets(kind, seq, counts)


def counts_at(*pairs):
    c = [0] * 16
    for i, k in pairs:
        c[i] = k
    return c


def test_tails_merge():
    a = counts_at((0, 90), (1, 9), (3, 1))        # worst in bucket 3: [512, 1024) us
    b = counts_at((0, 95), (2, 4), (9, 1))        # worst in bucket 9: [32768, 65536) us
    lines = (h_interval("seq", 1, a, 900, seek=10) + h_interval("seq", 2, b, 40000, seek=700)
             + h_interval("rand", 1, a, 800, seek=3000))
    t = read_sd.tails(lines)
    check("tails: one entry per kind", sorted(t) == ["rand", "seq"])
    s = t["seq"]
    check("tails: two lines of one kind merge their buckets",
          s["buckets"] == [x + y for x, y in zip(a, b)] and s["n"] == 200)
    check("tails: worst is the max, not the sum", s["worst_us"] == 40000)
    check("tails: seek_worst is the max", s["seek_worst_us"] == 700)
    check("tails: kinds do not mix", t["rand"]["buckets"] == a and t["rand"]["worst_us"] == 800)
    # 200 calls, rank ceil(0.999 * 200) = 200: the last non-empty bucket, 9,
    # whose upper edge 65 536 us is cut to the worst call.
    check("tails: p99.9 is the bucket edge, cut at the worst", s["p999_us"] == 40000)
    # 2000 calls: rank 1998. The slowest 0.1 % (two calls) are out of it ...
    tight = h_interval("seq", 1, counts_at((0, 1998), (4, 1), (5, 1)), 5000)
    check("tails: p99.9 leaves out the slowest 0.1 %",
          read_sd.tails(tight)["seq"]["p999_us"] == 128)
    # ... and three calls are one too many: bucket 4 ends at 32 * 64 = 2048 us.
    wide = h_interval("seq", 1, counts_at((0, 1997), (4, 1), (5, 2)), 5000)
    check("tails: p99.9 reaches the bucket of the 1998th call",
          read_sd.tails(wide)["seq"]["p999_us"] == 2048)
    check("tails: a clean capture has no error", s["error"] is None and s["incomplete"] == 0)

    # The brief's single-line form (b= with all 16 counts) reads the same.
    one = ("SHELL_SD_H kind=wmax n=%d worst_us=1500 seek_worst_us=0 b=%s"
           % (sum(a), ",".join(map(str, a))))
    check("tails: a single line with b= is one interval",
          read_sd.tails([one])["wmax"]["buckets"] == a)
    # Host-time prefixes, and lines that are not histogram lines.
    noise = ["12.5 SHELL_SD_H fill mib=64 ms=7000 fr=0", "SHELL_SD_H done", "SHELL_SD_S w=1 on=0"]
    stamped = ["%d.250 %s" % (i, l) for i, l in enumerate(h_interval("seq", 1, a, 900))]
    check("tails: host-time prefixes are stripped, other lines ignored",
          read_sd.tails(noise + stamped) == read_sd.tails(h_interval("seq", 1, a, 900)))
    check("tails: no histogram lines, no kinds", read_sd.tails(CARD) == {})


def test_tail_ring_size():
    # 2 x 20 ms x 384 KB/s = 15.36 KB: rounded up to 32 KB.
    check("ring: 20 000 us at 384 KB/s is 32 KB", read_sd.ring_kib(20000, 384) == 32)
    check("ring: exactly one 32 KB step stays", read_sd.ring_kib(41666, 384) == 32)   # 31.99998 KB
    check("ring: a hair over goes to the next step", read_sd.ring_kib(41700, 384) == 64)
    check("ring: 100 ms at 288 KB/s (writes) is 64 KB", read_sd.ring_kib(100000, 288) == 64)
    check("ring: no call, no ring", read_sd.ring_kib(0, 384) == 0)
    t = read_sd.tails(h_interval("rand", 1, counts_at((0, 99), (8, 1)), 20000)
                      + h_interval("w16", 1, counts_at((0, 99), (8, 1)), 100000))
    check("ring: tails() uses 384 KB/s for reads", t["rand"]["ring_kib"] == 32)
    check("ring: tails() uses 288 KB/s for writes", t["w16"]["ring_kib"] == 64)


def test_tail_errors():
    a = counts_at((0, 100))
    good = h_interval("seq", 1, a, 900)
    bad_fr = h_interval("rand", 1, counts_at((0, 40)), 99000, fr=1)   # FR_DISK_ERR
    again = h_interval("rand", 2, [0] * 16, 0, fr=1)                  # sticky: repeated
    fine = h_interval("rand", 0, a, 800)
    t = read_sd.tails(fine + bad_fr + again + good)
    check("tails: fr != 0 is an error of that kind", "fr=1" in t["rand"]["error"] and "seq=1" in t["rand"]["error"])
    check("tails: and not merged into the histogram",
          t["rand"]["buckets"] == a and t["rand"]["n"] == 100 and t["rand"]["worst_us"] == 800)
    check("tails: the other kinds are untouched", t["seq"]["error"] is None)
    t = read_sd.tails(h_interval("seq", 1, a, 900, bad=2))
    check("tails: bad chunks are an error too", "bad=2" in t["seq"]["error"] and t["seq"]["n"] == 0)

    # A lost bucket line: the stats say 100 calls, the buckets 50. The worst
    # still counts (it can only raise the ring), the buckets and n do not.
    lost = [h_stats("seq", 5, 100, 70000)] + h_buckets("seq", 5, counts_at((0, 50)))[:1]
    t = read_sd.tails(good + lost)["seq"]
    check("tails: a group with lost bucket lines is counted incomplete",
          t["incomplete"] == 1 and t["error"] is None)
    check("tails: its worst counts, its buckets do not",
          t["worst_us"] == 70000 and t["buckets"] == a and t["n"] == 100)


def tail_capture(kinds, fr=None):
    lines = list(CARD)
    seq = 0
    for k in kinds:
        for _ in range(3):
            seq += 1
            lines += h_interval(k, seq, counts_at((0, 90), (2, 10)), 1500,
                                fr=(fr or {}).get(k, 0))
    return [l + "\n" for l in lines]


def test_tail_report():
    code, text = run(tail_capture(["seq", "rand", "randfs"]))
    check("tail report: a full read capture passes", code == 0 and "card test: PASS" in text)
    check("tail report: one row per kind with worst, p99.9 and the ring",
          all(("%s:" % k) in text for k in ("seq", "rand", "randfs"))
          and "worst 1500 us" in text and "ring 32 KB" in text)
    code, text = run(tail_capture(["w16", "w24", "wmax"]))
    check("tail report: a full write capture passes", code == 0)
    code, text = run(tail_capture(["seq", "rand"]))
    check("tail report: a missing kind fails and is named",
          code == 1 and "randfs" in text and "missing" in text)
    code, text = run(tail_capture(["seq", "rand", "randfs"], fr={"rand": 1}))
    check("tail report: an error fails and is named",
          code == 1 and "  rand: error (fr=1 at seq=" in text)
    # A failure before the first kind has its own name, not the first kind's.
    fill_err = tail_capture([]) + [l + "\n" for l in h_interval("fill", 0, [0] * 16, 0, fr=5)]
    code, text = run(fill_err)
    check("tail report: a fill error is reported as fill, not as seq",
          code == 1 and "  fill: error (fr=5 at seq=0)" in text and "seq: error" not in text)
    setup_err = tail_capture([]) + [l + "\n" for l in h_interval("setup", 0, [0] * 16, 0, fr=-3)]
    code, text = run(setup_err)
    check("tail report: no card-test file is reported as setup",
          code == 1 and "  setup: error (fr=-3 at seq=0)" in text)
    # A capture that holds only setup / fill errors (a wtail run that died
    # before its first kind) has no family: the errors are printed and neither
    # family's kinds are listed as missing. The exit code stays 1.
    for name, err in (("setup", setup_err), ("fill", fill_err)):
        code, text = run(err)
        check("tail report: only a %s error lists no kind as missing" % name,
              code == 1 and ("  %s: error" % name) in text and "missing" not in text
              and not any((k + ":") in text for k in ("seq", "rand", "randfs", "w16", "w24", "wmax")))
    both = tail_capture([]) + [l + "\n" for l in (h_interval("setup", 0, [0] * 16, 0, fr=-3)
                                                  + h_interval("fill", 0, [0] * 16, 0, fr=5))]
    code, text = run(both)
    check("tail report: setup and fill together are both printed, nothing missing",
          code == 1 and "  setup: error" in text and "  fill: error" in text and "missing" not in text)
    # With a real kind beside the error the family is still picked from the kinds.
    mixed = tail_capture(["w16"]) + [l + "\n" for l in h_interval("fill", 0, [0] * 16, 0, fr=5)]
    code, text = run(mixed)
    check("tail report: a real kind beside the error still picks its family",
          code == 1 and "(writes;" in text and "w24: missing" in text and "randfs" not in text)
    t = read_sd.tails(h_interval("fill", 0, [0] * 16, 0, fr=5))
    check("tails: fill is a kind of its own", list(t) == ["fill"] and t["fill"]["error"].startswith("fr=5"))
    code, _ = run([l for l in tail_capture(["seq", "rand", "randfs"]) if "SHELL_SD verify" not in l])
    check("tail report: a failed card test fails", code == 1)


test_tails_merge()
test_tail_ring_size()
test_tail_errors()
test_tail_report()


# --- bank reload, buffer save/load, patch file (spec section 5, D1, D2, D4) ---
# One line per operation: SHELL_SD_K per bank load (plus the export once),
# SHELL_SD_B per buffer save or load, SHELL_SD_P per patch-file round trip.
K_EXPORT = "SHELL_SD_K export kib=63 us=31000 worst_us=9000 fr=0"


def k_line(load_us=14000, worst_us=4000, cmp="ok", held=8, fr=0, slot=1):
    return ("SHELL_SD_K load_us=%d worst_us=%d cmp=%s held=%d fr=%d slot=%d"
            % (load_us, worst_us, cmp, held, fr, slot))


def b_line(fmt, op, ms, worst_us=30000, held=900, fr=0, cmp=None, off=0):
    if cmp is None:
        cmp = "-" if op == "save" else "ok"
    return ("SHELL_SD_B fmt=%s op=%s ms=%d worst_us=%d held=%d fr=%d cmp=%s off=%d"
            % (fmt, op, ms, worst_us, held, fr, cmp, off))


def p_line(i, cmp="ok", seq=42, fr=0, write_us=900, sync_us=6000, read_us=700, held=4):
    return ("SHELL_SD_P i=%d write_us=%d sync_us=%d read_us=%d cmp=%s seq=%d held=%d fr=%d"
            % (i, write_us, sync_us, read_us, cmp, seq, held, fr))


def ops_capture(op_lines):
    return [l + "\n" for l in list(CARD) + list(op_lines)]


def bank_lines(n=10, bad_at=None, fr_at=None):
    out = [K_EXPORT]
    for i in range(n):
        out.append(k_line(load_us=14000 + 100 * i, worst_us=4000 + i, slot=1 - i % 2,
                          cmp="bad" if i == bad_at else "ok", fr=5 if i == fr_at else 0))
    return out


def test_bank_stats():
    st = read_sd.bank_stats(bank_lines())
    check("bank: ten loads, all ok, pass", st["loads"] == 10 and st["ok"] == 10 and st["pass"])
    check("bank: load time max and mean", st["load_us_max"] == 14900 and st["load_us_mean"] == 14450)
    check("bank: the worst f_read over all loads", st["worst_us"] == 4009)
    check("bank: both slots used", st["slots"] == [0, 1])
    check("bank: the export is read", st["export"] is not None and st["export"]["kib"] == 63)
    stamped = ["7.5 " + l for l in bank_lines()]
    check("bank: host-time prefixes are stripped", read_sd.bank_stats(stamped) == st)
    check("bank: no load line fails", not read_sd.bank_stats([K_EXPORT])["pass"])
    code, text = run(ops_capture(bank_lines()))
    check("bank: report exits 0 and prints the loads", code == 0 and "bank:" in text)


def test_bank_cmp_bad_fails():
    st = read_sd.bank_stats(bank_lines(bad_at=6))
    check("bank cmp=bad: fails", not st["pass"] and st["ok"] == 9)
    check("bank cmp=bad: the reason names the load", any("cmp=bad" in w for w in st["why"]))
    st = read_sd.bank_stats(bank_lines(fr_at=3))
    check("bank fr: a failed load fails and says fr", not st["pass"] and any("fr=5" in w for w in st["why"]))
    st = read_sd.bank_stats(bank_lines() + ["SHELL_SD_K stop fr=1"])
    check("bank: a stop line fails", not st["pass"] and any("fr=1" in w for w in st["why"]))
    code, text = run(ops_capture(bank_lines(bad_at=6)))
    check("bank cmp=bad: report exits 1 and says FAIL", code == 1 and "FAIL" in text)


def buffer_lines(s16_cmp="ok", s16_off=0):
    return [b_line("f32", "save", 2000), b_line("f32", "load", 1300),
            b_line("s16", "save", 1100), b_line("s16", "load", 700, cmp=s16_cmp, off=s16_off)]


def test_buffer_times_and_cmp():
    st = read_sd.buffer_stats(buffer_lines())
    check("buffer: f32 save 2 000 ms", st["ops"][("f32", "save")]["ms_max"] == 2000)
    check("buffer: f32 load 1 300 ms", st["ops"][("f32", "load")]["ms_max"] == 1300)
    check("buffer: cmp ok passes", st["pass"] and not st["why"])
    check("buffer: per operation the worst call and the held blocks",
          st["ops"][("s16", "load")]["worst_us"] == 30000 and st["ops"][("s16", "load")]["held_max"] == 900)
    stamped = ["3.25 " + l for l in buffer_lines()]
    check("buffer: host-time prefixes are stripped", read_sd.buffer_stats(stamped) == st)
    st = read_sd.buffer_stats(buffer_lines(s16_cmp="bad", s16_off=123456))
    check("buffer: an s16 cmp=bad fails", not st["pass"])
    check("buffer: the reason names the format and its offset",
          any("s16" in w and "off=123456" in w for w in st["why"]))
    st = read_sd.buffer_stats(buffer_lines()[:2])
    check("buffer: a missing format fails and is named",
          not st["pass"] and any("s16" in w for w in st["why"]))
    st = read_sd.buffer_stats(buffer_lines() + [b_line("f32", "save", 2100, fr=1)])
    check("buffer: fr fails", not st["pass"] and any("fr=1" in w for w in st["why"]))
    code, _ = run(ops_capture(buffer_lines()))
    check("buffer: report exits 0", code == 0)
    code, text = run(ops_capture(buffer_lines(s16_cmp="bad", s16_off=123456)))
    check("buffer: report exits 1 and names the offset", code == 1 and "off=123456" in text)


def test_patch_100_of_100():
    good = [p_line(i) for i in range(100)]
    st = read_sd.patch_stats(good)
    check("patch: 100 of 100 passes", st["n"] == 100 and st["ok"] == 100 and st["pass"])
    check("patch: worst write, sync and read", st["sync_us_max"] == 6000 and st["write_us_max"] == 900
          and st["read_us_max"] == 700)
    check("patch: the record's seq", st["seqs"] == [42])
    one_bad = [p_line(i, cmp="bad" if i == 57 else "ok") for i in range(100)]
    st = read_sd.patch_stats(one_bad)
    check("patch: 99 ok + 1 bad fails", not st["pass"] and st["ok"] == 99)
    check("patch: the bad one is named", any("i=57" in w for w in st["why"]))
    st = read_sd.patch_stats(good[:99])
    check("patch: 99 iterations are not 100", not st["pass"] and any("99" in w for w in st["why"]))
    st = read_sd.patch_stats([p_line(i, fr=1 if i == 3 else 0) for i in range(100)])
    check("patch: an fr fails", not st["pass"] and any("fr=1" in w for w in st["why"]))
    check("patch: a done line is not an iteration",
          read_sd.patch_stats(good + ["SHELL_SD_P done n=100 ok=100"])["n"] == 100)
    code, _ = run(ops_capture(good))
    check("patch: report exits 0", code == 0)
    code, text = run(ops_capture(one_bad))
    check("patch: report exits 1", code == 1 and "FAIL" in text)


def test_ops_by_phase():
    # A frame belongs to the pass it follows: a frame right after an operation
    # line measured the pass that ran the operation (busy), the other frames
    # idle passes. Frames before the first operation line (the card test) are
    # nobody's.
    lines = (list(CARD) + [play_win(990000, mean=900000)]
             + [K_EXPORT, play_win(800000, mean=503000), play_win(700000), play_win(700000)]
             + [k_line(), play_win(810000, mean=505000), play_win(700000)]
             + ["SHELL_SD_K stop fr=0", play_win(700000)])
    by = read_sd.ops_by_phase(lines)
    check("ops cb: two classes", sorted(by) == ["busy", "idle"])
    check("ops cb: frames after an operation line are busy",
          by["busy"]["means"] == [503000.0, 505000.0] and by["busy"]["win"] == [800000, 810000])
    check("ops cb: the others are idle, a stop line is no operation",
          by["idle"]["means"] == [500000.0] * 4)
    check("ops cb: the frame before the first operation is nobody's",
          900000.0 not in by["idle"]["means"] + by["busy"]["means"])
    check("ops cb: every mode's operation line counts",
          [len(read_sd.ops_by_phase([l, play_win(1)])["busy"]["means"])
           for l in (K_EXPORT, k_line(), b_line("f32", "save", 2000), p_line(0))] == [1, 1, 1, 1])
    stamped = ["9.5 " + l for l in lines]
    check("ops cb: host-time prefixes are stripped", read_sd.ops_by_phase(stamped) == by)
    torn = lines[:-1] + ["SHELL_PLAY_WIN win=999999 gap=0 over=0 n=0 sum=0"]
    check("ops cb: a torn frame is skipped", len(read_sd.ops_by_phase(torn)["idle"]["means"]) == 3)
    ok, why = read_sd.ops_cb_verdict(by)
    check("ops cb: +3 000 / +5 000 passes", ok and not why)
    far = [l.replace("sum=%d" % (250 * 505000), "sum=%d" % (250 * 510000)) for l in lines]
    ok, why = read_sd.ops_cb_verdict(read_sd.ops_by_phase(far))
    check("ops cb: busy 6 500 off idle on average still passes", ok)
    far = [l.replace("sum=%d" % (250 * 505000), "sum=%d" % (250 * 520000)) for l in lines]
    ok, why = read_sd.ops_cb_verdict(read_sd.ops_by_phase(far))
    check("ops cb: busy 11 500 off idle fails", not ok and any("busy" in w for w in why))
    ok, why = read_sd.ops_cb_verdict(read_sd.ops_by_phase(lines + [K_EXPORT, play_win(945001)]))
    check("ops cb: a win over the click line fails", not ok and any("945" in w for w in why))
    ok, why = read_sd.ops_cb_verdict(read_sd.ops_by_phase(lines[:3]))
    check("ops cb: no busy frames fails", not ok and "no busy frames" in why)
    # The stream classes do not see these lines.
    st = read_sd.cb_by_phase(lines)
    check("ops cb: cb_by_phase ignores the operation lines", not any(st[k]["means"] for k in st))
    _, text = run([l + "\n" for l in lines])
    check("ops cb: the report prints the callback by operation", "callback by operation" in text)


test_bank_stats()
test_bank_cmp_bad_fails()
test_buffer_times_and_cmp()
test_patch_100_of_100()
test_ops_by_phase()


# --- continuous stream and record (SHELL_SD_PROBE=duplex, spec section 5, D3) --
# One SHELL_SD_D line per pass: the blocks the read ring could not feed (under)
# and the write ring could not take (over), the read ring's lowest fill and the
# write ring's highest, the slowest f_read and f_write, the held blocks, fr.
# The first pass opens the files and primes the read ring: the raw window it
# ran in and the frame that measured it are slow and wide, and are not judged.
def d_line(w, under=0, over=0, rfill=17000, wfill=17000, worst_r=3000, worst_w=9000, held=30, fr=0):
    return ("SHELL_SD_D w=%d under=%d over=%d rfill=%d wfill=%d worst_r_us=%d worst_w_us=%d "
            "held=%d fr=%d" % (w, under, over, rfill, wfill, worst_r, worst_w, held, fr))


DX_FIRST = 3          # the first pass's counter: the card test ran before it
DX_PRIME_WIN = 990000  # the priming frame's worst block: over the click line on purpose


def duplex_capture(under_at=None, over_at=None, fr_at=None, spread=6, win=700000):
    """Card lines, then per counter c from DX_FIRST two passes (a D line, then
    the frame that measured it) and the raw window collected at c (printed
    w=c+1). The ring figures move with c, so the minimum and maximum come from
    one known line each. under_at / over_at / fr_at: the counter whose first
    line says under=1 / over=1 / fr=1."""
    lines = list(CARD)
    for c in range(DX_FIRST, COUNTERS):
        for k in range(2):
            mine = k == 0
            lines.append(d_line(c, under=1 if mine and c == under_at else 0,
                                over=1 if mine and c == over_at else 0,
                                rfill=16000 + 100 * c, wfill=16384 + 1000 * c,
                                worst_r=3000 + c, worst_w=9000 + 10 * c,
                                fr=1 if mine and c == fr_at else 0))
            prime = c == DX_FIRST and k == 0
            lines.append(play_win(DX_PRIME_WIN if prime else win, mean=900000 if prime else 510000))
        s = 300 if c == DX_FIRST else spread
        for r0 in range(0, ROWS, 6):
            pairs = " ".join("%d:%d" % (1000, 1000 + s) for _ in range(min(6, ROWS - r0)))
            lines.append("SHELL_PLAY_R w=%d r=%d %s" % (c + 1, r0, pairs))
    return [l + "\n" for l in lines]


def test_duplex_clean_passes():
    lines = duplex_capture()
    st = read_sd.duplex_stats(lines)
    check("duplex: a clean capture passes", st["pass"] and not st["why"])
    check("duplex: every pass counted", st["passes"] == 2 * (COUNTERS - DX_FIRST))
    check("duplex: no underrun, no overrun", st["under"] == 0 and st["over"] == 0)
    check("duplex: the read ring's lowest fill over all passes",
          st["rfill_min"] == 16000 + 100 * DX_FIRST)
    check("duplex: the write ring's highest fill over all passes",
          st["wfill_max"] == 16384 + 1000 * (COUNTERS - 1))
    check("duplex: the slowest f_read and f_write",
          st["worst_r_us"] == 3000 + COUNTERS - 1 and st["worst_w_us"] == 9000 + 10 * (COUNTERS - 1))
    check("duplex: the held share is held over 250 blocks a pass",
          abs(st["held_share"] - 30 / 250.0) < 1e-9)
    # Pots: windows from the one after the priming's (counter DX_FIRST + 1) on.
    check("duplex: the priming window is not judged, the others are",
          st["windows"] == COUNTERS - DX_FIRST - 1)
    rows_by_pin = [SENSE_OF_ROW.count(p) for p in range(rp.SENSES)]
    check("duplex: the pots' kept spread, per sense pin (Task 5's summary)",
          all(st["pots"][p]["worst"] == 6 for p in range(rp.SENSES) if rows_by_pin[p]))
    check("duplex: pots at the floor pass", st["pots_ok"])
    # The callback: the frame after the first line measured the priming and is
    # dropped (its 990 000 would fail the click line).
    check("duplex: the callback frames, the priming's dropped",
          st["cb"]["frames"] == 2 * (COUNTERS - DX_FIRST) - 1 and st["cb"]["mean"] == 510000.0
          and st["cb"]["max"] == 700000)
    check("duplex: the callback under the click line passes", st["cb_ok"])
    stamped = ["5.125 " + l for l in lines]
    check("duplex: host-time prefixes are stripped", read_sd.duplex_stats(stamped) == st)
    code, text = run(lines)
    check("duplex: the report exits 0", code == 0)
    check("duplex: the report says the verdict", "duplex verdict: PASS" in text)
    check("duplex: the report prints the pots and the callback",
          "pots verdict: PASS" in text and "callback verdict: PASS" in text)
    # The stream and operation readers do not see these lines.
    check("duplex: cb_by_phase and ops_by_phase ignore the D lines",
          not any(v["means"] for v in read_sd.cb_by_phase(lines).values())
          and not any(v["means"] for v in read_sd.ops_by_phase(lines).values()))
    # Pots over their band, or a block over the click line, are printed as a
    # FAIL but leave the exit code to the rings (A1 and A3 print theirs too).
    st = read_sd.duplex_stats(duplex_capture(spread=150))
    check("duplex: pots over the band fail the pot verdict only",
          st["pass"] and not st["pots_ok"])
    st = read_sd.duplex_stats(duplex_capture(win=945001))
    check("duplex: a block over the click line fails the callback verdict only",
          st["pass"] and not st["cb_ok"])
    code, text = run(duplex_capture(spread=150))
    check("duplex: the pot verdict does not change the exit code",
          code == 0 and "pots verdict: FAIL" in text)


def test_duplex_underrun_fails():
    st = read_sd.duplex_stats(duplex_capture(under_at=20))
    check("duplex: one under=1 fails", not st["pass"] and st["under"] == 1)
    check("duplex: the reason names the underrun", any("under" in w for w in st["why"]))
    code, text = run(duplex_capture(under_at=20))
    check("duplex: an underrun exits 1 and says FAIL", code == 1 and "duplex verdict: FAIL" in text)
    st = read_sd.duplex_stats(duplex_capture(over_at=30))
    check("duplex: one over=1 fails and is named",
          not st["pass"] and st["over"] == 1 and any("over" in w for w in st["why"]))
    st = read_sd.duplex_stats(duplex_capture(fr_at=12))
    check("duplex: an fr fails and is named with its pass",
          not st["pass"] and any("fr=1" in w and "w=12" in w for w in st["why"]))
    st = read_sd.duplex_stats(CARD)
    check("duplex: no D line fails", not st["pass"] and st["passes"] == 0)
    code, _ = run([l for l in duplex_capture() if "SHELL_SD verify" not in l])
    check("duplex: a failed card test fails", code == 1)


test_duplex_clean_passes()
test_duplex_underrun_fails()

print("%d failed" % len(FAILS))
sys.exit(1 if FAILS else 0)
