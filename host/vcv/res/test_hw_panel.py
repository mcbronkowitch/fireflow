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
    assert [c.enum for c in hw.HW_PARAMS] == [c.enum for c in gp.RUNTIME_PANEL_PARAMS]
    assert [c.enum for c in hw.HW_INPUTS] == (
        [c.enum for c in gp.INPUTS] + [c.enum for c in gp.HW_MOD_INPUTS])
    assert [c.enum for c in hw.HW_OUTPUTS] == [c.enum for c in gp.OUTPUTS]
    assert [c.enum for c in hw.HW_LIGHTS] == [c.enum for c in gp.LIGHTS]

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
    # Radius-sum clearance with REAL footprints. Exactly one legal overlap:
    # BEND shares ATTACK's knob (deliberate dual assignment, spec §1).
    SHARED_OK = {frozenset(("ATTACK_A", "STAGES_A")),
                 frozenset(("ATTACK_B", "STAGES_B"))}
    items = hw.ALL_HW
    for i, a in enumerate(items):
        for b in items[i + 1:]:
            if frozenset((a.enum, b.enum)) in SHARED_OK:
                continue
            d = ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5
            assert d >= a.r + b.r - 1e-6, (a.enum, b.enum, round(d, 2))

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
    # HW_PARAMS -- same widening as test_mirror_symmetry above.
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
    at a control that no longer exists."""
    live = {c.enum for c in gp.RUNTIME_PANEL_PARAMS}
    stems = set(hw.DECK_POS) | set(hw.CENTER_POS)
    dead = [s for s in stems
            if s not in live and f"{s}_A" not in live and s not in hw.JACK_POS]
    check(not dead, f"hw slots for controls that no longer exist: {dead}")
    check(len(hw.HW_PARAMS) == len(gp.RUNTIME_PANEL_PARAMS),
          "hw param count drifted from the shared inventory")


LBL_MARGIN = 1.5


def test_labels_stay_off_neighbour_footprints():
    # A caption may sit near its own control, but its anchor must clear every
    # OTHER control's footprint by LBL_MARGIN. Bare non-overlap is not a
    # margin: at 16 mm row spacing the default offset lands 8.1 mm from an
    # 8.0 mm knob and would pass a zero-margin test with 0.1 mm to spare.
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS + hw.HW_ONLY:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        for other in hw.ALL_HW:
            if other is c:
                continue
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            check(d >= other.r + LBL_MARGIN - 1e-6,
                  f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is {d:.2f} from "
                  f"{other.enum} (needs {other.r + LBL_MARGIN:.2f})")


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
    BIG = {"DENSITY", "MOD", "COLOR", "FILT", "FLUX", "REV_MIX",
           "COMP", "MORPH", "REV_DECAY"}
    got = {b for b, cls in hw.HW_SIZE.items() if cls == "G"}
    check(got == BIG, f"big-knob set drifted: extra={got-BIG} missing={BIG-got}")
    big_positions = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "G"]
    check(len(big_positions) == 16, f"expected 16 big positions, got {len(big_positions)}")
    small = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "S"]
    # 47 (PACE) + SOURCE×2 (was G) + ENGINE×2 (was P) = 51
    check(len(small) == 51, f"expected 51 small params, got {len(small)}")
    check(abs(hw.CLASS_R["G"] - 8.5) < 1e-9, "CLASS_R G is not 8.5")
    check(abs(hw.CLASS_R["S"] - 6.0) < 1e-9, "CLASS_R S is not 6.0")
    check(hw.HW_SIZE["SOURCE"] == "S", "TIMB/SOURCE is not small")
    check(hw.HW_SIZE["ENGINE"] == "S", "ENGINE is not a small knob")


def test_hw_only_inventory():
    """Was es auf Blech gibt, aber nicht im VCV-Modul: 2 Taster, 6 zusätzliche
    LEDs. Die acht MOD-Buchsen sind echte Inputs (unwired), keine HW_ONLY-
    Platzhalter mehr. Zehn LEDs insgesamt."""
    kinds = {}
    for c in hw.HW_ONLY:
        kinds[hw.hw_class(c.enum)] = kinds.get(hw.hw_class(c.enum), 0) + 1
    check(kinds.get("P") == 2, f"expected 2 hw-only pads, got {kinds.get('P')}")
    check(kinds.get("J", 0) == 0, f"expected 0 hw-only jacks, got {kinds.get('J')}")
    check(kinds.get("L") == 6, f"expected 6 hw-only LEDs, got {kinds.get('L')}")
    assert [c.enum for c in hw.HW_PARAMS] == [c.enum for c in gp.RUNTIME_PANEL_PARAMS]
    total_leds = len([c for c in hw.ALL_HW if hw.hw_class(c.enum) == "L"])
    check(total_leds == 10, f"expected 10 LEDs on the panel, got {total_leds}")


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


def test_zone_wash_follows_the_deck_silhouette():
    """The A/B wash is a smoothed polygon along the deck, not a vertical at
    ZONE_A and not a chain of raw circles. The edge stays west of the centre
    wells and still moves with the silhouette (not a straight slab)."""
    edge = hw.wash_edge_xs()
    xs = [x for _y, x in edge]
    centre_min = hw.centre_well_left()
    check(max(xs) < centre_min,
          f"wash edge reaches {max(xs):.2f}, centre wells start {centre_min:.2f}")
    check(min(xs) >= hw.SLAB_X - 1e-9, "wash edge dropped below the slab floor")
    check(max(xs) - min(xs) > 8.0,
          f"wash edge is too flat ({max(xs) - min(xs):.1f} mm) — smoothing ate the silhouette")
    svg = hw.svg()
    check(f'<polygon fill="{hw.KNOB_DISC_A}"' in svg, "left wash polygon is missing")
    check(f'<polygon fill="{hw.KNOB_DISC_B}"' in svg, "right wash polygon is missing")
    check(svg.count("<polygon") == 2, f"expected 2 wash polygons, got {svg.count('<polygon')}")


def test_sd_cutout_is_drawn():
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    check(f'width="{hw.SD_W:.3f}"' in svg and f'height="{hw.SD_H:.3f}"' in svg,
          "the SD cutout is not in the SVG")


def test_drawing_geometry():
    """Pins the 15 Aug graphics-round drawing: row rhythm, SD, no title,
    no rail dashes, zone wash to the edge, jack captions under the jacks."""
    by = {c.enum: c for c in hw.ALL_HW}
    check(abs(hw.JACK_Y - 114.0) < 1e-9, f"JACK_Y is {hw.JACK_Y}, not 114")
    check((hw.SD_W, hw.SD_H) == (11.0, 6.0), f"SD size is {hw.SD_W}x{hw.SD_H}")
    check(abs(hw.SD_Y - hw.JACK_Y) < 1e-9, f"SD_Y is {hw.SD_Y}, not on the jack row")
    check(abs(by["ENGINE_A"].x - 70.25) < 1e-9, "ENGINE is not at the VOICE head")
    check(abs(by["ENGINE_A"].y - 49.25) < 1e-9, "ENG did not rise toward ATK")
    check(abs(by["SOURCE_A"].y - 47.61) < 1e-9, "TIMB did not rise toward SUB")
    for enum in ("ATTACK_A", "DECAY_A", "RES_A", "SUB_A"):
        ly = hw.hw_label(by[enum])[1]
        check(ly > by[enum].y, f"{enum} caption flipped above the knob")
        check(abs(by[enum].y - hw.Y_B1K) < 1e-9, f"{enum} left the ATK row")
    check(abs(by["FLUXRATE_A"].y - 89.86) < 1e-9, "TIME did not rise toward MIX")
    check(abs(by["LINK_A"].y - 89.86) < 1e-9, "LINK did not rise toward MIX")
    check(abs(by["TIDE"].y - 50.22) < 1e-9, "TIDE did not rise toward TEMP")
    check(abs(by["PACE"].y - 50.22) < 1e-9, "PACE did not rise toward SHFL")
    check(abs(by["REV_DECAY"].y - 79.00) < 1e-9, "DECY did not drop away from MORPH")
    check(abs(by["REV_TONE"].y - 97.00) < 1e-9, "TONE did not follow DECY")
    check(abs(by["SHIFTBTN"].y - hw.JACK_Y) < 1e-9, "SHIFT is not on the jack row")
    check(abs(by["MODBTN"].y - hw.JACK_Y) < 1e-9, "MOD is not on the jack row")
    check(not hw.TEXTS, "title/legend TEXTS should be empty")
    lx, ly = hw.hw_label(by["IN_L"])[:2]
    check(ly > by["IN_L"].y, "IN L caption is not under the jack")
    svg = hw.svg()
    check('y1="9.00"' not in svg and 'y1="119.50"' not in svg,
          "rail keep-out dashes are still drawn")
    check('y="0"' in svg or 'y="0.000"' in svg, "zone wash does not start at the edge")
    check(f'fill="{hw.HW_KNOB}"' not in svg, "black knob discs are back")
    check("<polygon" in svg, "smoothed silhouette wash is missing")
    check(f'width="{hw.mm(hw.ZONE_A)}"' not in svg, "hard ZONE_A wash rect is back")
    for c in hw.HW_PARAMS:
        if hw.hw_class(c.enum) == "P":
            continue
        fill = hw.knob_disc_fill(c.x)
        needle = (f'cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="{hw.mm(c.r)}" '
                  f'fill="{fill}"')
        check(needle in svg, f"{c.enum} disc is not the zone colour ({fill})")
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
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw panel guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
