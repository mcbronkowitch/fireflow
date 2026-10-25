#!/usr/bin/env python3
"""Reads the callback profile's window lines out of a play capture
(SPKY_PROBE_MARKS=1, shell/cb_profile.cpp) and tabulates them: one row per
2 s window, the whole callback, each deck, the mix, the law, and since fault
R4 each deck split into modulation + control (m), engine (e) and FX (f), and
the mix into observers + taps (xo), blend + duck (xd), reverb (xr), return +
sleep (xt) and limiter (xl), and each deck's modulation + control half into
before-the-deck (ap / bp), lanes (al / bl), fade + swap + fire bookkeeping
(ai / bi), control tick (at / bt) and trigger + gate edge + tap (ar / br),
and the lanes into pitch lane (aq / bq), step clock + onset ring (ao / bo)
and texture tick (au / bu), and the pitch lane into the call in (aqc /
bqc), counters + phase advance + wrap (aqh / bqh), step search (aqs / bqs)
and slew + RANGE + latch + stores (aqt / bqt), and GRIT into the FX setters
+ call in (agp / bgp), switch (agw / bgw), stage 1 (ag1 / bg1: Overdrive or
Decimator), stage 2 (ag2 / bg2: nothing or SampleRateReducer) and XFade +
crossfade (agx / bgx). CBP_WORST (each block's worst sample, broken
down) is parsed into the worst table the report prints below the windows.

Call:
    python read_cbp.py capture.txt                 # every window
    python read_cbp.py capture.txt --from 5 --to 14   # those windows, plus their means
    python read_cbp.py capture.txt --hot 920000       # a lower bar for the hot-block table

Lines read (transcribed from cb_profile.cpp's PrintLine calls):
    CBP_WIN  w=N cb= cb_max= proc= a= b= mix= law=
    CBP_WIN2 w=N am= ae= af= bm= be= bf=
    CBP_WIN3 w=N as= ag= ax= ac= an= bs= bg= bx= bc= bn=
    CBP_WIN4 w=N xo= xd= xr= xt= xl=
    CBP_WIN5 w=N ap= al= ai= at= ar= bp= bl= bi= bt= br=
    CBP_WIN6 w=N aq= ao= au= bq= bo= bu=
    CBP_WIN7 w=N aqc= aqh= aqs= aqt= bqc= bqh= bqs= bqt=
    CBP_WIN8 w=N agp= agw= ag1= ag2= agx= bgp= bgw= bg1= bg2= bgx=
    CBP_WORST w=N last= am= al= at= ae= af= bm= bl= bt= be= bf= x=
    CBP_WIN9 w=N awp= awc= awv= ati= ata= bwp= bwc= bwv= bti= bta=
                                           (a WAVE deck's engine: strewn tones,
                                           control tick, voices; strikes inside
                                           process() and all, window totals --
                                           read as per-block means)
    CBP_BOOT patch= eng_a= eng_b=          (the last one seen)
    CBP_RVCLR n= last= max=                (every one; the report lists them)
    CBP_REC  blk=N cb= proc= law= led= maxs=V@I med= ev=HEX
    CBP_REC2 blk=N a= b= mix= va= vb=
    CBP_REC3 blk=N am= ae= af= bm= be= bf=
    CBP_REC4 blk=N al= at= ax= bl= bt= bx=
    CBP_REC5 blk=N d=0|1 wp= wc= wv= ti= ta=   (deck d on WAVE: CBP_WIN9's
                                           fields for that block, a* / b*)
The CBP_REC* lines are the ring's single blocks (one above the firmware's
kHot, or one in which a phrase advanced), joined by blk. Those above --hot
(default 930000, the firmware's kHot) are tabulated with their excess over
the window means: which segment makes a peak block (fault R4, the noon
peaks). CBP_REC3 / CBP_REC4 exist since that split; a record without them
prints dashes. A hot block's am+ae+af must equal its a exactly (raw sums,
no floors).
Everything else, SHELL_PLAY_* included, is ignored. A window with a CBP_WIN
but no CBP_WIN2 / CBP_WIN3 / CBP_WIN4 (an image from before that split)
prints dashes. am+ae+af is a's three parts, as+ag+ax+ac+an is af's five
(smoothers, GRIT, FLUX, tap + FX mix + COMP, send law), xo+xd+xr+xt+xl
is mix's five, ap+al+ai+at+ar is am's five, aq+ao+au is al's three and
aqc+aqh+aqs+aqt is aq's four and agp+agw+ag1+ag2+agx is ag's five, each a
floor of its own
1000-block mean, so a sum may fall a few cycles short of its whole; a gap
above kSplitSlack is reported as a reader failure, since it means the lines
do not describe the same window.

Exit code: 0 with at least one window and no gap; 1 otherwise.
"""
import argparse
import re
import sys

_WIN = re.compile(r"^CBP_WIN w=(\d+) cb=(\d+) cb_max=(\d+) proc=(\d+) a=(\d+) b=(\d+) mix=(\d+) law=(\d+)\s*$")
_WIN2 = re.compile(r"^CBP_WIN2 w=(\d+) am=(\d+) ae=(\d+) af=(\d+) bm=(\d+) be=(\d+) bf=(\d+)\s*$")
_WIN3 = re.compile(r"^CBP_WIN3 w=(\d+) as=(\d+) ag=(\d+) ax=(\d+) ac=(\d+) an=(\d+)"
                   r" bs=(\d+) bg=(\d+) bx=(\d+) bc=(\d+) bn=(\d+)\s*$")
_WIN4 = re.compile(r"^CBP_WIN4 w=(\d+) xo=(\d+) xd=(\d+) xr=(\d+) xt=(\d+) xl=(\d+)\s*$")
_WIN5 = re.compile(r"^CBP_WIN5 w=(\d+) ap=(\d+) al=(\d+) ai=(\d+) at=(\d+) ar=(\d+)"
                   r" bp=(\d+) bl=(\d+) bi=(\d+) bt=(\d+) br=(\d+)\s*$")
_WIN6 = re.compile(r"^CBP_WIN6 w=(\d+) aq=(\d+) ao=(\d+) au=(\d+) bq=(\d+) bo=(\d+) bu=(\d+)\s*$")
_WIN7 = re.compile(r"^CBP_WIN7 w=(\d+) aqc=(\d+) aqh=(\d+) aqs=(\d+) aqt=(\d+)"
                   r" bqc=(\d+) bqh=(\d+) bqs=(\d+) bqt=(\d+)\s*$")
_WIN8 = re.compile(r"^CBP_WIN8 w=(\d+) agp=(\d+) agw=(\d+) ag1=(\d+) ag2=(\d+) agx=(\d+)"
                   r" bgp=(\d+) bgw=(\d+) bg1=(\d+) bg2=(\d+) bgx=(\d+)\s*$")
_WIN9 = re.compile(r"^CBP_WIN9 w=(\d+) awp=(\d+) awc=(\d+) awv=(\d+) ati=(\d+) ata=(\d+)"
                   r" bwp=(\d+) bwc=(\d+) bwv=(\d+) bti=(\d+) bta=(\d+)\s*$")
WIN9_FIELDS = ("awp", "awc", "awv", "ati", "ata", "bwp", "bwc", "bwv", "bti", "bta")
kWinBlocks = 1000   # cb_profile.cpp: a window's blocks; ti / ta print as totals
_WORST = re.compile(r"^CBP_WORST w=(\d+) last=(\d+) am=(\d+) al=(\d+) at=(\d+) ae=(\d+) af=(\d+)"
                    r" bm=(\d+) bl=(\d+) bt=(\d+) be=(\d+) bf=(\d+) x=(\d+)\s*$")
WORST_FIELDS = ("last", "am", "al", "at", "ae", "af", "bm", "bl", "bt", "be", "bf", "x")
_BOOT = re.compile(r"^CBP_BOOT patch=(\d+) eng_a=(-?\d+) eng_b=(-?\d+)\s*$")
_RVCLR = re.compile(r"^CBP_RVCLR n=(\d+) last=(\d+) max=(\d+)\s*$")
_REC = re.compile(r"^CBP_REC blk=(\d+) cb=(\d+) proc=(\d+) law=(\d+) led=(\d+) maxs=(\d+)@(\d+)"
                  r" med=(\d+) ev=([0-9a-f]+)\s*$")
_REC2 = re.compile(r"^CBP_REC2 blk=(\d+) a=(\d+) b=(\d+) mix=(\d+) va=(-?\d+) vb=(-?\d+)\s*$")
_REC3 = re.compile(r"^CBP_REC3 blk=(\d+) am=(\d+) ae=(\d+) af=(\d+) bm=(\d+) be=(\d+) bf=(\d+)\s*$")
_REC4 = re.compile(r"^CBP_REC4 blk=(\d+) al=(\d+) at=(\d+) ax=(\d+) bl=(\d+) bt=(\d+) bx=(\d+)\s*$")
_REC5 = re.compile(r"^CBP_REC5 blk=(\d+) d=([01]) wp=(\d+) wc=(\d+) wv=(\d+) ti=(\d+) ta=(\d+)\s*$")
# A hot block's columns; every one but ev also names a window field, so the
# excess row can subtract the window mean.
REC_FIELDS = ("cb", "law", "a", "am", "al", "at", "ae", "af", "ax",
              "b", "bm", "bl", "bt", "be", "bf", "bx", "mix") + WIN9_FIELDS
REC_SPLITS = (("a", ("am", "ae", "af")), ("b", ("bm", "be", "bf")))
kHot = 930000

FIELDS = ("cb", "cb_max", "a", "am", "ap", "al", "aq", "aqc", "aqh", "aqs", "aqt", "ao", "au", "ai", "at", "ar", "ae", "af",
          "as", "ag", "agp", "agw", "ag1", "ag2", "agx", "ax", "ac", "an",
          "b", "bm", "bp", "bl", "bq", "bqc", "bqh", "bqs", "bqt", "bo", "bu", "bi", "bt", "br", "be", "bf",
          "bs", "bg", "bgp", "bgw", "bg1", "bg2", "bgx", "bx", "bc", "bn",
          "mix", "xo", "xd", "xr", "xt", "xl", "law") + WIN9_FIELDS
# A whole and the parts that must add up to it, within kSplitSlack.
SPLITS = (("a", ("am", "ae", "af")), ("b", ("bm", "be", "bf")),
          ("af", ("as", "ag", "ax", "ac", "an")), ("bf", ("bs", "bg", "bx", "bc", "bn")),
          ("mix", ("xo", "xd", "xr", "xt", "xl")),
          ("am", ("ap", "al", "ai", "at", "ar")), ("bm", ("bp", "bl", "bi", "bt", "br")),
          ("al", ("aq", "ao", "au")), ("bl", ("bq", "bo", "bu")),
          ("aq", ("aqc", "aqh", "aqs", "aqt")), ("bq", ("bqc", "bqh", "bqs", "bqt")),
          ("ag", ("agp", "agw", "ag1", "ag2", "agx")), ("bg", ("bgp", "bgw", "bg1", "bg2", "bgx")))
kSplitSlack = 6   # cycles: up to five floors against one


def parse(lines, clears=None, worst=None, recs=None):
    """-> (windows: {w: {field: int}}, boot: (patch, eng_a, eng_b) or None).
    clears, if a list, collects every CBP_RVCLR as (n, last, max); worst, if
    a dict, collects every CBP_WORST as {w: {field: int}}; recs, if a dict,
    collects the CBP_REC* lines as {blk: {field: int}} (ev as an int)."""
    wins, boot = {}, None
    for line in lines:
        if recs is not None and line.startswith("CBP_REC"):
            m = _REC.match(line)
            if m:
                d = recs.setdefault(int(m.group(1)), {})
                for k, name in enumerate(("cb", "proc", "law", "led", "maxs", "maxi", "med")):
                    d[name] = int(m.group(2 + k))
                d["ev"] = int(m.group(9), 16)
                continue
            m = _REC5.match(line)
            if m:
                d = recs.setdefault(int(m.group(1)), {})
                k = "ab"[int(m.group(2))]
                for j, name in enumerate(("wp", "wc", "wv", "ti", "ta")):
                    d[k + name] = int(m.group(3 + j))
                continue
            for rx, names in ((_REC2, ("a", "b", "mix", "va", "vb")),
                              (_REC3, ("am", "ae", "af", "bm", "be", "bf")),
                              (_REC4, ("al", "at", "ax", "bl", "bt", "bx"))):
                m = rx.match(line)
                if m:
                    d = recs.setdefault(int(m.group(1)), {})
                    for k, name in enumerate(names):
                        d[name] = int(m.group(2 + k))
                    break
            continue
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
        m = _WIN3.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("as", "ag", "ax", "ac", "an", "bs", "bg", "bx", "bc", "bn")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN4.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("xo", "xd", "xr", "xt", "xl")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN5.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("ap", "al", "ai", "at", "ar", "bp", "bl", "bi", "bt", "br")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN6.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("aq", "ao", "au", "bq", "bo", "bu")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN7.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("aqc", "aqh", "aqs", "aqt", "bqc", "bqh", "bqs", "bqt")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN8.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(("agp", "agw", "ag1", "ag2", "agx", "bgp", "bgw", "bg1", "bg2", "bgx")):
                d[name] = int(m.group(2 + k))
            continue
        m = _WIN9.match(line)
        if m:
            w = int(m.group(1))
            d = wins.setdefault(w, {})
            for k, name in enumerate(WIN9_FIELDS):
                v = int(m.group(2 + k))
                # Strikes print as window totals; per block, like the rest.
                d[name] = v / kWinBlocks if name.endswith(("ti", "ta")) else v
            continue
        m = _WORST.match(line)
        if m:
            if worst is not None:
                worst[int(m.group(1))] = {f: int(m.group(2 + k)) for k, f in enumerate(WORST_FIELDS)}
            continue
        m = _BOOT.match(line)
        if m:
            boot = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            continue
        m = _RVCLR.match(line)
        if m and clears is not None:
            clears.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    return wins, boot


def split_gaps(wins):
    """Windows whose split does not add up to its whole: [(w, whole, gap)]."""
    out = []
    for w, d in sorted(wins.items()):
        for whole, parts in SPLITS:
            if whole not in d or any(p not in d for p in parts):
                continue
            gap = d[whole] - sum(d[p] for p in parts)
            if gap < 0 or gap > kSplitSlack:
                out.append((w, whole, gap))
    return out


def hot_blocks(recs, hot=kHot):
    """The ring's blocks above hot, in block order: [(blk, {field: int})]."""
    return [(b, d) for b, d in sorted(recs.items()) if d.get("cb", 0) > hot]


def rec_gaps(recs):
    """Records whose deck split does not equal its deck: [(blk, whole, gap)]."""
    out = []
    for b, d in sorted(recs.items()):
        for whole, parts in REC_SPLITS:
            if whole in d and all(p in d for p in parts):
                gap = d[whole] - sum(d[p] for p in parts)
                if gap != 0:
                    out.append((b, whole, gap))
    return out


def hot_excess(rows, win_mean):
    """Mean of the hot blocks minus the window mean, per REC_FIELDS column
    both sides carry: {field: float}, or None without a hot block."""
    if not rows:
        return None
    out = {}
    for f in REC_FIELDS:
        vals = [d[f] for _, d in rows if f in d]
        if len(vals) == len(rows) and f in win_mean:
            out[f] = sum(vals) / len(vals) - win_mean[f]
    return out


def means(wins, lo, hi):
    """Per-field means over the windows lo..hi (both ends) that carry every
    field the capture's image prints (a pre-split image has fewer), or None."""
    present = [f for f in FIELDS if any(f in d for d in wins.values())]
    rows = [d for w, d in wins.items() if lo <= w <= hi and all(f in d for f in present)]
    if not rows:
        return None
    return {f: sum(d[f] for d in rows) / len(rows) for f in present}, len(rows)


def fmt_row(label, d):
    cells = [label]
    for f in FIELDS:
        v = d.get(f)
        cells.append("%8s" % ("-" if v is None else ("%d" % round(v))))
    return " ".join(cells)


def report_hot(recs, win_mean, hot, out):
    """The hot-block table and its excess row; False on a record gap."""
    rows = hot_blocks(recs, hot)
    print("hot blocks (cb > %d): %d" % (hot, len(rows)), file=out)
    if rows:
        print(" ".join(["%8s" % "blk", "%7s" % "ev"] + ["%8s" % f for f in REC_FIELDS]), file=out)
        for b, d in rows:
            cells = ["%8d" % b, "%7x" % d.get("ev", 0)]
            cells += ["%8s" % ("-" if d.get(f) is None else "%d" % d[f]) for f in REC_FIELDS]
            print(" ".join(cells), file=out)
        ex = hot_excess(rows, win_mean or {})
        if ex:
            cells = ["  excess", "%7s" % ""]
            cells += ["%8s" % ("-" if f not in ex else "%+d" % round(ex[f])) for f in REC_FIELDS]
            print(" ".join(cells) + "   (hot mean - window mean)", file=out)
    gaps = rec_gaps(recs)
    for b, whole, gap in gaps:
        print("FAIL block %d: %s is %d cycles off its split" % (b, whole, gap), file=out)
    return not gaps


def report(wins, boot, lo, hi, out=sys.stdout, clears=(), worst=None, recs=None, hot=kHot):
    if boot:
        print("boot patch=%d eng_a=%d eng_b=%d" % boot, file=out)
    for c in clears:
        print("reverb clear n=%d last=%d max=%d" % c, file=out)
    print(" ".join(["%6s" % "w"] + ["%8s" % f for f in FIELDS]), file=out)
    for w in sorted(wins):
        if lo <= w <= hi:
            print(fmt_row("%6d" % w, wins[w]), file=out)
    m = means(wins, lo, hi)
    if m:
        print(fmt_row("  mean", m[0]) + "   (%d complete windows)" % m[1], file=out)
    rows = [d for w, d in sorted((worst or {}).items()) if lo <= w <= hi]
    if rows:
        print("worst sample, window means:", file=out)
        print(" ".join(["%6s" % "w"] + ["%8s" % f for f in WORST_FIELDS]), file=out)
        for w in sorted(worst):
            if lo <= w <= hi:
                print(" ".join(["%6d" % w] + ["%8d" % worst[w][f] for f in WORST_FIELDS]), file=out)
        mean = {f: sum(d[f] for d in rows) / len(rows) for f in WORST_FIELDS}
        print(" ".join(["  mean"] + ["%8d" % round(mean[f]) for f in WORST_FIELDS]), file=out)
    rec_ok = True
    if recs:
        rec_ok = report_hot(recs, m[0] if m else {}, hot, out)
    gaps = split_gaps(wins)
    for w, whole, gap in gaps:
        print("FAIL window %d: %s is %d cycles off its split" % (w, whole, gap), file=out)
    return bool(wins) and not gaps and rec_ok


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 30)
    ap.add_argument("--hot", type=int, default=kHot)
    a = ap.parse_args(argv)
    with open(a.capture, encoding="utf-8", errors="replace") as f:
        clears, worst, recs = [], {}, {}
        wins, boot = parse(f.read().splitlines(), clears, worst, recs)
    if not wins:
        print("no CBP_WIN line in %s" % a.capture, file=sys.stderr)
        return 1
    return 0 if report(wins, boot, a.lo, a.hi, clears=clears, worst=worst, recs=recs, hot=a.hot) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
