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


def test_plate_paints_survive_nanosvg():
    """The plate is the 15 Aug design round, variant 2a: three tinted zones,
    an airflow/ember print, a baked fade. Rack parses the panel with NanoSVG,
    which silently drops <mask>, <pattern> and <filter> -- a design that
    leans on any of them looks right in a browser and wrong on the module,
    which is exactly how it would ship unnoticed."""
    svg = hw.svg()
    for zone in ("zoneA", "zoneC", "zoneB"):
        check(f'fill="url(#hw-{zone})"' in svg, f"plate zone {zone} is missing")
    check("<mask" not in svg and "mask=" not in svg,
          "an SVG mask is back -- NanoSVG drops it, so the fade must be baked")
    check("<pattern" not in svg, "a <pattern> is in the plate -- NanoSVG drops it")
    check("<filter" not in svg and "feTurbulence" not in svg,
          "a <filter> is in the plate -- NanoSVG drops it")
    check("rgba(" not in svg,
          "rgba() is in the plate -- NanoSVG's colour parser is not a browser's")
    # Measured, not assumed: with objectBoundingBox gradients Rack painted
    # the fadeA overlay opaque across the whole of deck A and swallowed the
    # airflow print, while the mirrored deck B rendered correctly. The
    # browser preview showed both halves. Every gradient stays in mm.
    n_grad = svg.count("<linearGradient")
    check(n_grad == 8, f"expected 8 plate gradients, got {n_grad}")
    check(svg.count('gradientUnits="userSpaceOnUse"') == n_grad,
          "a gradient is in bounding-box units -- NanoSVG and the browser "
          "then disagree about the plate")
    check(f'stroke-opacity="{hw.SILHOUETTE_OPACITY}"' in svg,
          "the airflow/ember print is missing")
    check(svg.count('d="M-14.0,8.7C') == 1, "the airflow paths are missing")
    check(svg.count('transform="translate(304.800,0) scale(-1,1)"') == 1,
          "the ember half is not the mirrored airflow artwork")
    for fade in ("fadeA", "fadeB", "seamL", "seamR"):
        check(svg.count(f'fill="url(#hw-{fade})"') == 1,
              f"the baked {fade} overlay is missing")


def test_group_raster_closes():
    """The frames sit on ONE raster and the arithmetic has to close: 3 mm of
    air between any two boxes, a shared deck edge at 120 mm, and deck B is
    deck A mirrored. Hand-placed frames drift; a cut list cannot."""
    boxes = hw.BOXES
    check(len(boxes) == 24, f"expected 24 group frames, got {len(boxes)}")
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            ox = min(a.x + a.w, b.x + b.w) - max(a.x, b.x)
            oy = min(a.y + a.h, b.y + b.h) - max(a.y, b.y)
            tag = f"{a.n}/{a.side} and {b.n}/{b.side}"
            check(ox <= 1e-9 or oy <= 1e-9, f"{tag} overlap")
            if oy > 1e-9:
                check(-ox >= hw.BOX_GAP - 1e-9, f"{tag} are only {-ox:.2f} mm apart")
            elif ox > 1e-9:
                check(-oy >= hw.BOX_GAP - 1e-9, f"{tag} are only {-oy:.2f} mm apart")
    for (_sy, _sh, x0, _cuts, names_a, _nb, _c), (y, h) in zip(hw.GROUP_ROWS,
                                                               hw.ROW_FRAMES):
        row = [b for b in boxes if abs(b.y - y) < 1e-9]
        check(len(row) == 2 * len(names_a) + 1, f"row y={y} has {len(row)} boxes")
        for b in row:
            check(abs(b.h - h) < 1e-9, f"{b.n}/{b.side} is not row height {h}")
        left = sorted((b for b in row if b.side == "A"), key=lambda b: b.x)
        right = sorted((b for b in row if b.side == "B"), key=lambda b: b.x)
        check(abs(left[0].x - x0) < 1e-9,
              f"row y={y} does not start at {x0} (got {left[0].x})")
        check(abs(left[-1].x + left[-1].w - hw.DECK_EDGE) < 1e-9,
              f"row y={y} does not end on the deck edge {hw.DECK_EDGE}")
        for p, q in zip(left, reversed(right)):
            check(abs((hw.W - (q.x + q.w)) - p.x) < 1e-9,
                  f"{p.n}: deck B frame is not deck A mirrored")
            check(abs(p.w - q.w) < 1e-9, f"{p.n}: mirrored frame width differs")
        centre = [b for b in row if b.side == "C"]
        check(abs(centre[0].x - hw.CENTRE_L) < 1e-9, "centre column moved")
        check(abs(centre[0].x + centre[0].w - (hw.W - hw.CENTRE_L)) < 1e-9,
              "centre column is not symmetric")
    check(len({b.idx for b in boxes}) == len(hw.GROUP_ORDER),
          "group legend numbering is not one number per group name")


def test_middle_band_runs_on_three_lines():
    """Nothing in MOTION/VOICE/TIMING sits between the band's three lines.

    This replaces four separate pins (ENG at 49.25, TIMB at 47.61, TIDE and
    PACE at 50.22) that each recorded where one knob had been nudged. Four
    such numbers cannot disagree with each other -- which is how the band
    ended up with five different heights, each individually "as approved".
    A line rule can disagree, and does the moment anything drifts off one.

    FILT came DOWN to the big-cap line rather than the other three coming
    up: MORPH cannot pass 52.0 without displacing SYNC's caption, and a
    shorter band leaves the jack row without a margin."""
    lines = (hw.Y_B1K, hw.Y_B1M, hw.Y_B1G)
    seed_y, seed_h = hw.GROUP_ROWS[1][0], hw.GROUP_ROWS[1][1]
    seen, off = {}, []
    for c in hw.ALL_HW:
        if not (seed_y <= c.y <= seed_y + seed_h):
            continue
        hit = [ln for ln in lines if abs(c.y - ln) < 1e-9]
        if hit:
            seen.setdefault(hit[0], []).append(c.enum)
        else:
            off.append(f"{c.enum} at y={c.y}")
    check(not off, f"middle-band controls between the lines: {off}")
    check(len(seen) == 3,
          f"only {len(seen)} of the three band lines are used -- {sorted(seen)}")
    for ln, members in seen.items():
        check(len(members) >= 2, f"line y={ln} carries only {members}")
    # Every big cap on the band's big line, and the line is the lowest one.
    for c in hw.HW_PARAMS:
        if hw.hw_class(c.enum) != "G":
            continue
        if not (seed_y <= c.y <= seed_y + seed_h):
            continue
        check(abs(c.y - hw.Y_B1G) < 1e-9,
              f"{c.enum} is a big cap in the middle band but not on Y_B1G")


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
    # The drawn keycap is 8 mm square, so 4.0 is its half-width. A BODY_R
    # that disagrees with the drawing would put the pads' gap silently off.
    svg = hw.svg()
    for c in hw.HW_PARAMS:
        if hw.hw_class(c.enum) != "P":
            continue
        check(f'width="{hw.mm(2 * hw.body_r(c))}"' in svg,
              f"{c.enum} is drawn at a size BODY_R does not know about")


def test_rows_are_centred_on_their_ink():
    """A group whose contents sit high in its frame with a fat empty strip
    underneath reads as a mistake, and a fixed row height produces exactly
    that, because captions hang below their controls. Every row's frame
    must carry the SAME margin above and below what it prints.

    The chain is what makes this checkable at all: rows are spaced by
    BOX_GAP from each other, so once ROW1_TOP is chosen every other margin
    follows. Bolted-on row heights would each be a free number and nothing
    would ever go red."""
    prev_bot = None
    for row, (y, h) in zip(hw.GROUP_ROWS, hw.ROW_FRAMES):
        t, b = hw._row_ink(row)
        up, dn = t - y, (y + h) - b
        check(abs(up - dn) < 1e-6,
              f"row at y={y:.2f} is off centre: {up:.2f} above the ink, "
              f"{dn:.2f} below")
        check(up > 0.0, f"row at y={y:.2f} clips its own contents")
        if prev_bot is not None:
            check(abs((y - prev_bot) - hw.BOX_GAP) < 1e-6,
                  f"row at y={y:.2f} is {y - prev_bot:.2f} mm below the one "
                  f"above, not {hw.BOX_GAP}")
        prev_bot = y + h
    # The status row is as high as its own legend may print, and no higher:
    # that legend's baseline is the rail line itself.
    top = hw.ROW_FRAMES[0][0]
    check(abs((top + hw.LEGEND_DY) - hw.KEEP_TOP) < 1e-6,
          f"the status row's legend is at {top + hw.LEGEND_DY:.2f}, not on "
          f"the rail line {hw.KEEP_TOP}")


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


def test_bodies_and_captions_sit_inside_their_frame():
    """Variant 2a draws the frames against the REAL bodies (12 mm and 8.8 mm
    pots, 6.2 mm jacks), not the finger-clearance circles the layout is
    spaced on -- that is where its air between the boxes comes from. So a
    body or a caption crossing its own frame is the one way this raster can
    go wrong, and reading the SVG will not show it."""
    loose = []
    for c in hw.ALL_HW:
        b = hw.box_of(c)
        if b is None:
            loose.append(c.enum)
            continue
        r = hw.body_r(c)
        check(c.x - r >= b.x - 1e-9 and c.x + r <= b.x + b.w + 1e-9 and
              c.y - r >= b.y - 1e-9 and c.y + r <= b.y + b.h + 1e-9,
              f"{c.enum} ({r} mm body) pokes out of frame {b.n}/{b.side}")
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[:2]
        check(b.x <= lx <= b.x + b.w and b.y <= ly <= b.y + b.h,
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is outside {b.n}/{b.side}")
    check(sorted(loose) == ["MODBTN", "SHIFTBTN"],
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
    """Pins the 15 Aug graphics-round drawing: row rhythm, SD, no title,
    no rail dashes, zone wash to the edge, jack captions under the jacks."""
    by = {c.enum: c for c in hw.ALL_HW}
    check(abs(hw.JACK_Y - 114.0) < 1e-9, f"JACK_Y is {hw.JACK_Y}, not 114")
    check((hw.SD_W, hw.SD_H) == (11.0, 6.0), f"SD size is {hw.SD_W}x{hw.SD_H}")
    check(abs(hw.SD_Y - hw.JACK_Y) < 1e-9, f"SD_Y is {hw.SD_Y}, not on the jack row")
    check(abs(by["ENGINE_A"].x - 70.25) < 1e-9, "ENGINE is not at the VOICE head")
    for enum in ("ATTACK_A", "DECAY_A", "RES_A", "SUB_A"):
        ly = hw.hw_label(by[enum])[1]
        check(ly > by[enum].y, f"{enum} caption flipped above the knob")
        check(abs(by[enum].y - hw.Y_B1K) < 1e-9, f"{enum} left the ATK row")
    check(abs(by["FLUXRATE_A"].y - 89.86) < 1e-9, "TIME did not rise toward MIX")
    check(abs(by["LINK_A"].y - 89.86) < 1e-9, "LINK did not rise toward MIX")
    check(abs(by["REV_DECAY"].y - 79.00) < 1e-9, "DECY did not drop away from MORPH")
    check(abs(by["REV_TONE"].y - 97.00) < 1e-9, "TONE did not follow DECY")
    check(abs(by["SHIFTBTN"].y - hw.JACK_Y) < 1e-9, "SHIFT is not on the jack row")
    check(abs(by["MODBTN"].y - hw.JACK_Y) < 1e-9, "MOD is not on the jack row")
    # Lettering Rack has to draw itself: brand block plus two rows per frame
    # (index, name). Rack does not render SVG text, so an empty TEXTS means a
    # plate whose legends exist in the preview and nowhere else.
    check(len(hw.TEXTS) == len(hw.BRAND_TEXTS) + 2 * len(hw.BOXES),
          f"TEXTS carries {len(hw.TEXTS)} rows, not brand + one pair per frame")
    words = {t[6] for t in hw.TEXTS}
    for w in ("FIREFLOW", "DECK A", "DECK B", "60 HP", "SEQUENCE", "ROOM"):
        check(w in words, f"{w!r} is not in the panel lettering")
    lx, ly = hw.hw_label(by["IN_L"])[:2]
    check(ly > by["IN_L"].y, "IN L caption is not under the jack")
    svg = hw.svg()
    check('y1="9.00"' not in svg and 'y1="119.50"' not in svg,
          "rail keep-out dashes are still drawn")
    check('y="0"' in svg or 'y="0.000"' in svg, "the plate zones do not start at the edge")
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
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw panel guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
