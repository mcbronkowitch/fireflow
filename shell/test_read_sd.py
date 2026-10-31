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
ROOT_OK = "SHELL_SD root=2 bin=0 names=FFSDTEST.DAT,SYSTEM~1"
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

print("%d failed" % len(FAILS))
sys.exit(1 if FAILS else 0)
