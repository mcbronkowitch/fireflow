#!/usr/bin/env python3
"""Guard for shell/read_cbp.py. Plain script -- pytest is not installed here --
and its exit code is the verdict. The fixture is hand-written in the line
format cb_profile.cpp prints; the committed R4 capture (an image from before
the split) proves the no-CBP_WIN2 path on real lines.
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import read_cbp as r  # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


FIXTURE = """SHELL_PLAY_V r=0 531 523 562 460 531 450 445 527 605 517
CBP_WIN w=5 cb=900000 cb_max=950000 proc=870000 a=360000 b=380000 mix=150000 law=12000
CBP_WIN2 w=5 am=100000 ae=200000 af=59998 bm=90000 be=210000 bf=80000
CBP_WIN3 w=5 as=5000 ag=1000 ax=40000 ac=10000 an=3995 bs=5000 bg=1000 bx=60000 bc=10000 bn=4000
CBP_WIN4 w=5 xo=10000 xd=20000 xr=98000 xt=2000 xl=19998
CBP_WIN5 w=5 ap=5000 al=40000 ai=5000 at=45000 ar=5000 bp=4000 bl=40000 bi=5000 bt=36000 br=4998
CBP_WIN6 w=5 aq=30000 ao=2000 au=8000 bq=30000 bo=2000 bu=7998
CBP_WIN7 w=5 aqc=500 aqh=4000 aqs=20000 aqt=5497 bqc=500 bqh=4000 bqs=20000 bqt=5500
CBP_WIN8 w=5 agp=100 agw=200 ag1=300 ag2=200 agx=199 bgp=100 bgw=200 bg1=500 bg2=0 bgx=198
CBP_WORST w=5 last=990 am=900 al=500 at=300 ae=20000 af=1500 bm=800 bl=500 bt=250 be=9000 bf=1400 x=1600
CBP_BOOT patch=3 eng_a=4 eng_b=3
CBP_RVCLR n=1 last=393216 max=393216
CBP_WIN w=6 cb=910000 cb_max=960000 proc=880000 a=362000 b=382000 mix=152000 law=12200
CBP_WIN2 w=6 am=101000 ae=201000 af=60000 bm=91000 be=211000 bf=80000
CBP_WIN3 w=6 as=5000 ag=1000 ax=40000 ac=10000 an=4000 bs=5000 bg=1000 bx=60000 bc=10000 bn=4000
CBP_WIN4 w=6 xo=10000 xd=20000 xr=100000 xt=2000 xl=20000
CBP_WIN5 w=6 ap=5000 al=40000 ai=5000 at=46000 ar=5000 bp=4000 bl=40000 bi=5000 bt=37000 br=5000
CBP_WIN6 w=6 aq=30000 ao=2000 au=8000 bq=30000 bo=2000 bu=8000
CBP_WIN7 w=6 aqc=500 aqh=4000 aqs=20000 aqt=5500 bqc=500 bqh=4000 bqs=20000 bqt=5500
CBP_WIN8 w=6 agp=100 agw=200 ag1=300 ag2=200 agx=200 bgp=100 bgw=200 bg1=500 bg2=0 bgx=200
CBP_WORST w=6 last=1000 am=1000 al=500 at=400 ae=21000 af=1500 bm=800 bl=500 bt=250 be=9000 bf=1400 x=1600
CBP_WIN w=7 cb=920000 cb_max=970000 proc=890000 a=364000 b=384000 mix=154000 law=12400
""".splitlines()

wins, boot = r.parse(FIXTURE)
check("three windows parsed", sorted(wins) == [5, 6, 7])
check("the boot line is kept", boot == (3, 4, 3))
check("a window joins its three lines",
      wins[5]["a"] == 360000 and wins[5]["ae"] == 200000 and wins[5]["bx"] == 60000)
check("a window without CBP_WIN2 / CBP_WIN3 has no split", "am" not in wins[7] and "ax" not in wins[7])
check("a window joins its mix split", wins[5]["xr"] == 98000 and wins[6]["xl"] == 20000
      and "xo" not in wins[7])
check("a window joins its modulation split", wins[5]["at"] == 45000 and wins[6]["bt"] == 37000
      and "ap" not in wins[7])
check("a window joins its lanes split", wins[5]["aq"] == 30000 and wins[6]["bu"] == 8000
      and "aq" not in wins[7])
check("a window joins its GRIT split", wins[5].get("ag1") == 300 and wins[6].get("bgx") == 200
      and "agp" not in wins[7])
check("a window joins its pitch-lane split", wins[5].get("aqt") == 5497 and wins[6].get("bqs") == 20000
      and "aqc" not in wins[7])
worst = {}
r.parse(FIXTURE, None, worst)
check("the worst-sample lines are collected", sorted(worst) == [5, 6]
      and worst[5]["ae"] == 20000 and worst[6]["last"] == 1000)
clears = []
r.parse(FIXTURE, clears)
check("a reverb clear is collected", clears == [(1, 393216, 393216)])
m = r.means(wins, 5, 6)
check("means run over the complete windows only", m is not None and m[1] == 2
      and m[0]["cb"] == 905000 and m[0]["ae"] == 200500 and m[0]["an"] == 3997.5)
check("a range with no complete window has no mean", r.means(wins, 7, 7) is None)
check("a split a few cycles short is within slack", r.split_gaps(wins) == [])
out = io.StringIO()
check("the report succeeds on the fixture", r.report(wins, boot, 0, 1 << 30, out, clears, worst) is True)
text = out.getvalue()
check("the report prints the worst-sample table and its mean",
      "worst sample, window means:" in text and "   20500" in text)
check("the report lists the reverb clear", "reverb clear n=1 last=393216 max=393216" in text)
check("the report prints the boot line, every window and the mean",
      "boot patch=3 eng_a=4 eng_b=3" in text and "\n     5 " in text and "\n     7 " in text
      and "mean" in text and "(2 complete windows)" in text)
check("a missing split prints dashes", "       -" in text.splitlines()[5])
# A pre-split capture still gets a mean over the fields its image printed.
old = r.parse([l for l in FIXTURE
               if not l.startswith(("CBP_WIN2", "CBP_WIN3", "CBP_WIN4", "CBP_WIN5", "CBP_WIN6", "CBP_WIN7", "CBP_WIN8"))])[0]
mo = r.means(old, 5, 7)
check("a pre-split capture's mean covers its own fields",
      mo is not None and mo[1] == 3 and mo[0]["cb"] == 910000 and "am" not in mo[0])

# --- sabotages ------------------------------------------------------------
bad = r.parse([l.replace("af=60000", "af=50000") for l in FIXTURE])[0]
check("sabotage: a split that does not add up goes red",
      r.split_gaps(bad) == [(6, "a", 10000), (6, "af", -10000)]
      and r.report(bad, None, 0, 99, io.StringIO()) is False)
bad = r.parse([l.replace("af=60000", "af=70000") for l in FIXTURE])[0]
check("sabotage: a split above the deck goes red", r.split_gaps(bad) == [(6, "a", -10000), (6, "af", 10000)])
bad = r.parse([l.replace("bx=60000", "bx=59000") for l in FIXTURE])[0]
check("sabotage: an FX split that does not add up goes red", r.split_gaps(bad) == [(5, "bf", 1000), (6, "bf", 1000)])
bad = r.parse([l.replace("xr=100000", "xr=90000") for l in FIXTURE])[0]
check("sabotage: a mix split that does not add up goes red", r.split_gaps(bad) == [(6, "mix", 10000)])
bad = r.parse([l.replace("at=46000", "at=56000") for l in FIXTURE])[0]
check("sabotage: a modulation split above its whole goes red", r.split_gaps(bad) == [(6, "am", -10000)])
bad = r.parse([l.replace("aq=30000 ao=2000 au=8000 bq", "aq=30000 ao=1000 au=8000 bq") for l in FIXTURE])[0]
check("sabotage: a lanes split short of its whole goes red",
      r.split_gaps(bad) == [(5, "al", 1000), (6, "al", 1000)])
bad = r.parse([l.replace("bqs=20000 bqt=5500", "bqs=21000 bqt=5500") for l in FIXTURE])[0]
check("sabotage: a pitch-lane split above its whole goes red",
      r.split_gaps(bad) == [(5, "bq", -1000), (6, "bq", -1000)])
bad = r.parse([l.replace("ag1=300 ag2=200 agx=200", "ag1=300 ag2=100 agx=200") for l in FIXTURE])[0]
check("sabotage: a GRIT split short of its whole goes red", r.split_gaps(bad) == [(6, "ag", 100)])
check("sabotage: a capture with no window is a failure",
      r.parse(["SHELL_PLAY_LAW cyc_last=1 cyc_max=2 led_last=3 led_max=4 cb_last=5 cb_max=6"])[0] == {})
check("sabotage: a malformed window line is not a window",
      r.parse(["CBP_WIN w=5 cb=900000 cb_max=950000 proc=870000 a=360000 b=380000 mix=150000"])[0] == {})

# --- the committed R4 capture: real lines, no split -------------------------
R4 = os.path.join(HERE, "..", "docs", "hardware", "captures", "reva-play-2026-10-24-r4-profile.txt")
with open(R4, encoding="utf-8", errors="replace") as f:
    w4, b4 = r.parse(f.read().splitlines())
check("the R4 capture's 178 windows parse", len(w4) == 178)
check("the R4 capture carries no split", all("am" not in d for d in w4.values()))
m4 = r.means(w4, 43, 49)
check("the R4 capture's mean covers the fields its image printed",
      m4 is not None and m4[1] == 5 and 939000 < m4[0]["cb"] < 940000 and "am" not in m4[0])
check("window 43 of the R4 capture reads as the record says",
      w4[43]["cb"] == 939268 and w4[43]["a"] == 369133 and w4[43]["b"] == 388728)

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
