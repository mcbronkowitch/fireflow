#!/usr/bin/env python3
"""Draw the coupon's assembly plan -- the sheet you stuff the board from.

The fabricated board's own silkscreen cannot be read. 0805 parts sit on a 2 mm
pitch and their reference designators need about 5 mm, so the printing overlaps
itself; in the analog corner the names are one grey smear. `proof/` already has
board renders, and they have the same problem for the same reason. You cannot
solder a board you cannot tell apart, so the plan is drawn instead of printed.

Two sheets come out of one pass over the board file:

  proof/coupon-assembly.svg   the working sheet -- whole board, the analog
                              corner enlarged, and a stuffing list with one row
                              per reel. All 78 parts named.
  proof/coupon-overview.svg   the same geometry at a width that survives a
                              narrow column, with eight numbered zones instead
                              of 78 labels. This is the one the journal prints.

**This script reads `coupon.kicad_pcb` and nothing else, so unlike `build.py`,
`build_pcb.py` and `review.py` it runs under the system interpreter** -- no
`pcbnew`, no symbol libraries, no KiCad install. Placement, rotation, courtyard
and pad geometry come from the footprints; the part VALUES come from each
footprint's own `Value` property, which is what the BOM in `proof/review.md`
also reports. Nothing here is transcribed by hand, so the plan cannot drift
away from the board the way a hand-kept table would: re-run it after any
placement change and the drawing follows.

Every label is placed by search. A candidate slot is rejected if it touches a
part body, another label, a leader already drawn, or the board outline; the
nearest surviving slot wins, and a label standing off from its part gets a
leader that may not cross another part unless nothing legal is left. The text
metrics that search runs on (`ADVANCE`, `ASCENDER`, `DESCENDER` below) were
measured in a browser against the rendered monospace stack rather than guessed,
because a label box that is wrong by a descender puts leaders under glyphs.

Usage:

    python scripts/assembly_plan.py
    python scripts/assembly_plan.py --out-dir DIR --prefix NAME

The second form is how the website's copies are refreshed, since it wants the
files under a different name:

    python scripts/assembly_plan.py \\
        --out-dir ../../../FireFlow_Website/public/media/site \\
        --prefix fireflow-hw-coupon
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ksexp

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.normpath(os.path.join(HERE, "..", "coupon.kicad_pcb"))
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "proof"))

# The journal's palette, so a drawing dropped into the log does not arrive as a
# foreign object: paper, ink, and the three accents the site already uses.
INK, MUTED, RULE, PAPER, BOARD = "#171713", "#656056", "#d7cdbb", "#f7f4ec", "#eae4d6"
ALERT = "#b03a2e"

# Measured against IBM Plex Mono / Cascadia Mono / Consolas at several sizes:
# the advance is a constant fraction of the em in any of them, and a glyph box
# reaches 0.80 em above the baseline and 0.22 em below it. The placer's whole
# correctness rests on these three numbers.
ADVANCE, ASCENDER, DESCENDER = 0.602, 0.80, 0.22

# fill, stroke. Keyed by the class classify() puts a footprint in, not by the
# raw Value string: "10k" is both a resistor and a pot, and they are not the
# same thing to a hand holding a reel.
STYLE = {
    "0R":      ("#ccd4da", "#5a6a75"),
    "1k":      ("#f2ceae", "#b96532"),
    "10k":     ("#c0ded4", "#1d6f5f"),
    "100n":    ("#cbe3f0", "#0f6e99"),
    "10u":     ("#9dc2da", "#0b5273"),
    "dnp":     (PAPER,     ALERT),
    "led":     ("#bde294", "#4a7c2b"),
    "probe":   ("#ecd07a", "#8a6d18"),
    "link":    ("#e8dcbd", "#8a7430"),
    "pot10k":  ("#ddd2be", "#6b5f44"),
    "pot20k":  ("#cfc3ab", "#6b5f44"),
    "chip":    ("#3a3a34", INK),
    "module":  ("#e4ded1", "#4a463c"),
    "conn":    ("#ded7c9", "#4a463c"),
}

# Order and wording of the stuffing list, and of the overview's colour key.
CLASS_ORDER = ["0R", "1k", "10k", "100n", "10u", "led", "dnp", "probe", "link",
               "pot10k", "pot20k", "chip", "conn", "module"]
CLASS_NAME = {
    "0R": "0 R", "1k": "1 k", "10k": "10 k", "100n": "100 n", "10u": "10 &#181;F 25 V",
    "led": "green LED", "dnp": "DNP &#8212; fit nothing", "probe": "test point",
    "link": "solder jumper", "pot10k": "10 k pot", "pot20k": "20 k pot",
    "chip": "SOIC chip", "conn": "connector / switch", "module": "the module",
}

# Parts with room to carry their own name inside the outline.
BIG = {"U_SM", "U_SR1", "U_SR2", "U_IN1", "U_MUX16", "U_MUX8",
       "J_PWR", "J_AUDIO", "SW1", "RV1", "RV2", "RV3", "RV4", "RV5", "RV6", "RV7"}


def classify(value, footprint):
    """Which reel a footprint comes off, from its Value and its land pattern."""
    family = footprint.split(":")[-1]
    if "Potentiometer" in family:
        return "pot20k" if value.startswith("20") else "pot10k"
    if "SOIC" in family:
        return "chip"
    if "DAISY" in family.upper():
        return "module"
    if "SolderJumper" in family:
        return "link"
    if "TestPoint" in family:
        return "probe"
    if "LED" in family:
        return "led"
    if value == "DNP":
        return "dnp"
    if value.startswith("10u"):
        return "10u"
    if value in ("0R", "1k", "10k", "100n"):
        return value
    return "conn"


def read_board(path):
    """Every footprint's reference, value, courtyard box and pads, in board mm."""
    root = ksexp.parse_file(path)
    parts = []

    for fp in ksexp.children(root, "footprint"):
        lib = fp[1]
        at = ksexp.child(fp, "at")
        x, y = float(at[1]), float(at[2])
        rot = float(at[3]) if len(at) > 3 else 0.0

        ref = value = None
        for prop in ksexp.children(fp, "property"):
            if prop[1] == "Reference":
                ref = prop[2]
            elif prop[1] == "Value":
                value = prop[2]
        if not ref or ref.startswith("#"):
            continue

        # Courtyard is the outline the board actually reserves for the part.
        xs, ys = [], []
        for g in fp:
            if not isinstance(g, list):
                continue
            layers = ksexp.children(g, "layer")
            if not layers or "CrtYd" not in layers[0][1]:
                continue
            if g[0] in ("fp_line", "fp_rect"):
                s, e = ksexp.child(g, "start"), ksexp.child(g, "end")
                xs += [float(s[1]), float(e[1])]
                ys += [float(s[2]), float(e[2])]
            elif g[0] == "fp_poly":
                for pt in ksexp.child(g, "pts")[1:]:
                    xs.append(float(pt[1]))
                    ys.append(float(pt[2]))
            elif g[0] == "fp_circle":
                c, e = ksexp.child(g, "center"), ksexp.child(g, "end")
                r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                xs += [float(c[1]) - r, float(c[1]) + r]
                ys += [float(c[2]) - r, float(c[2]) + r]

        pads = []
        for pad in ksexp.children(fp, "pad"):
            pat = ksexp.child(pad, "at")
            nets = ksexp.children(pad, "net")
            size = ksexp.children(pad, "size")
            pads.append({
                "n": pad[1], "lx": float(pat[1]), "ly": float(pat[2]),
                # (net "NAME") in this file's format; (net N "NAME") elsewhere.
                "net": (nets[0][2] if len(nets[0]) > 2 else nets[0][1]) if nets else None,
                "w": float(size[0][1]) if size else 0.5,
                "h": float(size[0][2]) if size else 0.5,
            })

        if not xs:                      # no courtyard: fall back to the pads
            for pd in pads:
                xs += [pd["lx"] - pd["w"] / 2, pd["lx"] + pd["w"] / 2]
                ys += [pd["ly"] - pd["h"] / 2, pd["ly"] + pd["h"] / 2]
        if not xs:
            continue

        # KiCad rotates counter-clockwise while y grows downward.
        ang = math.radians(-rot)
        ca, sa = math.cos(ang), math.sin(ang)

        bx, by = [], []
        for lx in (min(xs), max(xs)):
            for ly in (min(ys), max(ys)):
                bx.append(x + lx * ca - ly * sa)
                by.append(y + lx * sa + ly * ca)
        for pd in pads:
            pd["x"] = x + pd["lx"] * ca - pd["ly"] * sa
            pd["y"] = y + pd["lx"] * sa + pd["ly"] * ca

        parts.append({
            "ref": ref, "value": value or "", "lib": lib, "rot": rot,
            "x": x, "y": y, "x0": min(bx), "y0": min(by), "x1": max(bx), "y1": max(by),
            "pads": pads, "cls": classify(value or "", lib),
        })

    edge_x, edge_y = [], []
    for g in root:
        if not isinstance(g, list) or g[0] not in ("gr_line", "gr_rect"):
            continue
        layers = ksexp.children(g, "layer")
        if not layers or layers[0][1] != "Edge.Cuts":
            continue
        s, e = ksexp.child(g, "start"), ksexp.child(g, "end")
        edge_x += [float(s[1]), float(e[1])]
        edge_y += [float(s[2]), float(e[2])]

    size = (max(edge_x), max(edge_y)) if edge_x else (100.0, 80.0)
    return sorted(parts, key=lambda p: p["ref"]), size


def boxes_hit(box, others):
    ax0, ay0, ax1, ay1 = box
    for bx0, by0, bx1, by1 in others:
        if ax0 < bx1 and ax1 > bx0 and ay0 < by1 and ay1 > by0:
            return True
    return False


def segment_hits(x0, y0, x1, y1, boxes):
    """Liang-Barsky: does the segment enter any of these boxes?"""
    dx, dy = x1 - x0, y1 - y0
    for bx0, by0, bx1, by1 in boxes:
        t0, t1, ok = 0.0, 1.0, True
        for p, q in ((-dx, x0 - bx0), (dx, bx1 - x0), (-dy, y0 - by0), (dy, by1 - y0)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
            else:
                r = q / p
                if p < 0:
                    if r > t1:
                        ok = False
                        break
                    t0 = max(t0, r)
                else:
                    if r < t0:
                        ok = False
                        break
                    t1 = min(t1, r)
        if ok and t0 <= t1:
            return True
    return False


class View(object):
    """One drawing of the board, or of a window onto it, at a chosen scale."""

    def __init__(self, parts, ox, oy, scale, region=None, font=9.5, margin=135):
        self.parts, self.ox, self.oy, self.scale = parts, ox, oy, scale
        self.font, self.margin = font, margin
        self.mx0, self.my0, self.mx1, self.my1 = region or (0.0, 0.0, 100.0, 80.0)
        self.windowed = region is not None
        self.w = (self.mx1 - self.mx0) * scale
        self.h = (self.my1 - self.my0) * scale
        self.crossings = 0

        self.here = []
        for p in parts:
            if (p["x1"] < self.mx0 or p["x0"] > self.mx1
                    or p["y1"] < self.my0 or p["y0"] > self.my1):
                continue
            q = dict(p)
            q["px0"], q["py0"] = self.px(p["x0"], p["y0"])
            q["px1"], q["py1"] = self.px(p["x1"], p["y1"])
            q["pcx"], q["pcy"] = self.px(p["x"], p["y"])
            self.here.append(q)

        self.bodies = [(q["px0"] - 1.0, q["py0"] - 1.0, q["px1"] + 1.0, q["py1"] + 1.0)
                       for q in self.here]
        # A label straddling the board outline reads as clipped, so the edge is
        # an obstacle too -- but only on the full view, where the outline is a
        # real edge rather than the arbitrary side of a window.
        edges = [] if self.windowed else [
            (ox - 3, oy - 3, ox + 3, oy + self.h + 3),
            (ox + self.w - 3, oy - 3, ox + self.w + 3, oy + self.h + 3),
            (ox - 3, oy - 3, ox + self.w + 3, oy + 3),
            (ox - 3, oy + self.h - 3, ox + self.w + 3, oy + self.h + 3),
        ]
        self.obstacles = self.bodies + edges

    def px(self, mx, my):
        return (self.ox + (mx - self.mx0) * self.scale,
                self.oy + (my - self.my0) * self.scale)

    def text_box(self, tx, ty, anchor, width):
        x = tx if anchor == "start" else (tx - width / 2 if anchor == "middle" else tx - width)
        return (x, ty - ASCENDER * self.font, x + width, ty + DESCENDER * self.font)

    def place_labels(self):
        """Give every small part a name that collides with nothing."""
        placed, leaders, entries = [], [], []
        advance = self.font * ADVANCE

        def crowding(q):
            near = sum(1 for r in self.here if r is not q
                       and max(abs(r["pcx"] - q["pcx"]), abs(r["pcy"] - q["pcy"]))
                       < 7 * self.scale)
            return -near

        def place(q, strict=True):
            width = len(q["ref"]) * advance
            cx, cy = q["pcx"], q["pcy"]
            x0, y0, x1, y1 = q["px0"], q["py0"], q["px1"], q["py1"]
            asc, desc = ASCENDER * self.font, DESCENDER * self.font

            cands = []
            for gap in (3, 9, 16, 24, 34, 46, 60, 78, 100, 130, 170, 220):
                for slide in (0, gap * 0.7, -gap * 0.7):
                    cands += [
                        (cx + slide, y0 - gap - desc, "middle"),
                        (cx + slide, y1 + gap + asc, "middle"),
                        (x1 + gap, cy + slide + (asc - desc) / 2, "start"),
                        (x0 - gap, cy + slide + (asc - desc) / 2, "end"),
                    ]
            # Nearest slot first, whichever side it is on: a fixed side order
            # lets a far label above beat a near one beside.
            cands.sort(key=lambda c: (lambda b: ((b[0] + b[2]) / 2 - cx) ** 2
                                                + ((b[1] + b[3]) / 2 - cy) ** 2)
                       (self.text_box(c[0], c[1], c[2], width)))

            for tx, ty, anchor in cands:
                tb = self.text_box(tx, ty, anchor, width)
                box = (tb[0] - 2, tb[1] - 2, tb[2] + 2, tb[3] + 2)
                if (box[0] < self.ox - self.margin or box[1] < self.oy - self.margin
                        or box[2] > self.ox + self.w + self.margin
                        or box[3] > self.oy + self.h + self.margin):
                    continue
                if (boxes_hit(box, self.obstacles) or boxes_hit(box, placed)
                        or boxes_hit(box, leaders)):
                    continue

                lx, ly = (tb[0] + tb[2]) / 2, (tb[1] + tb[3]) / 2
                hw, hh = (tb[2] - tb[0]) / 2, (tb[3] - tb[1]) / 2
                stand = max(abs(lx - cx) - hw - (x1 - x0) / 2,
                            abs(ly - cy) - hh - (y1 - y0) / 2)
                leader = None
                if stand > 5:
                    dx, dy = lx - cx, ly - cy
                    dist = max(math.hypot(dx, dy), 1e-6)
                    ux, uy = dx / dist, dy / dist
                    # Stop on the label box's boundary, 4 px clear of it, so the
                    # line never runs under the text it points at.
                    u = min((hw + 4) / abs(ux) if abs(ux) > 1e-6 else 1e9,
                            (hh + 4) / abs(uy) if abs(uy) > 1e-6 else 1e9)
                    leader = (cx, cy, lx - ux * u, ly - uy * u)
                    if strict:
                        own = (q["px0"] - 1.0, q["py0"] - 1.0, q["px1"] + 1.0, q["py1"] + 1.0)
                        if segment_hits(leader[0], leader[1], leader[2], leader[3],
                                        [b for b in self.bodies if b != own]):
                            continue
                lead_box = None
                if leader:
                    a, b, c, d = leader
                    lead_box = (min(a, c) - 1.5, min(b, d) - 1.5,
                                max(a, c) + 1.5, max(b, d) + 1.5)
                return (q["ref"], tx, ty, anchor, leader), box, lead_box, strict

            if strict:                  # nowhere clean: let the leader cross
                return place(q, strict=False)
            raise SystemExit("assembly_plan: no slot for %s at scale %g"
                             % (q["ref"], self.scale))

        small = [q for q in self.here if q["ref"] not in BIG]
        for q in sorted(small, key=crowding):
            label, box, lead, clean = place(q)
            placed.append(box)
            if lead:
                leaders.append(lead)
            if not clean:
                self.crossings += 1
            entries.append([q, label, box, lead])

        # Repair: a label that a leader placed later happens to cross gets
        # another slot, this time with every leader already on the board.
        for _round in range(4):
            bad = [e for e in entries
                   if boxes_hit(e[2], [f[3] for f in entries if f is not e and f[3]])]
            if not bad:
                break
            for e in bad:
                _q, _label, box, lead = e
                placed.remove(box)
                if lead:
                    leaders.remove(lead)
                label2, box2, lead2, clean2 = place(e[0])
                placed.append(box2)
                if lead2:
                    leaders.append(lead2)
                if not clean2:
                    self.crossings += 1
                e[1], e[2], e[3] = label2, box2, lead2

        self.labels = [e[1] for e in entries]
        self.label_boxes = [e[2] for e in entries]
        return self.labels

    def check(self):
        """The placer's invariants, asserted rather than assumed."""
        problems = []
        named = {lab[0] for lab in self.labels}
        for q in self.here:
            if q["ref"] not in BIG and q["ref"] not in named:
                problems.append("%s carries no label" % q["ref"])
        for i, a in enumerate(self.label_boxes):
            for b in self.label_boxes[i + 1:]:
                if boxes_hit(a, [b]):
                    problems.append("two labels overlap near x=%.0f y=%.0f" % (a[0], a[1]))
            if boxes_hit(a, self.obstacles):
                problems.append("a label sits on a part or the board edge at x=%.0f" % a[0])
        return problems

    # ---- drawing -----------------------------------------------------------
    def draw(self, out, grid=10, moat=True):
        clip = None
        if self.windowed:
            clip = "clip%d" % int(self.ox * 7 + self.oy)
            out.append('<defs><clipPath id="%s"><rect x="%g" y="%g" width="%g" height="%g"/>'
                       '</clipPath></defs>' % (clip, self.ox, self.oy, self.w, self.h))
        out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" '
                   'stroke="%s" stroke-width="2"/>'
                   % (self.ox, self.oy, self.w, self.h, 0 if self.windowed else 7, BOARD, INK))

        if clip:
            out.append('<g clip-path="url(#%s)">' % clip)
        m = grid
        while m < self.mx1 - self.mx0:
            out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" '
                       'stroke-width="0.7"/>'
                       % (self.ox + m * self.scale, self.oy,
                          self.ox + m * self.scale, self.oy + self.h))
            m += grid
        m = grid
        while m < self.my1 - self.my0:
            out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" '
                       'stroke-width="0.7"/>'
                       % (self.ox, self.oy + m * self.scale,
                          self.ox + self.w, self.oy + m * self.scale))
            m += grid

        if moat and self.my0 < 48.6 and self.my1 > 47.6:
            top = max(self.my0, 47.6)
            out.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#d9cfb8"/>'
                       % (self.ox, self.oy + (top - self.my0) * self.scale,
                          self.w, (min(self.my1, 48.6) - top) * self.scale))
            out.append('<text x="%g" y="%g" font-size="9.5" fill="%s">1.0 mm moat</text>'
                       % (self.ox + 6, self.oy + (top - self.my0) * self.scale - 5, MUTED))
        if clip:
            out.append('</g>')

        # Leaders stay outside the clip: in a windowed view they reach labels
        # that legitimately stand outside the frame.
        for _ref, _tx, _ty, _anchor, leader in self.labels:
            if leader:
                out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#9b9385" '
                           'stroke-width="0.8"/>' % leader)

        if clip:
            out.append('<g clip-path="url(#%s)">' % clip)
        for q in sorted(self.here, key=lambda r: -(r["px1"] - r["px0"]) * (r["py1"] - r["py0"])):
            fill, stroke = STYLE[q["cls"]]
            dash = ' stroke-dasharray="4 3"' if q["cls"] == "dnp" else ""
            out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="1.5" fill="%s" '
                       'stroke="%s" stroke-width="1.1"%s/>'
                       % (q["px0"], q["py0"], q["px1"] - q["px0"], q["py1"] - q["py0"],
                          fill, stroke, dash))
            mark = "#e8e2d4" if fill == "#3a3a34" else stroke
            pad1 = next((r for r in q["pads"] if r["n"] == "1"), None)
            if pad1 and q["cls"] not in ("probe", "module"):
                qx, qy = self.px(pad1["x"], pad1["y"])
                out.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>'
                           % (qx, qy, 1.4 + self.scale / 13.0, mark))
            if q["cls"] == "led":
                k = next((r for r in q["pads"] if (r["net"] or "").endswith("_K")), None)
                if k:
                    kx, ky = self.px(k["x"], k["y"])
                    wide = abs(kx - (q["px0"] + q["px1"]) / 2) > abs(ky - (q["py0"] + q["py1"]) / 2)
                    bar = 1.4 + self.scale / 10.0
                    if wide:
                        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#1d3d0f" '
                                   'stroke-width="%g"/>' % (kx, q["py0"] + 2, kx, q["py1"] - 2, bar))
                    else:
                        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#1d3d0f" '
                                   'stroke-width="%g"/>' % (q["px0"] + 2, ky, q["px1"] - 2, ky, bar))
        if clip:
            out.append('</g>')

        for q in self.here:
            if q["ref"] not in BIG:
                continue
            if not (self.mx0 <= q["x"] <= self.mx1 and self.my0 <= q["y"] <= self.my1):
                continue                # its centre is off-frame; its name would be too
            bw, bh = q["px1"] - q["px0"], q["py1"] - q["py0"]
            cx, cy = (q["px0"] + q["px1"]) / 2, (q["py0"] + q["py1"]) / 2
            col = "#f2eee2" if STYLE[q["cls"]][0] == "#3a3a34" else INK
            if q["ref"] == "U_SM":
                out.append('<text x="%g" y="%g" font-size="19" fill="%s" text-anchor="middle">'
                           'U_SM</text>' % (cx, cy - 6, col))
                out.append('<text x="%g" y="%g" font-size="12" fill="%s" text-anchor="middle">'
                           'Daisy Patch SM &#8212; seats on J_SM1 / J_SM2, two 2&#215;10 sockets'
                           '</text>' % (cx, cy + 14, MUTED))
                out.append('<text x="%g" y="%g" font-size="11" fill="%s" text-anchor="middle">'
                           'solder the sockets, not the module</text>' % (cx, cy + 32, MUTED))
            elif bw > bh + 20:
                out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle">'
                           '%s</text>' % (cx, cy - 1, self.font + 2.5, col, q["ref"]))
                out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle">'
                           '%s</text>' % (cx, cy + 12, self.font, col, q["value"]))
            else:
                out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle" '
                           'transform="rotate(-90 %g %g)">%s</text>'
                           % (cx, cy - 1, self.font + 2.5, col, cx, cy, q["ref"]))
                out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle" '
                           'transform="rotate(-90 %g %g)">%s</text>'
                           % (cx, cy + 12, self.font, col, cx, cy, q["value"]))

        for ref, tx, ty, anchor, _leader in self.labels:
            out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="%s">%s</text>'
                       % (tx, ty, self.font, INK, anchor, ref))


def open_svg(out, width, height):
    out.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" '
               'height="%d" font-family="IBM Plex Mono, Cascadia Mono, Consolas, monospace">'
               % (width, height, width, height))
    out.append('<rect x="0" y="0" width="%d" height="%d" fill="%s"/>' % (width, height, PAPER))


def scale_bar(out, x, y, scale, mm=10):
    out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="1.4"/>'
               % (x, y, x + mm * scale, y, INK))
    out.append('<text x="%g" y="%g" font-size="10" fill="%s">%d mm</text>'
               % (x + mm * scale + 8, y + 4, MUTED, mm))


def write_assembly(parts, path):
    """The working sheet: whole board, analog corner enlarged, stuffing list."""
    width = 1600
    scale, ox, oy = 13.0, 150, 175
    detail, dscale, dx = (58.0, 45.5, 100.0, 80.0), 25.0, 230
    dy = int(oy + 80.0 * scale + 160)
    list_y = int(dy + (detail[3] - detail[1]) * dscale + 130)
    height = list_y + 400

    full = View(parts, ox, oy, scale)
    full.place_labels()
    corner = View(parts, dx, dy, dscale, detail, font=11, margin=200)
    corner.place_labels()

    problems = full.check() + corner.check()

    out = []
    open_svg(out, width, height)
    out.append('<text x="40" y="44" font-size="20" fill="%s" letter-spacing="0.04em">'
               'FIREFLOW TEST COUPON &#183; ASSEMBLY PLAN</text>' % INK)
    out.append('<text x="40" y="66" font-size="12" fill="%s">Top view, component side &#183; '
               '100 &#215; 80 mm &#183; every part sits where the board file puts it</text>' % MUTED)
    out.append('<text x="%d" y="44" font-size="11.5" fill="%s" text-anchor="end">%d parts, all '
               'on the front copper</text>' % (width - 40, MUTED, len(parts)))
    out.append('<text x="%d" y="66" font-size="11.5" fill="%s" text-anchor="end">geometry read '
               'from hardware/coupon/coupon.kicad_pcb</text>' % (width - 40, MUTED))
    out.append('<line x1="40" y1="84" x2="%d" y2="84" stroke="%s"/>' % (width - 40, RULE))
    out.append('<circle cx="46" cy="108" r="2.6" fill="%s"/>' % INK)
    out.append('<text x="58" y="112" font-size="11" fill="%s">= pad 1, at its measured position '
               '&#8212; on the SOIC chips that is the notch end.</text>' % INK)
    out.append('<line x1="660" y1="102" x2="660" y2="114" stroke="#1d3d0f" stroke-width="2.6"/>')
    out.append('<text x="672" y="112" font-size="11" fill="%s">= LED cathode (the _K pad).</text>' % INK)
    out.append('<text x="930" y="112" font-size="11" fill="%s">C_COM16 and C_COM8 are DNP: fit '
               'the footprint, leave it empty.</text>' % ALERT)

    out.append('<text x="%d" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">'
               '1 &#183; THE WHOLE BOARD</text>' % (ox, oy - 18, INK))
    full.draw(out)

    fx, fy = ox + detail[0] * scale, oy + detail[1] * scale
    fw, fh = (detail[2] - detail[0]) * scale, (detail[3] - detail[1]) * scale
    for ax, ay, sx, sy in ((fx, fy, 1, 1), (fx + fw, fy, -1, 1),
                           (fx, fy + fh, 1, -1), (fx + fw, fy + fh, -1, -1)):
        out.append('<path d="M %g %g L %g %g M %g %g L %g %g" fill="none" stroke="%s" '
                   'stroke-width="2.2"/>'
                   % (ax, ay + sy * 30, ax, ay, ax, ay, ax + sx * 30, ay, ALERT))
    out.append('<text x="%g" y="%g" font-size="11" fill="%s" text-anchor="end">detail 2</text>'
               % (fx - 8, fy + 14, ALERT))
    scale_bar(out, ox, oy + 80.0 * scale + 26, scale)

    out.append('<text x="%d" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">2 &#183; THE '
               'ANALOG CORNER, ENLARGED &#8212; x %g..%g mm, y %g..%g mm of the same board</text>'
               % (dx, dy - 18, INK, detail[0], detail[2], detail[1], detail[3]))
    corner.draw(out, grid=5)

    out.append('<line x1="40" y1="%d" x2="%d" y2="%d" stroke="%s"/>'
               % (list_y - 40, width - 40, list_y - 40, RULE))
    out.append('<text x="40" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">3 &#183; WHAT '
               'GOES WHERE &#8212; one row per reel</text>' % (list_y - 14, INK))

    groups = {}
    for p in parts:
        groups.setdefault(p["cls"], []).append(p["ref"])
    columns = [40, 40 + (width - 80) // 2]
    per_column = (len(CLASS_ORDER) + 1) // 2
    for i, cls in enumerate(CLASS_ORDER):
        refs = sorted(groups.get(cls, []))
        if not refs:
            continue
        cx = columns[i // per_column]
        cy = list_y + 10 + (i % per_column) * 34
        fill, stroke = STYLE[cls]
        dash = ' stroke-dasharray="3 2"' if cls == "dnp" else ""
        out.append('<rect x="%d" y="%d" width="16" height="11" rx="1.5" fill="%s" stroke="%s" '
                   'stroke-width="1.1"%s/>' % (cx, cy - 9, fill, stroke, dash))
        out.append('<text x="%d" y="%d" font-size="11" fill="%s">%s</text>'
                   % (cx + 24, cy, INK, CLASS_NAME[cls]))
        out.append('<text x="%d" y="%d" font-size="10" fill="%s">&#215;%d</text>'
                   % (cx + 150, cy, MUTED, len(refs)))
        lines, cur = [], ""
        for ref in refs:
            if len(cur) + len(ref) + 2 > 92:
                lines.append(cur)
                cur = ref
            else:
                cur = (cur + "  " + ref) if cur else ref
        if cur:
            lines.append(cur)
        for n, line in enumerate(lines[:2]):
            out.append('<text x="%d" y="%d" font-size="9.5" fill="%s">%s</text>'
                       % (cx + 188, cy + n * 12, MUTED, line))

    out.append('</svg>')
    write(path, out)
    return problems, full.crossings + corner.crossings


# The overview's numbered zones: number, marker x/y in px, and the point in
# board mm the leader lands on.
ZONES = [
    (1, "left", 20.0, 8.0, 18.0, "the shift registers and the eight LEDs"),
    (2, "top", 40.0, 46.0, 26.0, "the Daisy module &#8212; it sits on two sockets"),
    (3, "top", 86.0, 90.0, 5.0, "the button and the ADC probe points"),
    (4, "right", 26.0, 95.5, 28.0, "Eurorack power in, bulk caps beside it"),
    (5, "left", 48.1, 30.0, 48.1, "the 1.0 mm moat &#8212; digital above, analog below"),
    (6, "left", 60.0, 3.0, 56.0, "the 3.5 mm audio jack"),
    (7, "bottom", 36.0, 39.5, 68.0, "seven pots, 10 k and 20 k mixed"),
    (8, "right", 66.0, 88.0, 66.0, "both mux chips, and most of the small parts"),
]


def write_overview(parts, path):
    """The column-width version: same board, eight numbered zones, colour key."""
    width, height = 760, 852
    scale, ox, oy = 6.4, 60, 55
    bw, bh = 100.0 * scale, 80.0 * scale

    out = []
    open_svg(out, width, height)
    out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="5" fill="%s" stroke="%s" '
               'stroke-width="1.6"/>' % (ox, oy, bw, bh, BOARD, INK))
    for mm in range(10, 100, 10):
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" stroke-width="0.6"/>'
                   % (ox + mm * scale, oy, ox + mm * scale, oy + bh))
    for mm in range(10, 80, 10):
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" stroke-width="0.6"/>'
                   % (ox, oy + mm * scale, ox + bw, oy + mm * scale))
    out.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#d3c7ab"/>'
               % (ox, oy + 47.6 * scale, bw, scale))

    for p in sorted(parts, key=lambda q: -(q["x1"] - q["x0"]) * (q["y1"] - q["y0"])):
        fill, stroke = STYLE[p["cls"]]
        dash = ' stroke-dasharray="3 2"' if p["cls"] == "dnp" else ""
        out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="1" fill="%s" stroke="%s" '
                   'stroke-width="0.8"%s/>'
                   % (ox + p["x0"] * scale, oy + p["y0"] * scale,
                      (p["x1"] - p["x0"]) * scale, (p["y1"] - p["y0"]) * scale,
                      fill, stroke, dash))

    markers = []
    for num, side, along, tx, ty, _text in ZONES:
        if side == "left":
            mx, my = 32, oy + along * scale
        elif side == "right":
            mx, my = width - 32, oy + along * scale
        elif side == "top":
            mx, my = ox + along * scale, 30
        else:
            mx, my = ox + along * scale, oy + bh + 18
        markers.append((num, mx, my, ox + tx * scale, oy + ty * scale))

    for _num, mx, my, px, py in markers:
        dx, dy = px - mx, py - my
        dist = max(math.hypot(dx, dy), 1e-6)
        # Start on the circle's edge, so the line never runs under its digit.
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="1"/>'
                   % (mx + dx / dist * 12, my + dy / dist * 12, px, py, ALERT))
    for num, mx, my, _px, _py in markers:
        out.append('<circle cx="%g" cy="%g" r="11" fill="%s" stroke="%s" stroke-width="1.6"/>'
                   % (mx, my, PAPER, ALERT))
        out.append('<text x="%g" y="%g" font-size="12" fill="%s" text-anchor="middle">%d</text>'
                   % (mx, my + 4.2, ALERT, num))

    scale_bar(out, ox, oy + bh + 22, scale)

    for i, zone in enumerate(ZONES):
        cx = 30 if i < 4 else 396
        cy = 638 + (i % 4) * 21
        out.append('<text x="%d" y="%d" font-size="11.5" fill="%s">%d  %s</text>'
                   % (cx, cy, INK, zone[0], zone[5]))

    out.append('<line x1="30" y1="740" x2="730" y2="740" stroke="%s"/>' % RULE)
    out.append('<text x="30" y="762" font-size="10.5" fill="%s" letter-spacing="0.08em">'
               'THE COLOUR IS THE VALUE</text>' % MUTED)
    # Every class the board actually carries gets a swatch: the two pot values
    # are different reels and the drawing already tells them apart by shade, so
    # a key that merged them would be lying about what the picture shows.
    key = ["0R", "1k", "10k", "100n", "10u", "led",
           "probe", "pot10k", "pot20k", "chip", "link", "dnp"]
    short = dict(CLASS_NAME, led="LED", probe="test point", pot10k="10 k pot",
                 pot20k="20 k pot", chip="chip", link="jumper", dnp="fit nothing",
                 **{"10u": "10 &#181;F"})
    for i, cls in enumerate(key):
        cx = 30 + (i % 6) * 118
        cy = 792 + (i // 6) * 26
        fill, stroke = STYLE[cls]
        dash = ' stroke-dasharray="3 2"' if cls == "dnp" else ""
        out.append('<rect x="%d" y="%d" width="15" height="10" rx="1" fill="%s" stroke="%s" '
                   'stroke-width="1"%s/>' % (cx, cy - 8, fill, stroke, dash))
        out.append('<text x="%d" y="%d" font-size="11" fill="%s">%s</text>'
                   % (cx + 22, cy, INK, short[cls]))

    out.append('</svg>')
    write(path, out)


def write(path, out):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-dir", default=DEFAULT_OUT,
                    help="where to write the two SVGs (default: hardware/coupon/proof)")
    ap.add_argument("--prefix", default="coupon",
                    help="basename prefix, so the website's copies can keep their own names")
    args = ap.parse_args(argv)

    parts, (board_w, board_h) = read_board(PCB)
    print("read %s: %d parts on a %g x %g mm board"
          % (os.path.relpath(PCB), len(parts), board_w, board_h))

    assembly = os.path.join(args.out_dir, "%s-assembly.svg" % args.prefix)
    overview = os.path.join(args.out_dir, "%s-overview.svg" % args.prefix)

    problems, crossings = write_assembly(parts, assembly)
    write_overview(parts, overview)

    print("wrote %s" % os.path.relpath(assembly))
    print("wrote %s" % os.path.relpath(overview))
    if crossings:
        print("%d label(s) had no clean slot; their leader crosses a part" % crossings)
    if problems:
        for line in problems:
            print("  PROBLEM: %s" % line)
        return 1
    print("label check: no overlaps, nothing on a part or the board edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
