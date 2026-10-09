#!/usr/bin/env python3
"""Pot -> (sense pin, mux, channel) and LED -> index, computed from the panel.

P3 spec §5. Input: host/vcv/res/FireflowHW-holes.json, P1's hole list and
the single source of positions. Output: hardware/reva/panel-map.json, read by
the schematic blocks (plan 2) and the firmware table (P6). A panel change in
the grip test is one re-run of this script, not a redesign.

    python hardware/reva/assign.py          # rewrite panel-map.json
"""
import itertools
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HOLES = os.path.join(ROOT, "host", "vcv", "res", "FireflowHW-holes.json")
OUT = os.path.join(HERE, "panel-map.json")

# P2 §3: SENSE_0/1 carry three muxes, SENSE_2/3 two. Mux n is enable ENn.
MUXES = {"SENSE_0": (0, 1, 2), "SENSE_1": (3, 4, 5),
         "SENSE_2": (6, 7), "SENSE_3": (8, 9)}
# Left-to-right order of the four regions -- the one knob P4 may turn.
SENSE_ORDER = ("SENSE_0", "SENSE_2", "SENSE_3", "SENSE_1")
CHANNELS = 8
CALIBRATION = ("CAL_GND", "CAL_3V3")   # panel-scan spec §8: the scan reads its span


def load_holes(path=HOLES):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)["holes"]


def _split_regions(pots):
    """Four contiguous bands, one per sense pin in SENSE_ORDER, in
    (x, y, id) order.

    A cut may fall inside a column (spec 2026-10-07 §8): a deck carries 29
    pots and its first five columns hold 25 against a 24-channel band, so
    whole columns no longer fit. A split is taken only when no split-free
    answer exists: every split whose bands fit their capacity -- both
    calibration channels going to the band with the most room -- is scored
    (columns split, fullest band's fill ratio, cuts); the lowest wins.
    Exhaustive: 73 pots give C(72, 3) = 59 640 splits, well under a second.
    """
    order = sorted(pots, key=lambda p: (p["x_mm"], p["y_mm"], p["id"]))
    caps = [CHANNELS * len(MUXES[s]) for s in SENSE_ORDER]
    best = None
    for cuts in itertools.combinations(range(1, len(order)), len(caps) - 1):
        bounds = (0,) + cuts + (len(order),)
        sizes = [bounds[i + 1] - bounds[i] for i in range(len(caps))]
        free = [c - s for c, s in zip(caps, sizes)]
        cal = max(range(len(caps)), key=lambda i: (free[i], -i))
        used = [s + (len(CALIBRATION) if i == cal else 0) for i, s in enumerate(sizes)]
        if any(u > c for u, c in zip(used, caps)):
            continue
        splits = sum(order[c - 1]["x_mm"] == order[c]["x_mm"] for c in cuts)
        score = (splits, max(u / c for u, c in zip(used, caps)), cuts)
        if best is None or score < best[0]:
            best = (score, bounds, cal)
    if best is None:
        raise ValueError("%d pots do not fit %d channels in four contiguous bands"
                         % (len(pots), sum(caps)))
    _, bounds, cal = best
    regions = {s: order[bounds[i]:bounds[i + 1]] for i, s in enumerate(SENSE_ORDER)}
    return regions, SENSE_ORDER[cal]


def objective(groups):
    """Grouping quality, smaller is better: (worst pot-to-centroid distance
    over all groups, sum of squared pot-to-centroid distances). P4 places each
    mux at its group's centre, so both are wire-length proxies. Rounded to
    1e-9 so float noise cannot flip a tie. groups: list of lists of hole dicts.
    """
    worst = total = 0.0
    for g in groups:
        if not g:
            continue
        cx = sum(p["x_mm"] for p in g) / len(g)
        cy = sum(p["y_mm"] for p in g) / len(g)
        for p in g:
            d2 = (p["x_mm"] - cx) ** 2 + (p["y_mm"] - cy) ** 2
            worst = max(worst, math.sqrt(d2))
            total += d2
    return (round(worst, 9), round(total, 9))


def _mux_groups(sense, pots):
    """Split a region's pots into one proximity group per mux, at most
    CHANNELS each (P3 spec §5).

    Start from horizontal bands (pots sorted by y, cut into near-equal
    chunks), then improve by local search: single moves into a group with
    room and pairwise swaps, in a fixed order, each accepted only if it
    strictly improves objective(); full passes until one changes nothing.
    Deterministic. Returns {mux: pots in channel order (y, x, id)}.
    """
    muxes = MUXES[sense]
    ordered = sorted(pots, key=lambda p: (p["y_mm"], p["x_mm"], p["id"]))
    base, extra = divmod(len(ordered), len(muxes))
    groups, i = [], 0
    for k in range(len(muxes)):
        size = base + (1 if k < extra else 0)
        groups.append(ordered[i:i + size])
        i += size
    if any(len(g) > CHANNELS for g in groups):
        raise ValueError("%d pots do not fit %d muxes of %d channels"
                         % (len(ordered), len(muxes), CHANNELS))
    best = objective(groups)
    n = len(groups)
    changed = True
    while changed:
        changed = False
        for gi in range(n):
            for p in sorted(groups[gi], key=lambda q: q["id"]):
                if p not in groups[gi]:
                    continue
                for gj in range(n):
                    if gj == gi or len(groups[gj]) >= CHANNELS:
                        continue
                    groups[gi].remove(p)
                    groups[gj].append(p)
                    score = objective(groups)
                    if score < best:
                        best, changed = score, True
                        break
                    groups[gj].remove(p)
                    groups[gi].append(p)
        for gi in range(n):
            for gj in range(gi + 1, n):
                for a in sorted(groups[gi], key=lambda q: q["id"]):
                    for b in sorted(groups[gj], key=lambda q: q["id"]):
                        if a not in groups[gi] or b not in groups[gj]:
                            continue
                        groups[gi].remove(a)
                        groups[gj].remove(b)
                        groups[gi].append(b)
                        groups[gj].append(a)
                        score = objective(groups)
                        if score < best:
                            best, changed = score, True
                            break
                        groups[gi].remove(b)
                        groups[gj].remove(a)
                        groups[gi].append(a)
                        groups[gj].append(b)
    return {mux: sorted(g, key=lambda p: (p["y_mm"], p["x_mm"], p["id"]))
            for mux, g in zip(muxes, groups)}


def assign(holes):
    pots = [h for h in holes if h["kind"] == "pot"]
    leds = [h for h in holes if h["kind"] == "led"]
    for kind, items in (("pot", pots), ("led", leds)):
        ids = [h["id"] for h in items]
        if len(ids) != len(set(ids)):
            raise ValueError("duplicate %s ids: %s"
                             % (kind, sorted({i for i in ids if ids.count(i) > 1})))
    regions, cal_sense = _split_regions(pots)
    pot_rows, cal_rows, spare_rows = [], [], []
    for sense in SENSE_ORDER:
        groups = _mux_groups(sense, regions[sense])
        fill = {m: [("pot", p) for p in groups[m]] for m in MUXES[sense]}
        if sense == cal_sense:
            for cal in CALIBRATION:
                m = min(MUXES[sense], key=lambda m: (len(fill[m]), m))
                fill[m].append(("cal", cal))
        for m in MUXES[sense]:
            if len(fill[m]) > CHANNELS:
                raise ValueError("mux %d holds %d inputs" % (m, len(fill[m])))
            for ch in range(CHANNELS):
                if ch >= len(fill[m]):
                    spare_rows.append({"sense": sense, "mux": m, "channel": ch})
                    continue
                kind, item = fill[m][ch]
                if kind == "cal":
                    cal_rows.append({"id": item, "sense": sense, "mux": m, "channel": ch})
                else:
                    pot_rows.append({"id": item["id"], "ids": item.get("ids", [item["id"]]),
                                     "sense": sense, "mux": m, "channel": ch,
                                     "x_mm": item["x_mm"], "y_mm": item["y_mm"]})
    led_rows = [{"id": h["id"], "index": i, "x_mm": h["x_mm"], "y_mm": h["y_mm"]}
                for i, h in enumerate(sorted(leds, key=lambda h: (h["x_mm"], h["y_mm"], h["id"])))]
    return {"generated_by": "hardware/reva/assign.py",
            "source": "host/vcv/res/FireflowHW-holes.json",
            "sense_order": list(SENSE_ORDER),
            "muxes": {s: list(MUXES[s]) for s in SENSE_ORDER},
            "pots": pot_rows, "calibration": cal_rows, "spare": spare_rows,
            "leds": led_rows}


def report(m):
    for sense in m["sense_order"]:
        rows = [p for p in m["pots"] if p["sense"] == sense]
        worst = 0.0
        for mux in m["muxes"][sense]:
            group = [p for p in rows if p["mux"] == mux]
            if not group:
                continue
            cx = sum(p["x_mm"] for p in group) / len(group)
            cy = sum(p["y_mm"] for p in group) / len(group)
            worst = max(worst, max(math.hypot(p["x_mm"] - cx, p["y_mm"] - cy) for p in group))
        xs = [p["x_mm"] for p in rows]
        print("%s: %2d pots, x %.1f..%.1f mm, farthest pot %.1f mm from its mux "
              "group's centre" % (sense, len(rows), min(xs), max(xs), worst))
    print("calibration on %s; %d spare channels; %d LEDs"
          % (", ".join("mux %d ch %d" % (c["mux"], c["channel"]) for c in m["calibration"]),
             len(m["spare"]), len(m["leds"])))


def main():
    m = assign(load_holes())
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps(m, indent=1, sort_keys=True) + "\n")
    report(m)
    print("wrote %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
