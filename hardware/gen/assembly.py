"""Assembly sheets drawn from a KiCad board file -- moved from
hardware/coupon/scripts/assembly_plan.py for Rev A (P4-3 spec §4.4.5)."""
import math

from gen import ksexp

# The journal's palette, so a drawing dropped into the log does not arrive as a
# foreign object: paper, ink, and the three accents the site already uses.
INK, MUTED, RULE, PAPER, BOARD = "#171713", "#656056", "#d7cdbb", "#f7f4ec", "#eae4d6"
ALERT = "#b03a2e"

# Measured against IBM Plex Mono / Cascadia Mono / Consolas at several sizes:
# the advance is a constant fraction of the em in any of them, and a glyph box
# reaches 0.80 em above the baseline and 0.22 em below it. The placer's whole
# correctness rests on these three numbers.
ADVANCE, ASCENDER, DESCENDER = 0.602, 0.80, 0.22


def read_board(path, classify):
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

        layer = ksexp.children(fp, "layer")
        side = "B" if layer and str(layer[0][1]).startswith("B.") else "F"
        attr = ksexp.children(fp, "attr")
        dnp = bool(attr) and "dnp" in [str(a) for a in attr[0][1:]]
        cls = "dnp" if dnp else classify(value or "", lib)

        parts.append({
            "ref": ref, "value": value or "", "lib": lib, "rot": rot,
            "x": x, "y": y, "x0": min(bx), "y0": min(by), "x1": max(bx), "y1": max(by),
            "pads": pads, "cls": cls, "side": side, "dnp": dnp,
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

    def __init__(self, parts, ox, oy, scale, region=None, font=9.5, margin=135,
                 big=frozenset(), style=None, bounds=None):
        self.parts, self.ox, self.oy, self.scale = parts, ox, oy, scale
        self.font, self.margin = font, margin
        self.big, self.style = big, style
        self.mx0, self.my0, self.mx1, self.my1 = region or bounds or (0.0, 0.0, 100.0, 80.0)
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

        small = [q for q in self.here if q["ref"] not in self.big]
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
            if q["ref"] not in self.big and q["ref"] not in named:
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
            fill, stroke = self.style[q["cls"]]
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
            if q["ref"] not in self.big:
                continue
            if not (self.mx0 <= q["x"] <= self.mx1 and self.my0 <= q["y"] <= self.my1):
                continue                # its centre is off-frame; its name would be too
            bw, bh = q["px1"] - q["px0"], q["py1"] - q["py0"]
            cx, cy = (q["px0"] + q["px1"]) / 2, (q["py0"] + q["py1"]) / 2
            col = "#f2eee2" if self.style[q["cls"]][0] == "#3a3a34" else INK
            if q["ref"] == "U_SM":
                out.append('<text x="%g" y="%g" font-size="19" fill="%s" text-anchor="middle">'
                           'U_SM</text>' % (cx, cy - 6, col))
                out.append('<text x="%g" y="%g" font-size="12" fill="%s" text-anchor="middle">'
                           'Daisy Patch SM &#8212; seats on four 2&#215;5 sockets J_SM1..J_SM4, each cut from a 2&#215;10 strip'
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
