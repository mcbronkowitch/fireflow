"""Reads the Rev A play image's output (SHELL_PANEL_SCAN=1, no coupon) and
judges it against docs/hardware/reva-bring-up.md sections 5, 6 and 10.

Call:
    python read_play.py PORT capture.txt [seconds]   # record, watch, analyse
    python read_play.py --file capture.txt [--rest] [--stops] [--raw] [--seconds S]

Recording writes every line the board sends, unedited, to capture.txt --
that file is what gets committed under docs/hardware/captures/ -- and while
it records it prints every row that changed since the last frame, by name:
turn one pot and read which row moved. Default recording time 60 s.

A frame is what run_panel_scan_report() prints every ~500 ms: eight
SHELL_PLAY_V lines (73 rows, values x1000 of the last EMITTED normalised
value, -1000 = never emitted), one SHELL_PLAY line (span, sweeps, keys,
presses) and one SHELL_PLAY_LAW line (the three DWT cycle counters). A frame
missing any of them is dropped. CBP_* lines (callback profile) are ignored.

What the printed value can and cannot say: the firmware emits a new value
only when the raw word has moved more than kPotHysteresis (16 counts) from
the last emitted one, and it snaps to exactly 0 / 1000 at the stops
(scan_value.h). So "the printed value does not change at rest" IS the
hysteresis criterion of checklist row 5.3, and "reaches 0 and 1000" IS row
5.2's "both stops". Raw counts are not printed by the default image.

Raw counts (SHELL_RAW_SPREAD=1, fault R3): once per 20-sweep window (one
second) the image prints SHELL_PLAY_R lines, `w=<window> r=<first row>` and
up to six `lo:hi` pairs, the min and max of each row's raw 16-bit word in
that window. --raw judges them: per row the worst one-second spread, per
sense pin the worst and median of those, and row 5.7's touch rule over the
worst spread against kPotTouch. Window 1 opens at boot and is skipped.

Verdicts (exit code): no complete frame, or a span that is never valid, is
1. --rest makes any row that changed during the capture a failure (hands
off the panel). --stops makes any row that did not reach both stops a
failure (every pot turned end to end). --raw makes a missing raw window, or
a touch rule above kPotTouch, a failure. Without a flag the rows are
reported, not judged.

The line format was transcribed from panel_scan.cpp's PrintLine calls; the
row names come from generated_panel_map.h's comments.
"""
import re
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PANEL_MAP = HERE / "generated_panel_map.h"

# coupon_expect.h / scan_value.h: the span is valid once the rail reads at
# least kRailFloor and the zero at most kRailMargin.
RAIL_FLOOR = 58982
RAIL_MARGIN = 1311
# cpu-headroom spec section 3: the init patch's cb_max criterion, and the
# measured window's real deadline (shell/README.md).
CB_MAX_INIT = 900000
CB_DEADLINE = 945000
# scan-budget.md: 24 steps x 2 ms per sweep.
SWEEP_MS_EXPECTED = 48.0
# scan_value.h kPotHysteresis, pot_owner.h kPotTouch, panel_scan.cpp
# kRawWindowSweeps (the guard checks all three against the source).
POT_HYSTERESIS = 16
POT_TOUCH = 144
RAW_WINDOW_SWEEPS = 20
SENSES = 4

_V_LINE = re.compile(r"^SHELL_PLAY_V r=(\d+)((?: -?\d+)+)\s*$")
_R_LINE = re.compile(r"^SHELL_PLAY_R w=(\d+) r=(\d+)((?: \d+:\d+)+)\s*$")
_ROW_ENTRY = re.compile(r"\{\s*\d+,\s*\d+,\s*[\w:]+,\s*(\d+)[^}]*\},\s*//\s*row\s+(\d+)\s")
_PLAY_LINE = re.compile(r"^SHELL_PLAY (\S.*)$")
_LAW_LINE = re.compile(r"^SHELL_PLAY_LAW (\S.*)$")
_ROW_COMMENT = re.compile(r"//\s*row\s+(\d+)\s+(\w+)\s+--")


def row_names(path=PANEL_MAP):
    """Row index -> control name, from the generated header's comments."""
    names = {}
    for m in _ROW_COMMENT.finditer(path.read_text(encoding="utf-8")):
        names[int(m.group(1))] = m.group(2)
    return [names[i] for i in range(len(names))]


def row_senses(path=PANEL_MAP):
    """Row index -> sense pin, the table entry's fourth field."""
    senses = {}
    for m in _ROW_ENTRY.finditer(path.read_text(encoding="utf-8")):
        senses[int(m.group(2))] = int(m.group(1))
    return [senses[i] for i in range(len(senses))]


def _fields(text):
    out = {}
    for tok in text.split():
        if "=" not in tok:
            return None
        k, v = tok.split("=", 1)
        if k == "presses":
            try:
                out[k] = [int(x) for x in v.split(",")]
            except ValueError:
                return None
        else:
            try:
                out[k] = int(v)
            except ValueError:
                return None
    return out


def is_coupon_capture(lines):
    """The coupon play image prints SHELL_PLAY rv2=...; this reader is for
    Rev A and refuses those frames."""
    return any(_PLAY_LINE.match(l.strip()) and "rv2=" in l for l in lines)


def parse_frames(lines, rows=None):
    """Every complete frame in `lines`, in order. A frame opens at
    SHELL_PLAY_V r=0 and closes at SHELL_PLAY_LAW; a second r=0 before the
    close tears the open frame off."""
    rows = rows if rows is not None else len(row_names())
    frames, cur = [], None
    for raw in lines:
        line = raw.strip()
        m = _V_LINE.match(line)
        if m:
            r0 = int(m.group(1))
            vals = [int(x) for x in m.group(2).split()]
            if r0 == 0:
                cur = {"v": [None] * rows, "play": None, "law": None}
            if cur is None or cur["play"] is not None:
                continue
            if r0 + len(vals) > rows:
                cur = None
                continue
            cur["v"][r0:r0 + len(vals)] = vals
            continue
        m = _PLAY_LINE.match(line)
        if m:
            if cur is None or "rv2=" in line:
                cur = None
                continue
            f = _fields(m.group(1))
            if f is None or any(k not in f for k in ("zero", "rail", "valid", "sweeps", "keys", "presses")):
                cur = None
                continue
            cur["play"] = f
            continue
        m = _LAW_LINE.match(line)
        if m:
            if cur is None or cur["play"] is None:
                cur = None
                continue
            f = _fields(m.group(1))
            if f is not None and all(v is not None for v in cur["v"]):
                cur["law"] = f
                frames.append(cur)
            cur = None
    return frames


def stops(frames):
    """Row -> (reached 0, reached 1000) over the capture."""
    rows = len(frames[0]["v"]) if frames else 0
    out = {}
    for r in range(rows):
        vals = [f["v"][r] for f in frames]
        out[r] = (0 in vals, 1000 in vals)
    return out


def never_emitted(frames):
    """Rows that read -1000 in every frame: no valid span ever reached them."""
    rows = len(frames[0]["v"]) if frames else 0
    return [r for r in range(rows) if all(f["v"][r] == -1000 for f in frames)]


def unsteady(frames):
    """Row -> (min, max) of every row whose printed value changed."""
    rows = len(frames[0]["v"]) if frames else 0
    out = {}
    for r in range(rows):
        vals = [f["v"][r] for f in frames]
        if min(vals) != max(vals):
            out[r] = (min(vals), max(vals))
    return out


def changed(prev, cur, names=None):
    """[(row, name, old, new)] for every row that differs between two frames."""
    names = names or row_names()
    return [(r, names[r], a, b)
            for r, (a, b) in enumerate(zip(prev["v"], cur["v"])) if a != b]


def span_ok(frame):
    # The firmware's valid flag is exactly this rule (panel_span()); the rule
    # is checked first so the message names the rail or zero that failed it.
    p = frame["play"]
    if p["rail"] < RAIL_FLOOR:
        return False, "rail %d below the floor %d" % (p["rail"], RAIL_FLOOR)
    if p["zero"] > RAIL_MARGIN:
        return False, "zero %d above the margin %d" % (p["zero"], RAIL_MARGIN)
    if p["valid"] != 1:
        return False, "valid=0 although zero %d rail %d pass" % (p["zero"], p["rail"])
    return True, "zero %d rail %d" % (p["zero"], p["rail"])


def sweeps_per_second(frames, seconds):
    """Sweeps per second over the capture, None without a known duration:
    the board prints no clock, so only the recording's own length gives one."""
    if seconds is None or not frames or seconds <= 0:
        return None
    return (frames[-1]["play"]["sweeps"] - frames[0]["play"]["sweeps"]) / seconds


def parse_raw_windows(lines, rows=None):
    """Every complete raw window (SHELL_RAW_SPREAD=1), in order, each once:
    {"w", "lo", "hi"}. A window opens at SHELL_PLAY_R r=0 and is complete
    once every row is filled from lines of the same w; a line of another w
    tears the open window off."""
    rows = rows if rows is not None else len(row_names())
    windows, seen, cur = [], set(), None
    for raw in lines:
        m = _R_LINE.match(raw.strip())
        if not m:
            continue
        w, r0 = int(m.group(1)), int(m.group(2))
        pairs = [tuple(int(x) for x in p.split(":")) for p in m.group(3).split()]
        if r0 == 0:
            cur = {"w": w, "lo": [None] * rows, "hi": [None] * rows}
        if cur is None or cur["w"] != w or r0 + len(pairs) > rows:
            cur = None
            continue
        for i, (lo, hi) in enumerate(pairs):
            cur["lo"][r0 + i], cur["hi"][r0 + i] = lo, hi
        if all(x is not None for x in cur["lo"]):
            if w not in seen:
                windows.append(cur)
                seen.add(w)
            cur = None
    return windows


def raw_spreads(windows):
    """Row -> {"worst", "w", "lo", "hi", "windows"}: the worst max-min of the
    raw word in one window, which window, its lo/hi, and how many windows
    were judged. Window 1 opens at boot, before the span is valid and with a
    part-sweep in it, and is skipped."""
    judged = [w for w in windows if w["w"] > 1]
    rows = len(judged[0]["lo"]) if judged else 0
    out = {}
    for r in range(rows):
        best = None
        for w in judged:
            s = w["hi"][r] - w["lo"][r]
            if best is None or s > best["worst"]:
                best = {"worst": s, "w": w["w"], "lo": w["lo"][r], "hi": w["hi"][r]}
        best["windows"] = len(judged)
        out[r] = best
    return out


def touch_rule(worst):
    """Row 5.7's rule (pot_owner.h): max(2 x worst spread, 3 x H), rounded
    up to a multiple of 16."""
    v = max(2 * worst, 3 * POT_HYSTERESIS)
    return -(-v // 16) * 16


def sense_summary(spreads, senses):
    """Sense pin -> {"rows", "worst", "median", "over_h"} of the rows' worst
    spreads; over_h counts rows whose worst spread exceeds H."""
    from statistics import median
    out = {}
    for p in range(SENSES):
        vals = [spreads[r]["worst"] for r in spreads if senses[r] == p]
        out[p] = {"rows": len(vals),
                  "worst": max(vals) if vals else None,
                  "median": median(vals) if vals else None,
                  "over_h": sum(1 for v in vals if v > POT_HYSTERESIS)}
    return out


def report_raw(windows, out=None):
    """Judges row 5.7 from the raw windows: exit 1 without a judged window or
    when the touch rule over the worst spread lands above kPotTouch."""
    out = out or sys.stdout
    sp = raw_spreads(windows)
    if not sp:
        print("no judged raw window (SHELL_PLAY_R; window 1 is boot and "
              "skipped): was the image built with SHELL_RAW_SPREAD=1?", file=out)
        return 1
    names, senses = row_names(), row_senses()
    n = sp[0]["windows"]
    print("raw: %d windows of %d sweeps judged" % (n, RAW_WINDOW_SWEEPS), file=out)
    print("  row name          sense  worst     lo     hi  window", file=out)
    for r in sorted(sp, key=lambda r: -sp[r]["worst"]):
        s = sp[r]
        print("  %3d %-13s SENSE_%d %5d  %5d  %5d  w=%d%s"
              % (r, names[r], senses[r], s["worst"], s["lo"], s["hi"], s["w"],
                 "  > H" if s["worst"] > POT_HYSTERESIS else ""), file=out)
    for p, s in sense_summary(sp, senses).items():
        print("  SENSE_%d: %2d rows  worst %s  median %s  %d over H = %d"
              % (p, s["rows"], s["worst"], s["median"], s["over_h"], POT_HYSTERESIS),
              file=out)
    worst = max(s["worst"] for s in sp.values())
    rule = touch_rule(worst)
    print("  worst spread %d -> touch rule %d (kPotTouch %d)%s"
          % (worst, rule, POT_TOUCH, "  <- above kPotTouch" if rule > POT_TOUCH else ""),
          file=out)
    return 1 if rule > POT_TOUCH else 0


_stops = stops   # report()'s `stops` flag shadows the function


def report(frames, out=None, rest=False, stops=False, seconds=None):
    out = out or sys.stdout
    stops_required = stops
    if not frames:
        print("no complete SHELL_PLAY frame (eight SHELL_PLAY_V lines, "
              "SHELL_PLAY, SHELL_PLAY_LAW)", file=out)
        return 1
    names = row_names()
    failed = False

    last = frames[-1]
    ok, why = span_ok(last)
    valid_ever = any(span_ok(f)[0] for f in frames)
    print("frames %d  span last: %s (%s)  valid in %d of %d frames"
          % (len(frames), "ok" if ok else "INVALID", why,
             sum(1 for f in frames if span_ok(f)[0]), len(frames)), file=out)
    if not valid_ever:
        print("  span never valid: no knob reached the engine (5.1)", file=out)
        failed = True

    st = _stops(frames)
    never = never_emitted(frames)
    uns = unsteady(frames)
    print("  row name          last   min   max  stops", file=out)
    for r, name in enumerate(names):
        vals = [f["v"][r] for f in frames]
        lo, hi = st[r]
        mark = ("never emitted" if r in never
                else "both" if lo and hi
                else "0 only" if lo else "1000 only" if hi else "-")
        flag = ""
        if stops_required and not (lo and hi):
            flag = "  <- not both stops"
        if rest and r in uns:
            flag = "  <- moved at rest"
        print("  %3d %-13s %5d %5d %5d  %s%s"
              % (r, name, vals[-1], min(vals), max(vals), mark, flag), file=out)
    both = sum(1 for r in st if st[r] == (True, True))
    print("  stops: %d of %d rows reached both; %d never emitted; %d rows changed"
          % (both, len(names), len(never), len(uns)), file=out)
    if stops_required and both < len(names):
        failed = True
    if rest and uns:
        print("  rest: %s moved" % ", ".join(names[r] for r in uns), file=out)
        failed = True

    p = last["play"]
    print("keys mask=%d  presses REC_A=%d REC_B=%d MOD=%d SHFT=%d  sweeps=%d"
          % (p["keys"], *p["presses"], p["sweeps"]), file=out)
    rate = sweeps_per_second(frames, seconds)
    if rate is not None:
        print("  sweep rate %.2f/s (%.2f ms per sweep; %.0f ms expected)"
              % (rate, 1000.0 / rate if rate else float("inf"), SWEEP_MS_EXPECTED),
              file=out)

    law_max = {k: max(f["law"].get(k, 0) for f in frames)
               for k in ("cyc_max", "led_max", "cb_max")}
    print("law: cyc_max=%d led_max=%d cb_max=%d  (cb_max criterion %d, "
          "deadline %d)%s"
          % (law_max["cyc_max"], law_max["led_max"], law_max["cb_max"],
             CB_MAX_INIT, CB_DEADLINE,
             "  <- over" if law_max["cb_max"] > CB_MAX_INIT else ""), file=out)
    return 1 if failed else 0


def record(port, path, seconds, out=None):
    """Record every line to `path` and print the rows that changed between
    consecutive complete frames, by name, while recording."""
    import serial   # imported here so the guard runs without pyserial
    out = out or sys.stdout
    names = row_names()
    pending, prev = [], None
    with serial.Serial(port, timeout=1.0) as ser, \
            open(path, "w", encoding="utf-8", newline="\n") as dst:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if not line:
                continue
            dst.write(line if line.endswith("\n") else line + "\n")
            pending.append(line)
            if line.startswith("SHELL_PLAY_LAW"):
                frames = parse_frames(pending, len(names))
                pending = []
                if not frames:
                    continue
                cur = frames[-1]
                if prev is not None:
                    for r, name, a, b in changed(prev, cur, names):
                        print("  row %2d %-13s %5d -> %5d" % (r, name, a, b),
                              file=out)
                else:
                    ok, why = span_ok(cur)
                    print("first frame: span %s (%s)"
                          % ("ok" if ok else "INVALID", why), file=out)
                prev = cur


def main(argv):
    args = list(argv[1:])
    rest = "--rest" in args
    stops_flag = "--stops" in args
    raw = "--raw" in args
    seconds = None
    if "--seconds" in args:
        i = args.index("--seconds")
        seconds = float(args[i + 1])
        del args[i:i + 2]
    args = [a for a in args if a not in ("--rest", "--stops", "--raw")]
    if len(args) == 2 and args[0] == "--file":
        path = args[1]
    elif len(args) in (2, 3) and args[0] != "--file":
        path = args[1]
        seconds = float(args[2]) if len(args) == 3 else 60.0
        record(args[0], path, seconds)
    else:
        raise SystemExit("usage: read_play.py PORT capture.txt [seconds] "
                         "| --file capture.txt [--rest] [--stops] [--raw] [--seconds S]")
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    if is_coupon_capture(lines):
        print("this is a coupon play capture (SHELL_PLAY rv2=...); "
              "read_play.py reads the Rev A image only")
        return 1
    code = report(parse_frames(lines), rest=rest, stops=stops_flag,
                  seconds=seconds)
    if raw:
        code = max(code, report_raw(parse_raw_windows(lines)))
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
