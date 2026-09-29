#!/usr/bin/env python3
"""Rev A's pot and LED assignment (P3 spec §5) holds its invariants, and the
committed panel-map.json is what assign.py computes from today's hole list.

    python hardware/reva/test_assign.py      # exit code is the verdict
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import assign as A  # noqa: E402


def proximity_violations(m):
    """Each region's mux groups are locally optimal: no single move into a
    group with room and no pairwise swap strictly improves A.objective."""
    bad = []
    for sense in m["sense_order"]:
        groups = [[{"id": p["id"], "x_mm": p["x_mm"], "y_mm": p["y_mm"]}
                   for p in m["pots"] if p["sense"] == sense and p["mux"] == mux]
                  for mux in m["muxes"][sense]]
        base = A.objective(groups)
        for gi, g in enumerate(groups):
            if len(g) > A.CHANNELS:
                bad.append("%s group %d holds %d pots" % (sense, gi, len(g)))
            for p in g:
                for gj, h in enumerate(groups):
                    if gj == gi or len(h) >= A.CHANNELS:
                        continue
                    trial = [list(x) for x in groups]
                    trial[gi].remove(p)
                    trial[gj].append(p)
                    if A.objective(trial) < base:
                        bad.append("%s: moving %s to group %d improves the grouping"
                                   % (sense, p["id"], gj))
            for gj in range(gi + 1, len(groups)):
                for p in g:
                    for q in groups[gj]:
                        trial = [list(x) for x in groups]
                        trial[gi].remove(p)
                        trial[gj].remove(q)
                        trial[gi].append(q)
                        trial[gj].append(p)
                        if A.objective(trial) < base:
                            bad.append("%s: swapping %s and %s improves the grouping"
                                       % (sense, p["id"], q["id"]))
    return bad


def invariants(m, holes):
    bad = []
    pot_holes = {h["id"] for h in holes if h["kind"] == "pot"}
    led_holes = {h["id"] for h in holes if h["kind"] == "led"}
    ids = [p["id"] for p in m["pots"]]
    if len(ids) != len(set(ids)):
        bad.append("a pot appears twice")
    if set(ids) != pot_holes:
        bad.append("pots differ from the hole list: missing %s, extra %s"
                   % (sorted(pot_holes - set(ids)), sorted(set(ids) - pot_holes)))
    slots = [(r["mux"], r["channel"]) for r in m["pots"] + m["calibration"] + m["spare"]]
    if len(slots) != len(set(slots)):
        bad.append("a (mux, channel) slot is used twice")
    if len(slots) != A.CHANNELS * sum(len(v) for v in A.MUXES.values()):
        bad.append("%d slots accounted for, want 80" % len(slots))
    for r in m["pots"] + m["calibration"] + m["spare"]:
        if r["mux"] not in A.MUXES[r["sense"]] or not 0 <= r["channel"] < A.CHANNELS:
            bad.append("slot %s is not on its sense pin's muxes" % (r,))
    if sorted(c["id"] for c in m["calibration"]) != sorted(A.CALIBRATION):
        bad.append("calibration channels: %s" % m["calibration"])
    bands = [[p["x_mm"] for p in m["pots"] if p["sense"] == s] for s in A.SENSE_ORDER]
    for left, right in zip(bands, bands[1:]):
        if left and right and max(left) >= min(right):
            bad.append("regions overlap in x: %.2f >= %.2f" % (max(left), min(right)))
    bad += proximity_violations(m)
    leds = m["leds"]
    if sorted(l["id"] for l in leds) != sorted(led_holes):
        bad.append("LEDs differ from the hole list")
    if [l["index"] for l in leds] != list(range(len(leds))):
        bad.append("LED indices are not 0..%d in order" % (len(leds) - 1))
    if [l["x_mm"] for l in leds] != sorted(l["x_mm"] for l in leds):
        bad.append("LED indices do not run left to right")
    return bad


def main():
    holes = A.load_holes()
    failures = []
    m = A.assign(holes)
    failures += invariants(m, holes)
    if A.assign(holes) != m:
        failures.append("assign() is not deterministic")
    # Byte for byte, except git's own autocrlf: a Windows checkout turns the
    # committed LF into CRLF, which is not a content difference.
    with open(A.OUT, encoding="utf-8", newline="") as fh:
        committed = fh.read().replace("\r\n", "\n")
    if committed != json.dumps(m, indent=1, sort_keys=True) + "\n":
        failures.append("panel-map.json is stale -- run python hardware/reva/assign.py")
    failures += ["committed map: " + f for f in proximity_violations(json.loads(committed))]
    # The invariants must be able to fail, and bad input must be refused.
    broken = json.loads(json.dumps(m))
    broken["pots"][1]["mux"], broken["pots"][1]["channel"] = \
        broken["pots"][0]["mux"], broken["pots"][0]["channel"]
    if not invariants(broken, holes):
        failures.append("invariants() missed a doubly used slot")
    pots = [h for h in holes if h["kind"] == "pot"]
    for label, bad_holes in (
            ("a duplicated pot id", holes + [dict(pots[0])]),
            ("81 pots", holes + [dict(pots[0], id="EXTRA_%d" % i, ids=["EXTRA_%d" % i])
                                 for i in range(11)])):
        try:
            A.assign(bad_holes)
            failures.append("assign() accepted %s" % label)
        except ValueError:
            pass
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d pots on 10 muxes, %d calibration, %d spare, %d LEDs"
          % (len(m["pots"]), len(m["calibration"]), len(m["spare"]), len(m["leds"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
