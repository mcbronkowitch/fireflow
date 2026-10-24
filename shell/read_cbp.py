#!/usr/bin/env python3
"""Reads the callback profile's window lines out of a play capture
(SPKY_PROBE_MARKS=1, shell/cb_profile.cpp) and tabulates them: one row per
2 s window, the whole callback, each deck, the mix, the law, and since fault
R4 each deck split into modulation + control (m), engine (e) and FX (f).

Call:
    python read_cbp.py capture.txt                 # every window
    python read_cbp.py capture.txt --from 5 --to 14   # those windows, plus their means

Lines read (transcribed from cb_profile.cpp's PrintLine calls):
    CBP_WIN  w=N cb= cb_max= proc= a= b= mix= law=
    CBP_WIN2 w=N am= ae= af= bm= be= bf=
    CBP_BOOT patch= eng_a= eng_b=          (the last one seen)
Everything else, SHELL_PLAY_* included, is ignored. A window with a CBP_WIN
but no CBP_WIN2 (an image from before the split) prints dashes. am+ae+af is
a's three parts, each a floor of its own 1000-block mean, so it may fall a
cycle or two short of a; a gap above kSplitSlack is reported as a reader
failure, since it means the lines do not describe the same window.

Exit code: 0 with at least one window and no gap; 1 otherwise.
"""
import argparse
import re
import sys

_WIN = re.compile(r"^CBP_WIN w=(\d+) cb=(\d+) cb_max=(\d+) proc=(\d+) a=(\d+) b=(\d+) mix=(\d+) law=(\d+)\s*$")
_WIN2 = re.compile(r"^CBP_WIN2 w=(\d+) am=(\d+) ae=(\d+) af=(\d+) bm=(\d+) be=(\d+) bf=(\d+)\s*$")
_BOOT = re.compile(r"^CBP_BOOT patch=(\d+) eng_a=(-?\d+) eng_b=(-?\d+)\s*$")

FIELDS = ("cb", "cb_max", "a", "am", "ae", "af", "b", "bm", "be", "bf", "mix", "law")
kSplitSlack = 4   # cycles: three floors against one


def parse(lines):
    """-> (windows: {w: {field: int}}, boot: (patch, eng_a, eng_b) or None)."""
    wins, boot = {}, None
    for line in lines:
        m = _WIN.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            d.update(cb=int(m.group(2)), cb_max=int(m.group(3)), proc=int(m.group(4)),
                     a=int(m.group(5)), b=int(m.group(6)), mix=int(m.group(7)), law=int(m.group(8)))
            continue
        m = _WIN2.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            d.update(am=int(m.group(2)), ae=int(m.group(3)), af=int(m.group(4)),
                     bm=int(m.group(5)), be=int(m.group(6)), bf=int(m.group(7)))
            continue
        m = _BOOT.match(line)
        if m:
            boot = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
    return wins, boot


def split_gaps(wins):
    """Windows whose deck split does not add up to the deck: [(w, deck, gap)]."""
    out = []
    for w, d in sorted(wins.items()):
        for deck, parts in (("a", ("am", "ae", "af")), ("b", ("bm", "be", "bf"))):
            if deck not in d or any(p not in d for p in parts):
                continue
            gap = d[deck] - sum(d[p] for p in parts)
            if gap < 0 or gap > kSplitSlack:
                out.append((w, deck, gap))
    return out


def means(wins, lo, hi):
    """Per-field means over the complete windows lo..hi (both ends), or None."""
    rows = [d for w, d in wins.items() if lo <= w <= hi and all(f in d for f in FIELDS)]
    if not rows:
        return None
    return {f: sum(d[f] for d in rows) / len(rows) for f in FIELDS}, len(rows)


def fmt_row(label, d):
    cells = [label]
    for f in FIELDS:
        v = d.get(f)
        cells.append("%8s" % ("-" if v is None else ("%d" % round(v))))
    return " ".join(cells)


def report(wins, boot, lo, hi, out=sys.stdout):
    if boot:
        print("boot patch=%d eng_a=%d eng_b=%d" % boot, file=out)
    print(" ".join(["%6s" % "w"] + ["%8s" % f for f in FIELDS]), file=out)
    for w in sorted(wins):
        if lo <= w <= hi:
            print(fmt_row("%6d" % w, wins[w]), file=out)
    m = means(wins, lo, hi)
    if m:
        print(fmt_row("  mean", m[0]) + "   (%d complete windows)" % m[1], file=out)
    gaps = split_gaps(wins)
    for w, deck, gap in gaps:
        print("FAIL window %d: deck %s is %d cycles off its split" % (w, deck, gap), file=out)
    return bool(wins) and not gaps


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    a = ap.parse_args(argv)
    with open(a.capture, encoding="utf-8", errors="replace") as f:
        wins, boot = parse(f.read().splitlines())
    if not wins:
        print("no CBP_WIN line in %s" % a.capture, file=sys.stderr)
        return 1
    return 0 if report(wins, boot, a.lo, a.hi) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
