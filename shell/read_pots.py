"""Joins round four's two readers' files into the round's answer.

    python read_pots.py settle.csv.meta.csv wait.csv

reads `wait.csv.meta.csv` beside it. It computes nothing the two
readers already compute; it joins and compares (spec
2026-09-28-coupon-pot-round-design.md section 8):

  - per pot: its reading in each image, x and derived R_src; PG1 in both;
    PG2 between them, beside REF_A's and REF_C's own difference between the
    two images -- the instrument's share of any PG2 difference, which has
    never been measured on its own (spec section 7);
  - the settle table: true settle (knee + offset) per pot pair, the model's
    prediction and the ratio, with REF_A's P1/P2 beside RV4's P8/P9;
  - the wait table: per victim, arm A's shift at 2 ms and at saturation
    (mean of 10/20/50 ms), arm B's at saturation, arm L's largest |shift|.

Exit code: 1 when either input's own gates failed, G9 failed, PG1 failed in
either image, or PG2 failed. The tables are printed regardless -- a refused
run is read for what failed, not hidden.
"""
import csv
import sys

import pot_round

REF_A = (0, 8)
REF_C = (1, 6)
REF_B = (0, 9)
ARM_WAIT, ARM_DISCARD, ARM_LONG = 0, 1, 2
SAT_W_US = (10000, 20000, 50000)


def load_meta(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return {(r["scope"], r["pair"], r["key"]): r["value"]
                for r in csv.DictReader(fh)}


def load_rows(path):
    with open(path, encoding="utf-8", newline="") as fh:
        rows = []
        for r in csv.DictReader(fh):
            rows.append({"arm": int(r["arm"]),
                         "victim_group": int(r["victim_group"]),
                         "victim_ch": int(r["victim_ch"]),
                         "w_us": int(r["w_us"]), "n": int(r["n"]),
                         "mean": int(r["mean"]),
                         "shift": None if r["shift"] == "" else int(r["shift"])})
        return rows


def _int(meta, scope, pair, key):
    v = meta.get((scope, str(pair), key))
    return None if v is None or v == "" else int(v)


def _tail(meta, pair):
    return _int(meta, "ref", pair, "tail_ref")


def settle_readings(meta):
    out = []
    for i, pot in enumerate(pot_round.POTS):
        a = _tail(meta, pot_round.SETTLE_POT_PAIR0 + 2 * i)
        b = _tail(meta, pot_round.SETTLE_POT_PAIR0 + 2 * i + 1)
        out.append((pot[0], None if a is None or b is None else (a + b) / 2.0))
    return out


def _row(rows, arm, victim, w_us):
    for r in rows:
        if (r["arm"] == arm and (r["victim_group"], r["victim_ch"]) == victim
                and r["w_us"] == w_us):
            return r
    return None


def wait_readings(rows):
    out = []
    for name, g, ch, _, _, _ in pot_round.POTS:
        r = _row(rows, ARM_LONG, (g, ch), 0)
        out.append((name, r["mean"] if r is not None and r["n"] > 0 else None))
    return out


def ref_deltas(meta, rows):
    """[(name, wait - settle)] for REF_A and REF_C: how far the two images'
    conditions alone move a divider's reading."""
    out = []
    a1, a2 = _tail(meta, 1), _tail(meta, 2)
    settle = {"REF_A": None if a1 is None or a2 is None else (a1 + a2) / 2.0,
              "REF_C": None if _tail(meta, 4) is None else float(_tail(meta, 4))}
    for name, victim in (("REF_A", REF_A), ("REF_C", REF_C)):
        r = _row(rows, ARM_LONG, victim, 0)
        s = settle[name]
        out.append((name, None if r is None or s is None else r["mean"] - s))
    return out


def settle_rows(meta):
    """One dict per pair of interest: the pot pairs P6-P11 and REF_A's P1/P2."""
    out = []
    for pair in (1, 2) + tuple(range(6, 12)):
        d = _int(meta, "knee", pair, "d_settle_ns")
        off = _int(meta, "offset", pair, "offset_ns")
        pred = _int(meta, "knee", pair, "predicted_ns")
        below = _int(meta, "knee", pair, "at_or_below_offset")
        settle = d + off if d is not None and d >= 0 and off is not None else None
        out.append({"pair": pair, "settle_ns": settle, "predicted_ns": pred,
                    "at_or_below_offset": below, "offset_ns": off,
                    "ratio": None if settle is None or not pred else settle / pred})
    return out


def wait_summary(rows):
    names = [(p[0], (p[1], p[2])) for p in pot_round.POTS]
    names += [("REF_A", REF_A), ("REF_C", REF_C), ("REF_B", REF_B)]
    out = []
    for name, victim in names:
        def shift(arm, w):
            r = _row(rows, arm, victim, w)
            return None if r is None else r["shift"]

        def sat(arm):
            vals = [shift(arm, w) for w in SAT_W_US]
            return None if None in vals else sum(vals) / len(vals)

        l_vals = [abs(r["shift"]) for r in rows
                  if r["arm"] == ARM_LONG and r["shift"] is not None
                  and (r["victim_group"], r["victim_ch"]) == victim]
        out.append({"name": name, "a_2ms": shift(ARM_WAIT, 2000),
                    "a_sat": sat(ARM_WAIT), "b_sat": sat(ARM_DISCARD),
                    "l_max_abs": max(l_vals) if l_vals else None})
    return out


def verdict(settle_meta, wait_meta, rows):
    reasons = []
    if settle_meta.get(("cal", "", "gates_ok")) != "1":
        reasons.append("the settle image's gates failed")
    if wait_meta.get(("gates", "", "gates_ok")) != "1":
        reasons.append("the wait image's gates failed")
    if wait_meta.get(("host", "", "g9_pass")) != "1":
        reasons.append("G9 failed in the wait image")
    if settle_meta.get(("pot_cfg", "", "pots")) != str(len(pot_round.POTS)) \
            or wait_meta.get(("pot_cfg", "", "pots")) != str(len(pot_round.POTS)):
        reasons.append("an input is not a pot-round block")
    s_read = dict(settle_readings(settle_meta))
    w_read = dict(wait_readings(rows))
    for name, _, _, _, _, _ in pot_round.POTS:
        if not pot_round.pg1(s_read.get(name)):
            reasons.append("PG1 failed for %s in the settle image" % name)
        if not pot_round.pg1(w_read.get(name)):
            reasons.append("PG1 failed for %s in the wait image" % name)
        if not pot_round.pg2(s_read.get(name), w_read.get(name)):
            reasons.append("PG2 failed for %s (moved between the images?)" % name)
    return (not reasons), reasons


def _fmt(v, spec="%s"):
    return "-" if v is None else spec % v


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: read_pots.py settle.csv.meta.csv wait.csv")
    settle_meta = load_meta(argv[1])
    rows = load_rows(argv[2])
    wait_meta = load_meta(argv[2] + ".meta.csv")
    err = sys.stderr

    print("pots -- reading, x, derived R_src (R_track nominal):", file=err)
    s_read, w_read = dict(settle_readings(settle_meta)), dict(wait_readings(rows))
    for name, _, _, _, _, r_track in pot_round.POTS:
        for label, reading in (("settle", s_read.get(name)),
                               ("wait", w_read.get(name))):
            print("  %s %-6s reading=%s x=%s r_src=%s PG1 %s"
                  % (name, label, _fmt(reading, "%.1f"),
                     _fmt(None if reading is None else
                          reading / pot_round.FULL_SCALE, "%.4f"),
                     _fmt(None if reading is None else
                          pot_round.r_src(reading, r_track), "%.0f"),
                     "PASS" if pot_round.pg1(reading) else "FAIL"), file=err)
        a, b = s_read.get(name), w_read.get(name)
        print("  %s PG2 |wait - settle| = %s (bound %d) %s"
              % (name, _fmt(None if a is None or b is None else abs(b - a), "%.1f"),
                 pot_round.PG2_MAX,
                 "PASS" if pot_round.pg2(a, b) else "FAIL"), file=err)
    for name, d in ref_deltas(settle_meta, rows):
        print("  %s wait - settle = %s (the instrument's own share)"
              % (name, _fmt(d, "%.1f")), file=err)

    print("\nsettle -- true settle = knee + offset:", file=err)
    for r in settle_rows(settle_meta):
        tag = " (at or below offset %s ns)" % r["offset_ns"] \
            if r["settle_ns"] is None and r["at_or_below_offset"] else ""
        print("  P%-2d settle_ns=%s predicted_ns=%s ratio=%s%s"
              % (r["pair"], _fmt(r["settle_ns"]), _fmt(r["predicted_ns"]),
                 _fmt(r["ratio"], "%.2f"), tag), file=err)

    print("\nwait -- shifts in counts:", file=err)
    print("  %-6s %8s %8s %8s %8s" % ("", "A 2ms", "A sat", "B sat", "L max"),
          file=err)
    for v in wait_summary(rows):
        print("  %-6s %8s %8s %8s %8s"
              % (v["name"], _fmt(v["a_2ms"]), _fmt(v["a_sat"], "%.1f"),
                 _fmt(v["b_sat"], "%.1f"), _fmt(v["l_max_abs"])), file=err)

    ok, reasons = verdict(settle_meta, wait_meta, rows)
    if not ok:
        print("\nREFUSED: %s" % "; ".join(reasons), file=err)
        return 1
    print("\nall gates, PG1 and PG2 pass", file=err)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
