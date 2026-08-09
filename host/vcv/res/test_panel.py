#!/usr/bin/env python3
"""Guard rails for the generated Fireflow panel.

Runs the generator in-process and asserts the things that must never drift:
the param/input/output enum ORDER (patch compatibility), the panel geometry
(nothing overlaps, nothing falls off the plate) and the target coordinates of
the 2026-07-18 faceplate redesign.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_panel.py
"""
import os, sys, math, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as g

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def approx(a, b, tol=0.02):
    return abs(a - b) <= tol


def ctl(enum):
    for c in g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS:
        if c.enum == enum:
            return c
    raise KeyError(enum)


# --- the frozen contract: enum ORDER defines param ids in every saved patch ---
# DETUNE_A/B moved from the trailing hidden pair into the templated per-part
# block (spec 2026-08-09 hw-control-reduction task 10: DETUNE returns to the
# panel, filling the STEP pad's freed PAD_X[2] slot) -- patch compatibility is
# a non-goal for this plan, so every id from DETUNE_A onward shifted, and
# PART_STRIDE grew from 19 to 20 (the templated block gained one control; the
# total param COUNT is unchanged, only its position within PARAMS moved).
# SPOT, MASTER_DRIVE, REV_SMEAR, REV_MOD and DRIVE_A/B are gone entirely
# (task 9): the first four became fixed-by-ear constants in pushParams, SPOT
# is a genuine feature loss, and DRIVE_A/B was dead menu-only patch state
# that never reached the engine. HIDDEN_PARAMS is now empty, so DRIVE_A/B's
# old trailing pair is simply gone, not replaced.
PARAM_ORDER = [
    'RATE_A', 'SHAPE_A', 'DENSITY_A', 'SMOOTH_A', 'RANGE_A', 'MELODY_A',
    'MOD_A', 'TUNE_A', 'ATTACK_A', 'DECAY_A', 'RES_A', 'SUB_A', 'SOURCE_A',
    'FLUX_A', 'GRIT_A', 'COMP_A', 'STEPS_A', 'ENGINE_A', 'DETUNE_A',
    'SONG_A',
    'RATE_B', 'SHAPE_B', 'DENSITY_B', 'SMOOTH_B', 'RANGE_B', 'MELODY_B',
    'MOD_B', 'TUNE_B', 'ATTACK_B', 'DECAY_B', 'RES_B', 'SUB_B', 'SOURCE_B',
    'FLUX_B', 'GRIT_B', 'COMP_B', 'STEPS_B', 'ENGINE_B', 'DETUNE_B',
    'SONG_B',
    'MORPH', 'TEMPO', 'COUPLE', 'SCALE', 'DRIFT',
    'REV_SIZE', 'REV_DECAY', 'REV_TONE',
    'REV_DIFF', 'CHOKE', 'FILT_A', 'FILT_B', 'TIDE',
    'FLUXRATE_A', 'FLUXRATE_B', 'FLUXFB_A', 'FLUXFB_B', 'COLOR_A', 'COLOR_B',
    'LINK_A', 'LINK_B', 'STAGES_A', 'STAGES_B',
    'REC_A', 'REC_B', 'REV_MIX_A', 'REV_MIX_B',
    'SHUFFLE',
]
PARAM_TIPS = [
    'RATE', 'SHAPE', 'DENS', 'SMTH', 'RANGE', 'Variation', 'MOD', 'TUNE',
    'ATK', 'DEC', 'RES', 'SUB', 'SOURCE', 'FLUX', 'GRIT', 'Level / Comp', 'STPS',
    'ENG', 'Detune', 'SONG',
    'RATE', 'SHAPE', 'DENS', 'SMTH', 'RANGE', 'Variation', 'MOD', 'TUNE',
    'ATK', 'DEC', 'RES', 'SUB', 'SOURCE', 'FLUX', 'GRIT', 'Level / Comp', 'STPS',
    'ENG', 'Detune', 'SONG',
    'MORPH', 'TEMPO', 'FREE|GRID', 'SCALE', 'DRIFT',
    'SIZE', 'DECAY', 'TONE', 'DIFF', 'CHOKE',
    'FILT', 'FILT', 'TIDE', 'FLUX time', 'FLUX time', 'FFB', 'FFB',
    'COLOR', 'COLOR', 'LINK', 'LINK', 'BBD Bend', 'BBD Bend', 'REC', 'REC',
    'Room send', 'Room send',
    'SHUFL',
]
INPUT_ORDER = ['IN_L', 'IN_R', 'CLOCK', 'RESET']
OUTPUT_ORDER = ['OUT_L', 'OUT_R', 'PITCH_A', 'GATE_A', 'PITCH_B', 'GATE_B']
LIGHT_ORDER = ['GATE_A_L', 'GATE_B_L', 'REC_A_L', 'REC_B_L']


def test_enum_order():
    """Patch compatibility. If this fails, every saved .vcv breaks."""
    check([c.enum for c in g.PARAMS] == PARAM_ORDER, "PARAMS order changed")
    check(PARAM_ORDER[-1] == 'SHUFFLE',
          "SHUFFLE must be the trailing ParamId now that DRIVE_A/B "
          "(task 9, spec 2026-08-09 hw-control-reduction) is retired and "
          "HIDDEN_PARAMS is empty")
    check([c.enum for c in g.INPUTS] == INPUT_ORDER, "INPUTS order changed")
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER, "OUTPUTS order changed")
    check([c.enum for c in g.LIGHTS] == LIGHT_ORDER, "LIGHTS order changed")
    # 19 -> 20 (task 10, spec 2026-08-09 hw-control-reduction): DETUNE_A/B
    # moved from the trailing hidden pair into the templated per-part block,
    # one extra control per deck. The total param COUNT is unchanged (they
    # were already counted, in HIDDEN_PARAMS) -- only their stride position.
    check(g.PART_STRIDE == 20, f"PART_STRIDE is {g.PART_STRIDE}, must be 20")


def test_source_and_hidden_detune_partition():
    """SOURCE owns the former DTUN widgets; DETUNE is a real panel knob again
    (spec 2026-08-09 hw-control-reduction task 10 -- out of HIDDEN_PARAMS and
    into the templated per-part block, "bei drones ist das sehr stark").
    DRIVE_A/B, the last HIDDEN_PARAMS resident, is retired outright (task 9):
    it was menu-only patch state that never reached the engine, so
    HIDDEN_PARAMS is now empty -- no widgetless patch state survives at all."""
    visible = [c.enum for c in g.PANEL_PARAMS]
    hidden = [c.enum for c in g.HIDDEN_PARAMS]
    check("SOURCE_A" in visible and "SOURCE_B" in visible,
          "SOURCE controls must stay visible")
    check("DETUNE_A" in visible and "DETUNE_B" in visible,
          "DETUNE must be a visible panel control")
    check(hidden == [], f"HIDDEN_PARAMS is not empty: {hidden!r}")
    check("DRIVE_A" not in visible and "DRIVE_B" not in visible,
          "dead DRIVE_A/B leaked into panel controls")
    appended = [c.enum for c in g.APPENDED_PANEL_PARAMS]
    check([c.enum for c in g.PARAMS] == visible + hidden + appended,
          "complete ParamId order must preserve declared partitions")
    h = g.header()
    check(h.count("{DETUNE_A, WK_SMKNOB,") == 1
          and h.count("{DETUNE_B, WK_SMKNOB,") == 1,
          "DETUNE must be a real widget in kParamCtls")
    check("{DRIVE_A," not in h and "{DRIVE_B," not in h,
          "dead DRIVE_A/B leaked into kParamCtls")


def test_time_knob_replaces_div_and_mult():
    """DIV and MULT described one quantity. One notched knob does it, and the
    modulation sink keeps a neutral base so CV can still bend the tape."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("FLUXTIME_A" not in names and "FLUXTIME_B" not in names,
          "the MULT knobs still exist")
    rate = [c for c in gp.PARAMS if c.enum == "FLUXRATE_A"][0]
    check(rate.label == "TIME", f"FLUXRATE_A still prints {rate.label!r}")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("spky::FXT_FLUX_TIME, 0.5f" in cpp,
          "the flux-time modulation base is not pinned to neutral")
    check("FLUXTIME_A" not in cpp, "Fireflow.cpp still references FLUXTIME_A")


def test_bbd_pitch_flux_time_collections():
    """The three generator views keep saved ParamIds, Rack widgets, and the
    static Synth preview independently intentional. FLUXTIME_A/B (MULT) is
    retired (task 6, spec 2026-08-09 hw-control-reduction), so
    APPENDED_PANEL_PARAMS is now empty and TIME (FLUXRATE_A/B, unchanged
    ParamId) is the only surviving delay-time widget."""
    persistent = [c.enum for c in g.PARAMS]
    runtime = [c.enum for c in g.RUNTIME_PANEL_PARAMS]
    static = [c.enum for c in g.STATIC_PANEL_PARAMS]
    check(g.APPENDED_PANEL_PARAMS == [],
          "APPENDED_PANEL_PARAMS must stay empty -- MULT was its only member")
    check("FLUXTIME_A" not in persistent and "FLUXTIME_B" not in persistent,
          "FLUXTIME must not survive as a saved ParamId")
    check(persistent[-1] == 'SHUFFLE',
          "SHUFFLE must be the trailing ParamId now that DRIVE_A/B "
          "(task 9) is retired")
    check(all(e in runtime for e in ('STAGES_A', 'STAGES_B',
                                      'FLUXRATE_A', 'FLUXRATE_B')),
          "runtime table lacks PITCH or TIME widgets")
    check('STAGES_A' not in static and 'STAGES_B' not in static,
          "static preview contains the BBD-only PITCH widgets")
    check(all(e in static for e in ('ATTACK_A', 'ATTACK_B',
                                     'FLUXRATE_A', 'FLUXRATE_B')),
          "static Synth preview lacks ATK or TIME")
    check(g.PARAMS == g.PANEL_PARAMS + g.HIDDEN_PARAMS
                      + g.APPENDED_PANEL_PARAMS,
          "persistent ParamId order no longer matches the declared partitions")
    check(not any(c.enum in runtime for c in g.HIDDEN_PARAMS),
          "menu-only DRIVE leaked into runtime widgets")
    check(persistent == PARAM_ORDER, "legacy ParamId order changed")

    header = g.header()
    for suffix in ('_A', '_B'):
        check(header.count(f"{{STAGES{suffix}, WK_SMKNOB,") == 1,
              f"generated header lacks runtime PITCH{suffix} row")
        check(header.count(f"{{FLUXRATE{suffix}, WK_KNOBI,") == 1,
              f"generated header lacks runtime TIME{suffix} row")
    check("FLUXTIME" not in header,
          "generated header still carries FLUXTIME")


def test_readme_matches_the_caption_table():
    """Every word the generator prints must be findable in the manual, and no
    retired word may still be presented as current."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    for _target, _driver, words in g.DYNAMIC_CAPTIONS:
        for word in set(words):
            check(word in readme, f"README never mentions the caption {word!r}")
    for word in ("BEND", "TIME", "SEND", "PUSH"):
        check(word in readme, f"README never mentions the caption {word!r}")
    # The STGS-to-BEND migration sentence names the retired `STGS` label on
    # purpose (test_bbd_pitch_and_tape_time_user_documentation requires it
    # verbatim) -- it explains STGS is gone, it does not present STGS as a
    # live caption, so it is excluded here the same way that test excludes it.
    migration = re.compile(r"the visible `STGS`\s+label is gone")
    unmigrated = migration.sub("", readme)
    for stale in ("`STGS`", "`MELO` / `SCAN`", "`SUB` / `LEN`"):
        check(stale not in unmigrated,
              f"README still presents the retired {stale}")


def test_source_and_detune_user_documentation():
    """The VCV README must explain the contextual SOURCE control and the
    independent constant detune spread users configure from the menu."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    for term in ("TIMB", "FRAME", "ORG", "6 ct"):
        check(term in readme, f"VCV README does not document {term}")
    check(
        re.search(
            r"Detune\b[^.\n]*\bindependent of SOURCE\b",
            readme,
            flags=re.IGNORECASE,
        ) is not None,
        "VCV README must state that Detune is independent of SOURCE",
    )


def test_bbd_pitch_and_tape_time_user_documentation():
    """The README must match the BBD/BEND faceplate contract and still
    explain the retired MULT knob's surviving modulation sink (task 6,
    spec 2026-08-09 hw-control-reduction)."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    check("BBD BEND" in readme, "README omits the BBD BEND faceplate slot")
    check("Freeze Attack" in readme, "README omits menu-only BBD Freeze Attack")
    check("MULT" in readme and "x0.25" in readme and "x4" in readme,
          "README omits the retired tape multiplier's surviving mod sink")
    migration = re.compile(r"the visible `STGS`\s+label is gone")
    check(migration.search(readme) is not None,
          "README omits the STGS-to-BEND migration explanation")
    check("STGS" not in migration.sub("", readme),
          "README still presents STGS outside the migration explanation")


def test_source_and_detune_documentation_has_no_legacy_surface_contract():
    """The old visible-DTUN and SOURCE-coupled-detune claims must not return."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    with open(os.path.join(here, "..", "..", "..", "docs", "roadmap.md"),
              encoding="utf-8") as f:
        roadmap = f.read()
    for legacy in (
            "SUB and DTUN give up their Synth jobs in the Sampler.",
            "Synth keeps both.",
    ):
        check(legacy not in readme, f"VCV README retains legacy claim: {legacy}")
    check(
        re.search(
            r"dialing in SUB ahead of time audibly detunes the\s+Synth that's still playing\.",
            readme,
        ) is None,
        "VCV README retains the legacy SUB-detunes-Synth claim",
    )
    for legacy, message in (
            (r"wie SUB\s+und DTUN es schon taten\.",
             "VCV README retains DTUN as a sampler-bound control"),
            (r"aus Konsistenz mit SUB und\s+DTUN:",
             "VCV README retains DTUN as a sampler control alias"),
    ):
        check(re.search(legacy, readme) is None, message)
    for legacy in (
            "(ATK DEC FILT RES SUB DTUN)",
            "Morphagene-style surface (DENS, SCAN, NEW, LEN/ORG)",
    ):
        check(legacy not in roadmap, f"roadmap retains legacy surface: {legacy}")


def test_param_runtime_tip_contract():
    """Faceplate captions may change, but Rack parameter names/tooltips may not."""
    got = [c.tip for c in g.PARAMS]
    check(got == PARAM_TIPS,
          "parameter runtime tip contract changed: "
          + repr([(c.enum, c.tip, want) for c, want in zip(g.PARAMS, PARAM_TIPS)
                  if c.tip != want]))
    ids = {c.enum: i for i, c in enumerate(g.PARAMS)}
    check(PARAM_TIPS[ids["LINK_A"]:ids["STAGES_B"] + 1]
          == ['LINK', 'LINK', 'BBD Bend', 'BBD Bend'],
          "BBD Bend runtime tips drifted")
    check(PARAM_TIPS[-1] == 'SHUFL',
          "SHUFFLE must be the trailing runtime tip now that the DRIVE "
          "pair (task 9) is retired")
    for enum, caption, tip in (
            ("FLUX_A", "MIX", "FLUX"), ("FLUX_B", "MIX", "FLUX"),
            ("FLUXRATE_A", "TIME", "FLUX time"),
            ("FLUXRATE_B", "TIME", "FLUX time"),
            ("FLUXFB_A", "FB", "FFB"), ("FLUXFB_B", "FB", "FFB")):
        c = ctl(enum)
        check(c.label == caption and c.tip == tip,
              f"{enum}: caption/tip {c.label!r}/{c.tip!r}, "
              f"want {caption!r}/{tip!r}")


def test_link_stages_params():
    """LINK/STAGES are appended at the end of PARAMS, not templated into
    part_controls() -- appending keeps PART_STRIDE unchanged so SONG_A/B,
    every part-B id and every already-appended tail param keep their id.
    LINK took over the panel slot DRIVE, then DRAG, used to hold, renamed
    in place (spec 2026-07-28 flux-link) exactly as DRIVE -> DRAG was
    renamed in place the same day, and DUST -> DRIVE was renamed in place
    on 2026-07-27, which is itself where STAGES came from (DUST/ROT,
    spec 2026-07-27 flux-bbd-delay): the POSITIONS are what saved patches
    depend on, and they did not move. DRIVE itself became hidden patch state
    -- see test_source_and_hidden_detune_partition for that half. The literal
    PART_STRIDE number is pinned once, in test_enum_order -- this test only
    needs the live value to prove LINK/STAGES sit past both templated part
    blocks."""
    ids = {c.enum: i for i, c in enumerate(g.PARAMS)}
    for e in ("LINK_A", "LINK_B", "STAGES_A", "STAGES_B"):
        check(e in ids, f"{e} missing")
        check(ids[e] >= 2 * g.PART_STRIDE, f"{e} must be appended, not templated")
    # The rename must not have reordered them: COLOR_B then LINK_A, LINK_B,
    # STAGES_A, STAGES_B, then REC_A. A reorder here silently remaps every
    # saved patch's two FLUX voicing knobs onto each other.
    check(ids["LINK_A"] == ids["COLOR_B"] + 1, "LINK_A must follow COLOR_B")
    check(ids["STAGES_B"] + 1 == ids["REC_A"], "REC_A must follow STAGES_B")


def test_link_stages_kind():
    """LINK/STAGES must render as the small knob (GLYPH_R[SMKNOB] = 3.0 mm),
    not the big knob (4.2 mm) -- a SMKNOB->BIGKNOB typo still clears
    test_no_overlap's minimum spacing by 0.43 mm, so it would ship silently
    without a kind pin of its own. Read the generated header string, not
    g.PARAMS' in-memory `.kind`."""
    h = g.header()
    for enum in ("LINK_A", "LINK_B", "STAGES_A", "STAGES_B"):
        check(h.count(f"{{{enum}, WK_SMKNOB,") == 1,
              f"{enum} is not WK_SMKNOB in the generated header")


def test_rec_params():
    """REC is appended, not templated -- appending keeps PART_STRIDE unchanged
    so every saved .vcv keeps its param ids (the literal number is pinned once,
    in test_enum_order). Same guard shape as test_link_stages_params, and the
    kind is pinned the same way test_link_stages_kind pins LINK/STAGES: a LATCH
    that silently became an SMBTN would still clear test_no_overlap (identical
    radius), so the kind needs its own check."""
    ids = {c.enum: i for i, c in enumerate(g.PARAMS)}
    for e in ("REC_A", "REC_B"):
        check(e in ids, f"{e} missing")
        check(ids[e] >= 2 * g.PART_STRIDE, f"{e} must be appended, not templated")
    check(ids["REC_A"] > ids["STAGES_B"], "REC must append AFTER the existing tail")
    h = g.header()
    for e in ("REC_A", "REC_B"):
        check(h.count(f"{{{e}, WK_LATCH,") == 1,
              f"{e} is not WK_LATCH in the generated header")


def test_reverb_mix_params():
    """REV_MIX_A/B are appended (not templated) so PART_STRIDE stays unchanged
    (the literal number is pinned once, in test_enum_order), and they carry
    the 'SEND' label as the FX top row's 4th slot -- the shared centre
    REV_MIX is gone."""
    ids = {c.enum: i for i, c in enumerate(g.PARAMS)}
    check('REV_MIX' not in ids, "the shared centre REV_MIX must be removed")
    for e in ("REV_MIX_A", "REV_MIX_B"):
        check(e in ids, f"{e} missing")
        check(ids[e] >= 2 * g.PART_STRIDE, f"{e} must be appended, not templated")
        check(ctl(e).label == "SEND", f"{e} label must be 'SEND'")
    h = g.header()
    for e in ("REV_MIX_A", "REV_MIX_B"):
        check(h.count(f"{{{e}, WK_SMKNOB,") == 1,
              f"{e} is not WK_SMKNOB in the generated header")


def test_no_overlap():
    """No two glyphs may touch -- Rack widgets would steal each other's clicks."""
    all_c = g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS
    for i, a in enumerate(all_c):
        ra = g.GLYPH_R[a.kind]
        for b in all_c[i + 1:]:
            if {a.enum, b.enum} in ({'ATTACK_A', 'STAGES_A'},
                                    {'ATTACK_B', 'STAGES_B'}):
                continue
            rb = g.GLYPH_R[b.kind]
            d = math.hypot(a.x - b.x, a.y - b.y)
            check(d >= ra + rb - 0.001,
                  f"{a.enum} and {b.enum} overlap (d={d:.2f} < {ra + rb:.2f})")


def test_on_panel():
    """Every glyph stays 2 mm inside the plate."""
    for c in g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS + g.LIGHTS:
        check(2.0 <= c.x <= g.W - 2.0 and 2.0 <= c.y <= g.Hh - 2.0,
              f"{c.enum} off panel at ({c.x:.2f}, {c.y:.2f})")


def test_panel_size():
    check(approx(g.W, 213.36) and approx(g.Hh, 128.5), "panel is no longer 42HP")


def test_label_metadata_exists():
    """Every labelled control resolves to an absolute label placement."""
    for c in g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS:
        if not c.label:
            continue
        x, y, anchor, size, colour = g.label_of(c)
        check(anchor in ("middle", "start", "end"),
              f"{c.enum}: bad anchor {anchor!r}")
        check(size > 0, f"{c.enum}: label size {size}")
        check(colour.startswith("#"), f"{c.enum}: label colour {colour!r}")


def test_label_defaults_match_todays_layout():
    """The default rule must reproduce the pre-redesign placement exactly."""
    for c in g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS:
        if not c.label or c.lbl is not None:
            continue
        x, y, anchor, size, colour = g.label_of(c)
        check(approx(x, c.x), f"{c.enum}: default label x {x} != {c.x}")
        check(anchor == "middle", f"{c.enum}: default anchor {anchor!r}")
        check(colour == g.INK, f"{c.enum}: default colour {colour!r}")


def test_header_carries_label_columns():
    """The C++ table must ship the label placement, not recompute it."""
    h = g.header()
    check("XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb;" in h,
          "PanelCtl has no label placement columns")
    check(h.count("{RATE_A, WK_BIGKNOB,") == 1, "kParamCtls lost RATE_A")


# --- 2026-07-18 redesign: target coordinates, read off layout-b-v7.html -------
ORBIT_A = {           # enum -> (knob x, knob y, label x, label y, anchor)
    'RATE_A':    (39.500,  9.000, 39.500,  3.000, 'middle'),
    'DENSITY_A': (55.891, 14.966, 59.876, 10.216, 'start'),
    'SMOOTH_A':  (64.613, 30.072, 70.718, 29.695, 'start'),
    'SHAPE_A':   (61.584, 47.250, 66.607, 52.350, 'start'),
    'MOD_A':     (48.222, 58.462, 50.205, 66.112, 'middle'),
    'RANGE_A':   (30.778, 58.462, 28.795, 66.112, 'middle'),
    'MELODY_A':  (17.416, 47.250, 12.393, 52.350, 'end'),
    'TUNE_A':    (14.387, 30.072,  8.282, 29.695, 'end'),
    'COLOR_A':   (23.109, 14.966, 19.124, 10.216, 'end'),
}


def test_layout_constants():
    check(approx(g.RING_CX_A, 39.5), f"RING_CX_A {g.RING_CX_A}, want 39.5")
    check(approx(g.RING_CY, 34.5), f"RING_CY {g.RING_CY}, want 34.5")
    check(approx(g.KNOB_R, 25.5), f"KNOB_R {g.KNOB_R}, want 25.5")
    check(g.VOICE_X == [9.25, 19.75, 30.25], f"VOICE_X {g.VOICE_X}")
    check(g.FX_TOP == [44.25, 54.75, 65.25, 75.75], f"FX_TOP {g.FX_TOP}")
    check(g.FX_BOT == g.FX_TOP, f"FX rows disagree: {g.FX_TOP} / {g.FX_BOT}")


def test_quiet_technical_tokens():
    want = {
        "GROUP_STROKE": 0.30,
        "GROUP_FILL_OPACITY": 0.45,
        "SECTOR_R_IN": 20.50,
        "SECTOR_R_OUT": 31.00,
        "SECTOR_OPACITY": 0.045,
        "PLAY_FIELD_OPACITY": 0.25,
    }
    for name, expected in want.items():
        actual = getattr(g, name, None)
        check(actual is not None, f"{name} is missing")
        if actual is not None:
            check(approx(actual, expected), f"{name} {actual}, want {expected}")

    box = g.group_box(4.0, 72.4, 31.5, 24.5, "VOICE")
    check(f'fill="{g.PAPER_DEEP}" fill-opacity="0.45"' in box,
          "group box must use PAPER_DEEP at fill-opacity 0.45")
    check(f'stroke="{g.LINE}" stroke-width="0.30"' in box,
          "group box must use LINE at stroke-width 0.30")
    wedge = g.wedge_svg(g.RING_CX_A, -16.0, 96.0, g.GREEN, False)
    check('A 31.000 31.000' in wedge and 'A 20.500 20.500' in wedge,
          "sector wedge must use the 31.00 / 20.50 mm annulus")
    check('opacity="0.045"' in wedge,
          "sector wedge must use opacity 0.045")


def test_orbit_positions():
    for enum, (kx, ky, lx, ly, anchor) in ORBIT_A.items():
        c = ctl(enum)
        check(approx(c.x, kx) and approx(c.y, ky),
              f"{enum} knob at ({c.x:.2f}, {c.y:.2f}), want ({kx}, {ky})")
        ax, ay, aanchor, size, colour = g.label_of(c)
        check(approx(ax, lx) and approx(ay, ly),
              f"{enum} label at ({ax:.2f}, {ay:.2f}), want ({lx}, {ly})")
        check(aanchor == anchor, f"{enum} anchor {aanchor!r}, want {anchor!r}")
        check(approx(size, 1.9), f"{enum} label size {size}, want 1.9")


def test_orbit_mirrors():
    """Part B is part A mirrored -- including the flipped label anchors."""
    flip = {'start': 'end', 'end': 'start', 'middle': 'middle'}
    for enum, (kx, ky, lx, ly, anchor) in ORBIT_A.items():
        b = ctl(enum[:-2] + '_B')
        check(approx(b.x, g.W - kx) and approx(b.y, ky),
              f"{b.enum} knob at ({b.x:.2f}, {b.y:.2f}), want ({g.W - kx:.2f}, {ky})")
        ax, ay, aanchor, _, _ = g.label_of(b)
        check(approx(ax, g.W - lx) and approx(ay, ly),
              f"{b.enum} label at ({ax:.2f}, {ay:.2f}), want ({g.W - lx:.2f}, {ly})")
        check(aanchor == flip[anchor],
              f"{b.enum} anchor {aanchor!r}, want {flip[anchor]!r}")


def test_no_label_between_knob_and_ring():
    """The point of radial labels: no caption falls inside the LED ring's reach."""
    for suffix, cx in (('_A', g.RING_CX_A), ('_B', g.W - g.RING_CX_A)):
        for base in ORBIT_A:
            c = ctl(base[:-2] + suffix)
            lx, ly, _, _, _ = g.label_of(c)
            d = math.hypot(lx - cx, ly - g.RING_CY)
            check(d > g.KNOB_R + 4.2,
                  f"{c.enum} label sits inside the orbit (d={d:.2f})")


def test_sector_captions():
    want = [(70.00, 8.20, 'MOTION'), (70.00, 67.00, 'TIMBRE'),
            (9.00, 8.20, 'PITCH'),
            (g.W - 70.00, 8.20, 'MOTION'), (g.W - 70.00, 67.00, 'TIMBRE'),
            (g.W - 9.00, 8.20, 'PITCH')]
    got = [(x, y, t) for (x, y, sz, sp, col, an, t) in g.TEXTS
           if t in ('MOTION', 'TIMBRE', 'PITCH')]
    check(len(got) == 6, f"{len(got)} sector captions, want 6")
    for wx, wy, wt in want:
        check(any(approx(x, wx) and approx(y, wy) and t == wt for x, y, t in got),
              f"sector caption {wt} missing at ({wx:.2f}, {wy})")


FX_FIELDS_A = {
    "FLUX_TOP":    (39.0, 73.6, 31.0, 10.0, "#dfe5dc"),
    "ROOM":        (70.5, 73.6, 10.5, 10.0, "#e8e0d4"),
    "FLUX_BOTTOM": (39.0, 84.4, 21.0, 11.3, "#dfe5dc"),
    "GRIT":        (60.5, 84.4, 20.5, 11.3, "#e6ddd1"),
}


def test_fx_fields_are_exact_mirrors():
    check(len(g.FX_FIELDS) == 8, f"{len(g.FX_FIELDS)} FX fields, want 8")
    for name, (x, y, w, h, fill) in FX_FIELDS_A.items():
        a = next((f for f in g.FX_FIELDS if not f[0] and f[1] == name), None)
        b = next((f for f in g.FX_FIELDS if f[0] and f[1] == name), None)
        check(a is not None and b is not None, f"{name}: missing A or B field")
        if a is None or b is None:
            continue
        _, _, ax, ay, aw, ah, af = a
        _, _, bx, by, bw, bh, bf = b
        check(all(approx(v, want) for v, want in
                  zip((ax, ay, aw, ah), (x, y, w, h))),
              f"{name} A geometry {a[2:6]}")
        check(approx(bx, g.W - x - w) and approx(by, y)
              and approx(bw, w) and approx(bh, h),
              f"{name} B is not mirrored: {b[2:6]}")
        check(af == fill and bf == fill, f"{name} fill {af}/{bf}, want {fill}")


def test_fx_fields_render_in_explicit_layer():
    s = g.svg()
    group_svgs = [g.group_box(x, y, w, h, name)
                  for (x, y, w, h, name, _colour) in g.GROUPS]
    field_svgs = [g.fx_field_svg(field) for field in g.FX_FIELDS]
    well_svgs = [
        (f'<rect x="{g.mm(bx + 1.4)}" y="{g.mm(g.JACK_BOX_Y + 1.6)}" '
         f'width="{g.mm(g.JACK_BOX_W - 2.8)}" '
         f'height="{g.mm(g.JACK_BOX_H - 3.2)}" rx="1.2" fill="{g.WELL}"/>')
        for (bx, _lg, _col, well, _items) in g.JACK_GROUPS if well
    ]
    control_svgs = [
        g.knob_svg(c) for c in g.STATIC_PANEL_PARAMS
        if c.kind in (g.BIGKNOB, g.KNOBC, g.SMKNOB, g.KNOBI)
    ]

    last_group_end = max(s.index(box) + len(box) for box in group_svgs)
    first_well_or_control = min(
        s.index(item) for item in well_svgs + control_svgs)
    field_spans = []
    for field, rendered in zip(g.FX_FIELDS, field_svgs):
        check(s.count(rendered) == 1,
              f"{field[1]} {'B' if field[0] else 'A'} field must render once")
        if rendered in s:
            start = s.index(rendered)
            field_spans.append((start, start + len(rendered)))

    check(len(field_spans) == len(g.FX_FIELDS),
          f"{len(field_spans)} rendered FX fields, want {len(g.FX_FIELDS)}")
    if field_spans:
        check(last_group_end < min(start for start, _end in field_spans),
              "FX fields must render after every group box")
        check(max(end for _start, end in field_spans) < first_well_or_control,
              "FX fields must render before wells and controls")


def test_play_mode_fields_are_exact_mirrors():
    fields = getattr(g, "PLAY_FIELDS", [])
    check(len(fields) == 2, f"{len(fields)} PLAY fields, want 2")
    if len(fields) != 2:
        return

    a = next((field for field in fields if not field[0]), None)
    b = next((field for field in fields if field[0]), None)
    check(a is not None and b is not None, "PLAY fields must contain A and B")
    if a is None or b is None:
        return

    _, ax, ay, aw, ah = a
    _, bx, by, bw, bh = b
    check(all(approx(v, want) for v, want in
              zip((ax, ay, aw, ah), (5.0, 99.6, 29.0, 10.6))),
          f"PLAY A field {a[1:]}")
    check(approx(bx, g.W - ax - aw) and approx(by, ay)
          and approx(bw, aw) and approx(bh, ah),
          f"PLAY B is not mirrored: {b[1:]}")

    render = getattr(g, "play_field_svg", None)
    check(callable(render), "play_field_svg is missing")
    if not callable(render):
        return
    s = g.svg()
    rendered = [render(field) for field in fields]
    for field, field_svg in zip(fields, rendered):
        check(s.count(field_svg) == 1,
              f"PLAY {'B' if field[0] else 'A'} field must render once")
        check(f'fill="{g.PAPER_DEEP}"' in field_svg
              and 'rx="1.0"' in field_svg
              and 'fill-opacity="0.25"' in field_svg,
              f"PLAY field style is wrong: {field_svg}")

    fx_end = max(s.index(g.fx_field_svg(field)) + len(g.fx_field_svg(field))
                 for field in g.FX_FIELDS)
    play_spans = [(s.index(field_svg), s.index(field_svg) + len(field_svg))
                  for field_svg in rendered if field_svg in s]
    first_control = min(
        s.index(g.knob_svg(c)) for c in g.STATIC_PANEL_PARAMS
        if c.kind in (g.BIGKNOB, g.KNOBC, g.SMKNOB, g.KNOBI)
    )
    if len(play_spans) == len(fields):
        check(fx_end < min(start for start, _end in play_spans),
              "PLAY fields must render after every FX field")
        check(max(end for _start, end in play_spans) < first_control,
              "PLAY fields must render before controls")


def test_small_knobs_have_no_collar():
    """Spec §3: only the orbit and MORPH keep an accent collar."""
    s = g.svg()
    for enum in ('ATTACK_A', 'REV_SIZE', 'TEMPO', 'TIDE'):
        c = ctl(enum)
        needle = f'cx="{g.mm(c.x)}" cy="{g.mm(c.y)}" r="{g.mm(c.r + 0.85)}"'
        check(needle not in s, f"{enum} still draws a collar")


LOWER_A = {   # enum -> (x, y)   part A; part B is W - x
    'ATTACK_A': (9.25, 77.30), 'STAGES_A': (9.25, 77.30),
    'FILT_A': (19.75, 77.30), 'SUB_A': (30.25, 77.30),
    'DECAY_A': (9.25, 89.40), 'RES_A': (19.75, 89.40), 'SOURCE_A': (30.25, 89.40),
    'FLUXRATE_A': (44.25, 77.30), 'FLUX_A': (54.75, 77.30),
    'FLUXFB_A': (65.25, 77.30), 'REV_MIX_A': (75.75, 77.30),
    'LINK_A': (44.25, 89.40),
    'GRIT_A': (65.25, 89.40), 'COMP_A': (75.75, 89.40),
    'ENGINE_A': (10.00, 103.60),
    'STEPS_A': (37.00, 103.60),
    'SONG_A': (67.00, 103.60),
}


def test_lower_half_positions():
    for enum, (x, y) in LOWER_A.items():
        c = ctl(enum)
        check(approx(c.x, x) and approx(c.y, y),
              f"{enum} at ({c.x:.2f}, {c.y:.2f}), want ({x}, {y})")
        b = ctl(enum[:-2] + '_B')
        check(approx(b.x, g.W - x) and approx(b.y, y),
              f"{b.enum} at ({b.x:.2f}, {b.y:.2f}), want ({g.W - x:.2f}, {y})")

    for suffix in ('_A', '_B'):
        attack, pitch = ctl('ATTACK' + suffix), ctl('STAGES' + suffix)
        check((attack.x, attack.y) == (pitch.x, pitch.y),
              f"{suffix}: ATK/PITCH do not share coordinates")


def test_static_synth_preview_excludes_bbd_pitch():
    """The generated SVG must remain the Synth-only preview when runtime
    PITCH overlays ATK in Rack."""
    svg = g.svg()
    check('>STGS</text>' not in svg, "static SVG still exposes STGS")
    check(svg.count('>ATK</text>') == 2,
          "static preview must show two ATK captions")
    check(svg.count('font-size="1.9">TIME</text>') == 2,
          "static preview must show two TIME captions")
    check('font-size="1.9">BEND</text>' not in svg,
          "static preview must not overlay BEND on ATK")


def test_rec_off_sampler_leaves_no_static_artifacts():
    """The removed-REC-pad defect (spec 2026-08-03 rec-artifacts): hiding the
    runtime REC widget at runtime does not erase what the SVG painted
    underneath it. The static preview must never paint the REC pad's white
    key bed or the REC LED's dark housing -- those are exactly what a player
    sees left behind on any non-Sampler deck -- nor print the REC caption.
    Modeled on test_static_synth_preview_excludes_bbd_pitch, which pins the
    same "runtime-only control must not leak into the static plate" shape for
    STAGES/PITCH."""
    svg = g.svg()
    check('>REC<' not in svg, "static SVG still prints the REC caption")

    pad_r = g.GLYPH_R[g.LATCH]
    for x in (g.REC_X, g.W - g.REC_X):
        bed = (f'<rect x="{g.mm(x - pad_r)}" y="{g.mm(g.PLAY_Y - pad_r)}" '
               f'width="{g.mm(2 * pad_r)}" height="{g.mm(2 * pad_r)}" '
               f'rx="1.0" fill="{g.WHITE}"/>')
        check(bed not in svg, f"REC pad key bed still painted at x={x:.3f}")

    led_r = g.GLYPH_R[g.LIGHT]
    for x in (g.REC_LED_X, g.W - g.REC_LED_X):
        housing = (f'<circle cx="{g.mm(x)}" cy="{g.mm(g.PLAY_Y)}" '
                   f'r="{g.mm(led_r)}" fill="#1a1206" stroke="#3a2c12" '
                   f'stroke-width="0.25"/>')
        check(housing not in svg, f"REC LED housing still painted at x={x:.3f}")


def test_static_lights_excludes_rec_but_lights_keeps_all_four():
    """LIGHTS (all four, in the frozen order) still drives kLightCtls in the
    generated header -- the C++ centres its LED rings on kLightCtls[0..1], so
    the gate lights must keep indices 0 and 1 (see the comment above LIGHTS).
    STATIC_LIGHTS is a second, SVG-only view that drops the two REC entries,
    the same relationship STATIC_PANEL_PARAMS already has to
    RUNTIME_PANEL_PARAMS for STAGES_A/B."""
    check([c.enum for c in g.LIGHTS] == LIGHT_ORDER,
          "LIGHTS must keep all four entries in the frozen order")
    static_lights = getattr(g, "STATIC_LIGHTS", None)
    check(static_lights is not None, "gen_panel has no STATIC_LIGHTS list")
    if static_lights is None:
        return
    check([c.enum for c in static_lights] == ['GATE_A_L', 'GATE_B_L'],
          "STATIC_LIGHTS must contain only the two gate lights, in order")

    h = g.header()
    check("static const PanelCtl kLightCtls[] = {" in h,
          "generated header lost kLightCtls")
    body = h.split("static const PanelCtl kLightCtls[] = {", 1)[1]
    body = body.split("};", 1)[0]
    row_ids = [row.strip().lstrip("{").split(",", 1)[0]
               for row in body.strip().splitlines() if row.strip()]
    check(row_ids == LIGHT_ORDER,
          f"kLightCtls row order is {row_ids}, want {LIGHT_ORDER}")


def test_song_control_contract():
    """SONG swallowed FORM and the NEW pad (spec 2026-08-09
    hw-control-reduction task 3): the frozen final slot exposes one integer
    knob that walks a 14-rung ladder through (Principle, SongMode) and
    re-rolls the phrase on every rung change. The ladder's own ORDER is
    taste, pinned by test_song_ladder.cpp's structural checks, not here --
    this only pins the mechanism: composed labels, forwarded set_form/
    set_song, and that FORM no longer has its own switch."""
    for suffix in ("_A", "_B"):
        song = ctl("SONG" + suffix)
        check(song.kind == g.KNOBI,
              f"{song.enum} kind is {song.kind}, want snapped integer knob")
        check(song.label == "SONG" and song.tip == "SONG",
              f"{song.enum} caption/tip is {song.label!r}/{song.tip!r}, want SONG")

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        cpp = f.read()
    check('configSwitch(c.id, 0.f, 4.f, init, "Form",' not in cpp,
          "FORM must no longer be its own Rack switch")
    song_words = """
static const char* kFormWords[] = {
    "TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
    "CALL / RESPONSE", "OSTINATO"};
static const char* kSongWords[] = {
    "AAAB", "ABAB", "ABBB", "BUILD",
    "ROTATE", "MIRROR", "OFF"};"""
    check(compact_cpp(song_words) in compact_cpp(cpp),
          "SONG's composed labels must keep the five Form and seven Song words")
    compose_loop = """
for (int i = 0; i < spky::kSongLadderCount; ++i) {
    const spky::SongRung& r = spky::song_ladder_at(i);
    rungs.push_back(std::string(kFormWords[r.form]) +
                    " / " + kSongWords[r.song]);
}"""
    check(compact_cpp(compose_loop) in compact_cpp(cpp),
          "SONG labels must be composed from the ladder table itself, "
          "not written out by hand beside it")
    song_switch = """
configSwitch(c.id, 0.f,
             float(spky::kSongLadderCount - 1),
             init, "Song", rungs);"""
    check(compact_cpp(song_switch) in compact_cpp(cpp),
          "SONG must be a snapped Rack switch spanning the whole ladder")
    check("inst.set_form(p, r.form);" in cpp,
          "Rack SONG parameter is not forwarded to Instrument::set_form")
    check("inst.set_song(p, r.song);" in cpp,
          "Rack SONG parameter is not forwarded to Instrument::set_song")


def test_song_user_documentation():
    """The host README describes the SONG ladder that replaced the
    independent FORM/SONG/NEW controls."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    for term in ("TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
                 "CALL / RESPONSE", "OSTINATO", "AAAB", "ABAB", "ABBB",
                 "BUILD", "ROTATE", "MIRROR", "OFF"):
        check(term in readme, f"VCV README does not document {term}")
    check("STEP · SONG" in readme,
          "VCV README does not document the PLAY-row order")
    check(re.search(r"\bTRIG\b[^.\n]*(?:button|control|pad)", readme,
                    flags=re.IGNORECASE) is None,
          "VCV README still presents TRIG as an available control")
    check("## FORM, SONG, and NEW" not in readme,
          "VCV README still titles a section after the retired FORM/NEW controls")
    check("NEW always queues a fresh A/B pair" not in readme,
          "VCV README still describes NEW as its own gesture")


def test_song_knob_swallows_form_and_new():
    """FORM and the NEW pad are gone; SONG walks the curated ladder and
    re-rolls the phrase on every rung change."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    for dead in ("FORM_A", "FORM_B", "NEWPHRASE_A", "NEWPHRASE_B"):
        check(dead not in names, f"{dead} still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("song_ladder.h" in cpp, "the host does not include the ladder")
    # Debouncing itself lives in song_rung_state.hpp (spky::hyst_step under
    # the hood, dependency-free, unit-tested by test_song_rung_state.cpp) --
    # the review fix that also stops a RESTORED rung from firing a re-roll.
    check("song_rung_state.hpp" in cpp,
          "the host does not include the rung tracker")
    check("songRung[p].tick(" in cpp, "the host does not debounce the SONG pot")
    check("inst.new_phrase(p)" in cpp, "the host never re-rolls")
    check("FORM_A" not in cpp, "Fireflow.cpp still references FORM_A")


def test_steps_left_the_fx_row():
    """STPS is a sequencer parameter -- it belongs in PLAY, not in FX."""
    s = ctl('STEPS_A')
    check(approx(s.y, 103.60), f"STEPS_A at y {s.y:.2f}, want 103.60 (PLAY row)")


def test_part_group_boxes():
    want = [(4.0, 72.4, 31.5, 24.5, 'VOICE'), (38.0, 72.4, 44.0, 24.5, 'FX'),
            (4.0, 98.6, 78.0, 12.6, 'PLAY')]
    for (x, y, w, h, name) in want:
        check(any(approx(gx, x) and approx(gy, y) and approx(gw, w)
                  and approx(gh, h) and gn == name
                  for (gx, gy, gw, gh, gn, _c) in g.GROUPS),
              f"part-A group {name} missing at ({x}, {y}, {w}, {h})")
        bx = g.W - x - w
        check(any(approx(gx, bx) and approx(gy, y) and gn == name
                  for (gx, gy, gw, gh, gn, _c) in g.GROUPS),
              f"part-B group {name} missing at x {bx:.2f}")


def test_group_legend_geometry():
    """Legend chip rides the top border, text sits 5 mm in from the left."""
    s = g.svg()
    for (x, y, w, h, name, colour) in g.GROUPS:
        cw = 1.35 * len(name) + 2.5
        chip = (f'<rect x="{g.mm(x + 5.0 - cw / 2)}" y="{g.mm(y - 1.3)}" '
                f'width="{g.mm(cw)}" height="2.6" fill="{g.PAPER}"/>')
        check(chip in s, f"legend chip for {name} missing/misplaced")
        check(any(approx(tx, x + 5.0) and approx(ty, y + 0.75) and t == name
                  for (tx, ty, sz, sp, col, an, t) in g.TEXTS),
              f"legend text for {name} missing at ({x + 5.0:.2f}, {y + 0.75:.2f})")


def test_pad_backplates_are_gone():
    check('width="72.4" height="11.9"' not in g.svg(),
          "the old pad backplate is still drawn")


CENTER = {   # enum -> (x offset from CX, y)
    'MORPH': (-7.0, 21.5), 'TIDE': (11.0, 21.5),
    'TEMPO': (9.0, 42.0),
    'COUPLE': (-9.0, 54.0), 'SHUFFLE': (9.0, 54.0),
    'SCALE': (-10.5, 68.0), 'CHOKE': (0.0, 68.0), 'DRIFT': (10.5, 68.0),
    # SETTLE retired (task 8, spec 2026-08-09 hw-control-reduction): the
    # pad's job moved to DRIFT's own left stop. SPOT and MASTER_DRIVE
    # retired (task 9): the whole ROW_DUO2 row (78.0) is now empty -- no
    # regrouping.
    'REV_SIZE': (-10.5, 94.0), 'REV_TONE': (0.0, 94.0),
    # REV_SMEAR and REV_MOD retired (task 9); their R-column slots at
    # (10.5, 94.0)/(10.5, 104.5) stay empty -- no regrouping.
    'REV_DECAY': (-10.5, 104.5), 'REV_DIFF': (0.0, 104.5),
}


def test_center_positions():
    for enum, (dx, y) in CENTER.items():
        try:
            c = ctl(enum)
        except KeyError:
            check(False, f"{enum} missing from center controls")
            continue
        want_x = g.CX + dx
        check(approx(c.x, want_x) and approx(c.y, y),
              f"{enum} at ({c.x:.2f}, {c.y:.2f}), want ({want_x:.2f}, {y})")


def test_center_group_boxes():
    want = [(13.0, 19.5, 'BLEND'), (35.0, 25.0, 'TIMING'),
            (62.5, 22.5, 'DUO'), (87.5, 23.7, 'ROOM')]
    for (y, h, name) in want:
        check(any(approx(gx, g.CX - 20.5) and approx(gy, y) and approx(gw, 41.0)
                  and approx(gh, h) and gn == name
                  for (gx, gy, gw, gh, gn, _c) in g.GROUPS),
              f"centre group {name} missing at y {y} (h {h}, x {g.CX - 20.5:.2f})")


def test_center_card_is_gone():
    check('width="42.0" height="100.0"' not in g.svg(),
          "the full-height centre card is still drawn")


def test_old_eyebrow_texts_are_gone():
    """TIMING/ROOM are group legends now, not free-floating eyebrows."""
    for (x, y, sz, sp, col, an, t) in g.TEXTS:
        if t in ('TIMING', 'ROOM'):
            check(approx(sz, 1.8) and approx(x, g.CX - 20.5 + 5.0),
                  f"{t} is still the old eyebrow (size {sz} at x {x:.2f})")


def test_room_is_flush_with_play():
    """Spec §6: ROOM's bottom edge lines up with the PLAY boxes."""
    room = next((gr for gr in g.GROUPS if gr[4] == 'ROOM'), None)
    play = next((gr for gr in g.GROUPS if gr[4] == 'PLAY'), None)
    check(room is not None, "ROOM group missing from g.GROUPS")
    check(play is not None, "PLAY group missing from g.GROUPS")
    if room is not None and play is not None:
        check(approx(room[1] + room[3], play[1] + play[3]),
              f"ROOM ends at {room[1] + room[3]:.2f}, PLAY at {play[1] + play[3]:.2f}")


JACKS = {   # enum -> (x, y, label, white label?)
    'PITCH_A': (12.95, 118.4, 'PIT',  True),
    'GATE_A':  (24.45, 118.4, 'GATE', True),
    'IN_L':    (55.25, 118.4, 'L',    False),
    'IN_R':    (66.75, 118.4, 'R',    False),
    'CLOCK':   (100.93, 118.4, 'CLK', False),
    'RESET':   (112.43, 118.4, 'RST', False),
    'OUT_L':   (146.61, 118.4, 'L',   True),
    'OUT_R':   (158.11, 118.4, 'R',   True),
    'GATE_B':  (188.91, 118.4, 'GATE', True),
    'PITCH_B': (200.41, 118.4, 'PIT',  True),
}


def test_jack_positions_and_labels():
    for enum, (x, y, label, white) in JACKS.items():
        c = ctl(enum)
        check(approx(c.x, x) and approx(c.y, y),
              f"{enum} at ({c.x:.2f}, {c.y:.2f}), want ({x}, {y})")
        check(c.label == label, f"{enum} label {c.label!r}, want {label!r}")
        lx, ly, anchor, size, colour = g.label_of(c)
        check(approx(ly, 124.8), f"{enum} label y {ly:.2f}, want 124.8")
        check(approx(size, 1.8), f"{enum} label size {size}, want 1.8")
        want_col = g.WHITE if white else g.INK
        check(colour == want_col,
              f"{enum} label colour {colour}, want {want_col}")


def test_jack_groups():
    want = [(7.2, 'CV A', g.GREEN), (49.5, 'IN', g.MUTED),
            (g.CX - 11.5, 'CLOCK', g.MUTED), (g.W - 72.5, 'OUT', g.MUTED),
            (g.W - 30.2, 'CV B', g.COPPER)]
    for (x, name, colour) in want:
        check(any(approx(gx, x) and approx(gy, 112.6) and approx(gw, 23.0)
                  and approx(gh, 14.4) and gn == name and gc == colour
                  for (gx, gy, gw, gh, gn, gc) in g.GROUPS),
              f"jack group {name} missing at x {x:.2f}")


def test_output_wells():
    """Spec §7: output groups get a dark inner well; inputs stay on paper."""
    s = g.svg()
    for (x, has_well) in ((7.2, True), (49.5, False), (g.CX - 11.5, False),
                          (g.W - 72.5, True), (g.W - 30.2, True)):
        well = (f'<rect x="{g.mm(x + 1.4)}" y="{g.mm(112.6 + 1.6)}" '
                f'width="{g.mm(20.2)}" height="{g.mm(11.2)}" rx="1.2" '
                f'fill="{g.WELL}"/>')
        check((well in s) == has_well,
              f"well at x {x:.2f}: expected {has_well}")


def test_header_carries_tooltips():
    h = g.header()
    check('const char* tip;' in h, "PanelCtl has no tip column")
    check('"L", ' in h and '"IN L"' in h,
          "IN_L lost its 'IN L' tooltip while its panel label became 'L'")


def test_no_printed_screw_holes():
    """Rack draws real screw widgets in the corners; the printed circles never
    lined up with them (spec 2026-07-18, panel furniture)."""
    check('fill="#d8d0bf"' not in g.svg(), "printed screw-hole circles are back")


def test_group_count():
    """3 part groups x 2 + 4 centre + 5 jack = 15 fieldsets."""
    check(len(g.GROUPS) == 15, f"{len(g.GROUPS)} groups, want 15")
    names = sorted(n for (_x, _y, _w, _h, n, _c) in g.GROUPS)
    check(names == sorted(['VOICE', 'FX', 'PLAY'] * 2 +
                          ['BLEND', 'TIMING', 'DUO', 'ROOM'] +
                          ['CV A', 'IN', 'CLOCK', 'OUT', 'CV B']),
          f"unexpected group set: {names}")


def test_every_label_is_reachable():
    """Nothing may be drawn off-plate or under a neighbouring box edge."""
    for c in g.RUNTIME_PANEL_PARAMS + g.INPUTS + g.OUTPUTS:
        if not c.label:
            continue
        lx, ly, _a, size, _col = g.label_of(c)
        check(1.0 <= lx <= g.W - 1.0 and 1.0 <= ly <= g.Hh - 1.0,
              f"{c.enum} label off panel at ({lx:.2f}, {ly:.2f})")


# --- one word, one meaning ----------------------------------------------------
# Everything a player reads inside deck A's half plus the shared centre. Deck B
# is an exact mirror (test_all_deck_local_geometry_is_exactly_mirrored), so it
# contributes no word deck A does not already have.
#
# Jack captions are deliberately OUT of scope: PIT/GATE/L/R each appear twice
# across the five jack groups, and gen_panel's JACK_GROUPS comment records why
# that is correct -- the coloured wells and their legends disambiguate them.
# Their group LEGENDS are in scope, because those are read as ordinary words.
def dynamic_words_for(base):
    """State-dependent words for a control base, [] when it has none.

    Tolerates gen_panel having no DYNAMIC_CAPTIONS table yet, so this guard
    can land before the table it will eventually cover."""
    for target, _driver, words in getattr(g, "DYNAMIC_CAPTIONS", ()):
        if target == base:
            return list(words)
    return []


def printed_words():
    """word -> list of origins, for everything drawn in deck A + centre."""
    out = {}

    def add(word, origin):
        out.setdefault(word, []).append(origin)

    jacks = {c.enum for c in g.INPUTS + g.OUTPUTS}
    for c in g.RUNTIME_PANEL_PARAMS:
        if not c.label or c.enum.endswith('_B') or c.enum in jacks:
            continue
        base = c.enum[:-2] if c.enum.endswith('_A') else c.enum
        # A control's own states may repeat a word (e.g. ATTACK is ATK in
        # four of its five engine states) -- that is one meaning said
        # several times, not a collision, so dedupe before recording it.
        for word in set(dynamic_words_for(base) or [c.label]):
            add(word, base)

    # Deck B's three mirrored fieldsets repeat deck A's legends verbatim;
    # every other box (centre + jack groups) is its own word.
    mirrored = {(round(x, 3), round(y, 3))
                for (x, y, _w, _h, _n, _c) in g.part_groups(True)}
    for (x, y, _w, _h, name, _col) in g.GROUPS:
        if (round(x, 3), round(y, 3)) in mirrored:
            continue
        add(name, 'group legend')

    for (name, _a0, _a1, _cap) in g.SECTORS:
        add(name, 'sector eyebrow')
    return out


def test_every_printed_word_is_unique():
    """No word may name two different things on one plate.

    This is the guard that would have stopped RATE/RATE (orbit macro vs FLUX
    division) and GRIT/GRIT (mode pad vs mix knob) from ever being written."""
    for word, origins in sorted(printed_words().items()):
        check(len(origins) == 1,
              f"{word!r} is printed {len(origins)}x: {', '.join(origins)}")


def test_dynamic_caption_table_is_well_formed():
    """Every row targets a real control, is driven by a real control, and
    carries one word per state of its driver."""
    table = getattr(g, "DYNAMIC_CAPTIONS", None)
    check(table is not None, "gen_panel has no DYNAMIC_CAPTIONS table")
    if table is None:
        return
    enums = {c.enum for c in g.RUNTIME_PANEL_PARAMS}
    driver_states = {"ENGINE": 5}
    for target, driver, words in table:
        for suffix in ("_A", "_B"):
            check(target + suffix in enums,
                  f"DYNAMIC_CAPTIONS targets unknown control {target + suffix}")
            check(driver + suffix in enums,
                  f"DYNAMIC_CAPTIONS driven by unknown control {driver + suffix}")
        check(driver in driver_states,
              f"{target}: driver {driver!r} has no known state count")
        check(len(words) == driver_states.get(driver, -1),
              f"{target}: {len(words)} words for a {driver} driver")
        check(len(words) <= 5,
              f"{target}: {len(words)} words exceeds the header's word[5]")
        check(all(w and w.isupper() for w in words),
              f"{target}: captions must be non-empty upper case: {words}")


def test_static_label_is_the_tables_first_word():
    """The plate's resting caption and the table's state-0 word are the same
    thing said twice; they may never disagree."""
    for target, _driver, words in getattr(g, "DYNAMIC_CAPTIONS", ()):
        for suffix in ("_A", "_B"):
            c = ctl(target + suffix)
            check(c.label == words[0],
                  f"{c.enum} label {c.label!r} != table word[0] {words[0]!r}")


def test_printed_second_words_are_gone():
    """SCAN and LEN were printed on every engine and true on one. The whole
    inline-alias machinery goes with them."""
    words = [t[-1] for t in g.TEXTS]
    for stale in ("SCAN", "LEN", "ORG", "FRAME", "MATL"):
        check(stale not in words,
              f"{stale!r} is still a static kPanelTexts entry")
    for gone in ("sampler_texts", "SAMPLER_LBL", "SAMPLER_GAP",
                 "SAMPLER_RADIAL", "MONO_ADV", "text_w", "mirror_label",
                 "mirror_anchor", "SOURCE_CAPTIONS"):
        check(not hasattr(g, gone),
              f"gen_panel still carries the retired {gone}")


def test_header_carries_the_dynamic_caption_table():
    """Rack must read the words, never hold its own copy."""
    h = g.header()
    check("struct DynCaption { int id; int driverId; int count; "
          "const char* words[5]; };" in h,
          "generated header has no DynCaption struct")
    check("static const DynCaption kDynCaptions[]" in h,
          "generated header has no kDynCaptions table")
    rows = 2 * len(g.DYNAMIC_CAPTIONS)
    check(h.count("{SUB_A, ENGINE_A, 5, {") == 1,
          "SUB_A is not bound to its own deck's ENG")
    check(h.count("{SUB_B, ENGINE_B, 5, {") == 1,
          "SUB_B is not bound to its own deck's ENG")
    body = h.split("static const DynCaption kDynCaptions[] = {")[1].split("};")[0]
    check(body.count("},") == rows,
          f"kDynCaptions has {body.count('},')} rows, want {rows}")


def test_config_wires_tip_not_label():
    """The generated header carries a real tooltip in `tip` (see
    test_header_carries_tooltips), but a header can be correct while the C++
    that reads it quietly regresses -- e.g. `configInput(c.id, c.label)`
    still compiles, still passes every other test here, and only shows up
    when a human hovers a jack in Rack and sees "L" instead of "IN L". This
    guard reads the actual C++ source so that regression fails the suite
    instead of waiting for a human to notice (spec 2026-07-18, Task 6 review)."""
    here = os.path.dirname(os.path.abspath(__file__))
    cpp_path = os.path.join(here, "..", "src", "Fireflow.cpp")
    with open(cpp_path) as f:
        cpp = f.read()
    check("const std::string lbl = c.tip;" in cpp,
          "parameter configuration is not wired to c.tip")
    check("configInput(c.id, c.tip)" in cpp,
          "configInput is not wired to c.tip -- jack tooltips will show panel labels")
    check("configOutput(c.id, c.tip)" in cpp,
          "configOutput is not wired to c.tip -- jack tooltips will show panel labels")


def test_grit_is_one_bipolar_knob():
    """The SAT pad is gone: sign picks the mode, magnitude is the mix, and
    a dead zone around zero makes 'off' reachable on a real pot."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("GRITMODE_A" not in names and "GRITMODE_B" not in names,
          "GRITMODE pads still exist")
    grit_a = [c for c in gp.PARAMS if c.enum == "GRIT_A"][0]
    check(grit_a.kind == gp.KNOBC, "GRIT_A is not a bipolar knob")
    grit_b = [c for c in gp.PARAMS if c.enum == "GRIT_B"][0]
    check(grit_b.kind == gp.KNOBC, "GRIT_B is not a bipolar knob")
    check(all(row[0] != "GRITMODE" for row in gp.DYNAMIC_CAPTIONS),
          "GRITMODE still has a dynamic caption")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("spky::GritMode::Reduce" in cpp and "spky::GritMode::Drive" in cpp,
          "the host no longer names both grit modes")
    check("kGritDead" in cpp, "no dead zone around grit zero")
    check("GRITMODE_A" not in cpp, "Fireflow.cpp still references GRITMODE_A")


def test_couple_knob_carries_both_worlds():
    """SYNC was the right-hand end of COUPLE's own axis. Two zones, each
    sweeping couple 0..1, so neither world loses its spread."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("SYNC" not in names, "the SYNC switch still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kCoupleZoneSplit" in cpp, "no zone split constant in the host")
    check("inst.set_sync(" in cpp, "the host never sets sync any more")
    check("params[SYNC]" not in cpp, "Fireflow.cpp still reads a SYNC param")


def test_grit_dead_zone_and_mix_formula_agree_across_host_and_bench():
    """The bipolar-GRIT dead zone and its sign/magnitude mapping exist in
    two places: Fireflow.cpp (what Rack actually runs) and
    bench/audition/init_patch.cpp (the only copy a doctest can reach --
    Fireflow.cpp lives inside a Rack Module, unreachable from the engine
    test suite). tests/test_seed_audition_init.cpp exercises the mapping,
    but it can only ever prove the BENCH copy is right; nothing stops the
    Fireflow.cpp copy from silently drifting away from it. This scrapes
    both files' source text and requires the dead-zone constant's value and
    the mix formula to match exactly, so a hand-edit to only one copy fails
    loudly here instead of shipping a Rack build that disagrees with its
    own test coverage.

    Each extraction below is asserted to find exactly one match per file --
    zero matches (the extraction quietly finding nothing) is treated as a
    failure, not a pass, per the review that asked for this test: a
    scraper that matches nothing must not report success.

    Deliberately NOT a call to consolidate the two copies into a shared
    helper: FLUX's per-param push is duplicated between these same two
    files the same way, on purpose (spec 2026-08-09 hw-control-reduction
    task 4 review) -- this test polices the duplication, it does not
    remove it.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    def dead_zone_value(source, label):
        matches = re.findall(r"kGritDead\s*=\s*([\d.]+f?)", source)
        check(len(matches) == 1,
              f"{label}: expected exactly one kGritDead declaration, "
              f"found {len(matches)} ({matches!r})")
        return matches[0].rstrip("f") if len(matches) == 1 else None

    host_dead = dead_zone_value(host_cpp, "Fireflow.cpp")
    bench_dead = dead_zone_value(bench_cpp, "bench/audition/init_patch.cpp")
    if host_dead is not None and bench_dead is not None:
        check(float(host_dead) == float(bench_dead),
              f"kGritDead disagrees: Fireflow.cpp={host_dead} "
              f"bench/audition/init_patch.cpp={bench_dead}")

    needles = {
        "sign-picks-mode ternary": compact_cpp(
            "gritKnob<0.f?spky::GritMode::Reduce:spky::GritMode::Drive"),
        "dead-zone mix formula": compact_cpp(
            "gritMag<=kGritDead?0.f:(gritMag-kGritDead)/(1.f-kGritDead)"),
    }
    host_n, bench_n = compact_cpp(host_cpp), compact_cpp(bench_cpp)
    for label, needle in needles.items():
        check(host_n.count(needle) == 1,
              f"Fireflow.cpp: expected exactly one {label}, found "
              f"{host_n.count(needle)} matching {needle!r}")
        check(bench_n.count(needle) == 1,
              f"bench/audition/init_patch.cpp: expected exactly one "
              f"{label}, found {bench_n.count(needle)} matching {needle!r}")


def test_detune_is_a_panel_control_with_a_square_taper():
    """DETUNE leaves the context menu. The taper keeps the first ~20 cents
    usable instead of squeezing them into a fifth of the travel."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("DETUNE_A" in names and "DETUNE_B" in names, "DETUNE is missing")
    det = [c for c in gp.PARAMS if c.enum == "DETUNE_A"][0]
    check(det.label != "", "DETUNE_A still has the menu-only empty label")
    check((det.x, det.y) != (0.0, 0.0), "DETUNE_A still sits at the menu origin")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("detKnob * detKnob" in cpp, "the detune taper is not quadratic")


def test_detune_taper_agrees_across_host_and_bench():
    """The quadratic DETUNE taper (v*v feeding set_voice_detune, spec
    2026-08-09 hw-control-reduction task 10) exists in two places:
    Fireflow.cpp (what Rack actually runs) and bench/audition/init_patch.cpp
    (the only copy a doctest can reach -- Fireflow.cpp lives inside a Rack
    Module, unreachable from the engine test suite). Same house pattern as
    test_grit_dead_zone_and_mix_formula_agree_across_host_and_bench: scrape
    both files' source text with a len(matches) == 1 guard so a hand-edit to
    only one copy fails loudly, without consolidating the duplication away.

    The needle is the compacted multiplication itself, which does not appear
    in either file's prose comments (checked so this cannot false-match a
    comment describing the taper rather than implementing it)."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    needle = "detKnob*detKnob"
    host_n, bench_n = compact_cpp(host_cpp), compact_cpp(bench_cpp)
    check(host_n.count(needle) == 1,
          f"Fireflow.cpp: expected exactly one quadratic detune taper, "
          f"found {host_n.count(needle)} matching {needle!r}")
    check(bench_n.count(needle) == 1,
          f"bench/audition/init_patch.cpp: expected exactly one quadratic "
          f"detune taper, found {bench_n.count(needle)} matching {needle!r}")


def cpp_scope(source, anchor):
    """Return the braced C++ declaration beginning at *anchor*.

    The host guard deliberately inspects the owning declaration rather than a
    whole translation unit: a stale duplicate elsewhere must not make wiring
    appear correct.
    """
    start = source.find(anchor)
    if start < 0:
        return None
    opening = source.find("{", start)
    if opening < 0:
        return None
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    return None


def compact_cpp(source):
    return re.sub(r"\s+", "", source)


def engine_cycle_wiring_issues(cpp, makefile):
    """Return scoped ENG integration regressions found in host source."""
    issues = []
    latch = cpp_scope(cpp, "struct EngineCycleLatch : VCVLatch")
    shades = cpp_scope(cpp, "static const NVGcolor kEngineShades[]")
    config = cpp_scope(cpp, "void configControls()")
    push = cpp_scope(cpp, "void pushParams()")
    process = cpp_scope(cpp, "void process(const ProcessArgs& args) override")
    ring = cpp_scope(cpp, "struct SpkyRing : Widget")
    widget = cpp_scope(cpp, "FireflowWidget(Fireflow* module)")

    for label, block in (("latch", latch), ("shade table", shades),
                         ("config", config),
                         ("parameter push", push), ("REC LED", process),
                         ("sampler ring", ring), ("widget", widget)):
        if block is None:
            issues.append(f"ENG {label} scope is missing")
    if issues:
        return issues

    # The bounds-check mutation below (state < kShadeCount -> <=) proves the
    # *indexing* can go red, but nothing previously pinned the shade table's
    # actual colours -- a fifth (or wrong) RGBA tuple here would draw the
    # wrong ring colour for an entire engine and no guard would notice.
    got_shades = [compact_cpp(s) for s in re.findall(r"nvgRGBA\([^)]*\)", shades)]
    want_shades = [
        "nvgRGBA(0,0,0,0)",
        "nvgRGBA(255,174,92,105)",
        "nvgRGBA(120,210,255,145)",
        "nvgRGBA(160,255,150,140)",
        "nvgRGBA(230,140,255,140)",
    ]
    if got_shades != want_shades:
        issues.append(
            f"kEngineShades colours are {got_shades!r}, want {want_shades!r}")

    latch_expected = """
struct EngineCycleLatch : VCVLatch {
    void drawLayer(const DrawArgs& args, int layer) override {
        VCVLatch::drawLayer(args, layer);
        if (layer != 1) return;
        engine::ParamQuantity* pq = getParamQuantity();
        if (!pq) return;
        const int state = static_cast<int>(std::round(pq->getValue()));
        if (state == 0) return;
        constexpr int kShadeCount = sizeof(kEngineShades) / sizeof(kEngineShades[0]);
        const NVGcolor color = kEngineShades[
            state >= 0 && state < kShadeCount ? state : kShadeCount - 1];
        const Vec c = box.size.div(2.f);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, c.x, c.y, 5.2f);
        nvgStrokeWidth(args.vg, 1.4f);
        nvgStrokeColor(args.vg, color);
        nvgStroke(args.vg);
    }
}"""
    if compact_cpp(latch) != compact_cpp(latch_expected):
        issues.append("ENG latch must contain only the specified drawLayer overlay")

    engine_config = """
else if (c.id == ENGINE_A || c.id == ENGINE_B) {
    configSwitch(c.id, 0.f, 4.f, init, "Engine",
                 {"Synth", "Sampler", "Wave", "Body", "BBD"});
    getParamQuantity(c.id)->snapEnabled = true;
}"""
    if compact_cpp(config).count(compact_cpp(engine_config)) != 1:
        issues.append("ENG config must be one snapped Synth/Sampler/Wave/Body/BBD 0..4 branch")

    engine_widget = """
case WK_LATCH:
    if (c.id == ENGINE_A || c.id == ENGINE_B)
        addParam(createParamCentered<EngineCycleLatch>(pos, module, c.id));
    else if (c.id == REC_A || c.id == REC_B) {
        auto* pad = createParamCentered<SlotVisible<VCVLatch>>(
            pos, module, c.id);
        pad->fireflow = module;
        pad->ctlId = c.id;
        addParam(pad);
    }
    else
        addParam(createParamCentered<VCVLatch>(pos, module, c.id));
    break;"""
    if compact_cpp(widget).count(compact_cpp(engine_widget)) != 1:
        issues.append("only ENGINE_A/B may use EngineCycleLatch; REC uses a "
                      "slot-visible VCVLatch; other latches use VCVLatch directly")
    if widget.count("createParamCentered<EngineCycleLatch>") != 1:
        issues.append("widget must create exactly one EngineCycleLatch branch")

    push_n = compact_cpp(push)
    dispatch = """
const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
const spky::EngineId id =
    eng == 0 ? spky::ENGINE_SYNTH :
    eng == 2 ? spky::ENGINE_WAVE :
    eng == 3 ? spky::ENGINE_BODY :
    eng == 4 ? spky::ENGINE_BBD :
    smp[p].testTone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
inst.set_engine(p, id);"""
    if push_n.count(compact_cpp(dispatch)) != 1:
        issues.append("ENG dispatch must exactly preserve Synth/Sampler/Wave/Body/BBD/test-tone states")
    factory = "if(eng==1&&!smp[p].testTone&&inst.sampler_empty(p)&&!factoryTried[p]){"
    if push_n.count(factory) != 1:
        issues.append("factory autoload must be restricted to ENG state 1")

    sampler_id = "inst.engine_id(p)==spky::ENGINE_SAMPLER"
    want_rec = ("constboolwantRec=params[p?REC_B:REC_A].getValue()>0.5f&&" +
                sampler_id + ";")
    sampler_part = "constboolsamplerPart=" + sampler_id + ";"
    if push_n.count(want_rec) != 1:
        issues.append("REC must be gated by the exact sampler engine id")
    if push_n.count(sampler_part) != 1:
        issues.append("sampler controls must use one exact samplerPart engine-id gate")
    for required in ("if(samplerPart)inst.sampler_scan(p,pp(MELODY_A,p));",
                     "if(samplerPart){",
                     "inst.set_target_active(p,spky::LANE_PITCH,!samplerPart);"):
        if required not in push_n:
            issues.append("a sampler-only pushParams control escaped samplerPart gating")
            break
    new_punch = """
if (songRung[p].tick(songNorm, spky::kSongLadderCount)) {
    inst.new_phrase(p);          // turn the knob, get a new melody
    // Fires once per rung detent; inherited the retired NEW
    // pad's Sampler punch. Whether every detent should punch, or
    // only some, is still an open by-ear question -- on this
    // plan's listening checklist.
    if (samplerPart) inst.sampler_punch(p);
}"""
    if push_n.count(compact_cpp(new_punch)) != 1:
        issues.append("SONG's rung change must rebuild A/B and additionally punch the Sampler")
    if "triggerTrig" in push or "trigger_manual" in push:
        issues.append("removed TRIG behavior remains in pushParams")
    if any(bad in push_n for bad in ("eng>0", "eng!=0", "eng>=1", "eng==1||eng==2")):
        issues.append("pushParams has a boolean ENG alternative that can route Wave as Sampler")

    process_n = compact_cpp(process)
    if process_n.count(sampler_part) != 1:
        issues.append("REC LED must use the exact sampler engine id")
    if "elseif(samplerPart&&!inst.sampler_empty(p)){" not in process_n:
        issues.append("REC LED fill state must remain sampler-only")

    ring_n = compact_cpp(ring)
    if ring_n.count("if(module&&module->inst.engine_id(part)==spky::ENGINE_SAMPLER){") != 1:
        issues.append("sampler ring display must use the exact sampler engine id")

    cpp_n = compact_cpp(cpp)
    if "ppb(ENGINE_A,p)" in cpp_n or "eng2" in cpp:
        issues.append("legacy boolean ENG routing remains")
    if makefile.count("$(REPO)/engine/synth/wt_bank.cpp") != 1:
        issues.append("VCV build must link wt_bank.cpp exactly once")
    return issues


def test_engine_cycle_host_wiring():
    """ENG keeps its saved 0/1 meanings and exposes Wave at 2 without any
    boolean sampler routing that would mistake Wave for Sampler."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    with open(os.path.join(here, "..", "Makefile")) as f:
        makefile = f.read()

    for issue in engine_cycle_wiring_issues(cpp, makefile):
        check(False, issue)


def test_engine_cycle_guard_rejects_representative_regressions():
    """The source guard must fail when a scoped ENG behavior regresses."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    with open(os.path.join(here, "..", "Makefile")) as f:
        makefile = f.read()

    mutations = [
        ("createParamCentered<EngineCycleLatch>",
         "createParamCentered<VCVLatch>", "widget"),
        ("state >= 0 && state < kShadeCount",
         "state >= 0 && state <= kShadeCount", "latch"),
        ("nvgRGBA(230, 140, 255, 140),  // BBD: violet",
         "nvgRGBA(230, 140, 255, 141),  // BBD: violet", "shade values"),
        ("if (eng == 1 && !smp[p].testTone",
         "if (eng > 0 && !smp[p].testTone", "factory"),
        ("const bool samplerPart = inst.engine_id(p) == spky::ENGINE_SAMPLER;",
         "const bool samplerPart = eng > 0;", "sampler"),
        ("if (samplerPart) inst.sampler_punch(p);",
         "inst.sampler_punch(p);", "SONG sampler punch"),
        ("inst.new_phrase(p);          // turn the knob, get a new melody",
         "if (!samplerPart) inst.new_phrase(p);          // turn the knob, get a new melody",
         "SONG phrase rebuild"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(engine_cycle_wiring_issues(mutated, makefile),
              f"ENG guard accepted a {label} regression")


def test_variation_is_gated_off_the_sampler():
    """MELODY is one knob with one meaning per engine: VARY off the Sampler,
    SCAN on it. Variation parks at LOOP there -- the same shape as the
    LANE_SIZE gate that parks at 0.5f off the Sampler."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    push = cpp_scope(cpp, "void pushParams()")
    check(push is not None, "pushParams scope is missing")
    if push is None:
        return
    n = compact_cpp(push)
    check(n.count("inst.set_variation(") == 1,
          "set_variation must be pushed exactly once")
    check("inst.set_variation(p,samplerPart?0.f:pp(MELODY_A,p));" in n,
          "set_variation must park at LOOP on a Sampler deck")
    if "constboolsamplerPart=" in n and "inst.set_variation(" in n:
        check(n.index("constboolsamplerPart=") < n.index("inst.set_variation("),
              "set_variation must run after samplerPart resolves this tick's engine")
    check("if(samplerPart)inst.sampler_scan(p,pp(MELODY_A,p));" in n,
          "sampler_scan must stay gated on the same samplerPart")


def source_detune_wiring_issues(cpp):
    """Return regressions in the stable SOURCE/panel-DETUNE host boundary.

    DETUNE moved out of the context menu and onto the panel (spec 2026-08-09
    hw-control-reduction task 10): it is a normal templated kParamCtls
    control now, read with the strided pp() accessor like every other
    per-part knob, squared before it reaches the engine. SOURCE's own
    routing is untouched by that move -- this function still polices both
    boundaries in one place because a wrong-lane mutation historically
    touched either side."""
    issues = []
    config = cpp_scope(cpp, "void configControls()")
    push = cpp_scope(cpp, "void pushParams()")
    # appendContextMenu is a two-line delegator now (the menu body was
    # lifted into the free function appendFireflowMenu so both widgets can
    # share it) -- anchor on the function that actually holds the content,
    # not the delegator, or this scope is an almost-empty string and every
    # check below it is vacuous (review finding IMPORTANT 2).
    menu = cpp_scope(cpp, "static void appendFireflowMenu(Menu* menu, Fireflow* m)")
    detune_quantity = cpp_scope(cpp, "struct DetuneQuantity : ParamQuantity")
    for label, block in (("configuration", config), ("parameter push", push)):
        if block is None:
            issues.append(f"SOURCE/Detune {label} scope is missing")
    if issues:
        return issues

    if detune_quantity is None:
        issues.append("DetuneQuantity scope is missing")
    else:
        expected_quantity = (
            "structDetuneQuantity:ParamQuantity{std::stringgetDisplayValueString()override{"
            "constfloatv=getValue();returnstring::f(\"%.1fct\","
            "v*v*spky::SynthEngine::kDetuneCeilCt);}}")
        if compact_cpp(detune_quantity) != expected_quantity:
            issues.append("DetuneQuantity must display the squared taper's cents")

    config_n = compact_cpp(config)
    for required, label in (
        ("elseif(c.id==DETUNE_A||c.id==DETUNE_B)",
         "DETUNE_A/B need their own configControls branch"),
        ("configParam<DetuneQuantity>(c.id,0.f,1.f,init,lbl);",
         "DETUNE must be configured as a normalized persistent Rack parameter"),
        ("if(c.id==SOURCE_A||c.id==SOURCE_B)",
         "SOURCE controls need their own stable Rack names"),
        (compact_cpp(
            'auto* source = configParam(c.id, 0.f, 1.f, init, '
            'c.id == SOURCE_A ? "SOURCE A" : "SOURCE B");'
            'source->description = "Controls Synth TIMB, Sampler ORG, Wave '
            'FRAME, or Body MATL according to the selected engine.";'),
         "SOURCE A/B need stable names and a TIMB/FRAME/ORG/MATL description"),
    ):
        if required not in config_n:
            issues.append(label)

    push_n = compact_cpp(push)
    source_base = "inst.set_target_base(p,spky::LANE_SOURCE,pp(SOURCE_A,p));"
    detune_read = "constfloatdetKnob=pp(DETUNE_A,p);"
    detune_push = "inst.set_voice_detune(p,detKnob*detKnob);"
    if push_n.count(source_base) != 1:
        issues.append("SOURCE must set LANE_SOURCE once for every engine")
    if push_n.count(detune_read) != 1:
        issues.append("DETUNE must be read once per deck with the strided pp() accessor")
    if push_n.count(detune_push) != 1:
        issues.append("DETUNE must feed voice detune through the squared taper")
    if "set_voice_detune(p,pp(DETUNE_A,p))" in push_n:
        issues.append("DETUNE must not reach the engine linearly -- the taper is squared")
    if "set_voice_detune(p,pp(SOURCE_A,p))" in push_n:
        issues.append("SOURCE must not feed voice detune")
    if "if(samplerPart){inst.set_target_base(p,spky::LANE_SOURCE," in push_n:
        issues.append("SOURCE base must not be gated on samplerPart")

    # DETUNE's context-menu submenu is gone with the menu-only shape it used
    # to share with DRIVE; a well-meaning revert re-adding it would silently
    # bring back a second, disagreeing way to set the same parameter.
    menu_n = compact_cpp(menu) if menu is not None else ""
    if 'createSubmenuItem("DetuneA"' in menu_n or \
       'createSubmenuItem("DetuneB"' in menu_n:
        issues.append("DETUNE must not still have a context-menu submenu "
                       "now that it is a panel control")
    return issues


def test_source_detune_host_wiring():
    """SOURCE owns LANE_SOURCE on every engine; DETUNE is a strided panel
    control that reaches the engine through the squared taper."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in source_detune_wiring_issues(cpp):
        check(False, issue)


def test_source_detune_guard_rejects_representative_regressions():
    """The source guard must catch wrong lane routing and independently a
    detune taper that regressed to linear or to the old menu-only shape, not
    merely recognize today's source."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("pp(SOURCE_A, p)", "pp(DETUNE_A, p)", "SOURCE lane"),
        ("const float detKnob = pp(DETUNE_A, p);\n"
         "            inst.set_voice_detune(p, detKnob * detKnob);",
         "inst.set_voice_detune(p, pp(DETUNE_A, p));", "linear detune taper"),
        ("c.id == DETUNE_A || c.id == DETUNE_B",
         "c.id == DETUNE_A", "Detune B configControls branch"),
        ("Synth TIMB, Sampler ORG, Wave FRAME, or Body MATL",
         "Synth COLOR, Sampler POSITION, Wave START, or Body SHAPE",
         "SOURCE description"),
        ("string::f(\"%.1f ct\",\n            v * v * spky::SynthEngine::kDetuneCeilCt);",
         "string::f(\"%.1f ct\",\n            v * spky::SynthEngine::kDetuneCeilCt);",
         "Detune display taper"),
    ]
    for before, after, label in mutations:
        check(before in cpp, f"fixture drifted: {label!r} needle not found")
        mutated = cpp.replace(before, after, 1)
        check(source_detune_wiring_issues(mutated),
              f"SOURCE/Detune guard accepted a {label} regression")


def flux_time_wiring_issues(cpp):
    """Return regressions in FLUX TIME's Rack-to-FX boundary.

    FLUXTIME_A/B (MULT) is retired (task 6, spec 2026-08-09
    hw-control-reduction): FLUXRATE_A/B, renamed TIME, is now the only
    panel path to the tape's delay time, reading its raw 0..11 detent index
    directly instead of round-tripping through flux_division_index(). The
    modulation sink MULT used to feed, FXT_FLUX_TIME, survives pinned to a
    hard-coded neutral base so CV and the mod lanes can still bend the tape.
    """
    issues = []
    quantity = cpp_scope(cpp, "struct FluxRateQuantity : ParamQuantity")
    config = cpp_scope(cpp, "void configControls()")
    push = cpp_scope(cpp, "void pushParams()")
    if "struct FluxTimeQuantity" in cpp:
        issues.append("FluxTimeQuantity must be retired along with the MULT knob")
    if "FLUXTIME_A" in cpp or "FLUXTIME_B" in cpp:
        issues.append("Fireflow.cpp still references a retired FLUXTIME id")
    expected_display = '''
std::string getDisplayValueString() override {
    int k = spky::kFluxRateOffset + (int)std::lround(getValue());
    return spky::kDivisions[k].name;
}'''
    if quantity is None or compact_cpp(expected_display) not in compact_cpp(quantity):
        issues.append("FluxRateQuantity must read the raw detent index directly")
    if quantity and "flux_division_index" in quantity:
        issues.append("FluxRateQuantity still round-trips through flux_division_index")
    if config is None or config.count("configParam<FluxRateQuantity>") != 1:
        issues.append("FLUXRATE is not configured through FluxRateQuantity")
    if config and "configParam<FluxTimeQuantity>" in config:
        issues.append("configControls still configures a retired FluxTimeQuantity")
    expected_config = """
else if (c.id == FLUXRATE_A || c.id == FLUXRATE_B)
    configParam<FluxRateQuantity>(
        c.id, 0.f, (float)(spky::kFluxRateCount - 1),
        init, lbl);
"""
    if config is None or compact_cpp(expected_config) not in compact_cpp(config):
        issues.append("TIME's configParam<FluxRateQuantity> call drifted "
                      "(range, initParamDefault-sourced default, or lbl)")
    expected_rate_push = """
inst.set_flux_rate(p, (int)std::lround(
    params[p ? FLUXRATE_B : FLUXRATE_A].getValue()));
"""
    if push is None or compact_cpp(expected_rate_push) not in compact_cpp(push):
        issues.append("TIME does not push its raw index to set_flux_rate")
    expected_base = """
inst.set_fx_target_base(p, spky::FXT_FLUX_TIME, 0.5f);
"""
    if push is None or compact_cpp(expected_base) not in compact_cpp(push):
        issues.append("FXT_FLUX_TIME must be pinned to the neutral 0.5 base")
    pitch = """
if (bbdPart)
    inst.set_target_base(p, spky::LANE_PITCH,
        params[p ? STAGES_B : STAGES_A].getValue());
"""
    push_n = compact_cpp(push) if push else ""
    if push is None or push_n.count(compact_cpp(pitch)) != 1:
        issues.append("STAGES must route exactly once to BBD LANE_PITCH")
    if "set_fx_target_base(p,spky::FXT_FLUX_TIME,params[p?STAGES_B:STAGES_A]" in push_n:
        issues.append("STAGES is coupled to the tape TIME target")
    if "constboolbbdPart=inst.engine_id(p)==spky::ENGINE_BBD;" not in push_n:
        issues.append("LANE_PITCH routing lacks the BBD-only gate")
    return issues


def test_flux_time_host_wiring():
    """TIME reads its raw detent index and FXT_FLUX_TIME stays pinned neutral."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in flux_time_wiring_issues(cpp):
        check(False, issue)


def test_flux_time_guard_rejects_representative_regressions():
    """The TIME guard rejects realistic deck, target, index, and default bugs."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("params[p ? FLUXRATE_B : FLUXRATE_A].getValue()",
         "params[p ? FLUXRATE_A : FLUXRATE_A].getValue()", "deck B id"),
        ("spky::FXT_FLUX_TIME, 0.5f", "spky::FXT_FLUX_FB, 0.5f", "FX target"),
        ("int k = spky::kFluxRateOffset + (int)std::lround(getValue());",
         "int k = spky::kFluxRateOffset + spky::flux_division_index(getValue());",
         "reintroduced flux_division_index round-trip"),
        ("configParam<FluxRateQuantity>(\n"
         "                            c.id, 0.f, (float)(spky::kFluxRateCount - 1),\n"
         "                            init, lbl);",
         "configParam<FluxRateQuantity>(\n"
         "                            c.id, 0.f, (float)(spky::kFluxRateCount - 1),\n"
         "                            0.f, lbl);",
         "hard-coded default"),
    ]
    for before, after, label in mutations:
        check(before in cpp, f"fixture for {label!r} did not match current source")
        mutated = cpp.replace(before, after, 1)
        check(flux_time_wiring_issues(mutated),
              f"TIME guard accepted a {label} regression")


# Derived, not hand-copied: the branch's whole point is that caption words
# live in one place (DYNAMIC_CAPTIONS), so the guard that checks no caption
# word is typed into the C++ must draw from that same table, not a second
# transcription of it that a later DYNAMIC_CAPTIONS row can silently outrun.
# BEND/TIME/SEND/PUSH are static (never state-dependent) captions with no
# DYNAMIC_CAPTIONS row of their own, so they're added explicitly. DIV/MULT
# retired with FLUXRATE_A/B's rename to TIME (task 6, spec 2026-08-09
# hw-control-reduction).
CAPTION_WORDS = tuple(sorted(
    {w for _t, _d, words in g.DYNAMIC_CAPTIONS for w in words}
    | {"BEND", "TIME", "SEND", "PUSH"}))


def caption_wiring_issues(cpp):
    """Return regressions in the one-table-drives-every-caption contract."""
    issues = []
    panel = cpp_scope(cpp, "struct PanelText : Widget")
    if panel is None:
        issues.append("PanelText scope is missing")
        return issues
    panel_n = compact_cpp(panel)

    for word in CAPTION_WORDS:
        if f'"{word}"' in cpp:
            issues.append(f"caption word {word!r} is typed into the C++; "
                          "every word belongs in gen_panel.py")
    for gone in ("sourceCaption(", "sourceCaptionAt(", "attackPitchCaptionAt("):
        if gone in cpp:
            issues.append(f"the retired per-control helper {gone} is back")
    if "for(constauto&d:kDynCaptions)" not in panel_n:
        issues.append("PanelText must resolve captions from kDynCaptions")
    if "module->params[d.driverId].getValue()" not in panel_n:
        issues.append("a dynamic caption must read its own driver parameter")
    if panel_n.count("ctlVisible(module,t[i].id)") != 1:
        issues.append("the caption loop must skip a control that is not the "
                      "one occupying its slot")
    if "Fireflow*module;explicitPanelText(Fireflow*m):module(m){}" not in panel_n:
        issues.append("PanelText must retain its Fireflow module pointer")

    # Not in the brief's verbatim body, added because the mutation test below
    # (also verbatim) mutates the construction site, not the struct scope --
    # without this check that mutation passes uncaught. See task-3 report.
    widget = cpp_scope(cpp, "FireflowWidget(Fireflow* module)")
    if widget is None:
        issues.append("FireflowWidget scope is missing")
    else:
        widget_n = compact_cpp(widget)
        if widget_n.count("newPanelText(module)") != 1:
            issues.append("FireflowWidget must construct PanelText with its module")
    return issues


def test_caption_host_wiring():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in caption_wiring_issues(cpp):
        check(False, issue)


def dynamic_flag_issues(cpp):
    """Return regressions in the `dynamic` flag that keeps the caption lookup
    from crossing id spaces.

    PanelCtl.id is a ParamId in kParamCtls but an Input/OutputId in
    kInputCtls/kOutputCtls -- three separate enums that all start at 0.
    MELODY_A (ParamId 5) and GATE_B (OutputId 5) share that number for
    exactly that reason, and the frozen enum order (test_enum_order) forbids
    "fixing" it by moving either id -- every saved .vcv keeps its ids. The
    numbers coinciding is not itself a bug: a guard that only compared the
    raw numbers (pids[target] not in jack-id-set) would fail on
    MELODY_A/GATE_B forever, fix or no fix, since nothing in Fireflow.cpp can
    change what number gen_panel.py assigns either one. What must never
    happen is the caption lookup treating that coincidence as the same slot
    -- so this guard pins the `dynamic` flag that keeps it from doing so:
    kParamCtls resolves dynamic captions, kInputCtls and kOutputCtls never do.
    """
    issues = []
    panel = cpp_scope(cpp, "struct PanelText : Widget")
    if panel is None:
        issues.append("PanelText scope is missing")
        return issues
    panel_n = compact_cpp(panel)
    for required, label in (
        ("captions(kParamCtls,sizeof(kParamCtls)/sizeof(kParamCtls[0]),true);",
         "kParamCtls must resolve dynamic captions (dynamic=true)"),
        ("captions(kInputCtls,sizeof(kInputCtls)/sizeof(kInputCtls[0]),false);",
         "kInputCtls must not resolve captions as ParamIds (dynamic=false)"),
        ("captions(kOutputCtls,sizeof(kOutputCtls)/sizeof(kOutputCtls[0]),false);",
         "kOutputCtls must not resolve captions as ParamIds (dynamic=false)"),
        ("if(dynamic&&!ctlVisible(module,t[i].id))continue;",
         "ctlVisible must be gated on dynamic, or an Input/OutputId reaches it"),
        ("dynamic?caption(t[i]):t[i].label",
         "caption() must be gated on dynamic, or an Input/OutputId reaches it"),
    ):
        if required not in panel_n:
            issues.append(label)
    return issues


def test_dynamic_lookup_stays_inside_the_param_id_space():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in dynamic_flag_issues(cpp):
        check(False, issue)


def test_dynamic_flag_guard_rejects_representative_regressions():
    """The dynamic-flag guard rejects reverting to unconditional resolution,
    not merely recognizing today's source."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("captions(kInputCtls,  sizeof(kInputCtls)  / sizeof(kInputCtls[0]),  false);",
         "captions(kInputCtls,  sizeof(kInputCtls)  / sizeof(kInputCtls[0]),  true);",
         "kInputCtls resolved as dynamic"),
        ("captions(kOutputCtls, sizeof(kOutputCtls) / sizeof(kOutputCtls[0]), false);",
         "captions(kOutputCtls, sizeof(kOutputCtls) / sizeof(kOutputCtls[0]), true);",
         "kOutputCtls resolved as dynamic"),
        ("if (dynamic && !ctlVisible(module, t[i].id)) continue;",
         "if (!ctlVisible(module, t[i].id)) continue;",
         "ctlVisible ungated"),
        ("dynamic ? caption(t[i]) : t[i].label",
         "caption(t[i])",
         "caption() ungated"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(dynamic_flag_issues(mutated),
              f"dynamic-flag guard accepted a {label} regression")


def test_caption_guard_rejects_representative_regressions():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("for (const auto& d : kDynCaptions)", "for (const auto& d : kParamCtls)",
         "table binding"),
        ("module->params[d.driverId].getValue()",
         "module->params[ENGINE_A].getValue()", "per-deck driver binding"),
        ("new PanelText(module)", "new PanelText(nullptr)", "widget module"),
        ("if (dynamic && !ctlVisible(module, t[i].id)) continue;", "",
         "shared-slot caption skip"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(caption_wiring_issues(mutated),
              f"caption guard accepted a {label} regression")


def attack_pitch_wiring_issues(cpp):
    """Return regressions in the engine-exclusive shared VOICE control."""
    issues = []
    rounded = cpp_scope(
        cpp, "static int roundedEngineState(Fireflow* module, int engineId)")
    selected = cpp_scope(
        cpp, "static bool isBbdSelected(Fireflow* module, int engineId)")
    visible = cpp_scope(cpp, "static bool ctlVisible(Fireflow* m, int id)")
    exclusive = cpp_scope(cpp, "struct SlotVisible : W")
    widget = cpp_scope(cpp, "FireflowWidget(Fireflow* module)")
    # Same re-anchor as source_detune_wiring_issues above: appendContextMenu
    # is now a two-line delegator, the Freeze Attack submenu lives in
    # appendFireflowMenu (review finding IMPORTANT 2).
    menu = cpp_scope(cpp, "static void appendFireflowMenu(Menu* menu, Fireflow* m)")

    expected_rounded = """
static int roundedEngineState(Fireflow* module, int engineId) {
    return module
        ? static_cast<int>(std::round(module->params[engineId].getValue()))
        : 0;
}"""
    expected_selected = """
static bool isBbdSelected(Fireflow* module, int engineId) {
    return roundedEngineState(module, engineId) == 4;
}"""
    if rounded is None or compact_cpp(rounded) != compact_cpp(expected_rounded):
        issues.append("roundedEngineState must round Rack ENG state and preview as Synth")
    if selected is None or compact_cpp(selected) != compact_cpp(expected_selected):
        issues.append("isBbdSelected must recognize only rounded BBD state 4")

    # The ATTACK/STAGES exclusivity rule used to live on the widget itself
    # (engineId/bbdOnly); it now lives in ctlVisible, in the same place the
    # REC/Sampler rule lives, so pin the arms here instead of the retired
    # fields.
    if visible is None:
        issues.append("ctlVisible scope is missing")
    else:
        n = compact_cpp(visible)
        for arm in ("caseATTACK_A:return!isBbdSelected(m,ENGINE_A);",
                    "caseATTACK_B:return!isBbdSelected(m,ENGINE_B);",
                    "caseSTAGES_A:returnisBbdSelected(m,ENGINE_A);",
                    "caseSTAGES_B:returnisBbdSelected(m,ENGINE_B);"):
            if arm not in n:
                issues.append(f"ctlVisible has no ATTACK/STAGES arm: {arm}")

    for label, scope in (("SlotVisible", exclusive),
                         ("context menu", menu)):
        if scope is None:
            issues.append(f"{label} scope is missing")
        elif "inst.engine_id(" in scope:
            issues.append(f"{label} must use Rack ENG state, not inst.engine_id()")
    exclusive_n = compact_cpp(exclusive) if exclusive else ""
    expected_exclusive = """
struct SlotVisible : W {
    Fireflow* fireflow = nullptr;
    int ctlId = 0;

    void step() override {
        this->setVisible(ctlVisible(fireflow, ctlId));
        W::step();
    }
}"""
    if exclusive_n != compact_cpp(expected_exclusive):
        issues.append("SlotVisible must call setVisible from ctlVisible")
    if re.search(r"\bvisible\s*=", exclusive or ""):
        issues.append("SlotVisible must not write Widget::visible directly")

    widget_n = compact_cpp(widget) if widget else ""
    for required, label in (
        ("if(c.id==ATTACK_A||c.id==ATTACK_B||c.id==STAGES_A||c.id==STAGES_B)",
         "ATTACK/STAGES need the exclusive widget branch"),
        ("createParamCentered<SlotVisible<Trimpot>>(pos,module,c.id)",
         "ATTACK/STAGES must use SlotVisible<Trimpot> at their generated position"),
        ("knob->ctlId=c.id;",
         "the shared VOICE widget must set ctlId from the control's own id"),
    ):
        if required not in widget_n:
            issues.append(label)

    menu_n = compact_cpp(menu) if menu else ""
    for part, engine_id, attack_id in (
            ("A", "ENGINE_A", "ATTACK_A"),
            ("B", "ENGINE_B", "ATTACK_B")):
        required = compact_cpp(
            f'if (isBbdSelected(m, {engine_id})) {{'
            f'menu->addChild(createSubmenuItem("BBD {part} — Freeze Attack", "", '
            f'[m](Menu* sub) {{sub->addChild(new ParamMenuSlider('
            f'm->getParamQuantity({attack_id})));}}));}}')
        if menu_n.count(required) != 1:
            issues.append(f"BBD {part} Freeze Attack must bind matching ATTACK state conditionally")
    return issues


def test_attack_pitch_host_wiring():
    """The shared VOICE slot, caption, and menu follow rounded Rack ENG state."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        cpp = f.read()
    for issue in attack_pitch_wiring_issues(cpp):
        check(False, issue)


def test_attack_pitch_guard_rejects_representative_regressions():
    """The shared VOICE guard rejects state, deck, visibility, and menu bugs."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        cpp = f.read()
    mutations = [
        ("static int roundedEngineState(Fireflow* module, int engineId)",
         "static int roundedEngineState(const Fireflow* module, int engineId)",
         "Rack Param const incompatibility"),
        ("static bool isBbdSelected(Fireflow* module, int engineId)",
         "static bool isBbdSelected(const Fireflow* module, int engineId)",
         "BBD helper const incompatibility"),
        ("static_cast<int>(std::round(module->params[engineId].getValue()))",
         "static_cast<int>(module->params[engineId].getValue())", "ENG rounding"),
        ("case ATTACK_B: return !isBbdSelected(m, ENGINE_B);",
         "case ATTACK_B: return !isBbdSelected(m, ENGINE_A);",
         "part B widget ENG"),
        ("case STAGES_B: return  isBbdSelected(m, ENGINE_B);",
         "case STAGES_B: return  isBbdSelected(m, ENGINE_A);",
         "STAGES B exclusivity"),
        ("static_cast<int>(std::round(module->params[engineId].getValue()))\n"
         "        : 0;",
         "static_cast<int>(std::round(module->params[engineId].getValue()))\n"
         "        : 4;", "preview fallback"),
        ("this->setVisible(ctlVisible(fireflow, ctlId));",
         "visible = true;", "direct overlap visibility"),
        ("getParamQuantity(ATTACK_B)",
         "getParamQuantity(ATTACK_A)", "part B menu quantity"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(attack_pitch_wiring_issues(mutated),
              f"ATTACK/PITCH guard accepted a {label} regression")


def rec_visibility_issues(cpp):
    """REC may not be offered on an engine where pushParams ignores it."""
    issues = []
    visible = cpp_scope(cpp, "static bool ctlVisible(Fireflow* m, int id)")
    widget = cpp_scope(cpp, "FireflowWidget(Fireflow* module)")
    if visible is None:
        issues.append("ctlVisible scope is missing")
    else:
        n = compact_cpp(visible)
        for arm in ("caseREC_A:returnsamplerDeck(m,ENGINE_A);",
                    "caseREC_B:returnsamplerDeck(m,ENGINE_B);"):
            if arm not in n:
                issues.append(f"ctlVisible has no Sampler arm: {arm}")
    if widget is None:
        issues.append("widget scope is missing")
    else:
        n = compact_cpp(widget)
        if "SlotVisible<VCVLatch>" not in n:
            issues.append("REC is not built as a slot-visible latch")
        if "SlotVisible<Trimpot>" not in n:
            issues.append("the ATTACK/STAGES pair must use the same mixin")
    return issues


def test_rec_visibility_host_wiring():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in rec_visibility_issues(cpp):
        check(False, issue)


def test_rec_visibility_guard_rejects_representative_regressions():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("case REC_B: return samplerDeck(m, ENGINE_B);",
         "case REC_B: return samplerDeck(m, ENGINE_A);",
         "part B ENG binding"),
        ("SlotVisible<VCVLatch>", "VCVLatch", "slot-visible latch"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(rec_visibility_issues(mutated),
              f"REC visibility guard accepted a {label} regression")


def rec_light_visibility_issues(cpp):
    """The REC LED widgets must hide off-Sampler by asking about the deck's
    ENGINE param id, never a light id: LightId and ParamId are different
    enums that both start at 0, and REC_A_L (LightId 2) collides numerically
    with DENSITY_A (ParamId 2) -- REC_B_L (3) with SMOOTH_A (3). Passing a
    light id into ctlVisible would silently ask about the wrong control, the
    same shape of bug the caption lookup shipped last week (see
    dynamic_flag_issues). samplerDeck is the one shared predicate the pad's
    ctlVisible arm and the LED's SamplerOnly mixin must both call, so the two
    rules cannot drift apart the way test_sampler_deck_predicate_is_single_
    source_of_truth pins separately."""
    issues = []
    visible = cpp_scope(cpp, "static bool ctlVisible(Fireflow* m, int id)")
    mixin = cpp_scope(cpp, "struct SamplerOnly : W")
    widget = cpp_scope(cpp, "FireflowWidget(Fireflow* module)")
    if visible is None:
        issues.append("ctlVisible scope is missing")
    else:
        n = compact_cpp(visible)
        for bad in ("REC_A_L", "REC_B_L", "GATE_A_L", "GATE_B_L"):
            if bad in n:
                issues.append(f"ctlVisible must never be asked about a light id: {bad}")
    if mixin is None:
        issues.append("SamplerOnly<W> scope is missing")
    else:
        expected_mixin = compact_cpp("""
struct SamplerOnly : W {
    Fireflow* fireflow = nullptr;
    int engineId = ENGINE_A;

    void step() override {
        this->setVisible(samplerDeck(fireflow, engineId));
        W::step();
    }
}""")
        if compact_cpp(mixin) != expected_mixin:
            issues.append("SamplerOnly<W> must call setVisible from samplerDeck, "
                          "parallel to SlotVisible/ctlVisible")
    if widget is None:
        issues.append("widget scope is missing")
    else:
        n = compact_cpp(widget)
        if "SamplerOnly<SmallLight<RedLight>>" not in n:
            issues.append("REC lights must be built as SamplerOnly<SmallLight<RedLight>>")
        if "led->engineId=(c.id==REC_A_L)?ENGINE_A:ENGINE_B;" not in n:
            issues.append("each deck's REC light must bind its own ENGINE_* id")
    return issues


def test_rec_light_visibility_host_wiring():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    for issue in rec_light_visibility_issues(cpp):
        check(False, issue)


def test_rec_light_visibility_guard_rejects_representative_regressions():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("led->engineId = (c.id == REC_A_L) ? ENGINE_A : ENGINE_B;",
         "led->engineId = (c.id == REC_A_L) ? ENGINE_A : ENGINE_A;",
         "deck-B light bound to the wrong deck's ENGINE"),
        ("SamplerOnly<SmallLight<RedLight>>", "SmallLight<RedLight>",
         "REC light built with the bare widget type"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(rec_light_visibility_issues(mutated),
              f"REC light visibility guard accepted a {label} regression")


def test_sampler_deck_predicate_is_single_source_of_truth():
    """'This deck is running the Sampler' must be written exactly once --
    samplerDeck -- so the REC pad's ctlVisible arm and the REC LED's
    SamplerOnly mixin read the same rule instead of two copies that a later
    edit can silently desync (the ablation-verdict-discipline lesson: an
    unmeasured duplicate is how a residue gets mislabeled)."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    check(cpp.count("static bool samplerDeck(Fireflow* m, int engineId)") == 1,
          "samplerDeck must have exactly one definition")
    matches = re.findall(r"roundedEngineState\([^)]*\)\s*==\s*1", cpp)
    check(len(matches) == 1,
          f"'roundedEngineState(...) == 1' must appear exactly once, found {len(matches)}")
    scope = cpp_scope(cpp, "static bool samplerDeck(Fireflow* m, int engineId)")
    if matches and scope is not None:
        check(matches[0] in scope,
              "the lone '== 1' Sampler comparison must live inside samplerDeck")


def test_shuffle_host_wiring():
    """Rack pushes the appended shared knob into the instrument once per
    control update, before either deck can enter STEP and latch that
    control-tick's groove value. The init value lives in init_patch.hpp."""
    here = os.path.dirname(os.path.abspath(__file__))
    cpp_path = os.path.join(here, "..", "src", "Fireflow.cpp")
    with open(cpp_path) as f:
        cpp = f.read()
    check("inst.set_shuffle(params[SHUFFLE].getValue());" in cpp,
          "Rack SHUFFLE param is not wired to Instrument::set_shuffle")
    shuffle_push = cpp.index("inst.set_shuffle(params[SHUFFLE].getValue());")
    first_step_push = cpp.index("inst.set_step(")
    check(shuffle_push < first_step_push,
          "Rack must push shared SHUFFLE before either FLOW->STEP transition")


def test_sampler_preset_init_snapshot():
    """The approved init values, pinned by NAME. A control that leaves the
    panel takes its entry with it; nothing below it moves. Also covers the
    init-snapshot header's other invariants: no obsolete remembered-form
    state, Fireflow.cpp wiring, and the schema-migration/factory-asset
    contract, none of which the name-based refactor should touch."""
    import gen_panel as gp
    here = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(here, "..", "src", "init_patch.hpp")
    if not os.path.isfile(header_path):
        check(False, "init_patch.hpp missing")
        return
    with open(header_path) as f:
        header = f.read()
    approved = {
        # generated once by the command in the plan's Step 6; a second,
        # independent copy of the same numbers -- that is what makes this a test
        "RATE_A": 0.116716892,
        "SHAPE_A": 0.0,
        "DENSITY_A": 0.695181072,
        "SMOOTH_A": 0.995180666,
        "RANGE_A": 0.0,
        "MELODY_A": 0.0,
        "MOD_A": 0.612047195,
        "TUNE_A": 0.0,
        "ATTACK_A": 0.185333401,
        "DECAY_A": 0.322666585,
        "RES_A": 0.319000006,
        "SUB_A": 0.458666444,
        "SOURCE_A": 0.438666672,
        "FLUX_A": 0.86400038,
        "GRIT_A": 0.0,
        # COMP_A/COMP_B deliberately changed (spec 2026-08-09
        # hw-control-reduction task 5): the knob's meaning changed from
        # "compressor amount" to "LVL/COMP", so the old value cannot be
        # preserved -- 0.8 is full output level, no compressor.
        "COMP_A": 0.8,
        # STEPS_A now carries the retired STEP_A pad's boolean too (spec
        # 2026-08-09 hw-control-reduction task 3 review, Finding 7): the
        # approved boot was STEP_A=0 (off) / STEPS_A=16 (parked); the merge
        # restores the "off" half as 0 and cannot keep the parked count.
        "STEPS_A": 0.0,
        "ENGINE_A": 0.0,
        "GRITMODE_A": 1.0,
        "STEP_A": 0.0,
        "FORM_A": 2.0,
        "NEWPHRASE_A": 0.0,
        # SONG_A is a ladder rung index now, not a raw SongMode (spec
        # 2026-08-09 hw-control-reduction task 3 review). Rung 6 is
        # {form: Hierarchical, song: AAAB} -- the exact pair the retired
        # FORM_A=2.0/old SONG_A=0.0 held, so the approved init sound is
        # unchanged even though the number that encodes it is not.
        "SONG_A": 6.0,
        "RATE_B": 0.202409565,
        "SHAPE_B": 0.899999678,
        "DENSITY_B": 0.64457792,
        "SMOOTH_B": 0.613253355,
        "RANGE_B": 0.0,
        "MELODY_B": -1.0,
        "MOD_B": 0.35783118,
        "TUNE_B": 0.0,
        "ATTACK_B": 0.093333311,
        "DECAY_B": 0.450666398,
        "RES_B": 0.217333555,
        "SUB_B": 0.319999605,
        "SOURCE_B": 0.177333504,
        "FLUX_B": 1.0,
        "GRIT_B": 0.0,
        "COMP_B": 0.8,   # see COMP_A above
        "STEPS_B": 0.0,  # same flow-mode boot restoration as STEPS_A, see above
        "ENGINE_B": 3.0,
        "GRITMODE_B": 0.0,
        "STEP_B": 0.0,
        "FORM_B": 2.0,
        "NEWPHRASE_B": 0.0,
        "SONG_B": 6.0,  # same rung-6 preservation as SONG_A, see above
        "MORPH": 0.785541892,
        "TEMPO": 0.169333577,
        "COUPLE": 1.0,
        "SCALE": 5.0,
        # 0.958666623 is what set_drift() must receive; under the new zone
        # mapping (task 8, spec 2026-08-09 hw-control-reduction) the raw
        # knob position that reproduces it is 0.958666623 * 0.98 + 0.02 =
        # 0.959493291, not the old raw value -- see gen_panel.py's
        # INIT_DEFAULTS["DRIFT"] comment for the full derivation.
        "DRIFT": 0.959493291,
        "SETTLE": 0.0,
        # SPOT and MASTER_DRIVE retired (task 9, spec 2026-08-09
        # hw-control-reduction) -- their entries leave with them, per this
        # test's own docstring. MASTER_DRIVE's approved 0.482666761 is
        # superseded by the brief's by-ear pin, 0.40 -- not carried forward.
        "REV_SIZE": 0.869332671,
        "REV_DECAY": 0.790665507,
        "REV_TONE": 0.761333108,
        "REV_DIFF": 0.862999976,
        # REV_SMEAR and REV_MOD retired (task 9); their entries leave too.
        # Approved 0.484000504/0.237000003 are superseded by the brief's
        # by-ear pins, 0.30/0.15 -- not carried forward.
        "CHOKE": 0.0,
        "FILT_A": -0.172999933,
        "FILT_B": -0.19999963,
        "TIDE": 0.0,
        # FLUXRATE_A/B used to be normalized 0..1 floats run through
        # flux_division_index(); task 6 (spec 2026-08-09
        # hw-control-reduction) made the knob a 12-detent KNOBI whose value
        # IS the index, so the approved snapshot carries the converted
        # indices the old floats used to round to (0.392727494 -> 4,
        # 0.25466612 -> 3), not the old floats -- the factory delay time is
        # unchanged, only the number that encodes it.
        "FLUXRATE_A": 4.0,
        "FLUXRATE_B": 3.0,
        "FLUXFB_A": 0.285667986,
        "FLUXFB_B": 0.555337131,
        "COLOR_A": 0.0,
        "COLOR_B": 0.469879329,
        "LINK_A": 0.0,
        "LINK_B": 0.0,
        "STAGES_A": 0.800000012,
        "STAGES_B": 1.0,
        "REC_A": 0.0,
        "REC_B": 0.0,
        "REV_MIX_A": 0.422665179,
        "REV_MIX_B": 0.613332987,
        "SHUFFLE": 0.0,
        # DETUNE_A/B used to share one raw value, 0.171428576 ("= 6 / 35"): a
        # linear knob into the old 35 ct ceiling landed both decks at 6 ct.
        # Task 10 (spec 2026-08-09 hw-control-reduction) squared the taper
        # and tripled the synth-family ceiling to 105 ct; BODY's compensating
        # kDetuneScale shrank from 4 to 4/3 to hold its own 140 ct rail
        # exactly where it was, but that compensation only agrees with the
        # OLD shared value at full knob travel, not at this init position.
        # The approved patch boots ENGINE_A = SYNTH, ENGINE_B = BODY, so each
        # deck's init value is solved to preserve the cents ITS OWN engine
        # actually produces: DETUNE_A = sqrt(6 / 105) (6.000 ct on SYNTH),
        # DETUNE_B = sqrt(24 / 140) (24.000 ct on BODY -- 24 ct is what the
        # old shared value produced there: 0.171428576 * 35 * 4).
        "DETUNE_A": 0.239045722,
        "DETUNE_B": 0.414039341,
        # DRIVE_A/B retired here (task 9, spec 2026-08-09
        # hw-control-reduction) -- dead menu-only patch state, its entry
        # leaves with it. FLUXTIME_A/B (MULT) retired earlier (task 6) --
        # its entry left with it too, per this test's own docstring.
    }
    for name, want in approved.items():
        if name not in gp.INIT_DEFAULTS:
            continue          # control retired by a later task -- see the plan
        check(abs(gp.INIT_DEFAULTS[name] - want) < 1e-6,
              f"{name} init default drifted: {gp.INIT_DEFAULTS[name]} != {want}")

    check("kInitLastBasis" not in header,
          "obsolete remembered-form init state remains")

    cpp_path = os.path.join(here, "..", "src", "Fireflow.cpp")
    with open(cpp_path) as f:
        cpp = f.read()
    check('#include "init_patch.hpp"' in cpp,
          "Fireflow.cpp does not include the init snapshot")
    check("const float init = initParamDefault(c.id);" in cpp,
          "configControls does not read the indexed init snapshot")
    check("defaultFor(" not in cpp,
          "legacy split defaultFor table still exists")

    check("lastBasis[" not in cpp,
          "obsolete remembered-form runtime state remains")

    on_reset = cpp.split("void onReset() override {", 1)[1]
    on_reset = on_reset.split("// --- persistence", 1)[0]
    check("smp[p] = SamplerPartState{};" in on_reset,
          "Initialize does not reset sampler edit state")
    check("inst.sampler_clear(p);" in on_reset,
          "Initialize does not empty sampler audio before factory autoload")

    check('json_object_set_new(root, "formSongVersion", json_integer(1));' in cpp,
          "new patches do not carry the FORM/SONG schema marker")
    check('json_object_set_new(root, "linkVersion", json_integer(1));' in cpp,
          "new patches do not carry the LINK schema marker")
    check('json_object_get(root, "formSongVersion")' in cpp,
          "FORM/SONG migration does not check the schema marker")
    check('json_object_get(data, "linkVersion")' in cpp,
          "LINK migration does not check its independent marker")
    check('void fromJson(json_t* module_root) override' in cpp,
          "legacy LINK does not capture raw module params before Rack clamps them")
    check(cpp.count('Module::fromJson(module_root);') == 1,
          "module-level loader must call Rack's base loader exactly once")
    if ('json_object_get(module_root, "params")' in cpp
            and 'Module::fromJson(module_root);' in cpp):
        check(cpp.index('json_object_get(module_root, "params")') <
              cpp.index('Module::fromJson(module_root);'),
              "legacy LINK raw params are read after Rack clamps them")
    check('migrate_legacy_link((float)json_number_value(value_json))' in cpp,
          "legacy LINK migration does not use the raw numeric patch value")
    if ('Module::fromJson(module_root);' in cpp
            and 'params[p ? LINK_B : LINK_A].setValue(migrated[p])' in cpp):
        check(cpp.index('Module::fromJson(module_root);') <
              cpp.index('params[p ? LINK_B : LINK_A].setValue(migrated[p])'),
              "legacy LINK migration is not applied after Rack's base restore")
    check('json_object_get(root, "lastBasis")' in cpp,
          "legacy lastBasis state is not read for migration")
    check('json_object_get(root, "principle")' in cpp,
          "older legacy principle state is not read for migration")
    check("is_modern_form_song_version(" in cpp
          and "version && json_is_integer(version)" in cpp,
          "schema marker type/version is not validated")
    check("json_is_array(bases)" in cpp and "json_is_array(principles)" in cpp
          and "json_is_integer(basis)" in cpp
          and "json_is_integer(principle)" in cpp,
          "legacy arrays and values are not type-checked")
    check("params[p ? SONG_B : SONG_A].setValue((float)rung);" in cpp,
          "legacy FORM value is not migrated onto a matching SONG ladder rung")
    check("spky::song_ladder_at(i).form == migrated.form" in cpp,
          "legacy FORM migration no longer searches the ladder for a match")
    check('configParam<LinkQuantity>(c.id, 0.f, 1.f, init, lbl);' in cpp,
          "LINK is not configured as unipolar THIN")
    check('return v > 0.005f ? string::f("thin %.0f %%", 100.f * v) : "off";' in cpp,
          "LINK does not display only thin percentage or off")
    check('"drag %.0f %%"' not in cpp,
          "LINK still displays the removed DRAG behavior")
    check('migrate_legacy_link(params[id].getValue())' not in cpp,
          "LINK migration incorrectly reads Rack-clamped parameter values")

    makefile_path = os.path.join(here, "..", "Makefile")
    with open(makefile_path) as f:
        makefile = f.read()
    check("res/factory.wav" in makefile,
          "factory.wav is not included in the VCV distribution")


MONO_ADV = 0.6          # advance width of the monospace face, in ems


def text_w(s, size_mm):
    return len(s) * MONO_ADV * size_mm


def text_span(x, anchor, text, size):
    width = text_w(text, size)
    if anchor == 'end':
        return x - width, x
    if anchor == 'middle':
        return x - width / 2.0, x + width / 2.0
    return x, x + width


def wedge_points(svg):
    """Return the four annulus-corner points from a generated wedge path."""
    pattern = re.compile(
        r'(?:M|L)\s+([0-9.]+)\s+([0-9.]+)|'
        r'A\s+[0-9.]+\s+[0-9.]+\s+0\s+[01]\s+[01]\s+'
        r'([0-9.]+)\s+([0-9.]+)')
    points = []
    for match in pattern.finditer(svg):
        x = match.group(1) or match.group(3)
        y = match.group(2) or match.group(4)
        points.append((float(x), float(y)))
    return points


def test_all_deck_local_geometry_is_exactly_mirrored():
    """One property guard covers every deck-local glyph, label, and field."""
    flip = {'start': 'end', 'end': 'start', 'middle': 'middle'}

    params = {c.enum: c for c in g.RUNTIME_PANEL_PARAMS}
    a_params = [c for c in g.RUNTIME_PANEL_PARAMS if c.enum.endswith('_A')]
    check(len(a_params) > 0, "no deck-A parameters found")
    for a in a_params:
        b_name = a.enum[:-2] + '_B'
        b = params.get(b_name)
        check(b is not None, f"{a.enum}: missing mirror {b_name}")
        if b is None:
            continue
        check(a.kind == b.kind and a.label == b.label,
              f"{a.enum}/{b.enum}: kind or caption differs")
        check(approx(b.x, g.W - a.x) and approx(b.y, a.y),
              f"{a.enum}/{b.enum}: control coordinates are not mirrored")
        al = g.label_of(a)
        bl = g.label_of(b)
        check(approx(bl[0], g.W - al[0]) and approx(bl[1], al[1]),
              f"{a.enum}/{b.enum}: primary label coordinates are not mirrored")
        check(bl[2] == flip[al[2]],
              f"{a.enum}/{b.enum}: anchors {al[2]!r}/{bl[2]!r} are not mirrored")
        check(approx(bl[3], al[3]) and bl[4] == al[4],
              f"{a.enum}/{b.enum}: primary label styling differs")

    lights = {c.enum: c for c in g.LIGHTS}
    for a in (c for c in g.LIGHTS if c.enum.endswith('_A_L')):
        b_name = a.enum[:-4] + '_B_L'
        b = lights.get(b_name)
        check(b is not None, f"{a.enum}: missing mirror {b_name}")
        if b is not None:
            check(a.kind == b.kind and approx(b.x, g.W - a.x) and approx(b.y, a.y),
                  f"{a.enum}/{b.enum}: light coordinates are not mirrored")

    for name, a0, a1, _caption in g.SECTORS:
        a_points = wedge_points(g.wedge_svg(g.RING_CX_A, a0, a1, g.GREEN, False))
        b_points = wedge_points(
            g.wedge_svg(g.W - g.RING_CX_A, a0, a1, g.COPPER, True))
        want_b = sorted((round(g.W - x, 3), round(y, 3)) for x, y in a_points)
        got_b = sorted((round(x, 3), round(y, 3)) for x, y in b_points)
        check(len(a_points) == 4 and got_b == want_b,
              f"{name}: sector path points are not mirrored")

    for a, b in zip(g.part_groups(False), g.part_groups(True)):
        check(approx(b[0], g.W - a[0] - a[2]) and b[1:] == a[1:],
              f"{a[4]}: group record is not mirrored")

    for name in (field[1] for field in g.part_fx_fields(False)):
        a = next(field for field in g.FX_FIELDS if not field[0] and field[1] == name)
        b = next(field for field in g.FX_FIELDS if field[0] and field[1] == name)
        check(approx(b[2], g.W - a[2] - a[4]) and b[3:] == a[3:],
              f"{name}: FX field record is not mirrored")

    a, b = g.PLAY_FIELDS
    check(not a[0] and b[0] and approx(b[1], g.W - a[1] - a[3])
          and b[2:] == a[2:],
          "PLAY field records are not mirrored")


def test_dynamic_caption_geometry_for_every_state():
    """Every state-dependent word stays inside its fieldset and clear of the
    neighbouring glyphs and captions, on both decks. A caption that is only
    correct in one state is not correct."""
    voice_a = next(gr for gr in g.GROUPS if gr[4] == "VOICE" and gr[0] < g.CX)
    voice_b = next(gr for gr in g.GROUPS if gr[4] == "VOICE" and gr[0] > g.CX)

    def label_box(c, word):
        x, y, anchor, size, _colour = g.label_of(c)
        left, right = text_span(x, anchor, word, size)
        return left, y - size, right, y + size * 0.25

    def boxes_clear(a, b, gap=0.0):
        al, at, ar, ab = a
        bl, bt, br, bb = b
        return ar + gap <= bl or br + gap <= al or ab + gap <= bt or bb + gap <= at

    for target in ("SOURCE", "SUB", "DECAY", "RES", "FILT", "ATTACK"):
        words = g.dynamic_words(target)
        for suffix, box in (("_A", voice_a), ("_B", voice_b)):
            source = ctl(target + suffix)
            for state, word in enumerate(words):
                bounds = label_box(source, word)
                left, top, right, bottom = bounds
                check(left >= box[0] + 0.5 and right <= box[0] + box[2] - 0.5
                      and top >= box[1] + 0.5 and bottom <= box[1] + box[3] - 0.5,
                      f"{target}{suffix} state {state} {word} leaves VOICE: {bounds}")
                for base in ("ATTACK", "DECAY", "RES", "SUB", "FILT", "SOURCE"):
                    if base == target:
                        continue
                    other = ctl(base + suffix)
                    # Caption rectangle against the neighbouring circular control.
                    near_x = min(max(other.x, left), right)
                    near_y = min(max(other.y, top), bottom)
                    distance = math.hypot(near_x - other.x, near_y - other.y)
                    check(distance >= g.GLYPH_R[other.kind] + 0.3,
                          f"{target}{suffix} {word} crowds {other.enum} control")
                    widest = max(g.dynamic_words(base) or [other.label], key=len)
                    other_bounds = label_box(other, widest)
                    check(boxes_clear(bounds, other_bounds, 0.8),
                          f"{target}{suffix} {word} crowds {other.enum} label")


def test_panel_texts_stay_on_the_plate():
    for t in g.TEXTS:
        check(1.0 <= t[0] <= g.W - 1.0 and 1.0 <= t[1] <= g.Hh - 1.0,
              f"panel text {t[-1]!r} off plate at ({t[0]:.2f}, {t[1]:.2f})")


def test_header_carries_text_anchor():
    """The SVG preview and Rack must align these the same way; the C++ can only
    do that if the generated table ships the anchor."""
    h = g.header()
    check("unsigned char anchor; const char* str;" in h,
          "PanelTxt has no anchor column")
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    check("alignOf(t.anchor)" in cpp,
          "the kPanelTexts draw loop ignores the anchor column")


def test_vcv_tape_memory_is_heap_backed_stereo_storage():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp")) as f:
        cpp = f.read()
    check('std::vector<float> echoMem[spky::PART_COUNT][2];' in cpp,
          "VCV tape memory is not heap-backed stereo storage")
    check('float echo[spky::PART_COUNT][spky::Flux::kMaxSamples]' not in cpp,
          "VCV still embeds the tape arena by value in every Module")


def test_steps_knob_carries_the_mode():
    """STEP's pad is gone: 0 on the STEPS knob IS flow mode, and the host
    derives the boolean from the count instead of reading a second control."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("STEP_A" not in names and "STEP_B" not in names,
          "STEP pads still exist")
    steps = [c for c in gp.PARAMS if c.enum == "STEPS_A"]
    check(len(steps) == 1 and steps[0].kind == gp.KNOBI,
          "STEPS_A missing or no longer an integer knob")
    cpp = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "src", "Fireflow.cpp")).read()
    check("configParam(c.id, 0.f, 16.f" in cpp or
          "0.f, 16.f, initParamDefault" in cpp,
          "STEPS is not configured over 0..16")
    check("inst.set_step(p, steps > 0, steps)" in cpp,
          "set_step no longer derives its mode from the count")
    check("STEP_A" not in cpp, "Fireflow.cpp still references STEP_A")


def test_init_defaults_are_generated_from_names():
    """init_patch.hpp is emitted from INIT_DEFAULTS, keyed by param name, so
    removing a param cannot leave a stale positional value behind."""
    import gen_panel as gp
    here = os.path.dirname(os.path.abspath(__file__))
    header = open(os.path.join(here, "..", "src", "init_patch.hpp")).read()
    check("GENERATED by res/gen_panel.py" in header,
          "init_patch.hpp is not marked generated")
    missing = [c.enum for c in gp.PARAMS if c.enum not in gp.INIT_DEFAULTS]
    check(not missing, f"params without an INIT_DEFAULTS entry: {missing}")
    extra = [k for k in gp.INIT_DEFAULTS if k not in {c.enum for c in gp.PARAMS}]
    check(not extra, f"INIT_DEFAULTS entries for params that do not exist: {extra}")
    for c in gp.PARAMS:
        check(f"// {c.enum}\n" in header or f"// {c.enum}" in header,
              f"{c.enum} missing from the emitted table")

    # The checks above only look for the NAME comments and the marker string
    # -- none of them read the numbers the emitter actually wrote. Parse the
    # real kInitParamDefaults[] out of the header (the array the firmware
    # indexes by ParamId) and check both its values AND its order against
    # INIT_DEFAULTS/PARAMS, so an emitter bug (bad lookup, transposed line,
    # precision loss) can't silently ship a wrong array while every check
    # above still passes.
    match = re.search(r"kInitParamDefaults\[\]\s*=\s*\{(.*?)\};", header, re.DOTALL)
    check(match is not None, "kInitParamDefaults array missing from init_patch.hpp")
    if match is not None:
        entries = []
        for line in match.group(1).splitlines():
            m = re.match(r"\s*([-0-9.eE]+)f?,\s*//\s*(\w+)", line)
            if m:
                entries.append((m.group(2), float(m.group(1))))
        want_order = [c.enum for c in gp.PARAMS]
        got_order = [name for name, _ in entries]
        check(got_order == want_order,
              f"kInitParamDefaults order drifted from PARAMS order: "
              f"{got_order} != {want_order}")
        for name, value in entries:
            if name in gp.INIT_DEFAULTS:
                check(abs(value - gp.INIT_DEFAULTS[name]) < 1e-6,
                      f"kInitParamDefaults[{name}] emitted {value}, "
                      f"INIT_DEFAULTS has {gp.INIT_DEFAULTS[name]}")


def test_comp_knob_is_level_then_compressor():
    """COMP was a volume control in practice. The knob says so now, and the
    compressor lives in its top fifth with make-up."""
    import gen_panel as gp
    comp = [c for c in gp.PARAMS if c.enum == "COMP_A"][0]
    check(comp.label == "LVL", f"COMP_A still prints {comp.label!r}")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kLvlCompSplit" in cpp, "no zone split constant in the host")
    check("inst.set_part_level(" in cpp, "the host never sets a part level")


def test_lvl_comp_split_and_formulas_agree_across_host_and_bench():
    """LVL/COMP's zone split (kLvlCompSplit/kCompTop) and its level/comp
    formulas exist in two places: Fireflow.cpp (what Rack actually runs) and
    bench/audition/init_patch.cpp (the only copy a doctest can reach --
    Fireflow.cpp lives inside a Rack Module, unreachable from the engine test
    suite). This scrapes both files' source text and requires the split
    constants' values and both formulas to match exactly, so a hand-edit to
    only one copy fails loudly here instead of shipping a Rack build that
    disagrees with its own test coverage.

    Each extraction below is asserted to find exactly one match per file --
    zero matches (the extraction quietly finding nothing) is treated as a
    failure, not a pass: a scraper that matches nothing must not report
    success.

    Deliberately NOT a call to consolidate the two copies into a shared
    helper -- the codebase mirrors this logic on purpose (see the GRIT
    dead-zone guard above, same pattern).
    """
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    def const_value(source, name, label):
        matches = re.findall(name + r"\s*=\s*([\d.]+f?)", source)
        check(len(matches) == 1,
              f"{label}: expected exactly one {name} declaration, "
              f"found {len(matches)} ({matches!r})")
        return matches[0].rstrip("f") if len(matches) == 1 else None

    for const in ("kLvlCompSplit", "kCompTop"):
        host_v = const_value(host_cpp, const, "Fireflow.cpp")
        bench_v = const_value(bench_cpp, const, "bench/audition/init_patch.cpp")
        if host_v is not None and bench_v is not None:
            check(float(host_v) == float(bench_v),
                  f"{const} disagrees: Fireflow.cpp={host_v} "
                  f"bench/audition/init_patch.cpp={bench_v}")

    needles = {
        "level formula": compact_cpp(
            "std::min(1.f,lvlKnob/kLvlCompSplit)"),
        "comp formula": compact_cpp(
            "lvlKnob<=kLvlCompSplit?0.f:(lvlKnob-kLvlCompSplit)/"
            "(1.f-kLvlCompSplit)*kCompTop"),
    }
    host_n, bench_n = compact_cpp(host_cpp), compact_cpp(bench_cpp)
    for label, needle in needles.items():
        check(host_n.count(needle) == 1,
              f"Fireflow.cpp: expected exactly one {label}, found "
              f"{host_n.count(needle)} matching {needle!r}")
        check(bench_n.count(needle) == 1,
              f"bench/audition/init_patch.cpp: expected exactly one "
              f"{label}, found {bench_n.count(needle)} matching {needle!r}")


def test_couple_zone_split_and_formula_agree_across_host_and_bench():
    """COUPLE's zone split (kCoupleZoneSplit) and its two-zone formula --
    below the split SYNC is off and couple sweeps 0..1 in the FREE world,
    above it SYNC is on and couple sweeps 0..1 in the GRID world -- exist in
    two places: Fireflow.cpp (what Rack actually runs) and
    bench/audition/init_patch.cpp (the only copy a doctest can reach --
    Fireflow.cpp lives inside a Rack Module, unreachable from the engine test
    suite). This scrapes both files' source text and requires the split
    constant's value and the formula to match exactly, so a hand-edit to only
    one copy fails loudly here instead of shipping a Rack build that
    disagrees with its own test coverage.

    Each extraction below is asserted to find exactly one match per file --
    zero matches (the extraction quietly finding nothing) is treated as a
    failure, not a pass: a scraper that matches nothing must not report
    success.

    Deliberately NOT a call to consolidate the two copies into a shared
    helper -- the codebase mirrors this logic on purpose (see the GRIT
    dead-zone and LVL/COMP guards above, same pattern).
    """
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    def const_value(source, name, label):
        matches = re.findall(name + r"\s*=\s*([\d.]+f?)", source)
        check(len(matches) == 1,
              f"{label}: expected exactly one {name} declaration, "
              f"found {len(matches)} ({matches!r})")
        return matches[0].rstrip("f") if len(matches) == 1 else None

    host_v = const_value(host_cpp, "kCoupleZoneSplit", "Fireflow.cpp")
    bench_v = const_value(bench_cpp, "kCoupleZoneSplit",
                           "bench/audition/init_patch.cpp")
    if host_v is not None and bench_v is not None:
        check(float(host_v) == float(bench_v),
              f"kCoupleZoneSplit disagrees: Fireflow.cpp={host_v} "
              f"bench/audition/init_patch.cpp={bench_v}")

    needles = {
        "zone test": compact_cpp("grid=coupleKnob>=kCoupleZoneSplit"),
        "two-zone formula": compact_cpp(
            "grid?(coupleKnob-kCoupleZoneSplit)/(1.f-kCoupleZoneSplit)"
            ":coupleKnob/kCoupleZoneSplit"),
    }
    host_n, bench_n = compact_cpp(host_cpp), compact_cpp(bench_cpp)
    for label, needle in needles.items():
        check(host_n.count(needle) == 1,
              f"Fireflow.cpp: expected exactly one {label}, found "
              f"{host_n.count(needle)} matching {needle!r}")
        check(bench_n.count(needle) == 1,
              f"bench/audition/init_patch.cpp: expected exactly one "
              f"{label}, found {bench_n.count(needle)} matching {needle!r}")


def test_drift_knob_settles_at_its_left_stop():
    """SETL was drift-to-zero plus a glide. It lives at the end of the axis
    it always belonged to, and fires once on entry, not every tick."""
    import gen_panel as gp
    check("SETTLE" not in {c.enum for c in gp.PARAMS},
          "the SETL pad still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kDriftSettleZone" in cpp, "no settle zone constant in the host")
    check("driftSettled" in cpp, "settle is not edge-triggered")
    check("params[SETTLE]" not in cpp, "Fireflow.cpp still reads a SETTLE param")


def test_drift_settle_zone_and_formula_agree_across_host_and_bench():
    """DRIFT's settle zone (kDriftSettleZone) and its zone-mapped formula --
    at or below the zone the knob is the old SETL pad's left stop and drift
    is pinned to 0, above it drift sweeps 0..1 across the rest of the axis --
    exist in two places: Fireflow.cpp (what Rack actually runs) and
    bench/audition/init_patch.cpp (the only copy a doctest can reach --
    Fireflow.cpp lives inside a Rack Module, unreachable from the engine test
    suite). This scrapes both files' source text and requires the zone
    constant's value and the formula to match exactly, so a hand-edit to only
    one copy fails loudly here instead of shipping a Rack build that
    disagrees with its own test coverage.

    Each extraction below is asserted to find exactly one match per file --
    zero matches (the extraction quietly finding nothing) is treated as a
    failure, not a pass: a scraper that matches nothing must not report
    success.

    Deliberately NOT a call to consolidate the two copies into a shared
    helper -- the codebase mirrors this logic on purpose (see the GRIT
    dead-zone, LVL/COMP and COUPLE guards above, same pattern). The bench
    copy deliberately does NOT call inst.settle(): apply_init_patch() applies
    a snapshot once, so there is no "edge" to detect (see
    bench/audition/init_patch.cpp's comment at the DRIFT block).
    """
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    def const_value(source, name, label):
        matches = re.findall(name + r"\s*=\s*([\d.]+f?)", source)
        check(len(matches) == 1,
              f"{label}: expected exactly one {name} declaration, "
              f"found {len(matches)} ({matches!r})")
        return matches[0].rstrip("f") if len(matches) == 1 else None

    host_v = const_value(host_cpp, "kDriftSettleZone", "Fireflow.cpp")
    bench_v = const_value(bench_cpp, "kDriftSettleZone",
                           "bench/audition/init_patch.cpp")
    if host_v is not None and bench_v is not None:
        check(float(host_v) == float(bench_v),
              f"kDriftSettleZone disagrees: Fireflow.cpp={host_v} "
              f"bench/audition/init_patch.cpp={bench_v}")

    needles = {
        "zone-mapped formula": compact_cpp(
            "(driftKnob-kDriftSettleZone)/(1.f-kDriftSettleZone)"),
    }
    host_n, bench_n = compact_cpp(host_cpp), compact_cpp(bench_cpp)
    for label, needle in needles.items():
        check(host_n.count(needle) == 1,
              f"Fireflow.cpp: expected exactly one {label}, found "
              f"{host_n.count(needle)} matching {needle!r}")
        check(bench_n.count(needle) == 1,
              f"bench/audition/init_patch.cpp: expected exactly one "
              f"{label}, found {bench_n.count(needle)} matching {needle!r}")


def test_fixed_values_and_dead_controls():
    """PUSH, WOBL and SMEAR become constants; SPOT dies; DRIVE_A/B was never
    wired to anything and leaves with its menu entry."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    for dead in ("MASTER_DRIVE", "SPOT", "REV_MOD", "REV_SMEAR",
                 "DRIVE_A", "DRIVE_B"):
        check(dead not in names, f"{dead} still exists")
    check(gp.HIDDEN_PARAMS == [], "HIDDEN_PARAMS is not empty")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("set_master_drive(0.40f)" in cpp, "PUSH is not pinned to 0.40")
    check("DriveQuantity" not in cpp, "the dead Drive quantity is still here")
    check("inst.spot()" not in cpp, "SPOT is still wired in the host")


def test_fixed_values_agree_across_host_and_bench():
    """PUSH/SMEAR/WOBL lost their knobs (spec 2026-08-09 hw-control-reduction
    task 9, "push steht immer auf 0.4", "smear ... 0.3 sowas", "wobbel fest
    auf .1 - .2") and became literal constants at the two call sites that used
    to read a param: Fireflow.cpp (what Rack actually runs) and
    bench/audition/init_patch.cpp (the only copy a doctest can reach --
    Fireflow.cpp lives inside a Rack Module, unreachable from the engine test
    suite). This scrapes both files' source text and requires the three
    literals to match exactly, so a hand-edit to only one copy fails loudly
    here instead of shipping a Rack build that disagrees with its own test
    coverage.

    Each extraction below is asserted to find exactly one match per file --
    zero matches (the extraction quietly finding nothing) is treated as a
    failure, not a pass: a scraper that matches nothing must not report
    success.

    Deliberately NOT a call to consolidate the two copies into a shared
    helper -- the codebase mirrors this logic on purpose (see the GRIT
    dead-zone, LVL/COMP, COUPLE and DRIFT guards above, same pattern).
    """
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Fireflow.cpp"),
              encoding="utf-8") as f:
        host_cpp = f.read()
    bench_path = os.path.join(here, "..", "..", "..", "bench", "audition",
                               "init_patch.cpp")
    with open(bench_path, encoding="utf-8") as f:
        bench_cpp = f.read()

    def call_value(source, fn, label):
        matches = re.findall(re.escape(fn) + r"\(([\d.]+)f?\)", source)
        check(len(matches) == 1,
              f"{label}: expected exactly one {fn}(...) call with a literal "
              f"argument, found {len(matches)} ({matches!r})")
        return matches[0] if len(matches) == 1 else None

    for fn, want in (("set_master_drive", 0.40),
                      ("set_reverb_smear", 0.30),
                      ("set_reverb_mod", 0.15)):
        host_v = call_value(host_cpp, fn, "Fireflow.cpp")
        bench_v = call_value(bench_cpp, fn, "bench/audition/init_patch.cpp")
        if host_v is not None and bench_v is not None:
            check(float(host_v) == float(bench_v),
                  f"{fn} disagrees: Fireflow.cpp={host_v} "
                  f"bench/audition/init_patch.cpp={bench_v}")
            check(float(host_v) == want,
                  f"{fn} is {host_v}, want {want} (spec 2026-08-09 "
                  f"hw-control-reduction task 9)")


def test_committed_files_match_the_generator():
    """Every assertion above runs g.svg()/g.header() IN MEMORY -- none of
    them would notice gen_panel.py being edited without being re-run, which
    would leave the plugin compiling against a stale header while every
    guard here still passes (review finding IMPORTANT 5). Mirror
    test_flow_panel.py's test_committed_files_match_the_generator: compare
    the committed artifacts byte-for-byte against a fresh generator run.
    init_patch.hpp already gets an equivalent (if differently-shaped) check
    in test_init_defaults_are_generated_from_names above, so it is included
    here too for the same reason, not left out."""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    for path, produced in (
            (os.path.join(here, "Fireflow.svg"), g.svg()),
            (os.path.join(root, "src", "generated_panel.hpp"), g.header()),
            (os.path.join(root, "src", "init_patch.hpp"), g.init_patch_header())):
        if not os.path.exists(path):
            FAILS.append("%s is missing -- run res/gen_panel.py" % path)
            continue
        with open(path) as f:
            on_disk = f.read()
        check(on_disk == produced,
              "%s differs from the generator's output -- it was hand-edited, "
              "or the generator was changed without re-running it" % path)


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- panel guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
