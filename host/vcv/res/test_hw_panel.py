"""Contract tests for the hardware-mode panel (envelope spec 2026-08-08 §4).
These test CONSTRAINTS, not taste: regrouping iterations may move anything,
but can never violate size, keep-outs, footprints, or static lettering.

No pytest in this environment -- plain asserts (and check() for the slot-map
guard below), exit code says it all. Run from host/vcv/: python res/test_hw_panel.py
"""
import os, re, sys
import gen_panel as gp
import gen_hw_panel as hw

HERE = os.path.dirname(os.path.abspath(__file__))

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)

def test_panel_is_60hp():
    assert hw.HP == 60
    assert abs(hw.W - 60 * gp.MM_PER_HP) < 1e-9
    assert hw.Hh == 128.5

def test_same_runtime_params_same_order():
    # The hw panel is the SAME instrument: identical enum set, identical order.
    # MODBTN is the one exception -- it is a real latch param now (spec
    # 2026-08-22 mod-latch-layer §5) but lives outside RUNTIME_PANEL_PARAMS,
    # so the big panel never draws it. gen_hw_panel.py appends it explicitly.
    assert [c.enum for c in hw.HW_PARAMS] == \
        [c.enum for c in gp.RUNTIME_PANEL_PARAMS] + ["MODBTN"]
    assert [c.enum for c in hw.HW_INPUTS] == (
        [c.enum for c in gp.INPUTS] + [c.enum for c in gp.HW_MOD_INPUTS])
    assert [c.enum for c in hw.HW_OUTPUTS] == [c.enum for c in gp.OUTPUTS]
    flow = {"FLOW_A_L", "FLOW_B_L"}
    assert [c.enum for c in hw.HW_LIGHTS] == \
           [c.enum for c in gp.LIGHTS] + \
           [c.enum for c in gp.HW_ONLY_LIGHTS if c.enum not in flow]

def test_static_captions_only():
    # An aluminium panel is printed: every label is the resting word, and the
    # generated header must carry NO dynamic-caption table.
    for c in hw.HW_PARAMS:
        words = gp.dynamic_words(c.enum.rsplit("_", 1)[0])
        if words:
            assert c.label == words[0], c.enum
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    assert "DynCaption" not in src

def test_hardware_footprints():
    # Screen-widget radii are meaningless on sheet metal, and so is the
    # screen widget's KIND: it says bipolar/detented, not big/small. The
    # minimum clearance radius comes from the hardware size class
    # (spec 2026-08-10 §1).
    for c in hw.ALL_HW:
        want = hw.CLASS_R[hw.hw_class(c.enum)]
        assert c.r >= want - 1e-9, (c.enum, c.r, want)

def test_rail_keepout():
    # Rails and screws own the top/bottom ~9 mm; VCV's 2 mm rule is not enough.
    # Status-strip small knobs at y=14.5, r=6 sit 0.5 mm into KEEP_TOP — the
    # approved drawing, same nibble SCALE/DRIFT already had (spec 2026-08-10
    # §8 nachtrag). Bodies still cannot cross the rail by more than that.
    for c in hw.ALL_HW:
        assert c.y - c.r >= hw.KEEP_TOP - 0.5 - 1e-9, (c.enum, "top")
        assert c.y + c.r <= hw.KEEP_BOT + 1e-9, (c.enum, "bottom")
        assert c.x - c.r >= 2.0 and c.x + c.r <= hw.W - 2.0, (c.enum, "side")
    # Static lettering (hw.TEXTS) used to be invisible to this guard: nothing
    # stopped a title or legend from being placed inside a keep-out rail.
    # Text has no radius, so it is checked as a bare point.
    for (x, y, size, spacing, col, anchor, txt) in hw.TEXTS:
        check(y >= hw.KEEP_TOP - 1e-9, f"text {txt!r} at y={y} crosses the top rail")
        check(y <= hw.KEEP_BOT + 1e-9, f"text {txt!r} at y={y} crosses the bottom rail")
        check(x >= 2.0 and x <= hw.W - 2.0, f"text {txt!r} at x={x} crosses the side keepout")

def test_no_overlap_with_hw_radii():
    # Radius-sum clearance with REAL footprints. Legal overlaps:
    # BEND shares ATTACK's knob (deliberate dual assignment, spec §1), and a
    # knob-owned lamp sits inside its own knob's CLASS_R circle -- that
    # circle is finger spacing between controls, not between a knob and its
    # indicator. GATE is at ATTACK, so it also shares the shaft with STAGES.
    SHARED_OK = {frozenset(("ATTACK_A", "STAGES_A")),
                 frozenset(("ATTACK_B", "STAGES_B"))}
    for lamp, knob in hw.KNOB_LAMPS.items():
        SHARED_OK.add(frozenset((lamp, knob)))
        if knob.startswith("ATTACK_"):
            SHARED_OK.add(frozenset((lamp, knob.replace("ATTACK", "STAGES"))))
    items = hw.ALL_HW
    for i, a in enumerate(items):
        for b in items[i + 1:]:
            if frozenset((a.enum, b.enum)) in SHARED_OK:
                continue
            d = ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5
            assert d >= a.r + b.r - 1e-6, (a.enum, b.enum, round(d, 2))


# Real caps, written HERE and not read from hw.BODY_R: the 9 mm rule is a
# promise about the bought parts (spec 2026-10-07 §2), and a guard that
# imports its radii from the generator would loosen with it.
CAP_R = {"G": 6.0, "S": 3.85, "P": 3.0}
MIN_CAP_GAP = 9.0


def _cap_items():
    """Every knob position (params and reserved, ATTACK/STAGES once) and
    every key, as (enum, class, x, y)."""
    seen, out = set(), []
    for c in hw.HW_PARAMS + hw.HW_ONLY:
        cls = hw.hw_class(c.enum)
        if cls not in CAP_R:
            continue
        key = (round(c.x, 6), round(c.y, 6))
        if key in seen:
            continue
        seen.add(key)
        out.append((c.enum, cls, c.x, c.y))
    return out


def _cap_gap_failures(items):
    bad = []
    for i, (ea, ca, xa, ya) in enumerate(items):
        for eb, cb, xb, yb in items[i + 1:]:
            gap = ((xa - xb) ** 2 + (ya - yb) ** 2) ** 0.5 - CAP_R[ca] - CAP_R[cb]
            if gap < MIN_CAP_GAP - 1e-6:
                bad.append(f"{ea}/{eb} caps {gap:.2f} mm apart")
    return bad


def test_nine_mm_between_caps():
    """Spec 2026-10-07 §1: no two caps closer than 9 mm, edge to edge."""
    items = _cap_items()
    knobs = [i for i in items if i[1] in ("G", "S")]
    check(len(knobs) == 73, f"{len(knobs)} knob positions, expected 73")
    check(sum(i[1] == "G" for i in knobs) == 14, "expected 14 big caps")
    check(sum(i[1] == "P" for i in items) == 4, "expected 4 keys")
    for f in _cap_gap_failures(items):
        check(False, f)
    # The guard proves it can fail on every run: SHAPE_A moved 2.0 mm toward
    # MOD_A must break the rule (the real gap is 10.35).
    moved = [(e, c, x - 2.0 if e == "SHAPE_A" else x, y) for e, c, x, y in items]
    check(_cap_gap_failures(moved), "the 9 mm guard did not see SHAPE_A moved onto MOD_A")


def test_cells_are_the_source():
    """Every knob and key stands on its cell (spec 2026-10-07 §3)."""
    by = {c.enum: c for c in hw.HW_PARAMS + hw.HW_ONLY}
    seen = 0
    for stem, (row, col) in hw.DECK_CELLS.items():
        for side in "AB":
            c = by.get(f"{stem}_{side}")
            if c is None:
                check(False, f"{stem}_{side} has a cell but no control")
                continue
            x = hw.deck_col_x(col) if side == "A" else hw.W - hw.deck_col_x(col)
            check(abs(c.x - x) < 1e-9 and abs(c.y - hw.ROW_Y[row - 1]) < 1e-9,
                  f"{c.enum} at ({c.x:.3f},{c.y:.3f}), cell says ({x:.3f},{hw.ROW_Y[row - 1]:.3f})")
            seen += 1
    for name, (row, k) in hw.CENTRE_CELLS.items():
        c = by.get(name)
        if c is None:
            check(False, f"{name} has a cell but no control")
            continue
        check(abs(c.x - hw.centre_col_x(k)) < 1e-9 and abs(c.y - hw.ROW_Y[row - 1]) < 1e-9,
              f"{name} is not on its centre cell")
        seen += 1
    check(seen == 2 * len(hw.DECK_CELLS) + len(hw.CENTRE_CELLS), "cells not all checked")
    # Big caps: rows 2 and 4 only, never deck column 6, never side by side.
    for stem, (row, col) in hw.DECK_CELLS.items():
        if hw.HW_SIZE.get(stem) == "G":
            check(row in (2, 4) and col != 6, f"big cap {stem} at R{row} c{col}")
            for other, (r2, c2) in hw.DECK_CELLS.items():
                if other != stem and hw.HW_SIZE.get(other) == "G" and r2 == row:
                    check(abs(c2 - col) > 1, f"big caps {stem} and {other} side by side")
    occupied = {(r, k) for r, k in hw.CENTRE_CELLS.values()}
    check(all((r, -k) in occupied for r, k in occupied), "the centre is not mirror-symmetric")
    for name, (row, k) in hw.CENTRE_CELLS.items():
        if hw.HW_SIZE.get(name) == "G":
            check(row in (2, 4) and k == 0, f"big cap {name} off the centre line or row")
    check(abs(hw.deck_col_x(1) - 11.0) < 1e-9 and abs(hw.deck_col_x(6) - 112.0) < 1e-9,
          "deck columns drifted from 11.00 .. 112.00")
    # The centre runs on its own 23.0 mm pitch (spec §3, amended 2026-10-08):
    # what gives the Patch Submodule a spot on the board. Written here, not
    # read from hw.CENTRE_PITCH, so a generator edit cannot move it unseen.
    want = {-1: 129.4, 0: 152.4, 1: 175.4}
    check(all(abs(hw.centre_col_x(k) - x) < 1e-9 for k, x in want.items()),
          f"centre columns drifted from 129.40 / 152.40 / 175.40: "
          f"{[round(hw.centre_col_x(k), 3) for k in (-1, 0, 1)]}")


def test_fields_keep_box_gap():
    """Fields of different groups keep BOX_GAP; deck B mirrors deck A
    (spec 2026-10-07 §7)."""
    boxes = hw.BOXES
    check(len(boxes) == 26, f"expected 26 group fields, got {len(boxes)}")
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            g = a.gap_to(b)
            check(g >= hw.BOX_GAP - 1e-6, f"{a.n}/{a.side} and {b.n}/{b.side} are {g:.2f} mm apart")
    by = {(b.n, b.side): b for b in boxes}
    for name in hw.DECK_GROUPS:
        a, b = by.get((name, "A")), by.get((name, "B"))
        if a is None or b is None:
            check(False, f"{name} lacks a deck field")
            continue
        mir = sorted((round(hw.W - r[1], 6), round(hw.W - r[0], 6), round(r[2], 6), round(r[3], 6))
                     for r in a.rects)
        own = sorted(tuple(round(v, 6) for v in r) for r in b.rects)
        check(mir == own, f"{name}: deck B field is not deck A mirrored")
    check({b.stem for b in boxes} == set(hw.GROUP_ORDER),
          f"fields and GROUP_ORDER disagree: { {b.stem for b in boxes} ^ set(hw.GROUP_ORDER)}")


def test_column_cells_follow_the_centre_pitch():
    """Spec §7 (amended 2026-10-08): the column cell's half-width, side by
    side, written here as literals. Between the centre columns (23.0 - 3.0)
    / 2 = 10.0; the outer centre columns' outward side (toward the deck) has
    no column-cell minimum; deck column 6 keeps (20.2 - 3.0) / 2 = 8.6."""
    from types import SimpleNamespace as At
    want = {  # x: (left, right)
        129.4: (0.0, 10.0), 152.4: (10.0, 10.0), 175.4: (10.0, 0.0),
        112.0: (8.6, 8.6), 192.8: (8.6, 8.6), 11.0: (8.6, 8.6),
    }
    for x, (left, right) in want.items():
        got = (hw._cell_half(At(x=x), -1), hw._cell_half(At(x=x), +1))
        check(abs(got[0] - left) < 1e-9 and abs(got[1] - right) < 1e-9,
              f"column cell at x {x}: half-widths {got}, want {(left, right)}")


def _twin_enum(enum):
    """Name-declared mirror partner, or None if the name declares none.

    Two shapes occur in this inventory: a trailing _A/_B (RATE_A, PITCH_B)
    and an embedded _A_/_B_ segment for names that end in something else
    (ENG0_A_L, REC_A_L, CV_FILT_A is caught by the suffix rule already).
    Names carrying neither marker (centre controls, MODBTN/SHIFTBTN,
    IN_L/IN_R, CLOCK, RESET, ...) have no naming-declared twin and are left
    alone -- pairing them would be a guess, not a check."""
    if enum.endswith("_A"):
        return enum[:-2] + "_B"
    if enum.endswith("_B"):
        return enum[:-2] + "_A"
    if "_A_" in enum:
        return enum.replace("_A_", "_B_", 1)
    if "_B_" in enum:
        return enum.replace("_B_", "_A_", 1)
    return None


def _mirror_pairs(items):
    """Yield (enum_a, a, enum_b, b) once per name-declared mirror pair found
    in items (a list of objects with .enum, .x, .y)."""
    by_enum = {c.enum: c for c in items}
    seen = set()
    for enum, a in by_enum.items():
        twin = _twin_enum(enum)
        if twin is None or twin not in by_enum:
            continue
        key = frozenset((enum, twin))
        if key in seen:
            continue
        seen.add(key)
        yield enum, a, twin, by_enum[twin]


def test_mirror_symmetry():
    # Deck B is deck A mirrored -- the instrument's identity, kept by
    # machine. Covers every hand-written mirror (JACK_POS, LIGHT_POS, the
    # HW_ONLY loop), not only the DECK_POS-derived HW_PARAMS: those are
    # equally "the instrument's identity" and were equally unchecked.
    # Cluster lamps included: since 2026-10-08 deck B's stand mirrored too.
    items = hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_LIGHTS + hw.HW_ONLY
    pairs = 0
    for enum, a, twin, b in _mirror_pairs(items):
        check(abs((hw.W - a.x) - b.x) < 1e-6,
              f"{enum}/{twin}: x does not mirror ({a.x:.2f} vs {b.x:.2f})")
        check(abs(a.y - b.y) < 1e-6,
              f"{enum}/{twin}: y does not match ({a.y:.2f} vs {b.y:.2f})")
        pairs += 1
    check(pairs > 0, "test_mirror_symmetry found no mirror pairs -- vacuous")
    test_mirror_symmetry.pairs_checked = pairs

_MIRROR_ANCHOR = {"middle": "middle", "start": "end", "end": "start"}


def test_caption_mirror_symmetry():
    # test_mirror_symmetry above only compares CONTROL positions. Candidate 3
    # of hw_label's step-aside rule must itself mirror, or a mirrored twin
    # landing there would have one twin step toward the axis and the
    # other step off the panel edge, with nothing catching it (spec
    # 2026-08-10 §6 fix-2). Covers every hand-written mirror, not only
    # HW_PARAMS -- same widening as test_mirror_symmetry above. Cluster
    # captions are no exception since 2026-10-08: centred under their knob,
    # they mirror like every other word.
    items = hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_LIGHTS + hw.HW_ONLY
    pairs = 0
    for enum, a, twin, b in _mirror_pairs(items):
        if not a.label:
            continue
        lxa, lya, anchor_a = hw.hw_label(a)[:3]
        lxb, lyb, anchor_b = hw.hw_label(b)[:3]
        check(abs((hw.W - lxa) - lxb) < 1e-6,
              f"{enum}/{twin}: caption x does not mirror ({lxa:.2f} vs {lxb:.2f})")
        check(abs(lya - lyb) < 1e-6,
              f"{enum}/{twin}: caption y does not match ({lya:.2f} vs {lyb:.2f})")
        check(_MIRROR_ANCHOR[anchor_a] == anchor_b,
              f"{enum}/{twin}: anchors do not mirror ({anchor_a!r} vs {anchor_b!r})")
        pairs += 1
    check(pairs > 0, "test_caption_mirror_symmetry found no mirror pairs -- vacuous")
    test_caption_mirror_symmetry.pairs_checked = pairs


def test_header_contract():
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    assert "namespace spkyhw" in src
    assert "kHwHP = 60" in src
    for tbl in ("kParamCtls", "kInputCtls", "kOutputCtls", "kLightCtls",
                "kPanelTexts"):
        assert tbl in src, tbl
    # Ids are the MAIN module's enum values — one id space, two layouts.
    assert re.search(r"\{\s*RATE_A\s*,", src)
    # The rehearsal widget must pick knob size from the HARDWARE class, not
    # from c.kind -- otherwise Rack shows a big RATE while the plate prints a
    # small one (spec 2026-08-10 §1).
    assert "kParamSize" in src, "kParamSize missing from the hw header"
    body = src.split("kParamSize[] = {")[1].split("};")[0]
    vals = [v.strip() for v in body.replace("\n", "").split(",") if v.strip()]
    assert len(vals) == len(hw.HW_PARAMS), (len(vals), len(hw.HW_PARAMS))
    want = ["1" if hw.hw_class(c.enum) == "G" else "0" for c in hw.HW_PARAMS]
    assert vals == want, "kParamSize disagrees with HW_SIZE"
    assert "kHwOnlyCtls" in src

def test_svg_exists_and_is_60hp():
    svg = open(os.path.join(HERE, "FireflowHW.svg"), encoding="utf-8").read()
    assert f'width="{hw.W:.3f}mm"' in svg and 'height="128.500mm"' in svg

def test_shared_knob_labels_do_not_coincide():
    # BEND shares ATTACK's knob, but the graphics round left the second word
    # off the plate (spec 2026-08-10 §13). Printing it above ATK lands it in
    # the TIME/VOICE gap, which is how "BLEND" showed up under RATE/VARY.
    by_enum = {c.enum: c for c in hw.HW_PARAMS}
    for side in ("_A", "_B"):
        check(not by_enum["STAGES" + side].label,
              f"STAGES{side} still prints {by_enum['STAGES' + side].label!r}")
    svg = hw.svg()
    check(">BEND</text>" not in svg, "BEND is still drawn on the HW plate")

def test_hw_slot_map_matches_the_reduced_inventory():
    """Every runtime param has a hardware slot and no slot is left pointing
    at a control that no longer exists. MODBTN's CENTER_POS slot is the one
    exception: it is a real param (a latch, spec 2026-08-22 §5) but not a
    RUNTIME_PANEL_PARAMS member -- gen_hw_panel.py places it explicitly,
    same as JACK_POS names are excused below."""
    live = {c.enum for c in gp.RUNTIME_PANEL_PARAMS}
    stems = set(hw.DECK_POS) | set(hw.CENTER_POS)
    dead = [s for s in stems
            if s not in live and f"{s}_A" not in live and s not in hw.JACK_POS
            and s != "MODBTN"]
    check(not dead, f"hw slots for controls that no longer exist: {dead}")
    check(len(hw.HW_PARAMS) == len(gp.RUNTIME_PANEL_PARAMS) + 1,
          "hw param count drifted from the shared inventory (+1 MODBTN latch)")


LBL_MARGIN = 1.5


def test_labels_stay_off_neighbour_footprints():
    # A caption may sit near its own control, but its anchor must clear every
    # OTHER control's footprint by LBL_MARGIN. Bare non-overlap is not a
    # margin: at 16 mm row spacing the default offset lands 8.1 mm from an
    # 8.0 mm knob and would pass a zero-margin test with 0.1 mm to spare.
    #
    # A control on the caption owner's own shaft is not a neighbour: STAGES
    # shares ATTACK's knob (one hole, one pot), so ATTACK's word is held to
    # the own-knob floor against it -- the radius, as
    # test_captions_stay_off_their_own_knob holds every word -- not radius +
    # LBL_MARGIN. Every small knob's centred word sits 7.45 from its shaft;
    # ATTACK's does too since its cluster word is centred (2026-10-08).
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_ONLY:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        for other in hw.ALL_HW:
            if other is c:
                continue
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            same_shaft = abs(other.x - c.x) < 1e-9 and abs(other.y - c.y) < 1e-9
            need = other.r if same_shaft else other.r + LBL_MARGIN
            check(d >= need - 1e-6,
                  f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is {d:.2f} from "
                  f"{other.enum} (needs {need:.2f})")


def test_captions_stay_off_their_own_knob():
    # The reason a shortened offset cannot be the answer: a control's own
    # radius is the floor. A caption inside its own footprint is printed ON
    # the knob (spec 2026-08-10 §6, corrected).
    for c in hw.HW_PARAMS + hw.HW_ONLY:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        d = ((lx - c.x) ** 2 + (ly - c.y) ** 2) ** 0.5
        check(d >= c.r - 1e-6,
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) sits on its own knob")


def test_committed_files_match_the_generator():
    """test_header_contract/test_svg_exists_and_is_60hp above read the
    committed files but only grep for substrings -- they would not notice
    gen_hw_panel.py being edited without being re-run (review finding
    IMPORTANT 5, same gap as the big panel and already closed there in
    test_panel.py). Compare byte-for-byte against a fresh generator run.

    hw_label() raises ValueError by design when geometry gets too tight for
    some control's caption. Calling hw.svg()/hw.header() unguarded would let
    that exception escape main()'s test loop as a traceback instead of a
    named check() failure -- diagnose the traceback, don't guard against it,
    was the old failure mode here."""
    produced = {}
    for name, fn in (("FireflowHW.svg", hw.svg), ("generated_hw_panel.hpp", hw.header)):
        try:
            produced[name] = fn()
        except ValueError as e:
            FAILS.append(f"res/gen_hw_panel.py could not build {name}: {e}")
            produced[name] = None
    for name, path in (("FireflowHW.svg", os.path.join(HERE, "FireflowHW.svg")),
                        ("generated_hw_panel.hpp",
                         os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"))):
        if produced[name] is None:
            continue
        if not os.path.exists(path):
            FAILS.append(f"{path} is missing -- run res/gen_hw_panel.py")
            continue
        with open(path, encoding="utf-8") as f:
            on_disk = f.read()
        check(on_disk == produced[name],
              f"{path} differs from the generator's output -- it was "
              "hand-edited, or the generator was changed without re-running it")


def test_every_control_has_a_size_class():
    """Die Größe eines Bedienelements ist eine Hardware-Aussage und steht in
    HW_SIZE -- nicht in c.kind. KNOBC heißt bipolar, KNOBI heißt gerastert;
    beides sagt nichts über einen Durchmesser (spec 2026-08-10 §1)."""
    for c in gp.RUNTIME_PANEL_PARAMS:
        base = c.enum[:-2] if c.enum.endswith(("_A", "_B")) else c.enum
        check(base in hw.HW_SIZE, f"no hardware size class for {base}")
    for c in hw.ALL_HW:
        assert hw.hw_class(c.enum) in ("G", "S", "P", "J", "L"), c.enum
        assert abs(c.r - hw.CLASS_R[hw.hw_class(c.enum)]) < 1e-9, (c.enum, c.r)


def test_size_classes_match_the_spec():
    """Große Kappen nach Redistribution + Grafikrunde: SOURCE/TIMB ist klein,
    ENGINE ist ein Rastpoti (spec 2026-08-10 §5/§9, TIMB-Schrumpf 15. Aug)."""
    # FILT left this set on 2026-08-19 and came back 2026-08-23. It never left
    # for room -- four controls fit the VOICE row with FILT large -- but
    # because at r=8.5 its neighbour spacing is 14.5 mm against 12 for every
    # other pair, so it could not sit on the same 13 mm pitch as the four
    # knobs directly above it. It does not sit in that pitch any more: it is
    # at the END of the lower row with column 3 left empty beside it.
    # REV_MIX/SEND left this set on 2026-08-30, and for the reason FILT once
    # did: it moved onto a 13.0 mm pitch, and a big knob wants 14.5 between
    # neighbours. Unlike FILT it is not coming back -- the LEVEL band IS the
    # pitch, so there is no "end of the row" for it to retire to.
    BIG = {"DENSITY", "MOD", "COLOR", "FLUX",
           "COMP", "MORPH", "REV_DECAY", "FILT"}
    got = {b for b, cls in hw.HW_SIZE.items() if cls == "G"}
    check(got == BIG, f"big-knob set drifted: extra={got-BIG} missing={BIG-got}")
    big_positions = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "G"]
    check(len(big_positions) == 14, f"expected 14 big positions, got {len(big_positions)}")
    small = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "S"]
    # 51 + DEPTH×2 = 53, +1 (spec 2026-07-19 pull-chord-gravity): PULL joined
    # as a small knob, HW_SIZE["PULL"]="S". FILT×2 left again 2026-08-23,
    # REV_MIX×2 joined 2026-08-30, and PAN×2 joined the same day (spec
    # 2026-08-30 pan), filling the slot beside GRIT that had been held open
    # for it.
    check(len(small) == 58, f"expected 58 small params, got {len(small)}")
    check(abs(hw.CLASS_R["G"] - 8.5) < 1e-9, "CLASS_R G is not 8.5")
    check(abs(hw.CLASS_R["S"] - 6.0) < 1e-9, "CLASS_R S is not 6.0")
    check(hw.HW_SIZE["SOURCE"] == "S", "TIMB/SOURCE is not small")
    check(hw.HW_SIZE["ENGINE"] == "S", "ENGINE is not a small knob")


def test_hw_only_inventory():
    """What exists on sheet metal but not in the VCV module: 1 pad (SHIFT,
    still reserved and inert) and three reserved small pots -- ROOT_A,
    ROOT_B and REV_MOD (spec 2026-10-07 §4): a hole, a caption and a pot,
    no ParamId. No extra LEDs. MODBTN moved out of HW_ONLY 2026-08-22 -- it
    is a real latch param now, drawn from hw.HW_PARAMS instead. 15 lamps
    drawn on the plate (`FLOW_*` stay LightIds, undrawn). The eight MOD jacks
    are real inputs (unwired), no longer HW_ONLY placeholders."""
    kinds = {}
    for c in hw.HW_ONLY:
        kinds[hw.hw_class(c.enum)] = kinds.get(hw.hw_class(c.enum), 0) + 1
    check(kinds.get("P") == 1, f"expected 1 hw-only pad, got {kinds.get('P')}")
    check(kinds.get("S") == 3,
          f"expected 3 hw-only small pots (ROOT_A, ROOT_B, REV_MOD reserved, "
          f"spec 2026-10-07 §4), got {kinds.get('S')}")
    check(kinds.get("J", 0) == 0, f"expected 0 hw-only jacks, got {kinds.get('J')}")
    check(kinds.get("L", 0) == 0, f"expected 0 hw-only LEDs, got {kinds.get('L')}")
    assert [c.enum for c in hw.HW_PARAMS] == \
        [c.enum for c in gp.RUNTIME_PANEL_PARAMS] + ["MODBTN"]
    total_leds = len([c for c in hw.ALL_HW if hw.hw_class(c.enum) == "L"])
    check(total_leds == 15, f"expected 15 LEDs on the plate, got {total_leds}")


def test_mod_wreaths():
    """Spec 2026-08-22 §5 revision 3 (2026-08-30): the accent ring is not
    plate print any more. Every knob on the plate wears the plain HW_RING
    body ring; the accent moved into the generated header as kModRing[], and
    Rack draws it only while the MOD latch is engaged. The aluminium panel
    therefore marks nothing -- that is the stated cost of variant A."""
    want = ({f"{b}_A" for b, _, _, _ in gp.MOD_DECK_TARGETS}
            | {f"{b}_B" for b, _, _, _ in gp.MOD_DECK_TARGETS}
            | {b for b, _, _, _ in gp.MOD_CENTER_TARGETS})
    check(hw.MOD_WREATHED == want,
          f"MOD_WREATHED diverged from gp tables: {hw.MOD_WREATHED ^ want}")
    svg = open(os.path.join(HERE, "FireflowHW.svg"), encoding="utf-8").read()
    # no satellite ring survives: since Option B (2026-08-29) nothing on the
    # plate uses a dash at all, so any <circle ... stroke-dasharray> left
    # would be a wreath
    check(not re.search(r'<circle[^>]*stroke-dasharray', svg),
          "a dashed satellite circle still exists on the plate")
    knobs = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) != "P"]
    for c in knobs:
        br = hw.body_r(c)
        pat = (f'<circle cx="{c.x:.3f}" cy="{c.y:.3f}" r="{br:.3f}" '
               f'fill="{hw.HW_WELL}" stroke="{hw.HW_RING}" stroke-width="0.3"/>')
        check(pat in svg, f"{c.enum} body ring is not the plain HW_RING: {pat}")
    # Only body rings are checked, not every accent stroke: the keycaps have
    # worn an accent edge at the same width since 2026-08-30 (see
    # test_pad_keycaps_are_dark_and_accented), and since 2026-10-07 they are
    # round caps too -- so key centres are excused by position.
    keys = {(hw.mm(c.x), hw.mm(c.y)) for c in hw.ALL_HW if hw.hw_class(c.enum) == "P"}
    for col in set(hw.ACC.values()):
        for m in re.finditer(r'<circle cx="([0-9.]+)" cy="([0-9.]+)"[^>]*stroke="%s" '
                             r'stroke-width="0\.3"' % col, svg):
            check((m.group(1), m.group(2)) in keys,
                  f"an accent body ring in {col} is still printed at ({m.group(1)},{m.group(2)})")
    # the master knobs are deliberately unringed
    for enum in ("MOD_A", "MOD_B"):
        check(enum not in want, f"{enum} unexpectedly in MOD_WREATHED")
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    # kModRing is parallel to kParamCtls: {rgb, radius mm}, rgb 0 = no ring.
    # Colour and radius live HERE, not in the widget -- hardcoded literals
    # duplicating ACC / ZONE_A / W are what sank the first ModDepthRing
    # (spec §5 revision 2).
    m = re.search(r"static const HwModRing kModRing\[\] = \{(.*?)\n\};", src, re.S)
    check(m is not None, "kModRing table missing from generated_hw_panel.hpp")
    if m:
        rows = re.findall(r"\{\s*(0x[0-9A-F]+|0)\s*,\s*([0-9.]+)f\s*\}", m.group(1))
        check(len(rows) == len(hw.HW_PARAMS),
              f"kModRing has {len(rows)} rows, want {len(hw.HW_PARAMS)}")
        lit = 0
        for c, (rgbs, rs) in zip(hw.HW_PARAMS, rows):
            if c.enum in want:
                lit += 1
                check(rgbs == hw.rgb(hw.ACC[hw.zone_of(c.x)]),
                      f"{c.enum} ring colour {rgbs} is not its zone accent")
                check(abs(float(rs) - hw.body_r(c)) < 1e-6,
                      f"{c.enum} ring radius {rs} != body_r {hw.body_r(c)}")
            else:
                check(rgbs == "0", f"{c.enum} carries a ring but owns no depth")
        check(lit == len(want),
              f"{lit} knobs carry an accent ring, want {len(want)}")
    check("kModRing desynced" in src, "kModRing has no length static_assert")
    # MODBTN: real param, out of kHwOnlyCtls, caption on the jack-row baseline
    check(re.search(r"\{\s*MODBTN\s*,\s*WK_LATCH", src), "MODBTN not in kParamCtls")
    hwonly = src.split("kHwOnlyCtls")[1]
    check("MODBTN" not in hwonly.split("};")[0], "MODBTN still in kHwOnlyCtls")
    mod = next(c for c in hw.HW_PARAMS if c.enum == "MODBTN")
    shift = next(c for c in hw.HW_ONLY if c.enum == "SHIFTBTN")
    my, sy = hw.hw_label(mod)[1], hw.hw_label(shift)[1]
    check(abs(my - sy) < 1e-6, f"MOD caption baseline {my} != SHFT {sy}")


def test_knob_accent_table():
    """FfKnob draws its collar and pointer in the zone accent, and the colour
    reaches it as data: kParamAccent is parallel to kParamCtls, two colours per
    row so the big panel's two-tone MORPH needs no branch in the widget. On
    this plate both halves are always the same zone."""
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    m = re.search(r"static const FfAccent kParamAccent\[\] = \{(.*?)\n\};",
                  src, re.S)
    check(m is not None, "kParamAccent table missing from generated_hw_panel.hpp")
    if m:
        rows = re.findall(r"\{\s*(0x[0-9A-F]+)\s*,\s*(0x[0-9A-F]+)\s*\}", m.group(1))
        check(len(rows) == len(hw.HW_PARAMS),
              f"kParamAccent has {len(rows)} rows, want {len(hw.HW_PARAMS)}")
        for c, (a, b) in zip(hw.HW_PARAMS, rows):
            # Keycaps route through pad_accent(): MOD sits at the right end of
            # the jack row and would be filed under deck B by position alone.
            want = hw.rgb(hw.pad_accent(c) if hw.hw_class(c.enum) == "P"
                          else hw.ACC[hw.zone_of(c.x)])
            check(a == want and b == want,
                  f"{c.enum} accent {a}/{b} is not its zone accent {want}")
    check("kParamAccent desynced" in src, "kParamAccent has no length static_assert")


def test_pad_keycaps_are_dark_and_accented():
    """The keycaps went from a near-white bed to the knob/jack family on
    2026-08-30: a dark cap with an accent edge, printed at the real 6 mm
    round cap since 2026-10-07 (spec §7). MOD and SHFT are the exception the
    accent table has to be told about -- they sit at the two ends of the jack
    row, so zone_of() would file the global MOD latch under deck B and SHFT
    under deck A."""
    r, g, b = hw.rgb(hw.PAD_FILL)[2:4], hw.rgb(hw.PAD_FILL)[4:6], hw.rgb(hw.PAD_FILL)[6:8]
    check(max(int(r, 16), int(g, 16), int(b, 16)) < 0x40,
          f"PAD_FILL {hw.PAD_FILL} is still a light keycap")
    svg = open(os.path.join(HERE, "FireflowHW.svg"), encoding="utf-8").read()
    pads = [c for c in hw.ALL_HW if hw.hw_class(c.enum) == "P"]
    check(len(pads) == 4, f"expected 4 keycaps on the plate, got {len(pads)}")
    for c in pads:
        want = (hw.ACC["C"] if c.enum in hw.GLOBAL_KEYS
                else hw.ACC[hw.zone_of(c.x)])
        pat = (f'<circle cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="{hw.mm(hw.body_r(c))}" '
               f'fill="{hw.PAD_FILL}" stroke="{want}" stroke-width="0.3"/>')
        check(pat in svg, f"{c.enum} keycap is not dark with a {want} edge: {pat}")
    check(hw.GLOBAL_KEYS == {"MODBTN", "SHIFTBTN"},
          f"GLOBAL_KEYS drifted: {hw.GLOBAL_KEYS}")
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    m = re.search(r"static const FfAccent kParamAccent\[\] = \{(.*?)\n\};",
                  src, re.S)
    if m:
        rows = re.findall(r"\{\s*(0x[0-9A-F]+)\s*,\s*(0x[0-9A-F]+)\s*\}", m.group(1))
        by = {c.enum: rows[i] for i, c in enumerate(hw.HW_PARAMS) if i < len(rows)}
        want = hw.rgb(hw.ACC["C"])
        check(by.get("MODBTN") == (want, want),
              f"MODBTN accent {by.get('MODBTN')} is not the neutral {want}")
    check("kFfPadR" in src, "the hw header carries no pad radius")


def test_light_accent_table():
    """Every LED takes its zone accent, printed bed and live glow from the
    same led_accent() (2026-08-30). Two of the fifteen sit at the plate's
    edges and would be read as a deck by position alone: the MOD and SHFT
    lamps. REC stays red on both plates."""
    check(hw.LED_GLOBALS == {"MODBTN_L", "SHIFTBTN_L"},
          f"LED_GLOBALS drifted: {hw.LED_GLOBALS}")
    svg = open(os.path.join(HERE, "FireflowHW.svg"), encoding="utf-8").read()
    for c in hw.HW_LIGHTS:
        check(f'<circle cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="0.750" '
              f'fill="{hw.led_bed(c)}"/>' in svg,
              f"{c.enum} printed bed is not {hw.led_bed(c)}")
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp"),
               encoding="utf-8").read()
    m = re.search(r"static const FfAccent kLightAccent\[\] = \{(.*?)\n\};",
                  src, re.S)
    check(m is not None, "kLightAccent table missing from generated_hw_panel.hpp")
    if m:
        rows = re.findall(r"\{\s*(0x[0-9A-F]+)\s*,\s*(0x[0-9A-F]+)\s*\}", m.group(1))
        check(len(rows) == len(hw.HW_LIGHTS),
              f"kLightAccent has {len(rows)} rows, want {len(hw.HW_LIGHTS)}")
        for c, (a, b) in zip(hw.HW_LIGHTS, rows):
            want = hw.rgb(hw.led_accent(c))
            check(a == want and b == want,
                  f"{c.enum} light accent {a}/{b} is not {want}")
        by = {c.enum: rows[i] for i, c in enumerate(hw.HW_LIGHTS) if i < len(rows)}
        neutral = hw.rgb(hw.ACC["C"])
        for enum in ("MODBTN_L", "SHIFTBTN_L"):
            check(by.get(enum) == (neutral, neutral),
                  f"{enum} is not neutral: {by.get(enum)}")
    check("kLightAccent desynced" in src, "kLightAccent has no length static_assert")


def test_steps_has_no_lamp_on_the_hw_plate():
    """Spec 2026-08-16 song-phrase-flash S4: FLOW lamps are not drawn;
    STPS captions sit on the knob x."""
    names = {c.enum for c in hw.HW_LIGHTS}
    check("FLOW_A_L" not in names and "FLOW_B_L" not in names,
          "FLOW_* still drawn on FireflowHW")
    owners = set(hw.KNOB_LAMPS.values())
    check("STEPS_A" not in owners and "STEPS_B" not in owners,
          "STEPS still owns a lamp in KNOB_LAMPS")
    by = {c.enum: c for c in hw.HW_PARAMS}
    for enum in ("STEPS_A", "STEPS_B"):
        lx, ly = hw.hw_label(by[enum])[:2]
        check(abs(lx - by[enum].x) < 1e-6,
              f"{enum} caption x={lx:.2f} is not on the knob ({by[enum].x:.2f})")


def test_led_inventory_after_the_panel_pass():
    """15 lamps drawn (spec 2026-10-07 §5): the lane-excursion lamps and CEIL_L
    gone, FTIME and RST new, SYNC_L renamed CLK_L, SHIFT and MOD lamps centred
    between their key and jack."""
    names = {c.enum for c in hw.HW_LIGHTS}
    check(len(hw.HW_LIGHTS) == 15, f"{len(hw.HW_LIGHTS)} lights, expected 15")
    for dead in ("SRC_A_L", "SRC_B_L", "FLT_A_L", "FLT_B_L", "CLR_A_L", "CLR_B_L",
                 "CEIL_L", "SYNC_L", "CAP_A_L", "CAP_B_L"):
        check(dead not in names, f"{dead} is still drawn")
    for want in ("FTIME_A_L", "FTIME_B_L", "RST_L", "CLK_L", "LVL_A_L", "SONG_A_L",
                 "GATE_A_L", "REC_A_L", "TEMPO_L", "MODBTN_L", "SHIFTBTN_L"):
        check(want in names, f"{want} missing")
    lights = {c.enum for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS}
    for dead in ("SRC_A_L", "FLT_A_L", "CLR_A_L", "CEIL_L", "SYNC_L"):
        check(dead not in lights, f"{dead} is still a LightId")
    check(hw.KNOB_LAMPS.get("FTIME_A_L") == "FLUXRATE_A",
          "FTIME_A_L is not FLUXRATE_A's cluster lamp")


# Deck A side of each knob lamp, +1 = right of its word (spec §5, amended
# 2026-10-08): toward the lamp's own group. Deck B mirrors. Written here, not
# read from hw.LAMP_SIDE, so a flipped lamp cannot pass by flipping the table.
CLUSTER_SIDE = {"ATTACK": +1, "FLUXRATE": -1, "COMP": +1, "SONG": +1,
                "REC": +1, "TEMPO": +1}
# The board-probed |dx| from which each lamp's LED clears every front part
# on the Rev A board (2026-10-08, see LED_DX in the generator): the floor
# LED_DX / LED_DX_KEY may not go under. Where the two sides measured
# differently the stricter value is pinned (FLUXRATE 5.70/5.75, SONG
# 6.75/6.80). REC's 5.90 holds with the LED's flat side toward the key --
# its body box is not centred on its hole -- which the placer guarantees by
# its body-aware rotation pick (place.py _led_fits, P4.1 §4.2 amended
# 2026-10-08). COMP's lamp was clear at every probed dx (0.00, 2.05, 6.90).
CLUSTER_DX_FLOOR = {"ATTACK": 5.70, "FLUXRATE": 5.75, "TEMPO": 5.75,
                    "REC": 5.90, "SONG": 6.80, "COMP": 0.0}


def test_knob_lamps_sit_in_the_caption_cluster():
    """Knob-owned lamps (spec 2026-10-07 §5, amended 2026-10-08): the word is
    centred under its knob, the LED stands on the word's glyph midline,
    LED_DX (LED_DX_KEY for the REC key) beside the knob's x, on the side of
    its own group -- deck B mirrored -- and inside its owner's field.

    A lamp typed to a clear-but-wrong side of the knob still passes the
    overlap guard; this checks the cluster itself. REC's lamp joined the
    cluster on 2026-10-07, SONG's on 2026-10-08. CLK_L and RST_L stay on the
    satellite rule and SHIFTBTN_L/MODBTN_L stand between key and jack -- see
    test_jack_row_lamps."""
    by = {c.enum: c for c in hw.ALL_HW}
    checked = 0
    for lamp, knob_enum in hw.KNOB_LAMPS.items():
        if lamp not in by or knob_enum not in by:
            check(False, f"{lamp} or its knob {knob_enum} is missing")
            continue
        k, l = by[knob_enum], by[lamp]
        lx, ly, anchor = hw.hw_label(k)[:3]
        check(abs(lx - k.x) < 1e-6 and anchor == "middle",
              f"{knob_enum} word at x {lx:.2f} ({anchor}) is not centred on its knob ({k.x:.2f})")
        want_y = k.y + hw.CLASS_LBL_DY[hw.hw_class(knob_enum)]
        check(abs(ly - want_y) < 1e-6,
              f"{knob_enum} word baseline {ly:.3f} is not the class offset ({want_y:.3f})")
        stem = knob_enum[:-2] if knob_enum.endswith(("_A", "_B")) else knob_enum
        if stem not in CLUSTER_SIDE:
            check(False, f"{lamp}: the guard has no side for {stem}")
            continue
        side = CLUSTER_SIDE[stem] * (-1 if knob_enum.endswith("_B") else 1)
        key = hw.hw_class(knob_enum) == "P"
        dx = hw.LED_DX_KEY if key else hw.LED_DX
        check(abs(l.x - (k.x + side * dx)) < 1e-6,
              f"{lamp} at x {l.x:.3f}, want {k.x + side * dx:.3f} "
              f"({'right' if side > 0 else 'left'} of {knob_enum}, dx {dx})")
        check(dx >= CLUSTER_DX_FLOOR[stem] - 1e-9,
              f"{lamp}: dx {dx} is under the board-probed {CLUSTER_DX_FLOOR[stem]}")
        mid = ly - (hw.CAPTION_SIZE * hw.FONT_CAP) / 2.0
        check(abs(l.y - mid) < 1e-6,
              f"{lamp} y={l.y:.3f} is not on the {knob_enum} glyph midline "
              f"({mid:.3f})")
        b = hw.box_of(l)
        check(b is not None and b is hw.box_of(k),
              f"{lamp} does not stand in {knob_enum}'s own field")
        checked += 1
    check(checked == 11, f"expected 11 clustered lamps, checked {checked}")


def test_every_lamp_keeps_the_acrylic_web_to_its_owner():
    """Spec 2026-10-07: between a lamp's hole and its owner's hole the acrylic
    keeps MIN_WEB (P1). hw_cut_guard measures the same web on the cut file;
    this catches it at the generator, before any cut is written."""
    by = {c.enum: c for c in hw.ALL_HW}
    for lamp, owner in hw.KNOB_LAMPS.items():
        l, k = by[lamp], by[owner]
        d = ((l.x - k.x) ** 2 + (l.y - k.y) ** 2) ** 0.5
        web = d - hw.HOLE_D[hw.hw_class(owner)] / 2 - hw.HOLE_D["L"] / 2
        check(web >= hw.MIN_WEB - 1e-6,
              f"{lamp} leaves {web:.3f} mm of acrylic to {owner} "
              f"(MIN_WEB {hw.MIN_WEB})")


def test_song_lamp_is_a_cluster_lamp():
    """Spec 2026-10-07 §5.1, replaced 2026-10-08: SONG's lamp left its side
    spot beside the knob and joined the caption clusters -- one rule for
    every knob lamp, no second table of side lamps."""
    for side in "AB":
        check(hw.KNOB_LAMPS.get(f"SONG_{side}_L") == f"SONG_{side}",
              f"SONG_{side}_L is not SONG_{side}'s cluster lamp")
    check(not hasattr(hw, "SIDE_LAMPS"), "a SIDE_LAMPS table is back")


def test_jack_row_lamps():
    """CLK_L and RST_L are satellites at SAT_D; SHIFTBTN_L and MODBTN_L stand
    centred between their key and jack (spec 2026-10-07 §5), every hole web
    at least MIN_WEB."""
    by = {c.enum: c for c in hw.ALL_HW}
    for lamp, anchor in (("CLK_L", "CLOCK"), ("RST_L", "RESET")):
        l, a = by[lamp], by[anchor]
        d = ((l.x - a.x) ** 2 + (l.y - a.y) ** 2) ** 0.5
        check(abs(d - hw.SAT_D) < 0.01, f"{lamp} is {d:.3f} mm from {anchor}, not SAT_D")
    check(by["CLK_L"].x < by["CLOCK"].x and by["RST_L"].x > by["RESET"].x,
          "CLK_L is not inboard of CLOCK or RST_L not outboard of RESET")
    for lamp, key, jack in (("SHIFTBTN_L", "SHIFTBTN", "IN_L"), ("MODBTN_L", "MODBTN", "OUT_R")):
        l, k, j = by[lamp], by[key], by[jack]
        check(abs(l.x - (k.x + j.x) / 2) < 1e-9 and abs(l.y - hw.JACK_Y) < 1e-9,
              f"{lamp} is not centred between {key} and {jack}")
        for anchor in (k, j):
            web = (abs(l.x - anchor.x) - hw.HOLE_D[hw.hw_class(anchor.enum)] / 2
                   - hw.HOLE_D["L"] / 2)
            check(web >= hw.MIN_WEB - 1e-9, f"{lamp} leaves {web:.2f} mm to {anchor.enum}")
    need = max(hw.HOLE_D["P"], hw.HOLE_D["J"]) / 2 + hw.MIN_WEB + hw.HOLE_D["L"] / 2
    check(hw.SAT_D >= need - 1e-9, f"SAT_D {hw.SAT_D} leaves less than {hw.MIN_WEB} mm")


def test_mod_jacks_on_the_jack_row():
    """The eight green COLOR/FILT/TIMB/LVL placeholders are real Rack inputs
    labelled MOD1–MOD4, mirrored, on the jack row. process() does not read them."""
    mods = [c for c in hw.HW_INPUTS if c.enum.startswith("MOD")]
    check(len(mods) == 8, f"expected 8 MOD jacks, got {len(mods)}")
    check([c.enum for c in mods] == [c.enum for c in gp.HW_MOD_INPUTS],
          "HW MOD jack order drifted from HW_MOD_INPUTS")
    check([c.label for c in mods] == ["MOD1", "MOD2", "MOD3", "MOD4"] * 2,
          f"MOD captions drifted: {[c.label for c in mods]}")
    xs_a = (hw.X_COLOR, hw.X_FILT, hw.X_TIMB, hw.X_LVL)
    for j, x in zip(mods[:4], xs_a):
        check(abs(j.y - hw.JACK_Y) < 1e-6, f"{j.enum} is not on the jack row")
        check(abs(j.x - x) < 1e-6, f"{j.enum} x is {j.x}, not {x}")
    for c in hw.HW_ONLY:
        check(not c.enum.startswith("CV_"), f"placeholder {c.enum} is still HW_ONLY")
        check(c.enum not in {m.enum for m in gp.HW_MOD_INPUTS},
              f"{c.enum} is HW_ONLY, not a real input")


def test_sd_cutout_is_clear():
    """Kein Bedienelement, keine Buchse, keine LED und kein Beschriftungsanker
    liegt im SD-Ausschnitt. MicroSD-Platzhalter: 11 × 6 mm, mittig zwischen
    CLK und RST (Grafikrunde 15. Aug)."""
    x0, x1 = hw.SD_X - hw.SD_W / 2, hw.SD_X + hw.SD_W / 2
    y0, y1 = hw.SD_Y - hw.SD_H / 2, hw.SD_Y + hw.SD_H / 2

    def dist_to_rect(px, py):
        dx = max(x0 - px, 0.0, px - x1)
        dy = max(y0 - py, 0.0, py - y1)
        return (dx * dx + dy * dy) ** 0.5

    for c in hw.ALL_HW:
        check(dist_to_rect(c.x, c.y) >= c.r - 1e-6,
              f"{c.enum} overlaps the SD cutout")
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_ONLY:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        check(dist_to_rect(lx, ly) > 1e-6,
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is inside the SD cutout")
    # Static lettering (hw.TEXTS) used to be invisible to this guard too --
    # nothing stopped a title or legend from being centred in the cutout.
    for (x, y, size, spacing, col, anchor, txt) in hw.TEXTS:
        check(dist_to_rect(x, y) > 1e-6,
              f"text {txt!r} at ({x:.1f},{y:.1f}) is inside the SD cutout")
    check(y1 <= hw.KEEP_BOT + 1e-9, "SD cutout crosses the bottom rail")


def test_plate_paints_survive_nanosvg():
    """The plate is Option B (owner decision 2026-08-29): ONE continuous dark
    surface plus soft tinted group fields. No zone rects, no seam gradients,
    no baked fade overlays, no airflow/ember print, no drawing frames.

    Rack parses the panel with NanoSVG, which silently drops <mask>,
    <pattern> and <filter> -- a design that leans on any of them looks right
    in a browser and wrong on the module, which is exactly how it would ship
    unnoticed. That rationale outlives the design round, so it stays."""
    svg = hw.svg()
    check("<mask" not in svg and "mask=" not in svg,
          "an SVG mask is back -- NanoSVG drops it")
    check("<pattern" not in svg, "a <pattern> is in the plate -- NanoSVG drops it")
    check("<filter" not in svg and "feTurbulence" not in svg,
          "a <filter> is in the plate -- NanoSVG drops it")
    check("rgba(" not in svg,
          "rgba() is in the plate -- NanoSVG's colour parser is not a browser's")
    # Measured, not assumed: with objectBoundingBox gradients Rack painted an
    # overlay opaque across the whole of deck A and swallowed the print under
    # it, while the mirrored deck B rendered correctly; the browser preview
    # showed both halves. Whatever gradient the plate carries stays in mm.
    n_grad = svg.count("<linearGradient")
    check(n_grad == 1, f"expected 1 plate gradient, got {n_grad}")
    check('<linearGradient id="hw-plate"' in svg, "the plate gradient is missing")
    check(svg.count('gradientUnits="userSpaceOnUse"') == n_grad,
          "a gradient is in bounding-box units -- NanoSVG and the browser "
          "then disagree about the plate")
    check(svg.count('fill="url(#hw-plate)"') == 1,
          "the plate is not painted by exactly one hw-plate rect")
    # Struck with Option B and asserted ABSENT, so a half-reverted generator
    # cannot quietly bring any of it back.
    for gone in ("zoneA", "zoneC", "zoneB", "fadeA", "fadeB", "seamL", "seamR"):
        check(f"hw-{gone}" not in svg, f"plate paint {gone} is back")
    check('d="M-14.0,8.7C' not in svg, "the airflow/ember print is back")
    check('stroke-opacity="0.09"' not in svg,
          "the silhouette's print opacity is back on the plate")
    check("stroke-dasharray" not in svg,
          "a dashed stroke is back -- the drawing frames were struck")
    # The group fields: two washes on ONE outline per box, a white one that
    # lifts the field off the plate and the zone accent over it. The decks
    # carry the cool/warm identity the plate itself no longer has, so they run
    # at twice the centre's tint. The opacities are pinned here, not read from
    # hw, or the generator could redefine them unseen.
    check(svg.count("<path d=\"M") >= 2 * len(hw.BOXES),
          "the group fields are not drawn as paths")
    check(f'rx="{hw.mm(1.5)}"' not in svg,
          "a field is back to a <rect rx> -- a rect cannot carry the notch")
    for b in hw.BOXES:
        d = hw._field_d(b)
        check(f'<path d="{d}" fill="#ffffff" fill-opacity="0.02"/>' in svg,
              f"{b.n}/{b.side} has no white field wash")
        want = 0.025 if b.side == "C" else 0.05
        check(f'<path d="{d}" fill="{hw.ACC[b.side]}" fill-opacity="{want}"/>'
              in svg,
              f"{b.n}/{b.side} has no {b.side} accent field at {want}")
        # Both washes trace the SAME outline. Two nearly-identical path
        # builders is exactly how the accent would keep a notch the white
        # wash lost, and the seam would be a 2%-opacity sliver nobody sees.
        check(svg.count(f'<path d="{d}"') == 2,
              f"{b.n}/{b.side}: the two washes do not share one outline")


def test_text_run_counts_gaps_between_glyphs():
    """len-1 gaps, not len. nvgTextLetterSpacing emits one after the last
    glyph as well, but no ink follows it, so counting it overstates every run
    by one `spacing` on the right. A collision test cannot see that -- a box
    that is too wide only errs safe -- so it survived until something was
    CENTRED on the box and the legend notches came out lopsided."""
    x0, x1, ytop, ybase = hw.text_run(10.0, 20.0, 2.0, 0.5, "start", "AB")
    check(abs(x0 - 10.0) < 1e-9, f"a start-anchored run begins at {x0}, not 10")
    check(abs((x1 - x0) - (2 * 2.0 * hw.FONT_ADVANCE + 0.5)) < 1e-9,
          f"'AB' is {x1 - x0:.3f} wide, not two glyphs plus ONE gap "
          f"({2 * 2.0 * hw.FONT_ADVANCE + 0.5:.3f})")
    check(abs((ybase - ytop) - 2.0 * hw.FONT_CAP) < 1e-9, "cap height is off")
    # One glyph has no gap at all, and zero glyphs cannot have -1 of them.
    one = hw.text_run(0.0, 0.0, 2.0, 0.5, "start", "A")
    check(abs(one[1] - 2.0 * hw.FONT_ADVANCE) < 1e-9,
          f"a single glyph is {one[1]:.3f} wide, not one advance")
    check(hw.text_run(0.0, 0.0, 2.0, 0.5, "start", "")[1] == 0.0,
          "an empty run has a non-zero width")
    # The anchors hang off the same width, so the fix moves right- and
    # centre-set lettering too. Nothing on the plate is set that way today;
    # this is here so the next thing that is does not inherit the old bug.
    check(abs(hw.text_run(10.0, 0.0, 2.0, 0.5, "end", "AB")[1] - 10.0) < 1e-9,
          "an end-anchored run does not finish on its anchor")
    mid = hw.text_run(10.0, 0.0, 2.0, 0.5, "middle", "AB")
    check(abs((mid[0] + mid[1]) / 2 - 10.0) < 1e-9,
          "a centred run is not centred on its anchor")


def test_legend_notches_clear_their_lettering():
    """The legend straddles its field's top edge, so the edge is cut away
    around it (2026-08-30). Two ways that goes wrong and neither shows up in
    a thumbnail: the bite is too short and a glyph still sits half on the
    edge, or it is too long for a narrow frame and eats into a rounded
    corner, which reads as a dented box rather than a notch."""
    for b in hw.BOXES:
        if not b.prints_legend:
            check(b.notch is None,
                  f"{b.n}: this frame prints no legend, so there is nothing "
                  f"for a notch to make room for")
            continue
        check(b.notch is not None, f"{b.n}: printed legend with no notch")
        n0, n1 = b.notch
        x0, x1, ytop, ybase = hw.text_run(b.x + hw.LEGEND_INSET, b.legend_y,
                                          hw.LEGEND_SIZE, hw.LEGEND_SPACING,
                                          "start", b.n)
        check(abs((x0 - n0) - hw.NOTCH_PAD) < 1e-9 and
              abs((n1 - x1) - hw.NOTCH_PAD) < 1e-9,
              f"{b.n}: the notch does not clear its lettering by NOTCH_PAD "
              f"({n0:.2f}..{n1:.2f} around ink {x0:.2f}..{x1:.2f})")
        # Deep enough that the whole glyph sits on plate. Caps only, so the
        # baseline IS the bottom of the ink -- no descender to allow for.
        check(b.y + hw.NOTCH_DEPTH > ybase,
              f"{b.n}: the notch bottom {b.y + hw.NOTCH_DEPTH:.2f} is above "
              f"the legend's baseline {ybase:.2f} -- the edge still crosses "
              f"the glyphs")
        check(ytop < b.y, f"{b.n}: the legend no longer straddles the edge, "
                          f"so the notch is pointless")
        # ... and shallow enough to stay a notch in the top edge.
        check(hw.NOTCH_DEPTH < b.h / 2.0, f"{b.n}: the notch halves the field")
        # The corners it is cut with need room on both sides, or the bite
        # runs into the field's own rounded corner.
        check(n0 - hw.NOTCH_R >= b.x + hw.FIELD_R,
              f"{b.n}: the notch starts inside the field's left corner")
        check(n1 + hw.NOTCH_R <= b.x + b.w - hw.FIELD_R,
              f"{b.n}: {b.n!r} is too long for a {b.w:.1f} mm frame -- the "
              f"notch ends {n1 + hw.NOTCH_R - (b.x + b.w - hw.FIELD_R):.2f} mm "
              f"into the right corner")
        check(hw.NOTCH_DEPTH >= 2 * hw.NOTCH_R,
              "the notch is shallower than its own corner radii")
        # And it is the drawn outline that carries it, not just the numbers.
        d = hw._field_d(b)
        check(f"L{hw.mm(n0 - hw.NOTCH_R)},{hw.mm(b.y)}" in d,
              f"{b.n}: the drawn field does not step down at the notch")
        check(f"{hw.mm(n1)},{hw.mm(b.y + hw.NOTCH_DEPTH - hw.NOTCH_R)}" in d,
              f"{b.n}: the drawn field does not come back up after the notch")


def test_caption_gap_is_one_number():
    """Every printed word keeps the SAME distance to its own body edge.

    Per-class offsets shipped four different gaps -- 4.50 mm on the big
    pots, 3.60 on the small, 2.50 on the pads, 4.90 on the jacks. Each was
    a plausible number on its own; nothing ever put them side by side, and
    on the plate the big knobs visibly hung further from their labels than
    the small ones. This is that comparison.

    The jack row is the one exception, and a deliberate one: SHFT and MOD
    are keycaps standing in a line of jacks, so that row shares a baseline
    instead of a gap."""
    seen = {}
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_ONLY:
        if not c.label:
            continue
        cls = hw.hw_class(c.enum)
        ly = hw.hw_label(c)[1]
        if ly <= c.y:                      # caption stepped above or aside
            continue
        gap = ly - c.y - hw.body_r(c)
        if c.y >= hw.JACK_Y - 0.5:
            check(abs(ly - (hw.JACK_Y + hw.JACK_ROW_LBL_DY)) < 1e-6,
                  f"{c.enum} breaks the jack row's shared baseline "
                  f"({ly:.2f} vs {hw.JACK_Y + hw.JACK_ROW_LBL_DY:.2f})")
            continue
        check(abs(gap - hw.CAPTION_GAP) < 1e-6,
              f"{c.enum} ({cls}) sits {gap:.2f} mm from its body, "
              f"not {hw.CAPTION_GAP}")
        seen[cls] = gap
    check(set(seen) >= {"G", "S", "P"},
          f"only saw caption gaps for {sorted(seen)} -- the comparison that "
          "matters is big pot vs small pot, so both must be in it")
    # The drawn keycap is the round 6 mm cap, so 3.0 is its radius. A BODY_R
    # that disagrees with the drawing would put the pads' gap silently off.
    svg = hw.svg()
    for c in hw.HW_PARAMS:
        if hw.hw_class(c.enum) != "P":
            continue
        check(f'cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="{hw.mm(hw.body_r(c))}" '
              f'fill="{hw.PAD_FILL}"' in svg,
              f"{c.enum} is drawn at a size BODY_R does not know about")


def _rect_hits_circle(x0, x1, y0, y1, cx, cy, r):
    dx = max(x0 - cx, 0.0, cx - x1)
    dy = max(y0 - cy, 0.0, cy - y1)
    return (dx * dx + dy * dy) ** 0.5 < r - 1e-6


def test_legends_are_not_buried_by_rack_widgets():
    """A Rack widget is a THIRD radius, next to the plate body and the layout
    clearance circle, and it is the one that hides lettering. This bit for
    real: the jack-row legends sat at the frame's top edge, 111.15 mm, which
    the SVG showed clear of a 6.2 mm jack body -- and Rack's 8.03 mm PJ301M
    swallowed every one of them. The whole run is checked, not the anchor:
    the collision was at the tail of "13 MOD A", not under its first glyph."""
    for (x, y, size, spacing, col, anchor, txt) in hw.TEXTS:
        x0, x1, ytop, ybase = hw.text_run(x, y, size, spacing, anchor, txt)
        for c in hw.ALL_HW:
            r = hw.RACK_R[hw.hw_class(c.enum)]
            check(not _rect_hits_circle(x0, x1, ytop, ybase, c.x, c.y, r),
                  f"lettering {txt!r} at ({x:.1f},{y:.1f}) is under "
                  f"{c.enum}'s Rack widget")


def _rect_circle_margin(x0, x1, y0, y1, cx, cy, r):
    """Clear distance between a circle and a rectangle, mm (negative when
    they overlap)."""
    dx = max(x0 - cx, 0.0, cx - x1)
    dy = max(y0 - cy, 0.0, cy - y1)
    return (dx * dx + dy * dy) ** 0.5 - r


def test_captions_and_lamps_are_not_under_rack_widgets():
    """The legend guard above reads hw.TEXTS only. Every control's own
    caption and every lamp are checked here the same way: Rack draws a widget
    for each param, port and light, at RACK_R, and what lies under it is
    hidden although the SVG preview shows it clear (review focus 1 of the
    2026-10-07 plan). Captions against every widget, their own included
    (HW_ONLY rows draw no widget but do print a caption); lamps against every
    widget but their own. The tightest margin is printed by main()."""
    widgets = hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_LIGHTS
    captions = lamps = 0
    tightest = None
    for c in hw.ALL_HW:
        cap = None
        if c.label:
            lx, ly, anchor, size, _ = hw.hw_label(c)
            cap = hw.text_run(lx, ly, size, 0.0, anchor, c.label)
            captions += 1
        is_lamp = hw.hw_class(c.enum) == "L"
        lamps += is_lamp
        for w in widgets:
            r = hw.RACK_R[hw.hw_class(w.enum)]
            if cap is not None:
                m = _rect_circle_margin(*cap, w.x, w.y, r)
                if tightest is None or m < tightest[0]:
                    tightest = (m, f"caption {c.enum}", w.enum)
                check(m > 1e-6,
                      f"the caption of {c.enum} ({c.label!r}) is under "
                      f"{w.enum}'s Rack widget ({m:.3f} mm)")
            if is_lamp and w is not c:
                m = ((c.x - w.x) ** 2 + (c.y - w.y) ** 2) ** 0.5 - r - hw.RACK_R["L"]
                if tightest is None or m < tightest[0]:
                    tightest = (m, f"lamp {c.enum}", w.enum)
                check(m > 1e-6,
                      f"lamp {c.enum} is under {w.enum}'s Rack widget "
                      f"({m:.3f} mm)")
    # Not vacuous: a plate with no captions or no lamps would pass trivially.
    check(captions > 0 and lamps > 0,
          f"checked {captions} captions and {lamps} lamps -- the loop did not "
          f"see the plate")
    test_captions_and_lamps_are_not_under_rack_widgets.stats = (captions, lamps, tightest)


def test_bodies_and_captions_sit_inside_their_frame():
    """The fields are drawn against the real cap radii of spec 2026-10-07 §2
    (12 mm and 7.7 mm caps, 6 mm keys, 6.2 mm jacks), not the
    finger-clearance circles the layout is spaced on. So a body or a caption
    crossing its own field is the one way the fields can go wrong, and
    reading the SVG will not show it."""
    loose = []
    for c in hw.ALL_HW:
        b = hw.box_of(c)
        if b is None:
            loose.append(c.enum)
            continue
        r = hw.body_r(c)
        check(b.contains_rect(c.x - r, c.x + r, c.y - r, c.y + r),
              f"{c.enum} ({r} mm body) pokes out of frame {b.n}/{b.side}")
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[:2]
        check(b.covers(lx, ly),
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is outside {b.n}/{b.side}")
    # MODBTN_L/SHIFTBTN_L stand centred between their pad and jack (spec
    # 2026-10-07 §5) -- exactly as loose as the pads themselves, which is
    # what makes them read as "this pad is lit" rather than as members of
    # the jack-row frame.
    # PULL was on this list for one day (2026-08-22 -> 2026-08-23) while it sat
    # loose in the seam beside the GLOBAL box. It is gone again: re-pitching the
    # GLOBAL centre row to four knobs gave it a real frame slot, so this guard
    # is back to asserting "every knob lives inside a frame" with no knob
    # exception. Only pads and their satellite LEDs are loose, which is the
    # invariant this list is for -- keep it that way.
    check(sorted(loose) == ["MODBTN", "MODBTN_L", "SHIFTBTN", "SHIFTBTN_L"],
          f"controls outside the frame raster: {sorted(loose)}")
    # The SD slot is a body on the jack row like any other.
    sd = [b for b in hw.BOXES if b.n == "CLOCK"][0]
    check(sd.x <= hw.SD_X - hw.SD_W / 2 and hw.SD_X + hw.SD_W / 2 <= sd.x + sd.w
          and sd.y <= hw.SD_Y - hw.SD_H / 2 and hw.SD_Y + hw.SD_H / 2 <= sd.y + sd.h,
          "the SD slot pokes out of the CLOCK frame")


def test_sd_cutout_is_drawn():
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    check(f'width="{hw.SD_W:.3f}"' in svg and f'height="{hw.SD_H:.3f}"' in svg,
          "the SD cutout is not in the SVG")


def test_drawing_geometry():
    """Pins the drawing's fixed points: the jack row's height, SD, no title,
    no rail dashes, the plate gradient starting at the edge (Option B
    2026-08-29 -- it is the plate itself now, not a zone wash), jack
    captions under the jacks. Knob positions are not pinned here any more:
    the cell table is their source and test_cells_are_the_source its guard
    (spec 2026-10-07 §9)."""
    check(abs(hw.JACK_Y - 112.75) < 1e-9, f"JACK_Y is {hw.JACK_Y}, not 112.75 (spec 2026-10-07 §6)")
    check((hw.SD_W, hw.SD_H) == (11.0, 6.0), f"SD size is {hw.SD_W}x{hw.SD_H}")
    check(abs(hw.SD_Y - hw.JACK_Y) < 1e-9, f"SD_Y is {hw.SD_Y}, not on the jack row")
    by = {c.enum: c for c in hw.ALL_HW}
    check(abs(by["SHIFTBTN"].y - hw.JACK_Y) < 1e-9, "SHIFT is not on the jack row")
    check(abs(by["MODBTN"].y - hw.JACK_Y) < 1e-9, "MOD is not on the jack row")
    # Lettering Rack has to draw itself: brand block plus ONE row per frame
    # since 2026-08-30, when the two-digit index in front of each name was
    # struck. Rack does not render SVG text, so an empty TEXTS means a plate
    # whose legends exist in the preview and nowhere else.
    printing = [b for b in hw.BOXES if b.prints_legend]
    check(len(hw.TEXTS) == len(hw.BRAND_TEXTS) + len(printing),
          f"TEXTS carries {len(hw.TEXTS)} rows, not brand + one per frame "
          f"that prints a legend ({len(printing)} of {len(hw.BOXES)})")
    # The jack row's seven were struck 2026-08-30. Asserted absent, not merely
    # uncounted: a stale SVG or header could otherwise still carry them.
    words = {t[6] for t in hw.TEXTS}
    for w in ("IN", "OUT", "CV A", "CV B", "MOD A", "MOD B", "CLOCK"):
        check(w not in words, f"the jack-row legend {w!r} is back")
    check(all(b.y != hw.JACK_ROW_Y for b in printing),
          "a jack-row frame prints a legend again")
    # Asserted absent, not merely uncounted: a half-reverted generator that
    # brings the numbering back would otherwise only trip the count above,
    # which a second brand row could mask.
    check(not any(re.fullmatch(r"\d{2}", w) for w in words),
          f"a two-digit legend index is back: "
          f"{sorted(w for w in words if re.fullmatch(r'\d{2}', w))}")
    for w in ("SEQUENCE", "ROOM"):
        check(w in words, f"{w!r} is not in the panel lettering")
    # FIREFLOW/60 HP were pulled 2026-08-23 while the plate's branding is
    # redrawn; DECK A/DECK B were struck 2026-08-29 with the Option B round --
    # the tinted group fields carry the deck identity, so the words were
    # redundant. Asserted absent so a stale generated SVG or header cannot
    # quietly put any of them back.
    for w in ("FIREFLOW", "60 HP", "DECK A", "DECK B"):
        check(w not in words, f"{w!r} is back in the panel lettering")
    lx, ly = hw.hw_label(by["IN_L"])[:2]
    check(ly > by["IN_L"].y, "IN L caption is not under the jack")
    svg = hw.svg()
    check('y1="9.00"' not in svg and 'y1="119.50"' not in svg,
          "rail keep-out dashes are still drawn")
    check('y="0"' in svg or 'y="0.000"' in svg,
          "the plate does not start at the top edge")
    for c in hw.HW_PARAMS:
        if hw.hw_class(c.enum) == "P":
            continue
        needle = (f'cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="{hw.mm(hw.body_r(c))}" '
                  f'fill="{hw.HW_WELL}"')
        check(needle in svg, f"{c.enum} is not drawn as a mounting hole")
    for c in hw.HW_PARAMS:
        if c.label:
            check(len(c.label) <= 4, f"knob caption {c.enum}={c.label!r} is over 4 chars")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    for fn in (test_mirror_symmetry, test_caption_mirror_symmetry):
        n = getattr(fn, "pairs_checked", None)
        if n is not None:
            print(f"{fn.__name__}: checked {n} mirror pairs")
    stats = getattr(test_captions_and_lamps_are_not_under_rack_widgets, "stats", None)
    if stats is not None:
        n_cap, n_lamp, (margin, what, widget) = stats
        print(f"test_captions_and_lamps_are_not_under_rack_widgets: {n_cap} captions, "
              f"{n_lamp} lamps; tightest margin {margin:+.3f} mm ({what} / {widget}'s widget)")
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw panel guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
