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
    assert [c.enum for c in hw.HW_INPUTS] == [c.enum for c in gp.INPUTS]
    assert [c.enum for c in hw.HW_OUTPUTS] == [c.enum for c in gp.OUTPUTS]
    assert [c.enum for c in hw.HW_LIGHTS] == [c.enum for c in gp.LIGHTS]

def test_static_captions_only():
    # An aluminium panel is printed: every label is the resting word, and the
    # generated header must carry NO dynamic-caption table.
    for c in hw.HW_PARAMS:
        words = gp.dynamic_words(c.enum.rsplit("_", 1)[0])
        if words:
            assert c.label == words[0], c.enum
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp")).read()
    assert "DynCaption" not in src

def test_hardware_footprints():
    # Screen-widget radii are meaningless on sheet metal. Minimum clearance
    # radii per kind (mm): big cap 8.0, 9mm pot + mini cap 5.5, pad 4.0,
    # jack 4.0, LED 1.5.
    minima = {gp.BIGKNOB: 8.0, gp.KNOBC: 8.0, gp.SMKNOB: 5.5, gp.KNOBI: 5.5,
              gp.SW2: 4.0, gp.LATCH: 4.0, gp.SMBTN: 4.0,
              gp.IN: 4.0, gp.OUT: 4.0, gp.LIGHT: 1.5}
    for c in hw.ALL_HW:
        assert c.r >= minima[c.kind] - 1e-9, (c.enum, c.r)

def test_rail_keepout():
    # Rails and screws own the top/bottom ~9 mm; VCV's 2 mm rule is not enough.
    for c in hw.ALL_HW:
        assert c.y - c.r >= hw.KEEP_TOP - 1e-9, (c.enum, "top")
        assert c.y + c.r <= hw.KEEP_BOT + 1e-9, (c.enum, "bottom")
        assert c.x - c.r >= 2.0 and c.x + c.r <= hw.W - 2.0, (c.enum, "side")

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

def test_mirror_symmetry():
    # Deck B is deck A mirrored — the instrument's identity, kept by machine.
    pos_a = {c.enum[:-2]: (c.x, c.y) for c in hw.HW_PARAMS if c.enum.endswith("_A")}
    pos_b = {c.enum[:-2]: (c.x, c.y) for c in hw.HW_PARAMS if c.enum.endswith("_B")}
    assert pos_a.keys() == pos_b.keys()
    for k, (xa, ya) in pos_a.items():
        xb, yb = pos_b[k]
        assert abs((hw.W - xa) - xb) < 1e-6 and abs(ya - yb) < 1e-6, k

def test_header_contract():
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp")).read()
    assert "namespace spkyhw" in src
    assert "kHwHP = 60" in src
    for tbl in ("kParamCtls", "kInputCtls", "kOutputCtls", "kLightCtls",
                "kPanelTexts"):
        assert tbl in src, tbl
    # Ids are the MAIN module's enum values — one id space, two layouts.
    assert re.search(r"\{\s*RATE_A\s*,", src)

def test_svg_exists_and_is_60hp():
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    assert f'width="{hw.W:.3f}mm"' in svg and 'height="128.500mm"' in svg

def test_shared_knob_labels_do_not_coincide():
    # BEND shares ATTACK's knob; an aluminium plate prints both words
    # stacked, never superimposed.
    by_enum = {c.enum: c for c in hw.HW_PARAMS}
    for side in ("_A", "_B"):
        atk, bend = by_enum["ATTACK" + side], by_enum["STAGES" + side]
        la, lb = hw.hw_label(atk), hw.hw_label(bend)
        assert abs(la[1] - lb[1]) >= 2.0, side

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


def test_labels_stay_off_neighbour_footprints():
    # A caption may sit near its own control, but its anchor must never
    # land inside ANOTHER control's clearance circle.
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        for other in hw.ALL_HW:
            if other is c:
                continue
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            assert d >= other.r - 1e-6, (c.enum, other.enum, round(d, 2))


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw panel guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
