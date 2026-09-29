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
CALIBRATION = ("CAL_GND", "CAL_A3V3")   # panel-scan spec §8: the scan reads its span


def load_holes(path=HOLES):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)["holes"]


def _split_regions(pots):
    """Four contiguous x-bands, one per sense pin in SENSE_ORDER.

    Pots sharing an x stay in one band. Every split whose bands fit their
    capacity -- both calibration channels going to the band with the most room
    -- is scored by its fullest band's fill ratio; the lowest wins, ties to
    the split whose cuts sit furthest left. Exhaustive: 63 columns give about
    40 000 splits, well under a second.
    """
    columns = {}
    for p in pots:
        columns.setdefault(p["x_mm"], []).append(p)
    cols = [columns[x] for x in sorted(columns)]
    prefix = [0]
    for c in cols:
        prefix.append(prefix[-1] + len(c))
    caps = [CHANNELS * len(MUXES[s]) for s in SENSE_ORDER]
    best = None
    for cuts in itertools.combinations(range(1, len(cols)), len(caps) - 1):
        bounds = (0,) + cuts + (len(cols),)
        sizes = [prefix[bounds[i + 1]] - prefix[bounds[i]] for i in range(len(caps))]
        free = [c - s for c, s in zip(caps, sizes)]
        cal = max(range(len(caps)), key=lambda i: (free[i], -i))
        used = [s + (len(CALIBRATION) if i == cal else 0) for i, s in enumerate(sizes)]
        if any(u > c for u, c in zip(used, caps)):
            continue
        score = (max(u / c for u, c in zip(used, caps)), cuts)
        if best is None or score < best[0]:
            best = (score, bounds, cal)
    if best is None:
        raise ValueError("%d pots do not fit %d channels in four contiguous bands"
                         % (len(pots), sum(caps)))
    _, bounds, cal = best
    regions = {s: [p for c in cols[bounds[i]:bounds[i + 1]] for p in c]
               for i, s in enumerate(SENSE_ORDER)}
    return regions, SENSE_ORDER[cal]


def _mux_groups(sense, pots):
    """Split a region's pots, left to right, into one group per mux."""
    muxes = MUXES[sense]
    ordered = sorted(pots, key=lambda p: (p["x_mm"], p["y_mm"], p["id"]))
    base, extra = divmod(len(ordered), len(muxes))
    groups, i = {}, 0
    for k, mux in enumerate(muxes):
        size = base + (1 if k < extra else 0)
        groups[mux] = sorted(ordered[i:i + size], key=lambda p: (p["y_mm"], p["x_mm"], p["id"]))
        i += size
    return groups


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
