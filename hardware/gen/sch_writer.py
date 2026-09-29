#!/usr/bin/env python3
"""Schematic writer shared by the generated boards (coupon, Rev A).

Moved from hardware/coupon/scripts/generate_schematic.py and extended for
Rev A (P3 spec §2-§3): several sheets under an overview sheet, power symbols,
sheet-local labels, every unit of a multi-unit symbol, and UUIDs that come out
the same on every run.

Style, unchanged from the coupon: every pin gets a short stub and a label; no
wires run between symbols. A generated rat's nest of wires is unreadable and
hard to get right, while labels are exactly as connected and say the net name
at every pin.

Connectivity is NOT trusted to this file's geometry. check.py exports the
netlist with kicad-cli and compares it against the intent; a Y-flip of the pin
coordinates shows up there as a missing node. A wrong stub direction does not:
the stub still joins the pin to its label, so the netlist stays green and only
overlaps() (the drawing check) sees the clash.
"""
import itertools
import os
import uuid

from gen import ksexp
from gen import netlist as N

GRID = 1.27          # mm. Every placement snaps to it, or ERC reports each pin
                     # as "endpoint off connection grid" -- 344 of them on the
                     # coupon's first pass, purely from float placement.
STUB = 3.81          # pin connection point to label anchor (3 x GRID)
GUTTER_X = 12.0
GUTTER_Y = 10.0
TEXT_LINE = 2.54     # one line of reference/value text above the symbol
MARGIN = 20.0
TITLE_BLOCK_H = 40.0 # content has to end above KiCad's title block
PAPER = {"A2": (594.0, 420.0), "A3": (420.0, 297.0)}
FONT = 1.27          # every text; P3 spec §2 -- never smaller (the coupon used 1.0)
CHAR_W = 0.75 * FONT # KiCad stroke font advance, near enough
LABEL_PAD = 3.0      # a global label's arrow shape
POWER_LEN = 5.08     # a power symbol's graphic plus the gap to its value text
POWER_TEXT_GAP = 3.2 # pin to the near edge of the value text, on a horizontal stub
ADJACENT = 2 * GRID  # same-net power pins this close share one power symbol


class Uuids:
    """Deterministic UUIDs: uuid5 over a key naming the drawn item.

    Random uuid4s made every coupon board build a 26 000-line diff (memory
    fireflow-pcb-generator-speedups); the schematic gets the fix on day one.
    A key used twice raises -- two items would otherwise share an id.
    `random = True` exists only for the check tool's stability sabotage.
    """
    NS = uuid.UUID("6f1c2a4e-3b7d-5e8f-9a0b-1c2d3e4f5a6b")
    random = False

    def __init__(self, project):
        self.project = project
        self.seen = set()

    def __call__(self, *key):
        k = "/".join(str(x) for x in (self.project,) + key)
        if k in self.seen:
            raise ValueError("uuid key used twice: " + k)
        self.seen.add(k)
        if Uuids.random:
            return str(uuid.uuid4())
        return str(uuid.uuid5(self.NS, k))


def snap(v):
    return round(round(v / GRID) * GRID, 4)


def _n(v):
    """A coordinate as KiCad writes it: no float noise, no trailing zeros."""
    s = "%.4f" % v
    return s.rstrip("0").rstrip(".") if "." in s else s


def _esc(s):
    return str(s).replace("\\", "\\\\").replace('"', '\\"')


# --- units ---------------------------------------------------------------------

def units_of(part):
    """The units to draw: every unit that owns a pin.

    Unit 0 pins (common to all units) are refused on multi-unit symbols --
    nothing on these boards needs them, and drawing them once per unit would
    duplicate nodes.
    """
    units = sorted({p["unit"] for p in part.sym.pins.values()})
    if units == [0]:
        return [1]
    if 0 in units:
        raise ValueError("%s: %s has pins common to all units; not supported"
                         % (part.ref, part.lib_id))
    return units


def unit_pins(part, unit):
    return [n for n, p in part.sym.pins.items() if p["unit"] in (unit, 0)]


def cells(parts):
    return [(p, u) for p in parts for u in units_of(p)]


def _cell_name(part, unit):
    return part.ref if len(units_of(part)) == 1 else "%s:%d" % (part.ref, unit)


# --- geometry ------------------------------------------------------------------

def sym_extent(part, unit):
    pins = [part.sym.pins[n] for n in unit_pins(part, unit)]
    xs = [p["x"] for p in pins]
    ys = [p["y"] for p in pins]
    return (min(xs), max(xs), min(ys), max(ys))


def pin_point(part_x, part_y, pin):
    """Pin connection point in schematic coordinates (+Y down)."""
    return part_x + pin["x"], part_y - pin["y"]


def stub_end(cx, cy, angle):
    """Where the label sits: outward from the body, i.e. along the pin.

    Returns (x, y, rot); rot is also the outward direction in screen degrees
    (0 east, 90 north, 180 west, 270 south).
    """
    if angle == 0:            # pin body lies to the east; free end faces west
        return cx - STUB, cy, 180
    if angle == 180:
        return cx + STUB, cy, 0
    if angle == 90:           # symbol-space north -> schematic north is -Y
        return cx, cy + STUB, 270
    if angle == 270:
        return cx, cy - STUB, 90
    raise ValueError("unexpected pin angle %r" % angle)


def _along(x, y, rot, d):
    return {0: (x + d, y), 180: (x - d, y), 90: (x, y - d), 270: (x, y + d)}[rot]


def label_width(net):
    return len(net) * CHAR_W + LABEL_PAD


def ending_box(ex, ey, rot, net, power):
    """The rectangle a label or power symbol draws beyond its stub end."""
    h = FONT * 1.6
    if net in power:
        tw = len(net) * CHAR_W
        if rot in (0, 180):              # symbol, then the value text, in line
            length, thick = POWER_LEN + tw, max(h, 2.6)
        else:                            # value text centred past the tip
            length, thick = POWER_LEN + h, max(tw, 2.6)
    else:
        length, thick = label_width(net), h
    if rot == 180:
        return (ex - length, ey - thick / 2, ex, ey + thick / 2)
    if rot == 0:
        return (ex, ey - thick / 2, ex + length, ey + thick / 2)
    if rot == 90:
        return (ex - thick / 2, ey - length, ex + thick / 2, ey)
    return (ex - thick / 2, ey, ex + thick / 2, ey + length)


def label_groups(part, unit, px=0.0, py=0.0, power=()):
    """One stub and one ending per distinct connection point of `unit`.

    Electrosmith's Daisy_Patch_SM stacks A4 and A7 -- both GND -- on exactly
    the same coordinate. One stub per pin would double-strike the text. Pins
    that share a point but carry DIFFERENT nets are a short, so that raises.

    Pins on the same power net that sit next to each other on one edge -- the
    74HC4051's VEE and GND, 2.54 mm apart, both GND on a single supply --
    share ONE power symbol: their stub ends are chained by wires. Two symbols
    there print their values on top of each other ("GNGND").

    Returns [((cx, cy), angle, net, pin numbers, other points)], where other
    points are the connection points whose stubs join this group's ending.
    """
    groups = {}
    for number in unit_pins(part, unit):
        if number not in part.nets:
            continue
        net = part.nets[number]
        pin = part.sym.pin(number)
        key = (round(pin["x"], 4), round(pin["y"], 4), pin["angle"])
        if key in groups and groups[key][0] != net:
            raise ValueError(
                "%s: pins %s and %s share a connection point but carry "
                "different nets (%s vs %s) -- that is a short"
                % (part.ref, groups[key][1][0], number, groups[key][0], net))
        groups.setdefault(key, (net, []))[1].append(number)

    out, runs = [], {}
    for key, (net, nums) in sorted(groups.items()):
        if net in power:
            vertical = key[2] in (90, 270)          # pins on the top/bottom edge
            line = key[1] if vertical else key[0]
            runs.setdefault((net, key[2], line), []).append((key, nums))
        else:
            out.append(((key[0], key[1]), key[2], net, nums, []))
    for (net, angle, _line), members in sorted(runs.items()):
        along = 0 if angle in (90, 270) else 1
        members.sort(key=lambda m: m[0][along])
        run = [members[0]]
        for m in members[1:] + [None]:
            if m is not None and m[0][along] - run[-1][0][along] <= ADJACENT + 1e-6:
                run.append(m)
                continue
            head = run[0][0]
            out.append(((head[0], head[1]), angle, net,
                        [n for _, ns in run for n in ns],
                        [(k[0], k[1]) for k, _ in run[1:]]))
            run = [m] if m is not None else []
    out.sort(key=lambda g: (g[0][0], g[0][1], g[1]))
    return [(pin_point(px, py, dict(x=pt[0], y=pt[1])), angle, net, nums,
             [pin_point(px, py, dict(x=o[0], y=o[1])) for o in others])
            for pt, angle, net, nums, others in out]


def label_margins(part, unit, power):
    """How far stubs and endings reach beyond the symbol, on all four sides.

    Measured from the same ending_box() the collision check uses, so the
    packing and the check cannot disagree about a label's size. (The coupon
    once charged vertical labels to the width instead of the height, and
    two-pin parts in adjacent rows overlapped while the arithmetic said there
    was room.)
    """
    x0, x1, y0, y1 = sym_extent(part, unit)
    left = right = top = bottom = 0.0
    for (cx, cy), angle, net, _nums, _others in label_groups(part, unit, power=power):
        ex, ey, rot = stub_end(cx, cy, angle)
        bx0, by0, bx1, by1 = ending_box(ex, ey, rot, net, power)
        left = max(left, x0 - bx0)
        right = max(right, bx1 - x1)
        top = max(top, -y1 - by0)
        bottom = max(bottom, by1 + y0)
    return left, right, top, bottom


def layout(parts, paper, power):
    """Pack every (part, unit) into rows; return ({(ref, unit): (x, y)}, height)."""
    sheet_w, _ = PAPER[paper]
    placed = {}
    x, y, row_h = MARGIN, MARGIN, 0.0
    for p, u in cells(parts):
        x0, x1, y0, y1 = sym_extent(p, u)
        lw, rw, tw, bw = label_margins(p, u, power)
        w = (x1 - x0) + lw + rw + GUTTER_X
        # the two text lines sit above everything the part draws, labels included
        h = (y1 - y0) + tw + bw + GUTTER_Y + 2 * TEXT_LINE
        if w > sheet_w - 2 * MARGIN:
            raise ValueError("%s is %.0f mm wide; %s allows %.0f"
                             % (_cell_name(p, u), w, paper, sheet_w - 2 * MARGIN))
        if x + w > sheet_w - MARGIN:
            x = MARGIN
            y += row_h
            row_h = 0.0
        placed[(p.ref, u)] = (snap(x + lw - x0), snap(y + 2 * TEXT_LINE + tw + y1))
        x += w
        row_h = max(row_h, h)
    return placed, y + row_h + MARGIN


def fits(height, paper):
    return height <= PAPER[paper][1] - TITLE_BLOCK_H


def text_anchor(part, unit, px, py, power):
    """Reference and value go above everything the part draws.

    Not a fixed offset from the origin -- a tall symbol carries its origin in
    the middle, which once printed "U_MUX16" inside the CD74HC4067M's body --
    and not just above the symbol either, because a two-pin part's top label
    runs upwards through exactly that space.
    """
    _, _, _, y1 = sym_extent(part, unit)
    _, _, tw, _ = label_margins(part, unit, power)
    top = py - y1 - tw
    return snap(top - 2 * TEXT_LINE), snap(top - TEXT_LINE)


# --- collision checking --------------------------------------------------------
# Added after the coupon's first PDF: labels from one symbol ran through the
# labels of the next, and reference text sat inside tall symbol bodies. Both
# are obvious in the drawing and invisible in the netlist.

def _boxes(parts, placed, power):
    out = []
    for p, u in cells(parts):
        px, py = placed[(p.ref, u)]
        name = _cell_name(p, u)
        x0, x1, y0, y1 = sym_extent(p, u)
        out.append((px + x0, py - y1, px + x1, py - y0, "%s body" % name))
        if not N.is_virtual(p.ref):           # flags draw no ref/value text
            ref_y, val_y = text_anchor(p, u, px, py, power)
            for text, ty in ((p.ref, ref_y), (p.value, val_y)):
                w = len(str(text)) * CHAR_W
                out.append((px, ty - 0.9, px + w, ty + 0.9,
                            "%s text %r" % (name, text)))
        for (cx, cy), angle, net, nums, _others in label_groups(p, u, px, py, power):
            ex, ey, rot = stub_end(cx, cy, angle)
            out.append(ending_box(ex, ey, rot, net, power)
                       + ("%s.%s %s" % (p.ref, "/".join(nums), net),))
    return out


def overlaps(parts, placed, power, tolerance=0.2):
    """Pairs of drawn boxes that intersect by more than `tolerance` mm."""
    boxes = sorted(_boxes(parts, placed, power), key=lambda b: b[0])
    hits = []
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if b[0] >= a[2] - tolerance:
                break                     # sorted by x0: nothing further can hit
            if (a[0] < b[2] - tolerance and b[0] < a[2] - tolerance
                    and a[1] < b[3] - tolerance and b[1] < a[3] - tolerance):
                hits.append((a[4], b[4]))
    return hits


# --- emission ------------------------------------------------------------------

def _points_up(sym):
    """True if a power symbol draws above its pin (+3V3), False below (GND)."""
    ys = []

    def walk(node):
        for c in node:
            if isinstance(c, list) and c and isinstance(c[0], ksexp.Atom):
                if c[0] == "xy":
                    ys.append(float(c[2]))
                else:
                    walk(c)
    walk((sym.base if sym.extends else sym).node)
    if not ys:
        raise ValueError("%s draws nothing" % sym.name)
    return max(ys) >= -min(ys)


def _prop(name, value, x, y, hide=False, justify="left", angle=0):
    just = " (justify %s)" % justify if justify else ""
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s %d)\n'
            '\t\t\t(effects (font (size %s %s))%s%s))\n'
            % (name, _esc(value), _n(x), _n(y), angle, FONT, FONT, just,
               " (hide yes)" if hide else ""))


def _symbol(lib_id, ref, unit, x, y, rot, in_bom, on_board, dnp, sym_uuid,
            props, pins, project, path):
    def yn(b):
        return "yes" if b else "no"
    return ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %d)\n\t\t(unit %d)\n'
            '\t\t(exclude_from_sim no)\n\t\t(in_bom %s)\n\t\t(on_board %s)\n'
            '\t\t(dnp %s)\n\t\t(uuid "%s")\n%s%s'
            '\t\t(instances\n\t\t\t(project "%s"\n'
            '\t\t\t\t(path "%s" (reference "%s") (unit %d))))\n\t)\n'
            % (lib_id, _n(x), _n(y), rot, unit, yn(in_bom), yn(on_board),
               yn(dnp), sym_uuid, "".join(props),
               "".join('\t\t(pin "%s" (uuid "%s"))\n' % pu for pu in pins),
               project, path, ref, unit))


def _wire(x0, y0, x1, y1, u):
    return ('\t(wire (pts (xy %s %s) (xy %s %s))\n'
            '\t\t(stroke (width 0) (type default)) (uuid "%s"))\n'
            % (_n(x0), _n(y0), _n(x1), _n(y1), u))


def _global_label(net, x, y, rot, u):
    return ('\t(global_label "%s"\n\t\t(shape bidirectional)\n'
            '\t\t(at %s %s %d)\n\t\t(fields_autoplaced yes)\n'
            '\t\t(effects (font (size %s %s)) (justify %s))\n\t\t(uuid "%s")\n'
            '\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n'
            '\t\t\t(at %s %s 0)\n\t\t\t(effects (font (size %s %s)) (hide yes))))\n'
            % (_esc(net), _n(x), _n(y), rot, FONT, FONT,
               "right" if rot in (180, 270) else "left", u, _n(x), _n(y), FONT, FONT))


def _local_label(net, x, y, rot, u):
    just = "right bottom" if rot in (180, 270) else "left bottom"
    return ('\t(label "%s"\n\t\t(at %s %s %d)\n'
            '\t\t(effects (font (size %s %s)) (justify %s))\n\t\t(uuid "%s"))\n'
            % (_esc(net), _n(x), _n(y), rot, FONT, FONT, just, u))


def emit_items(parts, placed, uuids, project, path, kind, power, pwr_counter):
    """Symbols, stubs, labels, power symbols and no-connects for one sheet."""
    out = []
    for p, u in cells(parts):
        px, py = placed[(p.ref, u)]
        virtual = N.is_virtual(p.ref)
        ref_y, val_y = text_anchor(p, u, px, py, power)
        props = [_prop("Reference", p.ref, px, ref_y, hide=virtual),
                 _prop("Value", p.value, px, val_y, hide=virtual),
                 _prop("Footprint", p.footprint, px, py, hide=True),
                 _prop("Datasheet", "", px, py, hide=True),
                 _prop("Description", p.note, px, py, hide=True)]
        for name, val in (("LCSC", p.lcsc), ("Source", p.source),
                          ("PanelId", p.panel_id)):
            if val:
                props.append(_prop(name, val, px, py, hide=True))
        pins = [(n, uuids("pin", p.ref, n))
                for n in sorted(unit_pins(p, u), key=ksexp._pin_sort_key)]
        out.append(_symbol(p.lib_id, p.ref, u, px, py, 0,
                           p.in_bom and not virtual, p.on_board and not virtual,
                           p.dnp, uuids("sym", p.ref, u), props, pins, project, path))
        for (cx, cy), angle, net, nums, others in label_groups(p, u, px, py, power):
            ex, ey, rot = stub_end(cx, cy, angle)
            key = (p.ref, "/".join(nums))
            out.append(_wire(cx, cy, ex, ey, uuids("stub", *key)))
            prev = (ex, ey)
            for i, (ox, oy) in enumerate(others, 1):
                oex, oey, _ = stub_end(ox, oy, angle)
                out.append(_wire(ox, oy, oex, oey, uuids("stub", p.ref, key[1], i)))
                # chain end to end, so no wire end lands mid-segment
                out.append(_wire(prev[0], prev[1], oex, oey, uuids("join", p.ref, key[1], i)))
                prev = (oex, oey)
            k = kind(net)
            if k == "power":
                lib_id = power[net]
                psym = N.load(lib_id)
                (pnum,) = psym.pins      # a power symbol has exactly one pin
                srot = (rot - (90 if _points_up(psym) else 270)) % 360
                if rot in (0, 180):
                    # Value text centred (no justify: KiCad mirrors justify on
                    # text it flips, which put the text on top of the arrow)
                    # just past the graphic; the field angle is relative to the
                    # symbol, so 90 on a symbol turned 90/270 reads horizontal.
                    tx, ty = _along(ex, ey, rot, POWER_TEXT_GAP + len(net) * CHAR_W / 2)
                else:
                    tx, ty = _along(ex, ey, rot, POWER_LEN)
                ref = "#PWR%04d" % next(pwr_counter)
                pprops = [_prop("Reference", ref, ex, ey, hide=True),
                          _prop("Value", net, tx, ty, justify=None,
                                angle=90 if srot in (90, 270) else 0),
                          _prop("Footprint", "", ex, ey, hide=True),
                          _prop("Datasheet", "", ex, ey, hide=True)]
                out.append(_symbol(lib_id, ref, 1, ex, ey, srot, False, False, False,
                                   uuids("pwr", *key), pprops,
                                   [(pnum, uuids("pwrpin", *key))], project, path))
            elif k == "global":
                out.append(_global_label(net, ex, ey, rot, uuids("label", *key)))
            else:
                out.append(_local_label(net, ex, ey, rot, uuids("label", *key)))
        for number in sorted(unit_pins(p, u), key=ksexp._pin_sort_key):
            if number not in p.nets:
                cx, cy = pin_point(px, py, p.sym.pin(number))
                out.append('\t(no_connect (at %s %s) (uuid "%s"))\n'
                           % (_n(cx), _n(cy), uuids("nc", p.ref, number)))
    return out


def _derived_node(sym):
    """The base's drawing with the derived symbol's own fields.

    KiCad compares the embedded copy with the library's flattened symbol, in
    which a derived symbol's properties (Value, Description, Datasheet,
    footprint filters) override its base's. Embedding the base's fields made
    ERC report lib_symbol_mismatch on every derived part (TL072 in the demo;
    AMS1117, SS14 and 74HC165 on Rev A).
    """
    own = {str(c[1]): c for c in ksexp.children(sym.node, "property")}
    out, placed_extra = [], False
    for c in sym.base.node:
        is_prop = isinstance(c, list) and c and c[0] == "property"
        if is_prop and str(c[1]) in own:
            out.append(own.pop(str(c[1])))
            continue
        if not is_prop and not placed_extra and isinstance(c, list) and c and c[0] == "symbol":
            out.extend(own.values())       # derived-only fields before the units
            own, placed_extra = {}, True
        out.append(c)
    return out


def lib_symbols(lib_ids):
    chunks = []
    for lib_id in sorted(lib_ids):
        sym = N.load(lib_id)
        node = _derived_node(sym) if sym.extends else sym.node
        body = ksexp.dump(node, 2)
        # Naming, and it is asymmetric in a way that costs an afternoon if
        # guessed: the HEAD carries the library prefix ("74xx:74HC595"), the
        # unit sub-symbols must NOT ("74HC595_0_1"). Putting the prefix on the
        # units makes KiCad refuse the whole file with nothing but "could not
        # load schematic". For a derived symbol the units also move to the
        # DERIVED name: 74xx:74HC165 (extends "74LS165") is drawn by units
        # called 74LS165_1_1, which have to become 74HC165_1_1.
        base = sym.extends or sym.name
        body = body.replace('(symbol "%s_' % base, '(symbol "%s_' % sym.name)
        body = body.replace('(symbol "%s"' % base, '(symbol "%s"' % lib_id, 1)
        chunks.append("\t\t" + body)
    return "\n".join(chunks)


def _document(file_uuid, paper, title, comments, lib_ids, body, root):
    tb = '\t\t(title "%s")\n\t\t(rev "draft")\n' % _esc(title)
    tb += "".join('\t\t(comment %d "%s")\n' % (i + 1, _esc(c))
                  for i, c in enumerate(comments))
    tail = '\t(sheet_instances\n\t\t(path "/" (page "1"))\n\t)\n' if root else ""
    return ('(kicad_sch\n\t(version 20250114)\n\t(generator "fireflow-gen")\n'
            '\t(generator_version "10.0")\n\t(uuid "%s")\n\t(paper "%s")\n'
            '\t(title_block\n%s\t)\n\t(lib_symbols\n%s\n\t)\n%s%s'
            '\t(embedded_fonts no)\n)\n'
            % (file_uuid, paper, tb, lib_symbols(lib_ids), "".join(body), tail))


def _sheet_box(sheet, index, sheet_uuid, text_uuid, project, root_uuid):
    col, row = index % 4, index // 4
    x, y = snap(MARGIN + col * 95.0), snap(MARGIN + 30.0 + row * 45.0)
    w, h = 76.2, 25.4
    return ('\t(sheet\n\t\t(at %s %s)\n\t\t(size %s %s)\n\t\t(exclude_from_sim no)\n'
            '\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
            '\t\t(stroke (width 0) (type solid))\n\t\t(fill (color 0 0 0 0.0000))\n'
            '\t\t(uuid "%s")\n'
            '\t\t(property "Sheetname" "%s"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(effects (font (size %s %s)) (justify left bottom)))\n'
            '\t\t(property "Sheetfile" "%s.kicad_sch"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(effects (font (size %s %s)) (justify left top)))\n'
            '\t\t(instances\n\t\t\t(project "%s"\n'
            '\t\t\t\t(path "/%s" (page "%d")))))\n'
            '\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n'
            '\t\t(effects (font (size %s %s)) (justify left))\n\t\t(uuid "%s"))\n'
            % (_n(x), _n(y), _n(w), _n(h), sheet_uuid,
               sheet.name, _n(x), _n(y - 0.75), FONT, FONT,
               sheet.name, _n(x), _n(y + h + 0.6), FONT, FONT,
               project, root_uuid, index + 2,
               _esc(sheet.title), _n(x + 2.54), _n(y + h / 2), FONT, FONT, text_uuid))


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def write_project(project, out_dir):
    """Write <project>.kicad_sch (and one file per sheet unless flat).

    Returns {sheet name: (placed, height)} for the checks.
    """
    os.makedirs(out_dir, exist_ok=True)
    uuids = Uuids(project.name)
    sheet_of = project.sheet_of()
    net_sheets = {}
    for p in project.parts():
        for net in p.nets.values():
            net_sheets.setdefault(net, set()).add(sheet_of[p.ref])

    def kind(net):
        if net in project.power:
            return "power"
        if project.flat or len(net_sheets[net]) > 1:
            return "global"
        return "local"

    pwr = itertools.count(1)
    root_uuid = uuids("root")
    result = {}

    def body_for(sheet, path):
        placed, height = layout(sheet.parts, project.paper, project.power)
        result[sheet.name] = (placed, height)
        body = emit_items(sheet.parts, placed, uuids, project.name, path, kind,
                          project.power, pwr)
        lib_ids = {p.lib_id for p in sheet.parts}
        lib_ids |= {project.power[n] for p in sheet.parts
                    for n in p.nets.values() if n in project.power}
        return body, lib_ids

    if project.flat:
        sheet = project.sheets[0]
        body, lib_ids = body_for(sheet, "/" + root_uuid)
        _write(os.path.join(out_dir, project.name + ".kicad_sch"),
               _document(root_uuid, project.paper, project.title,
                         project.comments, lib_ids, body, root=True))
        return result

    boxes = []
    for i, sheet in enumerate(project.sheets):
        sheet_uuid = uuids("sheet", sheet.name)
        boxes.append(_sheet_box(sheet, i, sheet_uuid, uuids("sheet-text", sheet.name),
                                project.name, root_uuid))
        body, lib_ids = body_for(sheet, "/%s/%s" % (root_uuid, sheet_uuid))
        _write(os.path.join(out_dir, sheet.name + ".kicad_sch"),
               _document(uuids("file", sheet.name), project.paper,
                         "%s: %s" % (project.title, sheet.title),
                         project.comments, lib_ids, body, root=False))
    _write(os.path.join(out_dir, project.name + ".kicad_sch"),
           _document(root_uuid, project.paper, project.title, project.comments,
                     set(), boxes, root=True))
    return result
