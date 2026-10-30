"""Host test for section_means.py (plan 2026-10-30-cpu-engine-kernels Task 1).
Hand-written serial lines with the host-time prefix remote_run.py writes. Plain
script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import section_means as sm

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)


def test_parse_merges_lines_of_one_window():
    lines = ["12.345 CBP_WIN w=3 cb=900000 cb_max=910000 proc=1 a=2 b=3 mix=4 law=5",
             "12.346 CBP_WIN2 w=3 am=1 ae=150000 af=2 bm=3 be=4 bf=5"]
    wins = sm.parse_windows(lines)
    return (len(wins) == 1 and wins[0]["WIN.cb"] == 900000
            and wins[0]["WIN2.ae"] == 150000 and "WIN.w" not in wins[0]
            and "WIN2.w" not in wins[0])

def test_deck_lines_are_keyed_by_deck():
    lines = ["12.345 CBP_WIN10 w=3 d=0 m0=11 n0=1 m1=12 n1=2",
             "12.346 CBP_WIN10 w=3 d=1 m0=21 n0=3 m1=22 n1=4"]
    wins = sm.parse_windows(lines)
    return (len(wins) == 1 and wins[0]["WIN10.d0.m0"] == 11
            and wins[0]["WIN10.d1.m0"] == 21 and wins[0]["WIN10.d1.n1"] == 4
            and not any(k.endswith(".d") or k.endswith(".w") for k in wins[0]))

def test_run_means_skips_settle():
    lines = ["1.0 CBP_WIN w=1 cb=1", "2.0 CBP_WIN w=2 cb=10", "3.0 CBP_WIN w=3 cb=20"]
    return sm.run_means(lines, skip=1)["WIN.cb"] == 15.0

def test_compare_reports_spread():
    base = [{"WIN2.ae": 100.0}, {"WIN2.ae": 110.0}]
    cand = [{"WIN2.ae": 90.0}]
    return sm.compare(base, cand)["WIN2.ae"] == (105.0, 90.0, -15.0, 10.0)

def test_ignores_non_window_lines():
    clean = ["1.0 CBP_WIN w=1 cb=5", "2.0 CBP_WIN w=2 cb=7"]
    noisy = [clean[0], "1.1 SHELL_PLAY_LAW cyc_last=1 cb_last=9 w=77", "garbage",
             "", "1.2 CBP_REC5 blk=1 d=0 wp=3", "1.3 CBP_WINDOW w=9 cb=1",
             "#reconnect", clean[1]]
    return sm.parse_windows(noisy) == sm.parse_windows(clean) and len(sm.parse_windows(noisy)) == 2

for name, fn in list(globals().items()):
    if name.startswith("test_") and callable(fn):
        check(name, fn())

print("\n%d failed" % len(FAILS) if FAILS else "\nall ok")
sys.exit(1 if FAILS else 0)
