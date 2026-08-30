#!/usr/bin/env python3
"""Emit coupon.kicad_sch from netlist.build().

Style: every pin gets a short stub and a global label. No wires run between
symbols. That is deliberate -- a generated rat's nest of wires is unreadable and
hard to get right, while labels are exactly as connected and say the net name at
every pin. It is also how large hand-drawn sheets are usually organised.

Connectivity is NOT trusted to this file's geometry. build.py exports the
netlist with kicad-cli and compares it against netlist.build(); a wrong stub
direction or a Y-flip mistake shows up there as a missing node, not as a board
that is quietly wrong.
"""
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import ksexp
import netlist as N

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, ".."))
SCH_PATH = os.path.join(OUT_DIR, "coupon.kicad_sch")

GRID = 1.27          # mm. Every placement snaps to it, or ERC reports each pin
                     # as "endpoint off connection grid" -- 344 of them on the
                     # first pass, purely from float placement.
STUB = 3.81          # mm, pin connection point to label anchor (3 x GRID)
GUTTER_X = 12.0
GUTTER_Y = 10.0
TEXT_LINE = 2.54     # one line of reference/value text above the symbol
MARGIN = 20.0
SHEET_W = 594.0      # A2
SHEET_H = 420.0

def intentional_no_connects(parts):
    """Every pin the netlist does not assign gets a no-connect marker.

    netlist.py is the source of truth, so an unassigned pin is a decision, not
    an omission -- and saying so explicitly is what keeps ERC's "pin not
    connected" list empty and therefore worth reading.
    """
    return [(p.ref, pin) for p in parts for pin in p.unconnected()]


def uid():
    return str(uuid.uuid4())


def sym_extent(sym):
    xs = [p["x"] for p in sym.pins.values()]
    ys = [p["y"] for p in sym.pins.values()]
    return (min(xs), max(xs), min(ys), max(ys))


def pin_point(part_x, part_y, pin):
    """Pin connection point in schematic coordinates (+Y down)."""
    return part_x + pin["x"], part_y - pin["y"]


def stub_end(cx, cy, angle):
    """Where the label sits: outward from the body, i.e. along the pin."""
    if angle == 0:            # pin body lies to the east; free end faces west
        return cx - STUB, cy, 180
    if angle == 180:
        return cx + STUB, cy, 0
    if angle == 90:           # symbol-space north -> schematic north is -Y
        return cx, cy + STUB, 270
    if angle == 270:
        return cx, cy - STUB, 90
    raise ValueError("unexpected pin angle %r" % angle)


LABEL_SIZE = 1.0
CHAR_W = 0.75 * LABEL_SIZE     # KiCad stroke font advance, near enough
LABEL_PAD = 3.0                # the global label's arrow shape


def label_width(net):
    return len(net) * CHAR_W + LABEL_PAD


def label_groups(part, px=0.0, py=0.0):
    """One stub and one label per distinct connection point.

    Electrosmith's Daisy_Patch_SM stacks A4 and A7 -- both GND -- on exactly the
    same coordinate, which is a normal way to draw multiple ground pins. Emitting
    a stub and a label per pin then double-strikes the text in place. Pins that
    share a point but carry DIFFERENT nets are a short, not a drawing question,
    so that raises.
    """
    groups = {}
    for number, net in part.nets.items():
        pin = part.sym.pin(number)
        key = (round(pin["x"], 4), round(pin["y"], 4), pin["angle"])
        if key in groups and groups[key][0] != net:
            raise ValueError(
                "%s: pins %s and %s share a connection point but carry "
                "different nets (%s vs %s) -- that is a short"
                % (part.ref, groups[key][1][0], number, groups[key][0], net))
        groups.setdefault(key, (net, []))[1].append(number)
    return [(pin_point(px, py, dict(x=k[0], y=k[1])), k[2], net, nums)
            for k, (net, nums) in sorted(groups.items())]


def label_margins(part):
    """How far the labels reach beyond the symbol, on all four sides.

    Two things this got wrong before, both of them visible in the PDF and
    invisible in the netlist. The cell width was once a flat gutter, so a
    25 mm net name like MUX16_CH15 drove the 4067's right-hand labels straight
    through the 4051's left-hand ones. And VERTICAL labels were charged to the
    width instead of the height, so two-pin parts in adjacent rows overlapped
    each other's labels while the arithmetic said there was room.
    """
    left = right = top = bottom = 0.0
    half = LABEL_SIZE * 1.6 / 2
    for number, net in part.nets.items():
        angle = part.sym.pin(number)["angle"]
        w = label_width(net) + STUB
        if angle == 0:            # stub west, text west
            left = max(left, w)
        elif angle == 180:        # stub east, text east
            right = max(right, w)
        elif angle == 270:        # connection at the top, text runs up
            top = max(top, w)
            left = max(left, half)
            right = max(right, half)
        else:                     # angle 90: connection at the bottom
            bottom = max(bottom, w)
            left = max(left, half)
            right = max(right, half)
    return left, right, top, bottom


def layout(parts):
    """Pack parts into rows; return {ref: (x, y)} and the height needed."""
    placed = {}
    x, y, row_h = MARGIN, MARGIN, 0.0
    for p in parts:
        x0, x1, y0, y1 = sym_extent(p.sym)
        lw, rw, tw, bw = label_margins(p)
        w = (x1 - x0) + lw + rw + GUTTER_X
        # the two text lines sit above everything the part draws, labels included
        h = (y1 - y0) + tw + bw + GUTTER_Y + 2 * TEXT_LINE
        if x + w > SHEET_W - MARGIN:
            x = MARGIN
            y += row_h
            row_h = 0.0
        placed[p.ref] = (snap(x + lw - x0),
                         snap(y + 2 * TEXT_LINE + tw + y1))
        x += w
        row_h = max(row_h, h)
    return placed, y + row_h + MARGIN


def text_anchor(part, px, py):
    """Where reference and value go: above everything the part draws.

    Not a fixed offset from the origin -- a tall symbol carries its origin in
    the middle, which printed "U_MUX16" inside the CD74HC4067M's body -- and not
    just above the symbol either, because a two-pin part's top label runs
    upwards through exactly that space.
    """
    _, _, _, y1 = sym_extent(part.sym)
    _, _, tw, _ = label_margins(part)
    top = py - y1 - tw
    return snap(top - 2 * TEXT_LINE), snap(top - TEXT_LINE)


def snap(v):
    return round(round(v / GRID) * GRID, 4)


# --- collision checking -----------------------------------------------------
# Added after the first PDF: labels from one symbol ran through the labels of
# the next, and reference text sat inside tall symbol bodies. Both are obvious
# in the drawing and invisible in the netlist, which is exactly the class of
# defect a generator produces. Checking by eye finds some of them; this finds
# all of them.

def _boxes(parts, placed):
    """Every drawn rectangle, as (x0, y0, x1, y1, description)."""
    out = []
    for p in parts:
        px, py = placed[p.ref]
        if p.ref.startswith("#"):
            continue                      # power flags draw no ref/value text
        x0, x1, y0, y1 = sym_extent(p.sym)
        out.append((px + x0, py - y1, px + x1, py - y0, "%s body" % p.ref))
        ref_y, val_y = text_anchor(p, px, py)
        for text, ty in ((p.ref, ref_y), (p.value, val_y)):
            w = len(str(text)) * 0.75 * 1.27
            out.append((px, ty - 0.9, px + w, ty + 0.9,
                        "%s text %r" % (p.ref, text)))
        for (cx, cy), angle, net, _nums in label_groups(p, px, py):
            ex, ey, rot = stub_end(cx, cy, angle)
            w = label_width(net)
            h = LABEL_SIZE * 1.6
            # A rotated global label is drawn along its own axis: rot 90/270
            # boxes are narrow and tall, not wide and flat. Modelling them the
            # wrong way round invents collisions that are not on the page and
            # hides the ones that are.
            if rot == 180:                # text runs left from the anchor
                box = (ex - w, ey - h / 2, ex, ey + h / 2)
            elif rot == 0:                # text runs right
                box = (ex, ey - h / 2, ex + w, ey + h / 2)
            elif rot == 90:               # 90 deg CCW from +x is UP (+y is down)
                box = (ex - h / 2, ey - w, ex + h / 2, ey)
            else:                         # rot 270: text runs down
                box = (ex - h / 2, ey, ex + h / 2, ey + w)
            out.append(box + ("%s.%s label %s"
                              % (p.ref, "/".join(_nums), net),))
    return out


def overlaps(parts, placed, tolerance=0.2):
    """Pairs of drawn boxes that intersect by more than `tolerance` mm."""
    boxes = _boxes(parts, placed)
    boxes.sort(key=lambda b: b[0])
    hits = []
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if b[0] >= a[2] - tolerance:
                break                     # sorted by x0: nothing further can hit
            if (a[0] < b[2] - tolerance and b[0] < a[2] - tolerance
                    and a[1] < b[3] - tolerance and b[1] < a[3] - tolerance):
                hits.append((a[4], b[4]))
    return hits


def emit(parts, placed, sheet_uuid):
    used = {}
    for p in parts:
        used.setdefault(p.lib_id, p.sym)

    chunks = []
    for lib_id, sym in sorted(used.items()):
        node = sym.base.node if sym.extends else sym.node
        body = ksexp.dump(node, 2)
        # Naming, and it is asymmetric in a way that costs an afternoon if
        # guessed: the HEAD carries the library prefix ("74xx:74HC595"), the
        # unit sub-symbols must NOT ("74HC595_0_1"). Putting the prefix on the
        # units makes KiCad refuse the whole file with nothing but "could not
        # load schematic" -- verified by emitting the same symbol under four
        # names and loading each.
        #
        # For a derived symbol the units also have to move to the DERIVED name:
        # 74xx:74HC165 (extends "74LS165") is drawn by units called 74LS165_1_1,
        # and they have to become 74HC165_1_1 or the head and the units no
        # longer refer to each other.
        base = sym.extends or sym.name
        body = body.replace('(symbol "%s_' % base, '(symbol "%s_' % sym.name)
        body = body.replace('(symbol "%s"' % base, '(symbol "%s"' % lib_id, 1)
        chunks.append("\t\t" + body)
    lib_block = "\n".join(chunks)

    nc = set(intentional_no_connects(parts))
    body = []
    for p in parts:
        px, py = placed[p.ref]
        hidden_ref = p.ref.startswith("#")
        # Above the symbol's own top edge, not at a fixed offset from the
        # origin: a tall symbol has its origin in the middle, which is how
        # "U_MUX16" ended up printed inside the CD74HC4067M's body.
        ref_y, val_y = text_anchor(p, px, py)
        body.append(f'''	(symbol
		(lib_id "{p.lib_id}")
		(at {px} {py} 0)
		(unit 1)
		(exclude_from_sim no)
		(in_bom {"no" if hidden_ref else "yes"})
		(on_board {"no" if hidden_ref else "yes"})
		(dnp no)
		(uuid "{uid()}")
		(property "Reference" "{p.ref}"
			(at {px} {ref_y} 0)
			(effects (font (size 1.27 1.27)) (justify left) {"(hide yes)" if hidden_ref else ""}))
		(property "Value" "{p.value}"
			(at {px} {val_y} 0)
			(effects (font (size 1.27 1.27)) (justify left) {"(hide yes)" if hidden_ref else ""}))
		(property "Footprint" "{p.footprint}"
			(at {px} {py} 0)
			(effects (font (size 1.27 1.27)) (hide yes)))
		(property "Datasheet" ""
			(at {px} {py} 0)
			(effects (font (size 1.27 1.27)) (hide yes)))
		(property "Description" "{p.note}"
			(at {px} {py} 0)
			(effects (font (size 1.27 1.27)) (hide yes)))
''')
        for number in sorted(p.sym.pins, key=ksexp._pin_sort_key):
            body.append(f'		(pin "{number}" (uuid "{uid()}"))\n')
        body.append(f'''		(instances
			(project "coupon"
				(path "/{sheet_uuid}" (reference "{p.ref}") (unit 1))))
	)
''')
        for (cx, cy), angle, net, _nums in label_groups(p, px, py):
            ex, ey, label_rot = stub_end(cx, cy, angle)
            body.append(f'''	(wire (pts (xy {cx} {cy}) (xy {ex} {ey}))
		(stroke (width 0) (type default)) (uuid "{uid()}"))
	(global_label "{net}"
		(shape bidirectional)
		(at {ex} {ey} {label_rot})
		(fields_autoplaced yes)
		(effects (font (size 1.0 1.0)) (justify {"right" if label_rot == 180 else "left"}))
		(uuid "{uid()}")
		(property "Intersheetrefs" "${{INTERSHEET_REFS}}"
			(at {ex} {ey} 0)
			(effects (font (size 1.27 1.27)) (hide yes))))
''')
        for number in p.sym.pins:
            if (p.ref, number) in nc:
                pin = p.sym.pin(number)
                cx, cy = pin_point(px, py, pin)
                body.append(f'	(no_connect (at {cx} {cy}) (uuid "{uid()}"))\n')

    return f'''(kicad_sch
	(version 20250114)
	(generator "fireflow-coupon")
	(generator_version "10.0")
	(uuid "{sheet_uuid}")
	(paper "A2")
	(title_block
		(title "FireFlow test coupon")
		(rev "draft")
		(comment 1 "GENERATED by hardware/coupon/scripts/generate_schematic.py -- do not edit by hand")
		(comment 2 "Source of truth: scripts/design.py and scripts/netlist.py")
	)
	(lib_symbols
{lib_block}
	)
{"".join(body)}	(sheet_instances
		(path "/" (page "1"))
	)
	(embedded_fonts no)
)
'''


def main():
    parts = N.build()
    placed, needed_h = layout(parts)
    if needed_h > SHEET_H:
        print("note: content is %.0f mm tall, A2 is %.0f -- widen or split"
              % (needed_h, SHEET_H))
    doc = emit(parts, placed, uid())
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(SCH_PATH, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(doc)
    print("wrote %s (%d parts, %.0f mm of content)"
          % (os.path.relpath(SCH_PATH), len(parts), needed_h))


if __name__ == "__main__":
    main()
