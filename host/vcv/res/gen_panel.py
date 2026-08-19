#!/usr/bin/env python3
"""Single source of truth for the Fireflow VCV panel.

Layout (2026-07-18 redesign): two symmetric halves, each built around a 32-LED
ring with nine macro knobs orbiting it in three meaning-sorted sectors (MOTION /
TIMBRE / PITCH) and its secondary functions in three fieldset boxes below
(VOICE | FX, PLAY). A shared centre column of four boxes (BLEND / TIME / DUO /
ROOM) sits between them, and the ten jacks form five labelled groups along the
bottom edge. Identity is loosely inherited from the hardware -- the ring plus
macro orbit and the mirrored A/B split -- but Fireflow is its own instrument
and no longer reducible to the real panel.

Visual identity comes from the residency devlog ("workbench paper"): a warm
paper plate with ink lettering and one accent per side -- solder green for
part A (left), copper orange for part B (right). The shared center stays
neutral; MORPH, the knob that bridges the two parts, wears a split
green/copper collar.

Emits (both committed):
  - res/Fireflow.svg          the faceplate
  - src/generated_panel.hpp   enums + PART_STRIDE + control/text tables

Run from host/vcv/:  python3 res/gen_panel.py
The C++ never hardcodes a coordinate, label or colour; it reads them from the
generated header, so panel graphics and widget placement can never drift apart.
"""
import os, math

HP = 42
MM_PER_HP = 5.08
W  = HP * MM_PER_HP          # 213.36 mm
Hh = 128.5                   # standard Eurorack height

# --- devlog palette (website/styles.css) ---------------------------------------
PAPER      = "#f7f4ec"   # plate
PAPER_HI   = "#faf8f2"   # plate gradient top
PAPER_LO   = "#f0ebdd"   # plate gradient bottom
PAPER_DEEP = "#ede5d6"   # cards / pad backplates
FX_FLUX    = "#dfe5dc"   # connected FLUX/TAPE fields
FX_GRIT    = "#e6ddd1"   # connected GRIT/dynamics fields
FX_ROOM    = "#e8e0d4"   # per-deck ROOM fields
LINE       = "#d7cdbb"   # hairlines
INK        = "#171713"   # lettering
MUTED      = "#656056"   # eyebrows / neutral collars
BRICK      = "#8f4a45"   # "I refused you" -- a muted, greyish red. Same value
                         # range as COPPER (L ~42 % against ~46 %) so it does
                         # not shout louder than the accent it sits beside, but
                         # a third of its saturation and 20 degrees round the
                         # wheel, which is what makes it read as dull brick
                         # rather than as warm metal. Sits with MUTED, not
                         # above COPPER. Its only consumer, the deleted Glow
                         # module's refusal contour, went with that module on
                         # 2026-08-14; the value is kept as the palette's
                         # "refused" slot for whatever needs one next.
GRAPHITE   = "#252721"   # knob caps, jack wells
WELL       = "#1d1f1a"   # LED-ring well (dark, so the glow reads)
WHITE      = "#fffdf7"   # pad keys / knob ticks
GREEN      = "#1d6f5f"   # part A accent (solder green)
COPPER     = "#b96532"   # part B accent (copper orange)
GREEN_DIM  = "#2e6355"   # A ring track dots / letter (on the dark well)
COPPER_DIM = "#8a5230"   # B ring track dots / letter
GLOW       = (0x3FBF9C, 0xE8945A)   # runtime LED glow per part (A, B)

# --- Quiet Technical surface constants ---------------------------------------
GROUP_STROKE = 0.30
GROUP_FILL_OPACITY = 0.45
SECTOR_R_IN = 20.50
SECTOR_R_OUT = 31.00
SECTOR_OPACITY = 0.045
PLAY_FIELD_OPACITY = 0.25

# --- control kinds ------------------------------------------------------------
BIGKNOB = "BIGKNOB"   # macro pot (0..1)
KNOBC   = "KNOBC"     # bipolar macro (-1..1)  (MELODY)
SMKNOB  = "SMKNOB"    # small secondary pot (0..1)
KNOBI   = "KNOBI"     # small integer pot (snap)
SW2     = "SW2"       # 2-pos switch kind; unused since SYNC (its only user,
                      # spec 2026-08-09 hw-control-reduction task 7) folded
                      # into COUPLE -- kept for a future 2-pos control
LATCH   = "LATCH"     # on/off pad button (binary)
SMBTN   = "SMBTN"     # momentary pad button
IN      = "IN"
OUT     = "OUT"
LIGHT   = "LIGHT"

GLYPH_R = {BIGKNOB:4.2, KNOBC:4.2, SMKNOB:3.0, KNOBI:3.0, SW2:3.0,
           LATCH:2.7, SMBTN:2.7, IN:4.2, OUT:4.2, LIGHT:1.7}
WKMAP = {BIGKNOB:"WK_BIGKNOB", KNOBC:"WK_KNOBC", SMKNOB:"WK_SMKNOB",
         KNOBI:"WK_KNOBI", SW2:"WK_SW2", LATCH:"WK_LATCH", SMBTN:"WK_SMBTN",
         IN:"WK_IN", OUT:"WK_OUT", LIGHT:"WK_LIGHT"}

# --- label placement ----------------------------------------------------------
# Baseline offset below the glyph centre, per kind (spec 2026-07-18 §8). The
# C++ side no longer knows these numbers -- it reads the resolved position out
# of the generated table.
LBL_DY = {BIGKNOB: 7.2, KNOBC: 7.2, SMKNOB: 5.6, KNOBI: 5.6, SW2: 6.6,
          LATCH: 5.4, SMBTN: 5.4, IN: 6.4, OUT: 6.4, LIGHT: 0.0}

def label_of(c):
    """(x, y, anchor, size, colour) for a control's caption."""
    if c.lbl is not None:
        return c.lbl
    return (c.x, c.y + LBL_DY[c.kind], "middle", 1.9, INK)

class Ctl:
    def __init__(self, enum, kind, x, y, label, tip=None):
        self.enum, self.kind, self.x, self.y, self.label = enum, kind, x, y, label
        self.r = GLYPH_R[kind]
        # None -> default placement (centred below the glyph); otherwise an
        # explicit (x, y, anchor, size, colour) tuple. Radial orbit captions
        # and white-on-well jack labels set this.
        self.lbl = None
        self.tip = label if tip is None else tip

# geometry of a side ring
RING_CY   = 34.5
RING_R    = 16.0       # LED dot radius
KNOB_R    = 25.5       # macro-knob orbit radius
RING_CX_A = 39.5       # left ring center; B is mirrored (W - x)

# Sector orbit (spec 2026-07-18 §1): 9 positions at 40 deg pitch, sorted by
# meaning -- MOTION, then TIMBRE, then PITCH. 0 deg = top, clockwise on part A;
# part B mirrors x, which flips the sweep direction on screen.
ORBIT_ANG = {"RATE": 0.0, "DENSITY": 40.0, "SMOOTH": 80.0, "SHAPE": 120.0,
             "MOD": 160.0, "RANGE": 200.0, "MELODY": 240.0, "TUNE": 280.0,
             "COLOR": 320.0}

# (caption, start angle, end angle, part-A caption position)
SECTORS = [("MOTION", -16.0,  96.0, (70.0,  8.2)),
           ("TIMBRE", 112.0, 176.0, (70.0, 67.0)),
           ("PITCH",  192.0, 336.0, ( 9.0,  8.2))]

# --- fieldset groups (spec 2026-07-18 §4) ------------------------------------
# One shared style: paper-deep panel, hairline stroke, and a legend riding the
# top border on a small paper chip. (x, y, w, h, legend, legend colour)
def part_groups(mir):
    def fx(x, w): return (W - x - w) if mir else x
    return [(fx(4.0, 31.5),  72.4, 31.5, 24.5, "VOICE", MUTED),
            (fx(38.0, 44.0), 72.4, 44.0, 24.5, "FX",    MUTED),
            (fx(4.0, 78.0),  98.6, 78.0, 12.6, "PLAY",  MUTED)]

def part_fx_fields(mir):
    def mx(x, w):
        return W - x - w if mir else x
    return [
        (mir, "FLUX_TOP",    mx(39.0, 31.0), 73.6, 31.0, 10.0, FX_FLUX),
        (mir, "ROOM",        mx(70.5, 10.5), 73.6, 10.5, 10.0, FX_ROOM),
        (mir, "FLUX_BOTTOM", mx(39.0, 21.0), 84.4, 21.0, 11.3, FX_FLUX),
        (mir, "GRIT",        mx(60.5, 20.5), 84.4, 20.5, 11.3, FX_GRIT),
    ]

FX_FIELDS = part_fx_fields(False) + part_fx_fields(True)

def part_play_field(mir):
    x, y, w, h = 5.0, 99.6, 29.0, 10.6
    return (mir, W - x - w if mir else x, y, w, h)

PLAY_FIELDS = [part_play_field(False), part_play_field(True)]

def group_box(x, y, w, h, legend):
    """Box + the paper chip that breaks the top border for the legend. The
    legend TEXT itself goes through TEXTS, so Rack draws it too (NanoSVG
    ignores <text>)."""
    cw = 1.35 * len(legend) + 2.5
    return "\n".join([
        f'<rect x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" height="{mm(h)}" rx="1.5" '
        f'fill="{PAPER_DEEP}" fill-opacity="{GROUP_FILL_OPACITY:.2f}" '
        f'stroke="{LINE}" stroke-width="{GROUP_STROKE:.2f}"/>',
        f'<rect x="{mm(x + 5.0 - cw / 2)}" y="{mm(y - 1.3)}" width="{mm(cw)}" '
        f'height="2.6" fill="{PAPER}"/>'])

def legend_texts():
    return [(x + 5.0, y + 0.75, 1.8, 0.35, colour, "middle", name)
            for (x, y, w, h, name, colour) in GROUPS]

def orbit(cx, cy, r, ang_deg, mir=False):
    a = math.radians(ang_deg)
    s = math.sin(a)
    return (cx + (-s if mir else s) * r, cy - r * math.cos(a))

def orbit_label(cx, cy, ang_deg, mir):
    """Caption radially OUTSIDE the knob, so nothing ever lands between the
    knob and the LED ring (spec 2026-07-18 §2)."""
    a = math.radians(ang_deg)
    s, c = math.sin(a), math.cos(a)
    r = 31.3 if c < -0.38 else (31.5 if (abs(s) < 0.38 and c > 0.38) else 31.7)
    dy = 2.2 if c < -0.38 else (0.0 if c > 0.38 else 0.7)
    anchor = "start" if s > 0.38 else ("end" if s < -0.38 else "middle")
    if mir:
        s = -s
        anchor = {"start": "end", "end": "start", "middle": "middle"}[anchor]
    return (cx + r * s, cy - r * c + dy, anchor, 1.9, INK)

# --- lower half per part (spec 2026-07-18 §5) --------------------------------
# VOICE and FX sit side by side, PLAY spans the full part width below them.
VOICE_X  = [9.25, 19.75, 30.25]      # ATK FILT SUB / DEC RES TIMB
ROW_V1, ROW_V2 = 77.3, 89.4
# --- state-dependent captions (spec 2026-08-03) -------------------------------
# (target param base, driver param base, words indexed by the driver's value)
#
# A control whose meaning changes with state carries its words here instead of
# a second word printed permanently beside it. The DRIVER column is what lets
# the GRIT mode pad share this table with the engine captions: ENGINE resolves
# per deck (the _A target reads ENGINE_A), while a self-driving row reads its
# own value.
#
# Engine order is the frozen ENG order: 0 Synth, 1 Sampler, 2 Wave, 3 Body,
# 4 BBD, 5 Feed. Word[0] is also the control's resting label on the static
# plate -- test_static_label_is_the_tables_first_word holds the two together.
#
# Sources, so a later reader can check each word against the engine rather
# than against taste: BodyVoice::set_env_times is "exciter length, damping";
# set_resonance is "exciter character"; set_sub_level is "excitation bus
# level"; set_cutoff_hz is "brightness". BbdEngine::set_decay is "a trim BELOW
# k0"; set_resonance is "the feedback-path tilt"; set_sub is "the input
# level"; set_filt is "the loss-pole corner". MELODY is set_variation, the
# bipolar RENEW <- LOOP -> GROW axis -- never a melody control.
#
# The BBD's SUB word was FEED until 2026-08-04, and the rename is not taste:
# this same panel prints FB one fieldset over (FLUXFB, the tape echo's
# feedback), and the BBD's own strongest control is the MOTION lane read as
# FEEDBACK. A four-letter FEED between those two reads as a third feedback
# control -- the one thing this knob is NOT. INPUT is what set_sub does, and
# five letters is a length EXCIT already carries in this exact slot.
#
# FEED's words (spec 2026-08-18 feed-coupled-feedback-fm), each naming the
# setter it stands for so the next reader can check the word against the engine
# rather than against taste. RISE and FALL are the deck's ONE envelope in
# absolute seconds (FeedEngine::set_attack/set_decay), and FALL's top quarter
# is also FLOOR -- one knob, two meanings, which is what frees the RES slot.
# RATIO is set_resonance, the modulator:carrier ratio, magnet-locked to the
# integers in its lower half. FILT is set_filt, a real low-pass on the deck's
# output. BOND is the LANE_SOURCE target, morphing each modulator's input from
# its own feedback into its neighbour's output. SPRD is the LANE_SIZE base,
# re-pointed from the DETUNE knob in Fireflow.cpp -- the sampler's SUB ->
# LANE_SIZE re-point, one entry further down the same ledger.
#
# FEED's FILT word was BRITE until 2026-08-19, and the rename tracks what the
# control became rather than taste. It printed BRITE because set_filt drove a
# one-pole INSIDE the feedback path and DAMP -- the word that wanted -- was
# already BODY's DECAY word. Bastian reported that knob as not working, and
# feed_config.h now carries the measured reason: the in-loop one-pole never
# touched the carrier, filtered a phase wobble of at most 0.08 cycles, and
# saturated over the upper third of its travel. It is fixed now, and FILT is a
# real SvfLp on the output -- the same filter Synth, WAVE and the sampler run,
# on the same 60 Hz..14 kHz rails, with the same fade to silence at the left
# end. So the honest word is the one those three already print, and printing it
# here is legal because "no word is printed twice" is a guard against ONE word
# meaning two controls: FILT at the FILT base in a fourth engine state is the
# same meaning said again, which is what that guard permits.
#
# DETUNE is a NEW row here. It was a fixed DTUN plate until this spec, and a
# fixed plate would now lie: on a FEED deck that knob is SPREAD.
DYNAMIC_CAPTIONS = [
    ("MELODY",   "ENGINE",   ("VARY", "SCAN", "VARY", "VARY", "VARY", "VARY")),
    ("ATTACK",   "ENGINE",   ("ATK",  "ATK",  "ATK",   "HIT",   "ATK",   "RISE")),
    ("DECAY",    "ENGINE",   ("DEC",  "DEC",  "DEC",   "DAMP",  "TAIL",  "FALL")),
    ("RES",      "ENGINE",   ("RES",  "RES",  "RES",   "CHAR",  "TILT",  "RATIO")),
    ("SUB",      "ENGINE",   ("SUB",  "LEN",  "SUB",   "EXCIT", "INPUT", "SUB")),
    ("FILT",     "ENGINE",   ("FILT", "FILT", "FILT",  "BRITE", "LOSS",  "FILT")),
    ("SOURCE",   "ENGINE",   ("TIMB", "ORG",  "FRAME", "MATL",  "DRIVE", "BOND")),
    ("DETUNE",   "ENGINE",   ("DTUN", "DTUN", "DTUN",  "DTUN",  "DTUN",  "SPRD")),
]


def dynamic_words(base):
    """The state-dependent words for a control base, () when it has none."""
    for target, _driver, words in DYNAMIC_CAPTIONS:
        if target == base:
            return words
    return ()


# 4-wide, aligned to FX_BOT so the FX box's two rows flush: TIME MIX FB SEND.
FX_TOP   = [44.25, 54.75, 65.25, 75.75]   # TIME MIX FB | SEND (per-deck reverb mix)
# FX bottom row went from two slots to four (spec 2026-07-18 dust-grain-cloud);
# the left two were renamed in place when FLUX became a BBD (spec 2026-07-27):
# DUST/ROT -> DRIVE/STAGES. The first slot was renamed in place again (spec
# 2026-07-28 flux-rhythm-drag): DRIVE -> DRAG, DRIVE moving to the menu. And
# again (spec 2026-07-28 flux-link): DRAG -> LINK, because the control became
# bipolar and LINK names the axis rather than one of its two ends.
# The BBD-only BEND widget overlaps ATK at runtime. FX_BOT[1] held MULT until
# task 6 (spec 2026-08-09 hw-control-reduction) retired it; the slot stays
# empty (freed slots are not regrouped), so the static Synth preview now
# reads LINK . | GRIT COMP.
FX_BOT   = [44.25, 54.75, 65.25, 75.75]   # LINK . | GRIT COMP (FX_BOT[1] empty)
PLAY_Y   = 103.6
# The PLAY row's left block re-spaced to seat REC between GRIT and STEPS
# (spec 2026-07-18 "VCV layer": REC is the only new panel element). All four
# left-block glyphs are LATCH r=2.7 except STEPS and SONG (KNOBI r=3.0); the pitches
# below clear test_no_overlap's radius-sum minimum with >=1.8 mm to spare.
PAD_X    = [10.0, 17.5, 46.0, 56.5, 67.0, 77.5]   # ENG GRIT | (STEP, FORM, NEW retired) SONG
STEPS_X  = 37.0                     # sequencer knob, between the two pad blocks
REC_X    = 25.0                     # REC pad (appended param, not templated)
# Its state LED, centred in the gap between the REC pad and the STEPS knob.
# It used to sit at 30.0, hard against a hairline at 28.7 that separated the
# mode pads from the sequencer block; with that hairline gone (2026-07-22,
# Bastian: the LED and the rule crowded each other and read as one smudged
# element) the LED has the whole gap and is centred in it.
REC_LED_X = (REC_X + STEPS_X) / 2.0

# --- per-part control template (ORDER defines enum order; identical A/B) ------
# Returns Ctl list with per-part coordinates (mirrored for B when mir=True).
def part_controls(mir=False):
    cx = W - RING_CX_A if mir else RING_CX_A
    def fx(x): return W - x if mir else x
    out = []
    # 8 of the 9 orbit knobs (COLOR is appended at the end of PARAMS, see below)
    macros = [("RATE","RATE",None),("SHAPE","SHAPE",None),
              ("DENSITY","DENS",None),("SMOOTH","SMTH",None),
              ("RANGE","RANGE",None),
              ("MELODY", dynamic_words("MELODY")[0], "Variation"),
              ("MOD","MOD",None),("TUNE","TUNE",None)]
    for enum, lbl, tip in macros:
        ang = ORBIT_ANG[enum]
        x, y = orbit(cx, RING_CY, KNOB_R, ang, mir)
        c = Ctl(enum, KNOBC if enum == "MELODY" else BIGKNOB, x, y, lbl, tip)
        c.lbl = orbit_label(cx, RING_CY, ang, mir)
        out.append(c)
    # voice row (small): ATK FILT SUB | DEC RES TIMB. FILT fills slot 2 of the top
    # row but is appended at the END of PARAMS (see below), never here -- that
    # would grow PART_STRIDE and shift every part-B/SHARED param id.
    for (enum, lbl, x, y) in [("ATTACK", "ATK",  VOICE_X[0], ROW_V1),
                              ("DECAY",  "DEC",  VOICE_X[0], ROW_V2),
                              ("RES",    "RES",  VOICE_X[1], ROW_V2),
                              ("SUB",    "SUB",  VOICE_X[2], ROW_V1),
                              ("SOURCE", dynamic_words("SOURCE")[0], VOICE_X[2], ROW_V2)]:
        out.append(Ctl(enum, SMKNOB, fx(x), y, lbl,
                       "SOURCE" if enum == "SOURCE" else None))
    # fx box: the FLUX delay cluster (DIV . MIX . FB) on top, GRIT/COMP below.
    # FLUX (the delay MIX) is the template member; DIV/FB are appended at the
    # end of PARAMS. STEPS keeps its append slot here but has moved to the PLAY
    # box -- it is a sequencer parameter, not an effect (spec 2026-07-18 §5).
    out.append(Ctl("FLUX", SMKNOB, fx(FX_TOP[1]), ROW_V1, "MIX", "FLUX"))
    # GRIT is bipolar now (spec 2026-08-09 hw-control-reduction task 4): the
    # GRITMODE pad is gone, and GRIT's own sign picks Drive/Reduce while its
    # magnitude is the mix (see Fireflow.cpp pushParams). COMP stays a plain
    # unipolar SMKNOB, so the two can no longer share one templated loop.
    out.append(Ctl("GRIT", KNOBC, fx(FX_BOT[2]), ROW_V2, "GRIT"))
    # COMP prints LVL now (spec 2026-08-09 hw-control-reduction task 5): it was
    # always used as a volume control in practice (the engines are quiet, so
    # it sat at 0.5-0.7 in every patch, and what read as "louder" was the
    # compressor's make-up gain). The lower four fifths are now pure output
    # level; the compressor lives in the top fifth with make-up (see
    # Fireflow.cpp pushParams' kLvlCompSplit/kCompTop).
    out.append(Ctl("COMP", SMKNOB, fx(FX_BOT[3]), ROW_V2, "LVL", "Level / Comp"))
    out.append(Ctl("STEPS", KNOBI, fx(STEPS_X), PLAY_Y, "STPS"))
    # ENGINE cycles Synth/Sampler/Wave/Body/BBD (states 0..4); the C++ side
    # (Fireflow.cpp configSwitch/EngineCycleLatch) is the source of truth for
    # the labels, this comment just keeps the panel legend discoverable here.
    # The GRITMODE pad is gone (spec 2026-08-09 hw-control-reduction task 4):
    # GRIT above is bipolar now and its own sign picks Drive/Reduce, so
    # PAD_X[1] -- its old slot -- stays empty rather than being reclaimed.
    pads = [("ENGINE", LATCH, "ENG", None)]
    for i, (enum, kind, lbl, tip) in enumerate(pads):
        out.append(Ctl(enum, kind, fx(PAD_X[i]), PLAY_Y, lbl, tip))
    # DETUNE returns to the panel from the context menu (spec 2026-08-09
    # hw-control-reduction task 10): a real performance control now, not
    # patch-only state -- "bei drones ist das sehr stark". It fills PAD_X[2],
    # the STEP pad's slot freed in task 2, the only position this plan permits
    # filling with something new. A plain SMKNOB, like every other secondary
    # knob; the quadratic taper (kept the first ~20 ct usable) lives in
    # pushParams, not here -- see Fireflow.cpp/bench/audition/init_patch.cpp.
    out.append(Ctl("DETUNE", SMKNOB, fx(PAD_X[2]), PLAY_Y, "DTUN", "Detune"))
    # FORM and NEWPHRASE are gone (spec 2026-08-09 hw-control-reduction task 3):
    # SONG alone now walks a curated 14-rung (Principle, SongMode) ladder and
    # re-rolls the phrase on every rung change. PAD_X[3] and PAD_X[5], its old
    # neighbours, stay empty -- SONG keeps its own PAD_X[4] slot.
    out.append(Ctl("SONG", KNOBI, fx(PAD_X[4]), PLAY_Y, "SONG"))
    return out

def part(suffix, mir):
    """Same call, same order, mirrored x -- so PART_STRIDE and every param id
    stay put no matter how the coordinates move."""
    out = part_controls(mir)
    for c in out:
        c.enum += suffix
    return out

PART_A = part("_A", False)
PART_B = part("_B", True)
PART_STRIDE = len(PART_A)

# --- shared center strip ------------------------------------------------------
CX = W / 2.0

# --- inputs / outputs / lights ------------------------------------------------
# The ten jacks split into five labelled fieldset groups with real gaps, signal
# flow reading left -> right (spec 2026-07-18 §7). Output groups sit on a dark
# well with white lettering, inputs stay on paper with ink -- so in/out reads at
# a glance and the duplicated PIT/GATE labels are disambiguated by the legend.
# NOTE: this block sits here (above GROUPS, not down by LIGHTS) because
# JACK_GROUPS feeds GROUPS below and must be defined before it runs; it only
# needs CX/W, the colour constants and the JACK_* constants, all already in scope.
JACK_Y     = 118.4
JACK_BOX_Y = 112.6
JACK_BOX_W, JACK_BOX_H = 23.0, 14.4
JACK_DX    = 5.75            # jack offset from the box's left edge; pitch 11.5
JACK_LBL_SIZE = 1.8          # jack caption size; every other kind uses 1.9

# (box x, legend, legend colour, dark well?, [(enum, panel label, tooltip)])
JACK_GROUPS = [
    (7.2,        "CV A",  GREEN,  True,  [("PITCH_A", "PIT",  "Pitch A"),
                                          ("GATE_A",  "GATE", "Gate A")]),
    (49.5,       "IN",    MUTED,  False, [("IN_L", "L", "IN L"),
                                          ("IN_R", "R", "IN R")]),
    (CX - 11.5,  "CLOCK", MUTED,  False, [("CLOCK", "CLK", "Clock"),
                                          ("RESET", "RST", "Reset")]),
    (W - 72.5,   "OUT",   MUTED,  True,  [("OUT_L", "L", "OUT L"),
                                          ("OUT_R", "R", "OUT R")]),
    (W - 30.2,   "CV B",  COPPER, True,  [("GATE_B",  "GATE", "Gate B"),
                                          ("PITCH_B", "PIT",  "Pitch B")]),
]

def jack(enum, kind, x, label, tip, white):
    c = Ctl(enum, kind, x, JACK_Y, label)
    c.tip = tip
    c.lbl = (x, JACK_Y + LBL_DY[kind], "middle", JACK_LBL_SIZE, WHITE if white else INK)
    return c

def jack_at(enum):
    """Look a jack up in JACK_GROUPS and build it. Keeps the INPUTS/OUTPUTS
    enum ORDER free of the visual left-to-right order -- ids stay put."""
    for (bx, _lg, _col, well, items) in JACK_GROUPS:
        for i, (e, label, tip) in enumerate(items):
            if e == enum:
                kind = IN if enum in ("IN_L", "IN_R", "CLOCK", "RESET") else OUT
                return jack(enum, kind, bx + JACK_DX + i * 11.5, label, tip, well)
    raise KeyError(enum)

INPUTS = [jack_at(e) for e in ("IN_L", "IN_R", "CLOCK", "RESET")]
OUTPUTS = [jack_at(e) for e in ("OUT_L", "OUT_R", "PITCH_A", "GATE_A",
                                "PITCH_B", "GATE_B")]

# FireflowHW only. IDs live in the shared InputId enum so Rack can host the
# ports; they are not drawn on the 42 HP panel and process() does not read them.
HW_MOD_INPUTS = [
    Ctl("MOD1_A", IN, 0, 0, "MOD1", "Mod 1 (unwired)"),
    Ctl("MOD2_A", IN, 0, 0, "MOD2", "Mod 2 (unwired)"),
    Ctl("MOD3_A", IN, 0, 0, "MOD3", "Mod 3 (unwired)"),
    Ctl("MOD4_A", IN, 0, 0, "MOD4", "Mod 4 (unwired)"),
    Ctl("MOD1_B", IN, 0, 0, "MOD1", "Mod 1 (unwired)"),
    Ctl("MOD2_B", IN, 0, 0, "MOD2", "Mod 2 (unwired)"),
    Ctl("MOD3_B", IN, 0, 0, "MOD3", "Mod 3 (unwired)"),
    Ctl("MOD4_B", IN, 0, 0, "MOD4", "Mod 4 (unwired)"),
]

# The centre's outer background card is gone (spec 2026-07-18 §6) -- the four
# fieldset boxes carry the grouping alone and grew to 41 mm, so the columns
# use +/-10.5 outer columns.
L, R = CX - 10.5, CX + 10.5
ROW_BLEND = 21.5
ROW_TIME1, ROW_TIME2 = 42.0, 54.0
ROW_DUO1, ROW_DUO2 = 68.0, 78.0
ROW_ROOM1, ROW_ROOM2 = 94.0, 104.5
# The four free-standing centre boxes (spec 2026-07-18 §6); GROUPS is assigned
# here, not alongside part_groups() above, because these entries need CX.
GROUPS = part_groups(False) + part_groups(True) + [
    (CX - 20.5, 13.0, 41.0, 19.5, "BLEND",  MUTED),
    # Renamed from "TIME" (spec 2026-08-09 hw-control-reduction task 6):
    # FLUXRATE_A/B's knob took the word TIME on the FX box, and
    # test_every_printed_word_is_unique means the plate can't say the same
    # word in two different places for two different things. This box is
    # still the sync/tempo/couple/shuffle clock story -- TIMING keeps the
    # sense of the old legend without colliding with the FLUX knob.
    (CX - 20.5, 35.0, 41.0, 25.0, "TIMING", MUTED),
    (CX - 20.5, 62.5, 41.0, 22.5, "DUO",    MUTED),
    (CX - 20.5, 87.5, 41.0, 23.7, "ROOM",   MUTED),
] + [(bx, JACK_BOX_Y, JACK_BOX_W, JACK_BOX_H, lg, col)
     for (bx, lg, col, _well, _items) in JACK_GROUPS]
SHARED = [
    Ctl("MORPH",  BIGKNOB, CX - 7.0, ROW_BLEND, "MORPH"),
    # TIMING: a 2x2 clock story -- pace/tempo above, couple/shuffle below
    # (box legend renamed from TIME, spec 2026-08-09 hw-control-reduction
    # task 6 -- see GROUPS above; SYNC no longer has a row here, it folded
    # into COUPLE below). SHUFFLE's control is appended to PARAMS after
    # every id that existed before it; PACE (spec 2026-08-12 modulation-pace)
    # is appended after THAT, in its own APPENDED_PANEL_PARAMS list below --
    # so PACE, not SHUFFLE, is PARAMS' trailing member.
    Ctl("TEMPO",  SMKNOB,  CX + 9.0, ROW_TIME1, "TEMPO"),
    # COUPLE swallowed the SYNC switch (spec 2026-08-09 hw-control-reduction
    # task 7): SYNC was the right-hand end of COUPLE's own axis. Below the
    # zone split (Fireflow.cpp kCoupleZoneSplit) the knob is the FREE world,
    # above it the GRID world -- each zone sweeps couple across its own full
    # 0..1 range, so "on the grid but breathing" stays reachable. The freed
    # ROW_TIME1 slot beside TEMPO now holds PACE (APPENDED_PANEL_PARAMS below,
    # spec 2026-08-12 modulation-pace).
    Ctl("COUPLE", SMKNOB,  CX - 9.0, ROW_TIME2, "FREE|GRID"),
    Ctl("SCALE",  KNOBI,   L,  ROW_DUO1, "SCALE"),
    Ctl("DRIFT",  SMKNOB,  R,  ROW_DUO1, "DRIFT"),
    # SETTLE/SETL retired (spec 2026-08-09 hw-control-reduction task 8): the
    # pad was drift-to-zero plus a glide, which is just DRIFT's own left
    # stop. SPOT and MASTER_DRIVE (PUSH) retired here (task 9): SPOT is a
    # genuine feature loss (the owner never uses it), PUSH is fixed by ear
    # at 0.40 in Fireflow.cpp's pushParams -- "push steht immer auf 0.4".
    # The entire freed ROW_DUO2 row stays empty; regrouping is a later
    # session.
    # ROOM: three semantic columns, bottom edge flush with the PLAY boxes.
    # SMEAR (task 9, "smear ... 0.3 sowas") and MOD/WOBL ("wobbel fest auf
    # .1 - .2") are fixed by ear too, in the same pushParams block. Their
    # freed R-column slots stay empty; regrouping is a later session.
    Ctl("REV_SIZE",  SMKNOB, L,         ROW_ROOM1, "SIZE"),
    Ctl("REV_DECAY", SMKNOB, L,         ROW_ROOM2, "DECAY"),
    Ctl("REV_TONE",  SMKNOB, CX,        ROW_ROOM1, "TONE"),
    Ctl("REV_DIFF",  SMKNOB, CX,        ROW_ROOM2, "DIFF"),
    # CHOKE: bipolar event-priority between the decks (spec 2026-07-16
    # choke-priority). Appended LAST on purpose: existing .vcv patches keep
    # their param ids.
    Ctl("CHOKE",  SMKNOB, CX, ROW_DUO1, "CHOKE"),
]

def color_ctl(suffix, mir):
    cx = W - RING_CX_A if mir else RING_CX_A
    ang = ORBIT_ANG["COLOR"]
    x, y = orbit(cx, RING_CY, KNOB_R, ang, mir)
    c = Ctl("COLOR" + suffix, BIGKNOB, x, y, "COLOR")
    c.lbl = orbit_label(cx, RING_CY, ang, mir)
    return c

PANEL_PARAMS = PART_A + PART_B + SHARED + [
    # FILT: bipolar cutoff trim (spec 2026-07-17). Appended LAST like CHOKE so
    # existing .vcv patches keep their param ids; coordinates put it in the
    # top voice row's middle slot (after ATK).
    Ctl("FILT_A", SMKNOB, VOICE_X[1],     ROW_V1, dynamic_words("FILT")[0]),
    Ctl("FILT_B", SMKNOB, W - VOICE_X[1], ROW_V1, dynamic_words("FILT")[0]),
    # TIDE: texture-lane rate of both decks (spec 2026-07-17 mod-tide).
    # Appended LAST like CHOKE/FILT so existing .vcv patches keep their ids;
    # the coordinate puts it beside MORPH in the centre's movement column
    # (COUPLE/DRIFT -- SETL retired into DRIFT's own left stop, task 8).
    Ctl("TIDE", SMKNOB, CX + 11.0, ROW_BLEND, "TIDE"),
    # FLUX synced-delay controls (spec 2026-07-17 flux-synced-delay). Per part,
    # appended LAST like FILT/TIDE/CHOKE so existing .vcv patches keep their ids.
    # They complete the FLUX delay cluster atop the FX box: TIME (FX_TOP[0]),
    # MIX (FX_TOP[1], from the template), FB (FX_TOP[2]) sit together;
    # GRIT/COMP fill FX_BOT below. TIME used to be DIV beside a free MULT
    # multiplier (FX_BOT[1]); task 6 (spec 2026-08-09 hw-control-reduction)
    # retired MULT -- one notched knob over the 12 synced divisions is TIME
    # now, and FX_BOT[1] stays empty. FXT_FLUX_TIME, the modulation sink
    # MULT used to feed, survives at a pinned neutral base so CV and the mod
    # lanes can still bend the tape (Fireflow.cpp pushParams).
    Ctl("FLUXRATE_A", KNOBI, FX_TOP[0],     ROW_V1, "TIME", "FLUX time"),
    Ctl("FLUXRATE_B", KNOBI, W - FX_TOP[0], ROW_V1, "TIME", "FLUX time"),
    Ctl("FLUXFB_A",   SMKNOB, FX_TOP[2],     ROW_V1, "FB", "FFB"),
    Ctl("FLUXFB_B",   SMKNOB, W - FX_TOP[2], ROW_V1, "FB", "FFB"),
    # COLOR: chord density/colour per part (spec 2026-07-17 chord-layer), a full
    # orbit member since the 2026-07-18 redesign -- it is pitch material, so it
    # sits in the PITCH sector. Still appended LAST: order defines the param id.
    color_ctl("_A", False),
    color_ctl("_B", True),
    # LINK / STAGES: LINK takes over the BBD voicing slot that DRIVE, then
    # DRAG, held (spec 2026-07-28 flux-rhythm-drag, then flux-link). Renamed
    # IN PLACE from DRAG -- same positions, same param ids, PART_STRIDE
    # untouched, so every already-saved .vcv keeps every id it has -- exactly
    # the move DUST -> DRIVE made on 2026-07-27 and DRIVE -> DRAG made on
    # 2026-07-28. A patch's old DRAG value lands on the same VALUE in LINK's
    # wider range, not the same position on the knob's travel (a saved 0.5
    # was mid-travel on 0..1 and is now three-quarters travel on -1..1), and
    # that value means the same thing (DRAG was LINK's positive half all
    # along); the spec's no-migration decision accepts that. DRIVE itself
    # moved to the menu as
    # patch state and stays there, appended below in HIDDEN_PARAMS with fresh
    # trailing ids. STAGES is the BBD-only BEND widget, so Rack overlays it
    # on ATTACK while the static Synth preview omits it.
    Ctl("LINK_A",   SMKNOB, FX_BOT[0],     ROW_V2, "LINK"),
    Ctl("LINK_B",   SMKNOB, W - FX_BOT[0], ROW_V2, "LINK"),
    Ctl("STAGES_A", SMKNOB, VOICE_X[0],     ROW_V1, "BEND", "BBD Bend"),
    Ctl("STAGES_B", SMKNOB, W - VOICE_X[0], ROW_V1, "BEND", "BBD Bend"),
    # M5b: REC, the one new panel element of the texture deck. Appended LAST
    # so REC_A/REC_B take fresh trailing ids and PART_STRIDE stays 23 -- every
    # already-saved .vcv keeps every param id it has.
    Ctl("REC_A", LATCH, REC_X,     PLAY_Y, "REC"),
    Ctl("REC_B", LATCH, W - REC_X, PLAY_Y, "REC"),
    # Per-deck reverb mix (spec 2026-07-23 per-deck-reverb-mix). Appended LAST
    # like FILT/FLUXRATE/COLOR/LINK/REC so PART_STRIDE stays 23 and no id before
    # them moves. They fill the FX top row's 4th slot -- TIME.MIX.FB.SEND --
    # aligned to the FX bottom row. Label "SEND" (not "MIX": FLUX beside it is
    # already the delay mix). The old shared centre REV_MIX is removed from
    # SHARED; its id and every id after it shift by one (accepted: old .vcv
    # patches load shifted reverb/CHOKE/tail params).
    Ctl("REV_MIX_A", SMKNOB, FX_TOP[3],     ROW_V1, "SEND", "Room send"),
    Ctl("REV_MIX_B", SMKNOB, W - FX_TOP[3], ROW_V1, "SEND", "Room send"),
    # Shared STEP-lane shuffle. Appended after every existing ParamId so saved
    # patches retain all prior ids; the glyph fills TIME's bottom-right slot.
    Ctl("SHUFFLE", SMKNOB, CX + 9.0, ROW_TIME2, "SHUFL"),
]

# DRIVE_A/B retired here (spec 2026-08-09 hw-control-reduction task 9): the
# menu-only slider never reached the engine (its BBD drive target is a mod
# lane, spky::LANE_SOURCE -- see bbd_engine.cpp's set_targets()), so it was
# append-only saved patch state with no destination at all. DETUNE_A/B left
# this list before it (task 10, back onto the panel), so HIDDEN_PARAMS is now
# genuinely empty -- no more menu-only widgetless patch state survives.
HIDDEN_PARAMS = []

# FLUXTIME_A/B (the MULT knob) retired here (spec 2026-08-09
# hw-control-reduction task 6): DIV and MULT described one quantity, and
# FLUXRATE_A/B -- renamed TIME, now a 12-detent knob -- is that quantity.
# The modulation sink it fed, FXT_FLUX_TIME, survives at a pinned neutral
# base (Fireflow.cpp pushParams) so CV and the mod lanes can still bend the
# tape; only the panel's second way to set it is gone. FX_BOT[1], MULT's old
# slot, stays empty -- freed slots are not regrouped.
APPENDED_PANEL_PARAMS = [
    # PACE: the global modulation time-stretch (spec 2026-08-12). Takes the
    # ROW_TIME1 slot freed when SYNC folded into COUPLE. It belongs in TIMING
    # beside TEMPO, and next to TIDE on the hardware grid -- those two are the
    # pair this control came out of confusing: TIDE is a ratio, PACE is speed.
    Ctl("PACE", SMKNOB, CX - 9.0, ROW_TIME1, "PACE"),
]

# Persistent ids retain the legacy visible and hidden sequences exactly; runtime
# controls may include appended widgets, while the SVG is the Synth-only view
# minus the controls that appear on no engine's default state. STAGES_A/B is
# BBD-only and draws itself at runtime when the BBD is selected; REC_A/B joins
# it for the same reason (spec 2026-08-03 rec-artifacts) -- REC does nothing
# off the Sampler, its runtime widget already hides there (SlotVisible), and
# the static preview should not paint a pad that has no job in its own view.
RUNTIME_PANEL_PARAMS = PANEL_PARAMS + APPENDED_PANEL_PARAMS
STATIC_PANEL_PARAMS = [
    c for c in RUNTIME_PANEL_PARAMS
    if c.enum not in ("STAGES_A", "STAGES_B", "REC_A", "REC_B")
]
PARAMS = PANEL_PARAMS + HIDDEN_PARAMS + APPENDED_PANEL_PARAMS

# Approved init snapshot, keyed by param NAME rather than by position: adding
# or removing a control must not be able to shift somebody else's default.
#
# Provenance: FF_hw_Init.vcvm, a FireflowHW preset Bastian played and approved.
# The 2026-08-09 revision replaced the drone.vcvm (2026-07-28) lineage wholesale
# -- a different instrument at boot, not a retune, which is why no per-value
# note here justifies a number by what it preserved from drone.vcvm any more.
# The 2026-08-10 revision keeps that instrument and moves five values: both
# lane RATEs off their floors, ATTACK_A/DECAY_A to full, and SCALE from
# Mixolydian to Lydian. The preset's module data (sampler paths, excitation
# checkboxes) is NOT carried: only params reach this table, and nothing in that
# blob is audible for these engines anyway (Part::set_excitation is a no-op
# outside BODY, and no deck boots BODY now).
INIT_DEFAULTS = {
    "RATE_A": 0.184337318,
    "SHAPE_A": 0.000000000,
    "DENSITY_A": 0.534939826,
    # SMOOTH_A/B were 0.836144507 / 1.0 -- the top of the knob, where they sat
    # because the absolute-seconds slew law made the knob inert (measured: at
    # most 0.11 dB anywhere in this patch). Converted 2026-08-14 to preserve
    # the shipped sound under the interval-relative law; see spec
    # docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md 2.4.
    # tests/test_smooth_law.cpp's G4' is the gate that these two are right.
    "SMOOTH_A": 0.004974,
    "RANGE_A": 0.000000000,
    "MELODY_A": 0.768674195,
    "MOD_A": 0.403613269,
    "TUNE_A": 0.001204819,
    "ATTACK_A": 1.000000000,
    "DECAY_A": 1.000000000,
    "RES_A": 0.000000000,
    "SUB_A": 0.738666236,
    "SOURCE_A": 0.453333825,
    "FLUX_A": 0.353333473,
    "GRIT_A": 0.173493922,
    # LVL/COMP: both decks now boot INSIDE the compressor zone, above
    # kLvlCompSplit (0.6). That is a change of kind from the previous snapshot,
    # where the value sat exactly on the split and the compressor was
    # disengaged at boot. Deck A lands at amount 0.406 (~7.1 dB of make-up),
    # deck B at 0.525 (~10.9 dB) -- set by ear on the reshaped taper
    # (kCompShape), so the numbers only mean what they mean WITH that taper.
    # If kLvlCompSplit or kCompShape ever move again, these two do not follow
    # mechanically any more: they have to be re-heard.
    "COMP_A": 0.761333168,
    # 0 IS flow mode: the count is the mode (the separate STEP pad was merged
    # into this knob, spec 2026-08-09 hw-control-reduction task 3). Both decks
    # free-running, unchanged from the previous snapshot.
    "STEPS_A": 0.000000000,
    # 2 == Wave. The previous snapshot booted Synth here and BODY on deck B;
    # this patch runs Wave against Synth and boots no BODY deck at all. Two
    # consequences worth knowing: BodyVoice::kDetuneScale no longer touches
    # anything at boot, and the excitation bus is inert (Part::set_excitation
    # is a no-op outside BODY).
    "ENGINE_A": 2.000000000,
    # DETUNE is pushed SQUARED (Fireflow.cpp: set_voice_detune(knob*knob)) and
    # the ceiling is 105 ct, so this is 0.377^2 * 105 == 14.95 ct on deck A and
    # 0.456^2 * 105 == 21.83 ct on deck B. Both decks read the same law now
    # that neither runs BODY -- the per-deck split the previous snapshot needed
    # (SYNTH and BODY only agreeing at full scale) no longer applies.
    "DETUNE_A": 0.377333373,
    # Rung 0 of the ladder (song_ladder.h) == {form 0, song 6}: no alternation,
    # two generators. The previous snapshot sat on rung 6 ({2, 0},
    # Hierarchical/AAAB) because that rung reproduced the even older
    # FORM/SONG pair -- that lineage ends here, this is a fresh choice.
    "SONG_A": 0.000000000,
    "RATE_B": 0.163855359,
    "SHAPE_B": 0.000000000,
    "DENSITY_B": 0.000000000,
    "SMOOTH_B": 0.026026,
    "RANGE_B": 0.000000000,
    "MELODY_B": 0.671083927,
    "MOD_B": 0.681928277,
    "TUNE_B": 0.321686625,
    "ATTACK_B": 1.000000000,
    "DECAY_B": 1.000000000,
    "RES_B": 0.220000312,
    "SUB_B": 0.000000000,
    "SOURCE_B": 0.000000000,
    "FLUX_B": 0.650667071,
    "GRIT_B": 0.000000000,
    "COMP_B": 0.848000109,
    "STEPS_B": 0.000000000,
    "ENGINE_B": 0.000000000,
    "DETUNE_B": 0.455999434,
    # Rung 13 == {form 4, song 5}: Ostinato against Mirror, the top of the
    # ladder. The two decks deliberately sit at opposite ends now.
    "SONG_B": 13.000000000,
    "MORPH": 0.495180398,
    "TEMPO": 0.000000000,
    "COUPLE": 1.000000000,
    # 3 == SCALE_LYDIAN (quantizer.h), the bright end of the modes group.
    # Until this revision the table said 2 (Mixolydian) while the module booted
    # Lydian: Fireflow.cpp's WK_KNOBI branch passed spky::SCALE_LYDIAN instead
    # of the snapshot value, so this entry only reached the bench audition.
    # That branch reads `init` now, and the preset -- saved from the module --
    # agrees with it.
    "SCALE": 3.000000000,
    "DRIFT": 0.791999996,
    "REV_SIZE": 1.000000000,
    "REV_DECAY": 0.800755024,
    "REV_TONE": 0.905333221,
    "REV_DIFF": 0.768000245,
    "CHOKE": 0.000000000,
    "FILT_A": -0.199999928,
    "FILT_B": -0.292000026,
    "TIDE": 0.000000000,
    "FLUXRATE_A": 1.000000000,
    "FLUXRATE_B": 1.000000000,
    "FLUXFB_A": 0.643999279,
    "FLUXFB_B": 0.790665507,
    "COLOR_A": 0.001204819,
    "COLOR_B": 0.862999976,
    "LINK_A": 0.000000000,
    "LINK_B": 0.000000000,
    "STAGES_A": 0.000000000,
    "STAGES_B": 0.000000000,
    "REC_A": 0.000000000,
    "REC_B": 0.000000000,
    "REV_MIX_A": 0.343394309,
    "REV_MIX_B": 0.805333197,
    "SHUFFLE": 0.000000000,
    # PACE (spec 2026-08-12 modulation-pace): 0.5 is exactly x1 -- the factory
    # sound (FF_hw_Init.vcvm) must boot at the same speed it always has, not
    # x1/32 (0.0) or x4 (1.0).
    "PACE": 0.500000000,
}

# --- lights --------------------------------------------------------------------
# INPUTS/OUTPUTS are built above (see JACK_GROUPS, near CX) -- they had to move
# ahead of GROUPS, which folds the jack boxes in. Only the lights are left here.
LIGHTS = [
    # glow at each ring center, driven by that part's gate
    Ctl("GATE_A_L", LIGHT, RING_CX_A,     RING_CY, ""),
    Ctl("GATE_B_L", LIGHT, W - RING_CX_A, RING_CY, ""),
    # Appended after the gate lights: the C++ side centres the LED rings on
    # kLightCtls[0..1], so the gate lights must keep index 0 and 1.
    Ctl("REC_A_L", LIGHT, REC_LED_X,     PLAY_Y, ""),
    Ctl("REC_B_L", LIGHT, W - REC_LED_X, PLAY_Y, ""),
]

# SVG-only view: the static preview omits the two REC LED housings the same
# way STATIC_PANEL_PARAMS omits the REC pad bed above -- REC has no job off
# the Sampler, and the runtime SamplerOnly<...> widget already hides there.
# LIGHTS itself (all four, in this order) is unchanged and still drives
# kLightCtls in the generated header; only the SVG loop reads STATIC_LIGHTS.
STATIC_LIGHTS = [c for c in LIGHTS if c.enum not in ("REC_A_L", "REC_B_L")]

# Lights that exist only on the 60 HP hardware panel. Their IDs must be in
# the shared LightId enum -- both widgets share one Fireflow module class --
# but they must NOT enter kLightCtls, which FireflowWidget loops to build
# widgets: appended there they would be drawn on the large panel at hardware
# coordinates. Exactly the split HW_MOD_INPUTS already uses. Coordinates are
# 0/0 here and come from gen_hw_panel's LIGHT_POS, same as those jacks.
HW_ONLY_LIGHTS = [
    Ctl("SRC_A_L",     LIGHT, 0, 0, ""),   # LANE_SOURCE excursion
    Ctl("SRC_B_L",     LIGHT, 0, 0, ""),
    Ctl("FLT_A_L",     LIGHT, 0, 0, ""),   # LANE_SIZE excursion
    Ctl("FLT_B_L",     LIGHT, 0, 0, ""),
    Ctl("CLR_A_L",     LIGHT, 0, 0, ""),   # LANE_MOTION excursion
    Ctl("CLR_B_L",     LIGHT, 0, 0, ""),
    Ctl("LVL_A_L",     LIGHT, 0, 0, ""),   # LANE_LEVEL excursion
    Ctl("LVL_B_L",     LIGHT, 0, 0, ""),
    Ctl("SONG_A_L",    LIGHT, 0, 0, ""),   # which phrase snapshot is sounding
    Ctl("SONG_B_L",    LIGHT, 0, 0, ""),
    Ctl("FLOW_A_L",    LIGHT, 0, 0, ""),   # drawn since the regroup, never lit
    Ctl("FLOW_B_L",    LIGHT, 0, 0, ""),
    Ctl("TEMPO_L",     LIGHT, 0, 0, ""),
    Ctl("SYNC_L",      LIGHT, 0, 0, ""),
    Ctl("MODBTN_L",    LIGHT, 0, 0, ""),   # latched, per spec 3.4
    Ctl("SHIFTBTN_L",  LIGHT, 0, 0, ""),
    Ctl("CEIL_L",      LIGHT, 0, 0, ""),   # the master shaper is bending
]

# --- shared panel lettering (drawn by SVG for preview, by C++ at runtime) -----
# (x, y baseline, size mm, letter-spacing mm, hex colour, anchor, text)
TEXTS = [
    (RING_CX_A,     RING_CY + 1.6, 5.0, 0.0, GREEN_DIM,  "middle", "A"),
    (W - RING_CX_A, RING_CY + 1.6, 5.0, 0.0, COPPER_DIM, "middle", "B"),
    (CX,            7.0,           3.6, 0.9, INK,        "middle", "FIREFLOW"),
] + [
    # sector captions, tucked into the free panel corners (spec §1)
    (W - cx if mir else cx, cy, 1.7, 0.3, COPPER if mir else GREEN,
     "middle", name)
    for mir in (False, True)
    for (name, _a0, _a1, (cx, cy)) in SECTORS
] + legend_texts()

# =============================================================================
#  SVG
# =============================================================================
def mm(v): return f"{v:.3f}"

def fx_field_svg(field):
    _mir, _name, x, y, w, h, fill = field
    return (f'<rect x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" '
            f'height="{mm(h)}" rx="1.0" fill="{fill}"/>')

def play_field_svg(field):
    _mir, x, y, w, h = field
    return (f'<rect x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" '
            f'height="{mm(h)}" rx="1.0" fill="{PAPER_DEEP}" '
            f'fill-opacity="{PLAY_FIELD_OPACITY:.2f}"/>')

def side_accent(x):
    """Panel accent for a control: green left half, copper right half,
    neutral muted inside the center strip."""
    if x < CX - 21.0: return GREEN
    if x > CX + 21.0: return COPPER
    return MUTED

def ring_svg(cx, dot):
    """One LED ring: dark well + 32 dim track dots (the 'off' bed). The live
    SpkyRing widget draws the accent glow on top of these at runtime."""
    P = [f'<circle cx="{mm(cx)}" cy="{mm(RING_CY)}" r="{mm(RING_R+2.4)}" '
         f'fill="{WELL}" stroke="{GRAPHITE}" stroke-width="0.5"/>']
    for i in range(32):
        a = math.radians(360.0 * i / 32.0)
        x = cx + RING_R * math.sin(a)
        y = RING_CY - RING_R * math.cos(a)
        P.append(f'<circle cx="{mm(x)}" cy="{mm(y)}" r="0.7" fill="{dot}"/>')
    return "\n".join(P)

def knob_svg(c):
    """Graphite cap + accent collar + paper tick. The collar sits OUTSIDE the
    runtime widget's footprint (RoundBlackKnob r4.2, Trimpot ~r3.3), so the
    side colour stays visible in Rack, not just in this preview."""
    P = []
    big = c.kind in (BIGKNOB, KNOBC)
    accent = side_accent(c.x)
    if c.enum == "MORPH":  # signature: the bridge knob wears both colours
        collar_r = c.r + 0.85
        for (col, sweep) in ((GREEN, 0), (COPPER, 1)):
            P.append(f'<path d="M {mm(c.x)} {mm(c.y-collar_r)} '
                     f'A {mm(collar_r)} {mm(collar_r)} 0 0 {sweep} '
                     f'{mm(c.x)} {mm(c.y+collar_r)}" fill="none" '
                     f'stroke="{col}" stroke-width="0.5"/>')
    elif big:
        # Only the orbit keeps its collar -- it marks the performance layer.
        # 20+ rings per side on the small pots were noise (spec §3).
        P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r + 0.85)}" '
                 f'fill="none" stroke="{accent}" stroke-width="0.5"/>')
    P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
             f'fill="url(#knobCap)" stroke="{GRAPHITE}" stroke-width="0.3"/>')
    P.append(f'<line x1="{mm(c.x)}" y1="{mm(c.y)}" x2="{mm(c.x)}" '
             f'y2="{mm(c.y-c.r+0.7)}" stroke="{WHITE}" stroke-width="0.5" '
             f'stroke-linecap="round"/>')
    return "\n".join(P)

def wedge_svg(cx, a0, a1, colour, mir):
    """One tinted sector segment behind the orbit knobs: an annulus slice
    using the fixed Quiet Technical radii."""
    R_OUT, R_IN = SECTOR_R_OUT, SECTOR_R_IN
    if mir:
        a0, a1 = a1, a0          # keep the on-screen sweep direction
    def pt(r, a):
        rad = math.radians(a)
        s = math.sin(rad)
        return (cx + (-s if mir else s) * r, RING_CY - r * math.cos(rad))
    laf = 1 if abs(a1 - a0) > 180.0 else 0
    ox0, oy0 = pt(R_OUT, a0); ox1, oy1 = pt(R_OUT, a1)
    ix1, iy1 = pt(R_IN,  a1); ix0, iy0 = pt(R_IN,  a0)
    return (f'<path d="M {mm(ox0)} {mm(oy0)} A {mm(R_OUT)} {mm(R_OUT)} 0 {laf} 1 '
            f'{mm(ox1)} {mm(oy1)} L {mm(ix1)} {mm(iy1)} A {mm(R_IN)} {mm(R_IN)} '
            f'0 {laf} 0 {mm(ix0)} {mm(iy0)} Z" fill="{colour}" '
            f'opacity="{SECTOR_OPACITY:.3f}"/>')

def svg():
    P = []
    P.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{mm(W)}mm" '
             f'height="{mm(Hh)}mm" viewBox="0 0 {mm(W)} {mm(Hh)}">')
    P.append(
        '<defs>'
        '<radialGradient id="knobCap" cx="38%" cy="32%" r="75%">'
        f'<stop offset="0%" stop-color="#3a3d35"/>'
        f'<stop offset="55%" stop-color="{GRAPHITE}"/>'
        f'<stop offset="100%" stop-color="#15160f"/></radialGradient>'
        '<linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">'
        f'<stop offset="0%" stop-color="{PAPER_HI}"/>'
        f'<stop offset="100%" stop-color="{PAPER_LO}"/></linearGradient>'
        '</defs>')
    P.append(f'<rect x="0" y="0" width="{mm(W)}" height="{mm(Hh)}" fill="url(#plate)"/>')
    # the two-colour identity: a solder-green band on A's edge, copper on B's
    P.append(f'<rect x="0" y="0" width="1.4" height="{mm(Hh)}" fill="{GREEN}"/>')
    P.append(f'<rect x="{mm(W-1.4)}" y="0" width="1.4" height="{mm(Hh)}" fill="{COPPER}"/>')
    # sector tints behind the orbit (drawn first: everything else sits on top)
    for mir, cx, accent in ((False, RING_CX_A, GREEN), (True, W - RING_CX_A, COPPER)):
        for (name, a0, a1, _cap) in SECTORS:
            P.append(wedge_svg(cx, a0, a1, accent, mir))
    # two rings (dark well + dim track; the live SpkyRing widget lights them)
    P.append(ring_svg(RING_CX_A, GREEN_DIM))
    P.append(ring_svg(W - RING_CX_A, COPPER_DIM))
    # fieldset group boxes (drawn under the glyphs, over the sector tints)
    for (x, y, w, h, name, _colour) in GROUPS:
        P.append(group_box(x, y, w, h, name))
    # connected low-contrast family fields inside the mirrored FX boxes
    for field in FX_FIELDS:
        P.append(fx_field_svg(field))
    # mode/record fields inside PLAY, after FX fields and below every control
    for field in PLAY_FIELDS:
        P.append(play_field_svg(field))
    # dark inner wells under the output groups -- in/out at a glance (spec §7)
    for (bx, _lg, _col, well, _items) in JACK_GROUPS:
        if well:
            P.append(f'<rect x="{mm(bx + 1.4)}" y="{mm(JACK_BOX_Y + 1.6)}" '
                     f'width="{mm(JACK_BOX_W - 2.8)}" '
                     f'height="{mm(JACK_BOX_H - 3.2)}" rx="1.2" fill="{WELL}"/>')
    # The PLAY row used to carry a hairline at 28.7 dividing the mode pads
    # from the sequencer block. Removed 2026-07-22: the REC LED landed 1.3 mm
    # to its right and the two read as a single smudged element rather than a
    # rule and an indicator. The gap between REC and STEPS separates the two
    # blocks on its own, which is what the rest of the row already relies on.
    # brand flanking dots -- one per colour, flanking the top FIREFLOW logo
    P.append(f'<circle cx="{mm(CX-15)}" cy="5.9" r="0.9" fill="{GREEN}"/>')
    P.append(f'<circle cx="{mm(CX+15)}" cy="5.9" r="0.9" fill="{COPPER}"/>')
    # glyphs + labels
    for c in STATIC_PANEL_PARAMS + INPUTS + OUTPUTS + STATIC_LIGHTS:
        if c.kind in (IN, OUT):
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                     f'fill="{GRAPHITE}" stroke="#4a4a40" stroke-width="0.4"/>')
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="1.3" fill="#0e0e0c"/>')
        elif c.kind == LIGHT:  # dark LED housing; live YellowLight glows amber on top
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                     f'fill="#1a1206" stroke="#3a2c12" stroke-width="0.25"/>')
        elif c.kind == SW2:
            P.append(f'<rect x="{mm(c.x-1.7)}" y="{mm(c.y-3.0)}" width="3.4" '
                     f'height="6.0" rx="0.8" fill="{WHITE}" stroke="{INK}" stroke-width="0.35"/>')
            P.append(f'<rect x="{mm(c.x-1.1)}" y="{mm(c.y-2.4)}" width="2.2" '
                     f'height="2.4" rx="0.5" fill="{GRAPHITE}"/>')
        elif c.kind in (LATCH, SMBTN):
            # Pads: a plain paper key bed, no accent edge. Rack's button widgets
            # are ROUND and cover the square almost exactly, so a coloured
            # stroke survived only as a halo peeking out around each button
            # (spotted in Rack, 2026-07-18) -- side identity is carried by the
            # edge bands, ring letter and sector tints anyway.
            P.append(f'<rect x="{mm(c.x-c.r)}" y="{mm(c.y-c.r)}" width="{mm(2*c.r)}" '
                     f'height="{mm(2*c.r)}" rx="1.0" fill="{WHITE}"/>')
        else:
            P.append(knob_svg(c))
        if c.label:
            lx, ly, anchor, size, colour = label_of(c)
            P.append(f'<text x="{mm(lx)}" y="{mm(ly)}" fill="{colour}" '
                     f'text-anchor="{anchor}" font-family="monospace" '
                     f'font-size="{size}">{c.label}</text>')
    # shared lettering (preview only -- Rack draws these via PanelText)
    for (x, y, size, spacing, col, anchor, txt) in TEXTS:
        P.append(f'<text x="{mm(x)}" y="{mm(y)}" fill="{col}" text-anchor="{anchor}" '
                 f'font-family="monospace" font-size="{size}" '
                 f'letter-spacing="{spacing}" font-weight="bold">{txt}</text>')
    P.append('</svg>')
    return "\n".join(P)

# =============================================================================
#  C++ header
# =============================================================================
def rgb(hexcol): return "0x" + hexcol.lstrip("#").upper()

def header():
    L2 = []
    L2.append("// GENERATED by res/gen_panel.py -- do not edit by hand.")
    L2.append("#pragma once")
    L2.append("namespace spkyvcv {")
    L2.append("struct XY { float x, y; };")
    L2.append("enum WidgetKind { WK_BIGKNOB, WK_KNOBC, WK_SMKNOB, WK_KNOBI, "
              "WK_SW2, WK_LATCH, WK_SMBTN, WK_IN, WK_OUT, WK_LIGHT };")
    L2.append("struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label; "
              "XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb; "
              "const char* tip; };")
    L2.append("// anchor: 0 = middle, 1 = start (left-aligned), 2 = end (right-aligned)")
    L2.append("struct PanelTxt { XY mm; float size; float spacing; unsigned rgb; "
              "unsigned char anchor; const char* str; };")
    L2.append("struct DynCaption { int id; int driverId; int count; "
              "const char* words[6]; };")
    L2.append(f"static constexpr int PART_STRIDE = {PART_STRIDE};")
    L2.append(f"static constexpr float kRingR = {RING_R:.3f}f;      // mm, LED-dot orbit")
    L2.append(f"static constexpr float kRingDotR = 0.95f;   // mm, lit-dot radius")
    L2.append("static constexpr int kRingDots = 32;")
    L2.append(f"static constexpr unsigned kColLabel = {rgb(INK)};   // ink captions")
    L2.append(f"static constexpr unsigned kColGlow[2] = {{{hex(GLOW[0])}, {hex(GLOW[1])}}}; "
              "// per-part LED glow (A green, B copper)")

    def emit_enum(name, items, terminator):
        L2.append(f"enum {name} {{")
        for c in items:
            L2.append(f"    {c.enum},")
        L2.append(f"    {terminator}")
        L2.append("};")

    emit_enum("ParamId",  PARAMS,  "NUM_PARAMS")
    emit_enum("InputId",  INPUTS + HW_MOD_INPUTS,  "NUM_INPUTS")
    emit_enum("OutputId", OUTPUTS, "NUM_OUTPUTS")
    emit_enum("LightId",  LIGHTS + HW_ONLY_LIGHTS,  "NUM_LIGHTS")

    ANCHOR_ID = {"middle": 0, "start": 1, "end": 2}

    def emit_table(name, items):
        L2.append(f"static const PanelCtl {name}[] = {{")
        for c in items:
            lx, ly, anchor, size, colour = label_of(c)
            L2.append(f'    {{{c.enum}, {WKMAP[c.kind]}, {{{c.x:.3f}f, {c.y:.3f}f}}, '
                      f'"{c.label}", {{{lx:.3f}f, {ly:.3f}f}}, {ANCHOR_ID[anchor]}, '
                      f'{size:.2f}f, {rgb(colour)}, "{c.tip}"}},')
        L2.append("};")

    emit_table("kParamCtls",  RUNTIME_PANEL_PARAMS)
    emit_table("kInputCtls",  INPUTS)
    emit_table("kHwModInputCtls", HW_MOD_INPUTS)
    emit_table("kOutputCtls", OUTPUTS)
    emit_table("kLightCtls",  LIGHTS)

    # State-dependent captions, expanded per deck. The driver id is the
    # control whose value picks the word -- ENGINE_A for a deck-A target, and
    # the target itself for a self-driving pad.
    L2.append("static const DynCaption kDynCaptions[] = {")
    for target, driver, words in DYNAMIC_CAPTIONS:
        padded = list(words) + [""] * (6 - len(words))
        cells = ", ".join(f'"{w}"' for w in padded)
        for suffix in ("_A", "_B"):
            L2.append(f"    {{{target}{suffix}, {driver}{suffix}, "
                      f"{len(words)}, {{{cells}}}}},")
    L2.append("};")

    L2.append("static const PanelTxt kPanelTexts[] = {")
    for (x, y, size, spacing, col, anchor, txt) in TEXTS:
        L2.append(f'    {{{{{x:.3f}f, {y:.3f}f}}, {size:.2f}f, {spacing:.2f}f, '
                  f'{rgb(col)}, {ANCHOR_ID[anchor]}, "{txt}"}},')
    L2.append("};")
    L2.append("} // namespace spkyvcv")
    return "\n".join(L2) + "\n"

def _float_literal(v):
    """9-sig-fig text for a C++ float literal. %g strips the decimal point
    off whole numbers (16 -> "16"), and "16f" is not a valid C++ floating
    literal -- GCC parses it as an integer with an unknown suffix and dies
    looking for operator""f. Force the point back on so every entry parses
    as float, matching the (unformatted) values already in INIT_DEFAULTS."""
    s = f"{v:.9g}"
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s

def init_patch_header():
    L = ["// GENERATED by res/gen_panel.py -- do not edit by hand.",
         "#pragma once", "", "namespace spkyvcv {", "",
         "static constexpr float kInitParamDefaults[] = {"]
    for c in PARAMS:
        L.append(f"    {_float_literal(INIT_DEFAULTS[c.enum])}f, // {c.enum}")
    L.append("};")
    L.append("static_assert(sizeof(kInitParamDefaults) / "
             "sizeof(kInitParamDefaults[0])")
    L.append("              == NUM_PARAMS, "
             '"init snapshot must cover every ParamId");')
    L.append("")
    L.append("inline float initParamDefault(int id) {")
    L.append("    return kInitParamDefaults[id];")
    L.append("}")
    L.append("")
    L.append("} // namespace spkyvcv")
    return "\n".join(L) + "\n"

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    with open(os.path.join(here, "Fireflow.svg"), "w") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_panel.hpp"), "w") as f:
        f.write(header())
    with open(os.path.join(root, "src", "init_patch.hpp"), "w") as f:
        f.write(init_patch_header())
    print("wrote res/Fireflow.svg, src/generated_panel.hpp and src/init_patch.hpp")
    print(f"params={len(PARAMS)} (stride={PART_STRIDE}) inputs={len(INPUTS)} "
          f"outputs={len(OUTPUTS)} lights={len(LIGHTS)}  panel={HP}HP")
