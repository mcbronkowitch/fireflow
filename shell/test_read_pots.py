"""Guard for read_pots.py: PG2 at its edge, the input gates, and the two
tables' arithmetic, on synthetic metadata and rows built in memory.

Runs as a plain script; pytest is not installed on this machine.
Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""
import sys

import read_pots as rp

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def settle_meta(pot_tail=32768, gates_ok=1):
    """What read_settle.py's format_meta_csv() writes for a pot block,
    reduced to the rows read_pots.py reads. Keys are (scope, pair, key)."""
    m = {("cal", "", "gates_ok"): str(gates_ok),
         ("pot_cfg", "", "pots"): "3"}
    for p in range(12):
        tail = pot_tail if p >= 6 else {1: 32700, 2: 32710, 4: 32650}.get(p, 0)
        m[("ref", str(p), "tail_ref")] = str(tail)
        m[("offset", str(p), "offset_ns")] = "1154" if p in (1, 2, 4, 8, 9) else "991"
        m[("knee", str(p), "d_settle_ns")] = "3000" if p in (8, 9) else "1000"
        m[("knee", str(p), "at_or_below_offset")] = "0"
        m[("knee", str(p), "predicted_ns")] = "3016" if p in (8, 9) else "1552"
    return m


def wait_meta(gates_ok=1, g9_pass=1, pg1_pass=1):
    return {("gates", "", "gates_ok"): str(gates_ok),
            ("host", "", "g9_pass"): str(g9_pass),
            ("host", "", "pg1_pass"): str(pg1_pass),
            ("pot_cfg", "", "pots"): "3"}


def wait_rows(pot_mean=32768, ref_a_mean=32705, l_shift=2):
    """wait.csv rows for arms A (0), B (1) and L (2) on the pots and the
    three dividers, at the W points read_pots.py reads."""
    rows = []
    victims = {(0, 2): pot_mean, (0, 6): pot_mean, (1, 2): pot_mean,
               (0, 8): ref_a_mean, (1, 6): 32650, (0, 9): 32750}
    for arm in (0, 1, 2):
        for (g, ch), base in victims.items():
            for w in (0, 2000, 10000, 20000, 50000):
                if arm == 0:
                    shift = 0 if w == 0 else -(w // 20) if w < 10000 else -800
                elif arm == 1:
                    shift = 0 if w == 0 else -40
                else:
                    shift = 0 if w == 0 else l_shift
                rows.append({"arm": arm, "victim_group": g, "victim_ch": ch,
                             "w_us": w, "n": 64, "mean": base + shift,
                             "shift": shift})
    return rows


# --- A. PG2 at its edge ---
ok, why = rp.verdict(settle_meta(32768), wait_meta(), wait_rows(32768 + 1024))
check("A1 PG2 admits 1024 counts between the images", ok)
ok, why = rp.verdict(settle_meta(32768), wait_meta(), wait_rows(32768 + 1025))
check("A2 PG2 refuses 1025", not ok and any("PG2" in w for w in why))

# --- B. the inputs' own gates ---
check("B1 a failed settle gate refuses",
      not rp.verdict(settle_meta(gates_ok=0), wait_meta(), wait_rows())[0])
check("B2 a failed wait gate refuses",
      not rp.verdict(settle_meta(), wait_meta(gates_ok=0), wait_rows())[0])
check("B3 a failed G9 refuses",
      not rp.verdict(settle_meta(), wait_meta(g9_pass=0), wait_rows())[0])
ok, why = rp.verdict(settle_meta(28179), wait_meta(), wait_rows(28200))
check("B4 PG1 in the settle image refuses, isolated from the wait image",
      not ok and "PG1 failed for RV2 in the settle image" in why
      and not any("wait image" in w for w in why))
check("B5 a healthy pair of inputs passes",
      rp.verdict(settle_meta(), wait_meta(), wait_rows()) == (True, []))


def settle_meta_no_pot_cfg():
    """settle_meta() with its pot-block row removed."""
    m = settle_meta()
    del m[("pot_cfg", "", "pots")]
    return m


def wait_meta_wrong_pot_cfg():
    """wait_meta() with its pot-block row set to a wrong pot count."""
    m = wait_meta()
    m[("pot_cfg", "", "pots")] = "0"
    return m


ok, why = rp.verdict(settle_meta_no_pot_cfg(), wait_meta(), wait_rows())
check("B6 a settle image missing its pot-block row refuses",
      not ok and any("not a pot-round block" in w for w in why))
ok, why = rp.verdict(settle_meta(), wait_meta_wrong_pot_cfg(), wait_rows())
check("B7 a wait image with the wrong pot count refuses",
      not ok and any("not a pot-round block" in w for w in why))

# --- C. the arithmetic ---
check("C1 settle readings are the mean of each pot's two tail_refs",
      rp.settle_readings(settle_meta(33000)) == [("RV2", 33000.0),
                                                ("RV4", 33000.0),
                                                ("RV6", 33000.0)])
check("C2 wait readings are arm L at W=0",
      rp.wait_readings(wait_rows(33100)) == [("RV2", 33100), ("RV4", 33100),
                                             ("RV6", 33100)])
deltas = dict((n, d) for n, d in rp.ref_deltas(settle_meta(), wait_rows()))
check("C3 REF_A's instrument delta is wait minus the P1/P2 mean",
      deltas["REF_A"] == 32705 - 32705.0)
check("C4 REF_C's instrument delta is wait minus P4",
      deltas["REF_C"] == 32650 - 32650.0)
srows = {r["pair"]: r for r in rp.settle_rows(settle_meta())}
check("C5 true settle is knee plus offset",
      srows[8]["settle_ns"] == 3000 + 1154 and srows[6]["settle_ns"] == 1000 + 991)
check("C6 the ratio is against the prediction",
      abs(srows[8]["ratio"] - (4154 / 3016)) < 1e-9)
summary = dict((v["name"], v) for v in rp.wait_summary(wait_rows(l_shift=-4)))
check("C7 arm L's largest |shift| is reported per victim",
      summary["RV4"]["l_max_abs"] == 4)
check("C8 arm A's saturation is the mean of 10/20/50 ms",
      summary["REF_A"]["a_sat"] == -800)

# --- D. the settle table's did-not-settle tag ---
check("D1 a pair settled at or below its own offset is tagged that way",
      rp._settle_tag({"settle_ns": None, "at_or_below_offset": 1,
                      "offset_ns": 991}) == " (at or below offset 991 ns)")
check("D2 a pair that never settled within the grid is tagged that way",
      rp._settle_tag({"settle_ns": None, "at_or_below_offset": 0,
                      "offset_ns": 991}) == " (did not settle within the grid)")
check("D3 missing metadata (at_or_below_offset None, not 0) stays untagged",
      rp._settle_tag({"settle_ns": None, "at_or_below_offset": None,
                      "offset_ns": None}) == "")
check("D4 a pair that settled carries no tag",
      rp._settle_tag({"settle_ns": 1234, "at_or_below_offset": 0,
                      "offset_ns": 991}) == "")

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_pots guard: ok")
