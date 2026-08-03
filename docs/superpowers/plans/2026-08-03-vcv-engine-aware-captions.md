# VCV engine-aware panel captions — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every word printed on the Spotymod faceplate names what the control
actually does on the currently selected engine, and no word appears twice.

**Architecture:** One table in the panel generator (`res/gen_panel.py`) lists
every caption that depends on state, together with the parameter whose value
selects among its words. The generator emits that table into
`src/generated_panel.hpp`; `PanelText::draw` resolves it with a single lookup,
replacing two hand-written special cases. No coordinate moves.

**Tech Stack:** Python 3 (generator + test suite, no pytest — plain asserts),
C++17 against the VCV Rack 2 SDK, SVG emitted by the generator.

**Spec:** `docs/superpowers/specs/2026-08-03-vcv-engine-aware-captions-design.md`

## Global Constraints

- **Repository:** all work happens in the fork at `c:\Users\bernd\Documents\AI\Spotykach`, **not** in the residency repo. Every path below is relative to that root.
- **No compatibility burden.** This is a dev branch. Saved `.vcv` patches are allowed to break. Do not write migrations, do not preserve parameter ids for their own sake, do not add soft-takeover.
- **No coordinate moves.** This change renames and re-resolves text. `test_no_overlap`, `test_on_panel`, `test_orbit_positions`, `test_layout_constants`, `test_lower_half_positions` and `test_center_positions` must stay green without being edited for geometry.
- **The generator is the single source of truth.** No caption word may be typed into `src/Spotymod.cpp`. Every word lives in `res/gen_panel.py` and reaches C++ through `src/generated_panel.hpp`.
- **Engine order is frozen:** `0 Synth · 1 Sampler · 2 Wave · 3 Body · 4 BBD`.
- **Regenerate, never hand-edit.** `res/Spotymod.svg` and `src/generated_panel.hpp` are generated files. After any `gen_panel.py` change run `python res/gen_panel.py` from `host/vcv/` and commit both outputs.
- **Run the panel suite from `host/vcv/`:** `python res/test_panel.py`. It prints `PASS -- panel guards ok` or a `FAIL (n):` list. Exit code carries the result. It is currently green — keep it green at every commit boundary.
- **Build check for C++ tasks:** `./build-local.sh` from `host/vcv/`. Never hand-roll the compiler invocation; the system `g++` on this machine is the ARM cross-compiler and will fail with "MinGW not found".
- **Commit trailer:** every commit ends with `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Every new test must be shown red once.** A caption guard that cannot fail is decoration. Each task below has an explicit "verify it fails" step with the expected failure text; do not skip it.

---

### Task 1: The uniqueness guard, and the collisions it catches

The durable half of this change. A test that no word is printed twice would
have prevented `RATE`/`RATE` and `GRIT`/`GRIT` from ever being written. Write
it first, watch it condemn the current panel, then fix what it names.

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/res/gen_panel.py`
- Regenerate: `host/vcv/res/Spotymod.svg`, `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Produces: `dynamic_words_for(base)` in `test_panel.py` — returns the list of state-dependent words for a control base name (`"SUB"`, `"MELODY"`, …), or `[]` when the control has none. It reads `gen_panel.DYNAMIC_CAPTIONS` if that table exists and tolerates its absence, so Task 2 can add the table without touching this helper.
- Produces: `printed_words()` in `test_panel.py` — returns `{word: [origin, …]}` for everything a player reads in deck A's half plus the shared centre.

- [ ] **Step 1: Write the failing test**

Add to `host/vcv/res/test_panel.py`, immediately after `test_every_label_is_reachable`:

```python
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
        for word in dynamic_words_for(base) or [c.label]:
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
```

- [ ] **Step 2: Run test to verify it fails**

Run from `host/vcv/`: `python res/test_panel.py`

Expected: `FAIL (5):` naming exactly these five, in this order:

```
  - 'GRIT' is printed 2x: GRIT, GRITMODE
  - 'PITCH' is printed 2x: STAGES, sector eyebrow
  - 'RATE' is printed 2x: RATE, FLUXRATE
  - 'ROOM' is printed 2x: REV_MIX, group legend
  - 'TIME' is printed 2x: FLUXTIME, group legend
```

If a sixth appears, stop and report it — the probe run while writing the spec
found exactly these five, and a new one means the panel changed underneath
this plan.

`DRIVE` is deliberately absent here: it only collides once `SOURCE`'s BBD word
exists, which happens in Task 2. `MASTER_DRIVE` → `PUSH` is therefore Task 2's
rename, not this one.

- [ ] **Step 3: Rename the five colliding captions in the generator**

In `host/vcv/res/gen_panel.py`:

**(a)** The PLAY pads. Replace the three-tuple list and its loop in
`part_controls()` — the pad needs an explicit tooltip now that its caption is
no longer the block's name:

```python
    # ENGINE cycles Synth/Sampler/Wave/Body/BBD (states 0..4); the C++ side
    # (Spotymod.cpp configSwitch/EngineCycleLatch) is the source of truth for
    # the labels, this comment just keeps the panel legend discoverable here.
    # GRITMODE's caption is its MODE, not its block's name -- the block is the
    # GRIT knob one row up (spec 2026-08-03). Its resting word is SAT; the
    # runtime swaps in CRSH from DYNAMIC_CAPTIONS.
    pads = [("ENGINE", LATCH, "ENG", None),
            ("GRITMODE", LATCH, "SAT", "Grit mode"),
            ("STEP", LATCH, "STEP", None)]
    for i, (enum, kind, lbl, tip) in enumerate(pads):
        out.append(Ctl(enum, kind, fx(PAD_X[i]), PLAY_Y, lbl, tip))
```

**(b)** In `PANEL_PARAMS`, the FLUX rate pair — `DIV` because it selects a
division, and it now reads as a pair with `MULT`:

```python
    Ctl("FLUXRATE_A", SMKNOB, FX_TOP[0],     ROW_V1, "DIV", "FLUX division"),
    Ctl("FLUXRATE_B", SMKNOB, W - FX_TOP[0], ROW_V1, "DIV", "FLUX division"),
```

**(c)** In `PANEL_PARAMS`, the BBD bend pair. A bucket-brigade line's first
pass is always at unity pitch; only recirculating repeats sample the moved
clock more than once, so what is audible is a bend. This also frees `PITCH`
for the sector eyebrow, which it collided with:

```python
    Ctl("STAGES_A", SMKNOB, VOICE_X[0],     ROW_V1, "BEND", "BBD Bend"),
    Ctl("STAGES_B", SMKNOB, W - VOICE_X[0], ROW_V1, "BEND", "BBD Bend"),
```

**(d)** In `PANEL_PARAMS`, the per-deck reverb send. `Instrument::
set_reverb_mix`'s own comment already calls this the deck's SEND:

```python
    Ctl("REV_MIX_A", SMKNOB, FX_TOP[3],     ROW_V1, "SEND", "Room send"),
    Ctl("REV_MIX_B", SMKNOB, W - FX_TOP[3], ROW_V1, "SEND", "Room send"),
```

**(e)** In `APPENDED_PANEL_PARAMS`, the tape time multiplier:

```python
APPENDED_PANEL_PARAMS = [
    Ctl("FLUXTIME_A", SMKNOB, FX_BOT[1],     ROW_V2, "MULT", "Tape Time"),
    Ctl("FLUXTIME_B", SMKNOB, W - FX_BOT[1], ROW_V2, "MULT", "Tape Time"),
]
```

Also update the two stale comments that name the old words: the `FX_TOP`
comment (`# RATE MIX FB | ROOM …` → `# DIV MIX FB | SEND …`) and the `FX_BOT`
comment (`# LINK TIME | GRIT COMP` → `# LINK MULT | GRIT COMP`).

- [ ] **Step 4: Update the tests that pinned the old words**

Five existing tests assert the captions and tooltips being renamed. The
principle for tooltips: **the tooltip spells out what the caption
abbreviates.** In `host/vcv/res/test_panel.py`:

**(a)** `PARAM_TIPS` — four entries change. The two `'GRIT'` entries at the end
of each deck block (the second one in each block, following `'ENG'`) become
`'Grit mode'`; `'FRATE', 'FRATE'` become `'FLUX division', 'FLUX division'`;
`'BBD Pitch', 'BBD Pitch'` become `'BBD Bend', 'BBD Bend'`; `'ROOM', 'ROOM'`
become `'Room send', 'Room send'`. The resulting block reads:

```python
    'RATE', 'SHAPE', 'DENS', 'SMTH', 'RANGE', 'MELO', 'MOD', 'TUNE',
    'ATK', 'DEC', 'RES', 'SUB', 'SOURCE', 'FLUX', 'GRIT', 'COMP', 'STPS',
    'ENG', 'Grit mode', 'STEP', 'FORM', 'NEW', 'SONG',
    'RATE', 'SHAPE', 'DENS', 'SMTH', 'RANGE', 'MELO', 'MOD', 'TUNE',
    'ATK', 'DEC', 'RES', 'SUB', 'SOURCE', 'FLUX', 'GRIT', 'COMP', 'STPS',
    'ENG', 'Grit mode', 'STEP', 'FORM', 'NEW', 'SONG',
    'MORPH', 'SYNC', 'TEMPO', 'COUPL', 'SCALE', 'DRIFT', 'SPOT', 'DRIVE',
    'SETL', 'SIZE', 'DECAY', 'TONE', 'DIFF', 'SMEAR', 'WOBL', 'CHOKE',
    'FILT', 'FILT', 'TIDE', 'FLUX division', 'FLUX division', 'FFB', 'FFB',
    'COLOR', 'COLOR', 'LINK', 'LINK', 'BBD Bend', 'BBD Bend', 'REC', 'REC',
    'Room send', 'Room send',
    'SHUFL', 'Detune A', 'Detune B', 'Drive A', 'Drive B',
    'Tape Time', 'Tape Time',
```

**(b)** `test_param_runtime_tip_contract` — its slice assertion and its
caption/tip table:

```python
    check(PARAM_TIPS[71:75] == ['LINK', 'LINK', 'BBD Bend', 'BBD Bend'],
          "BBD Bend runtime tips drifted")
```

```python
    for enum, caption, tip in (
            ("FLUX_A", "MIX", "FLUX"), ("FLUX_B", "MIX", "FLUX"),
            ("FLUXRATE_A", "DIV", "FLUX division"),
            ("FLUXRATE_B", "DIV", "FLUX division"),
            ("FLUXFB_A", "FB", "FFB"), ("FLUXFB_B", "FB", "FFB")):
```

**(c)** `test_reverb_mix_params` — the label assertion and the docstring's
`'ROOM'`:

```python
        check(ctl(e).label == "SEND", f"{e} label must be 'SEND'")
```

**(d)** `test_lower_half_positions` — the FLUXTIME caption assertion. Rename
the local `time` to `mult` so the variable does not shadow the module name it
no longer describes:

```python
        mult = ctl('FLUXTIME' + suffix)
        check(mult.label == 'MULT' and mult.tip == 'Tape Time',
              f"{suffix}: tape multiplier caption/tooltip drifted")
```

**(e)** `test_static_synth_preview_excludes_bbd_pitch` — two of its four
assertions name renamed words:

```python
    check(svg.count('font-size="1.9">MULT</text>') == 2,
          "static preview must show two MULT captions")
    check('font-size="1.9">BEND</text>' not in svg,
          "static preview must not overlay BEND on ATK")
```

- [ ] **Step 5: Regenerate and run the suite**

Run from `host/vcv/`:

```bash
python res/gen_panel.py
python res/test_panel.py
```

Expected: `wrote res/Spotymod.svg and src/generated_panel.hpp` followed by
`PASS -- panel guards ok`.

- [ ] **Step 6: Prove the new guard can still fail**

Temporarily change `Ctl("FLUXRATE_A", …, "DIV", …)` back to `"RATE"`, run
`python res/gen_panel.py && python res/test_panel.py`, and confirm the failure
line `'RATE' is printed 2x: RATE, FLUXRATE` appears. Revert the edit, regenerate,
confirm `PASS` again. Do not commit the temporary change.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py \
        host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git commit -F - <<'EOF'
test(vcv): one word may not name two things

RATE named the modulator macro and the FLUX division. GRIT named the mode
pad and the mix knob beside it. TIME and ROOM each named a centre legend
and an FX control. PITCH named the orbit sector and the BBD voice slot.
Five collisions on one plate, none of them caught by a suite that checks
every coordinate.

The guard collects every word drawn in deck A's half plus the centre --
captions, group legends, sector eyebrows -- and refuses duplicates. Jack
captions stay out of it: PIT/GATE/L/R repeat across the five jack groups
by design, disambiguated by their wells and legends.

The FX box yields on every collision it is party to: DIV, MULT, SEND. The
mode pad shows its mode. The BBD slot becomes BEND -- a bucket-brigade
line's first pass is always at unity pitch, so what is audible is a bend.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 2: The caption table replaces the printed second words

`SAMPLER_LBL` prints `SCAN` beside `MELO` and `LEN` beside `SUB` permanently,
on both decks, on all five engines — true on exactly one. This task deletes
that machinery and stands up the table that replaces it, in one commit,
because the uniqueness guard from Task 1 couples them: the moment `SCAN`
enters the caption table it would collide with the `SCAN` still printed on the
plate.

**Files:**
- Modify: `host/vcv/res/gen_panel.py`
- Modify: `host/vcv/res/test_panel.py`
- Regenerate: `host/vcv/res/Spotymod.svg`, `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: `dynamic_words_for(base)` and `printed_words()` from Task 1.
- Produces: `gen_panel.DYNAMIC_CAPTIONS` — a list of `(target_base, driver_base, words)` tuples. `target_base` and `driver_base` are un-suffixed param names (`"SUB"`, `"ENGINE"`); the generator expands each row to `_A` and `_B`.
- Produces: `gen_panel.dynamic_words(base)` — returns the words tuple for a base, or `()`.
- Produces: in `src/generated_panel.hpp`, `struct DynCaption { int id; int driverId; int count; const char* words[5]; };` and `static const DynCaption kDynCaptions[]`. Task 3 consumes both.

- [ ] **Step 1: Write the failing tests**

Add to `host/vcv/res/test_panel.py`, after `test_every_printed_word_is_unique`:

```python
def test_dynamic_caption_table_is_well_formed():
    """Every row targets a real control, is driven by a real control, and
    carries one word per state of its driver."""
    table = getattr(g, "DYNAMIC_CAPTIONS", None)
    check(table is not None, "gen_panel has no DYNAMIC_CAPTIONS table")
    if table is None:
        return
    enums = {c.enum for c in g.RUNTIME_PANEL_PARAMS}
    driver_states = {"ENGINE": 5, "GRITMODE": 2}
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
    check(h.count("{GRITMODE_A, GRITMODE_A, 2, {") == 1,
          "the mode pad must drive its own caption")
    body = h.split("static const DynCaption kDynCaptions[] = {")[1].split("};")[0]
    check(body.count("},") == rows,
          f"kDynCaptions has {body.count('},')} rows, want {rows}")
```

- [ ] **Step 2: Run tests to verify they fail**

Run from `host/vcv/`: `python res/test_panel.py`

Expected: a `FAIL` list containing at least

```
  - gen_panel has no DYNAMIC_CAPTIONS table
  - 'SCAN' is still a static kPanelTexts entry
  - 'LEN' is still a static kPanelTexts entry
  - gen_panel still carries the retired sampler_texts
  - generated header has no DynCaption struct
```

- [ ] **Step 3: Add the table to the generator**

In `host/vcv/res/gen_panel.py`, replace the `SOURCE_CAPTIONS` line (in the
"lower half per part" block, just above `FX_TOP`) with the table and its
accessor:

```python
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
# 4 BBD. Word[0] is also the control's resting label on the static plate --
# test_static_label_is_the_tables_first_word holds the two together.
#
# Sources, so a later reader can check each word against the engine rather
# than against taste: BodyVoice::set_env_times is "exciter length, damping";
# set_resonance is "exciter character"; set_sub_level is "excitation bus
# level"; set_cutoff_hz is "brightness". BbdEngine::set_decay is "a trim BELOW
# k0"; set_resonance is "the feedback-path tilt"; set_sub is "the input
# level"; set_filt is "the loss-pole corner". MELODY is set_variation, the
# bipolar RENEW <- LOOP -> GROW axis -- never a melody control.
DYNAMIC_CAPTIONS = [
    ("MELODY",   "ENGINE",   ("VARY", "SCAN", "VARY", "VARY", "VARY")),
    ("ATTACK",   "ENGINE",   ("ATK",  "ATK",  "ATK",   "HIT",   "ATK")),
    ("DECAY",    "ENGINE",   ("DEC",  "DEC",  "DEC",   "DAMP",  "TAIL")),
    ("RES",      "ENGINE",   ("RES",  "RES",  "RES",   "CHAR",  "TILT")),
    ("SUB",      "ENGINE",   ("SUB",  "LEN",  "SUB",   "EXCIT", "FEED")),
    ("FILT",     "ENGINE",   ("FILT", "FILT", "FILT",  "BRITE", "LOSS")),
    ("SOURCE",   "ENGINE",   ("TIMB", "ORG",  "FRAME", "MATL",  "DRIVE")),
    ("GRITMODE", "GRITMODE", ("SAT",  "CRSH")),
]


def dynamic_words(base):
    """The state-dependent words for a control base, () when it has none."""
    for target, _driver, words in DYNAMIC_CAPTIONS:
        if target == base:
            return words
    return ()
```

Then bind the resting labels to `word[0]` at their four definition sites, so
the two can never drift:

- in `part_controls()`, the `SOURCE` row of the voice-row loop:
  `("SOURCE", dynamic_words("SOURCE")[0], VOICE_X[2], ROW_V2)`
- in `part_controls()`, the macro list: `("MELODY", dynamic_words("MELODY")[0])`
  replaces `("MELODY","MELO")`
- in `part_controls()`, the pads list from Task 1:
  `("GRITMODE", LATCH, dynamic_words("GRITMODE")[0], "Grit mode")`
- in `PANEL_PARAMS`, both `FILT` rows:
  `Ctl("FILT_A", SMKNOB, VOICE_X[1], ROW_V1, dynamic_words("FILT")[0])` and the
  mirrored `FILT_B`

`ATTACK`, `DECAY`, `RES` and `SUB` already carry `"ATK"`, `"DEC"`, `"RES"`,
`"SUB"` in the voice-row loop, which are already `word[0]`; leave those
literals in place — `test_static_label_is_the_tables_first_word` guards them.

`MELODY`'s tooltip must follow its caption. In `part_controls()`'s macro loop
the tooltip defaults to the label, which would now read `VARY`; pass the
spelled-out name instead by giving the macro list a tooltip column:

```python
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
```

- [ ] **Step 4: Delete the printed-second-word machinery**

In `host/vcv/res/gen_panel.py`, delete outright:

- the whole `# --- sampler meanings of the remapped knobs` block: `SAMPLER_LBL`, `SAMPLER_SIZE`, `SAMPLER_GAP`, `SAMPLER_RADIAL`, `MONO_ADV`, `text_w()`, `mirror_anchor()`, `mirror_label()` and `sampler_texts()`
- `default_label_of()` — it exists only because `sampler_texts()` overwrote `c.lbl` and needed to ask what the default had been. With that gone, `label_of()` is the only accessor. Fold its body back into `label_of()`:

```python
def label_of(c):
    """(x, y, anchor, size, colour) for a control's caption."""
    if c.lbl is not None:
        return c.lbl
    return (c.x, c.y + LBL_DY[c.kind], "middle", 1.9, INK)
```

- the `+ sampler_texts()` term at the end of the `TEXTS` assignment, and the
  paragraph of its comment that explains the anchor column's sampler origin.
  The anchor column itself stays — the radial orbit captions still need it.

- [ ] **Step 5: Rename MASTER_DRIVE, whose collision only now exists**

`SOURCE`'s BBD word is `DRIVE`, which collides with the centre knob. The
centre knob is `_limiter.set_drive`, not a distortion stage. In `SHARED`:

```python
    Ctl("MASTER_DRIVE", SMKNOB, CX, ROW_DUO2, "PUSH", "Master drive"),
```

and in `test_panel.py`'s `PARAM_TIPS`, the `'DRIVE'` entry in the centre block
(between `'SPOT'` and `'SETL'`) becomes `'Master drive'`.

- [ ] **Step 6: Emit the table into the generated header**

In `gen_panel.py`'s `header()`, add the struct beside the existing `PanelTxt`
declaration:

```python
    L2.append("struct DynCaption { int id; int driverId; int count; "
              "const char* words[5]; };")
```

and emit the table after `emit_table("kLightCtls", LIGHTS)`:

```python
    # State-dependent captions, expanded per deck. The driver id is the
    # control whose value picks the word -- ENGINE_A for a deck-A target, and
    # the target itself for a self-driving pad.
    L2.append("static const DynCaption kDynCaptions[] = {")
    for target, driver, words in DYNAMIC_CAPTIONS:
        padded = list(words) + [""] * (5 - len(words))
        cells = ", ".join(f'"{w}"' for w in padded)
        for suffix in ("_A", "_B"):
            L2.append(f"    {{{target}{suffix}, {driver}{suffix}, "
                      f"{len(words)}, {{{cells}}}}},")
    L2.append("};")
```

- [ ] **Step 7: Retire the six tests whose whole subject is the printed word**

In `host/vcv/res/test_panel.py`, delete: `test_sampler_captions_exist`,
`test_sampler_words_sit_inline_behind_their_caption`,
`test_scan_sits_outward_of_melo_and_clear_of_its_knob`,
`test_sampler_centred_captions_hand_their_centring_to_the_pair`,
`test_sampler_radial_caption_did_not_move`,
`test_sampler_inline_pairs_fit_the_voice_row`, plus the module-level
`SAMPLER_CAPTIONS` list, the `sampler_text()` helper and the `inline_span()`
helper they share.

Keep `text_span()` — `test_source_caption_geometry_for_every_engine_state`
still uses it. It calls `g.text_w`, which Step 4 deleted; move that one-line
function into the test file, where it is now the only consumer:

```python
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
```

Amend `test_all_deck_local_geometry_is_exactly_mirrored`: delete its trailing
`for base, word in SAMPLER_CAPTIONS:` loop and everything inside it. The rest
of the test — glyphs, primary labels, anchors, styling, lights — stays exactly
as it is.

- [ ] **Step 8: Generalise the SOURCE geometry test to the whole table**

`test_source_caption_geometry_for_every_engine_state` already walks five
states for one control. Replace its first three lines so it walks every
engine-driven row instead, and keep its body unchanged:

```python
def test_dynamic_caption_geometry_for_every_state():
    """Every state-dependent word stays inside its fieldset and clear of the
    neighbouring glyphs and captions, on both decks. A caption that is only
    correct in one state is not correct."""
    voice_a = next(gr for gr in g.GROUPS if gr[4] == "VOICE" and gr[0] < g.CX)
    voice_b = next(gr for gr in g.GROUPS if gr[4] == "VOICE" and gr[0] > g.CX)
```

then wrap the existing `for suffix, box in (...)` loop in an outer loop over
the table's VOICE-box members, and take the words from the table:

```python
    for target in ("SOURCE", "SUB", "DECAY", "RES", "FILT", "ATTACK"):
        words = g.dynamic_words(target)
        for suffix, box in (("_A", voice_a), ("_B", voice_b)):
            source = ctl(target + suffix)
            for state, word in enumerate(words):
```

Inside, the two neighbour checks must not compare a control against itself.
Replace the hardcoded `for base in ("SUB", "RES"):` with every other VOICE
control:

```python
            for base in ("ATTACK", "DECAY", "RES", "SUB", "FILT", "SOURCE"):
                if base == target:
                    continue
                other = ctl(base + suffix)
```

and where the body reads `label_box(other, other.label)`, use the other
control's own widest state instead, so the test measures the worst case:

```python
                widest = max(g.dynamic_words(base) or [other.label], key=len)
                other_bounds = label_box(other, widest)
```

`ATTACK` and `STAGES` share one coordinate by design, so `STAGES` is not in
either list — Task 3 makes only one of the two draw at a time.

Delete `test_source_caption_states_and_static_default`: its `SOURCE_CAPTIONS`
assertion is now `test_dynamic_caption_table_is_well_formed` plus
`test_static_label_is_the_tables_first_word`, and its "no static alias rows"
assertion is now `test_printed_second_words_are_gone`.

- [ ] **Step 9: Regenerate and run the suite**

Run from `host/vcv/`:

```bash
python res/gen_panel.py
python res/test_panel.py
```

Expected: `PASS -- panel guards ok`.

If `test_dynamic_caption_geometry_for_every_state` fails on a word that leaves
the VOICE box, stop and report which word and by how much — the longest new
captions are `EXCIT` and `BRITE` at five characters, the same length as the
existing `FRAME`, so a failure here means a real geometry problem rather than
a typo, and it is not this plan's job to move the box.

- [ ] **Step 10: Prove the new guards can fail**

Three separate mutations, each reverted before the next:

1. Change `("SUB", "ENGINE", ("SUB", "LEN", …))` to five words that include a
   duplicate of an existing caption, e.g. `"GRIT"` in place of `"LEN"`.
   Expect `'GRIT' is printed 2x: GRIT, SUB`.
2. Change `SUB`'s row to four words. Expect
   `SUB: 4 words for a ENGINE driver`.
3. Change the voice-row loop's `"SUB"` literal to `"SUBB"`. Expect
   `SUB_A label 'SUBB' != table word[0] 'SUB'`.

Regenerate and re-run after each revert; confirm `PASS` before continuing.

- [ ] **Step 11: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py \
        host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git commit -F - <<'EOF'
feat(vcv): captions come from a table, not from the plate

SCAN sat beside MELO and LEN beside SUB on every engine, printed once and
true on one. Six controls said ATK DEC RES SUB FILT while BODY heard
exciter length, damping, exciter character, excitation level and
brightness, and the BBD heard a freeze, a tail trim, a feedback tilt, an
input level and a loss pole.

One table now carries every word that depends on state, with a driver
column so the GRIT mode pad rides the same rail as the engine captions.
Word[0] is the resting label on the plate, held to the table by a test, so
the static preview and the live panel cannot disagree. MELO becomes VARY,
which is what set_variation has always been.

The whole inline-alias apparatus goes: SAMPLER_LBL, its geometry, its six
tests, and default_label_of, which only ever existed because that code
overwrote the label it then had to ask about.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 3: One lookup replaces the two hand-written caption cases

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp:1192-1300` (the caption helpers and `PanelText::draw`)
- Modify: `host/vcv/res/test_panel.py` (the two C++ guard helpers)

**Interfaces:**
- Consumes: `kDynCaptions` / `struct DynCaption` from Task 2's generated header.
- Produces: `static bool ctlVisible(Spotymod* m, int id)` in `Spotymod.cpp` — true when the control at that id is the one occupying its slot right now. Task 4 extends it with the `REC` arms and makes the widget read it.

- [ ] **Step 1: Write the failing tests**

In `host/vcv/res/test_panel.py`, replace the bodies of
`source_caption_wiring_issues` and `attack_pitch_wiring_issues` — they pin the
exact code being deleted. Delete `source_caption_wiring_issues` and its two
tests (`test_source_caption_host_wiring`,
`test_source_caption_guard_rejects_representative_regressions`) and add:

```python
CAPTION_WORDS = ("TIMB", "ORG", "FRAME", "MATL", "DRIVE", "ATK", "HIT",
                 "DEC", "DAMP", "TAIL", "RES", "CHAR", "TILT", "SUB", "LEN",
                 "EXCIT", "FEED", "FILT", "BRITE", "LOSS", "VARY", "SCAN",
                 "SAT", "CRSH", "BEND")


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
    if "Spotymod*module;explicitPanelText(Spotymod*m):module(m){}" not in panel_n:
        issues.append("PanelText must retain its Spotymod module pointer")
    return issues


def test_caption_host_wiring():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Spotymod.cpp")) as f:
        cpp = f.read()
    for issue in caption_wiring_issues(cpp):
        check(False, issue)


def test_caption_guard_rejects_representative_regressions():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Spotymod.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("for (const auto& d : kDynCaptions)", "for (const auto& d : kParamCtls)",
         "table binding"),
        ("module->params[d.driverId].getValue()",
         "module->params[ENGINE_A].getValue()", "per-deck driver binding"),
        ("new PanelText(module)", "new PanelText(nullptr)", "widget module"),
        ("if (!ctlVisible(module, t[i].id)) continue;", "",
         "shared-slot caption skip"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(caption_wiring_issues(mutated),
              f"caption guard accepted a {label} regression")
```

In `attack_pitch_wiring_issues`, delete the `expected_panel` block that pins
`attackPitchCaptionAt` and the `PanelText` entry from its scope loop; keep the
`roundedEngineState` / `isBbdSelected` / `EngineExclusiveTrimpot` / context-menu
assertions unchanged.

- [ ] **Step 2: Run tests to verify they fail**

Run from `host/vcv/`: `python res/test_panel.py`

Expected failures include:

```
  - caption word 'TIMB' is typed into the C++; every word belongs in gen_panel.py
  - the retired per-control helper sourceCaption( is back
  - PanelText must resolve captions from kDynCaptions
```

- [ ] **Step 3: Replace the caption code**

In `host/vcv/src/Spotymod.cpp`, delete `static const char* sourceCaption(int
state)` entirely. Keep `roundedEngineState` and `isBbdSelected` — the widget
visibility still needs both. Add below them:

```cpp
// Two controls share the upper-left VOICE coordinate: ATTACK on four engines,
// STAGES on the BBD. Only the one whose widget is visible may draw a caption
// there, or both words land on the same baseline.
static bool ctlVisible(Spotymod* m, int id) {
    switch (id) {
        case ATTACK_A: return !isBbdSelected(m, ENGINE_A);
        case ATTACK_B: return !isBbdSelected(m, ENGINE_B);
        case STAGES_A: return  isBbdSelected(m, ENGINE_A);
        case STAGES_B: return  isBbdSelected(m, ENGINE_B);
        default:       return true;
    }
}
```

In `PanelText::draw`, delete the `sourceCaptionAt` and `attackPitchCaptionAt`
lambdas and their four call sites, and replace the `captions` lambda with:

```cpp
        // Every caption that depends on state resolves here, out of the
        // generated table. Position, anchor, size and colour still come from
        // the same PanelCtl, so a dynamic word can never land anywhere its
        // resting word would not have.
        auto caption = [&](const PanelCtl& c) -> const char* {
            for (const auto& d : kDynCaptions) {
                if (d.id != c.id) continue;
                if (!module) return d.words[0];   // browser preview = Synth
                int v = (int)std::round(module->params[d.driverId].getValue());
                if (v < 0) v = 0;
                if (v >= d.count) v = d.count - 1;
                return d.words[v];
            }
            return c.label;
        };
        auto captions = [&](const PanelCtl* t, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                if (!t[i].label[0]) continue;
                if (!ctlVisible(module, t[i].id)) continue;
                nvgTextAlign(args.vg, alignOf(t[i].anchor) | NVG_ALIGN_BASELINE);
                text(t[i].lbl.x, t[i].lbl.y, t[i].lblSize, col(t[i].lblRgb),
                     caption(t[i]));
            }
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
        };
```

Note what left with the old code: the skip list that excluded `SOURCE_A/B`,
`ATTACK_A/B` and `STAGES_A/B` from the loop is gone. `SOURCE` is now an
ordinary table row, and the `ATTACK`/`STAGES` pair is handled by
`ctlVisible` instead of by omission.

- [ ] **Step 4: Build and run the suite**

Run from `host/vcv/`:

```bash
./build-local.sh
python res/test_panel.py
```

Expected: a clean build, then `PASS -- panel guards ok`.

- [ ] **Step 5: Prove the guard can fail**

Temporarily add `const char* unused = "ORG";` inside `PanelText::draw`, run
`python res/test_panel.py`, and confirm
`caption word 'ORG' is typed into the C++`. Revert and re-run; confirm `PASS`.

- [ ] **Step 6: Check it by eye in Rack**

Load the module, and on deck A step `ENG` through all five states. Confirm the
VOICE row reads `ATK FILT SUB / DEC RES TIMB` on Synth, `… LEN / … ORG` on
Sampler, `HIT BRITE EXCIT / DAMP CHAR MATL` on Body, and `BEND LOSS FEED /
TAIL TILT DRIVE` on BBD — and that only one word is ever drawn in the
upper-left VOICE slot. Confirm deck B is unaffected while deck A moves.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -F - <<'EOF'
feat(vcv): the panel reads its captions instead of knowing them

Two hand-written helpers drew the only two live captions the panel had, and
a third was about to be written. They are one lookup now: the caption loop
asks the generated table for a word and the table answers with the one this
deck's driver selects.

The skip list goes with them. SOURCE was skipped so a helper could draw it;
ATTACK and STAGES were skipped so another could pick between them. SOURCE
is an ordinary row now, and the shared VOICE slot is decided by whether the
control is the one occupying it -- the same question the widget answers.

The guard is that no caption word may appear as a literal in the C++ at all.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 4: REC is shown only where it does something

`REC` is inert on Synth, Wave, Body and BBD — `pushParams` gates it on
`engine_id(p) == ENGINE_SAMPLER`, and its LED is already dark there. Only the
pad and its caption still claim otherwise.

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp:1207-1216` (`EngineExclusiveTrimpot`), `:1341-1373` (widget construction)
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `ctlVisible(Spotymod*, int)` from Task 3.
- Produces: `template <typename W> struct SlotVisible : W` — a widget mixin that hides itself whenever `ctlVisible` says another control owns its slot. Used for `Trimpot` (ATTACK/STAGES) and `VCVLatch` (REC).

- [ ] **Step 1: Write the failing test**

Add to `host/vcv/res/test_panel.py`:

```python
def rec_visibility_issues(cpp):
    """REC may not be offered on an engine where pushParams ignores it."""
    issues = []
    visible = cpp_scope(cpp, "static bool ctlVisible(Spotymod* m, int id)")
    widget = cpp_scope(cpp, "SpotymodWidget(Spotymod* module)")
    if visible is None:
        issues.append("ctlVisible scope is missing")
    else:
        n = compact_cpp(visible)
        for arm in ("caseREC_A:returnroundedEngineState(m,ENGINE_A)==1;",
                    "caseREC_B:returnroundedEngineState(m,ENGINE_B)==1;"):
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
    with open(os.path.join(here, "..", "src", "Spotymod.cpp")) as f:
        cpp = f.read()
    for issue in rec_visibility_issues(cpp):
        check(False, issue)


def test_rec_visibility_guard_rejects_representative_regressions():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Spotymod.cpp")) as f:
        cpp = f.read()
    mutations = [
        ("case REC_B: return roundedEngineState(m, ENGINE_B) == 1;",
         "case REC_B: return roundedEngineState(m, ENGINE_A) == 1;",
         "part B ENG binding"),
        ("SlotVisible<VCVLatch>", "VCVLatch", "slot-visible latch"),
    ]
    for before, after, label in mutations:
        mutated = cpp.replace(before, after, 1)
        check(rec_visibility_issues(mutated),
              f"REC visibility guard accepted a {label} regression")
```

- [ ] **Step 2: Run test to verify it fails**

Run from `host/vcv/`: `python res/test_panel.py`

Expected:

```
  - ctlVisible has no Sampler arm: caseREC_A:returnroundedEngineState(m,ENGINE_A)==1;
  - ctlVisible has no Sampler arm: caseREC_B:returnroundedEngineState(m,ENGINE_B)==1;
  - REC is not built as a slot-visible latch
  - the ATTACK/STAGES pair must use the same mixin
```

- [ ] **Step 3: Extend `ctlVisible` and generalise the widget**

In `host/vcv/src/Spotymod.cpp`, add the two arms to `ctlVisible` (from Task 3)
and update its comment:

```cpp
// A control is visible only where it does something. Two share the upper-left
// VOICE coordinate -- ATTACK on four engines, STAGES on the BBD -- so only one
// may ever draw there. REC has a coordinate of its own but no job outside the
// Sampler: pushParams gates it on the exact Sampler engine id, and its LED is
// already dark elsewhere, so the pad was the last thing still claiming
// otherwise.
static bool ctlVisible(Spotymod* m, int id) {
    switch (id) {
        case ATTACK_A: return !isBbdSelected(m, ENGINE_A);
        case ATTACK_B: return !isBbdSelected(m, ENGINE_B);
        case STAGES_A: return  isBbdSelected(m, ENGINE_A);
        case STAGES_B: return  isBbdSelected(m, ENGINE_B);
        case REC_A: return roundedEngineState(m, ENGINE_A) == 1;
        case REC_B: return roundedEngineState(m, ENGINE_B) == 1;
        default:       return true;
    }
}
```

Replace `struct EngineExclusiveTrimpot : Trimpot` with the mixin, which asks
the same question the caption loop asks and therefore cannot disagree with it:

```cpp
// Widget half of ctlVisible. The caption loop and the widget must answer the
// same question from the same place, or a control can be hidden while its
// word is still drawn.
template <typename W>
struct SlotVisible : W {
    Spotymod* spotymod = nullptr;
    int ctlId = 0;

    void step() override {
        this->setVisible(ctlVisible(spotymod, ctlId));
        W::step();
    }
};
```

In `SpotymodWidget`, replace the `EngineExclusiveTrimpot` branch and add the
`REC` branch:

```cpp
                case WK_SMKNOB: case WK_KNOBI:
                    if (c.id == ATTACK_A || c.id == ATTACK_B
                            || c.id == STAGES_A || c.id == STAGES_B) {
                        auto* knob = createParamCentered<SlotVisible<Trimpot>>(
                            pos, module, c.id);
                        knob->spotymod = module;
                        knob->ctlId = c.id;
                        addParam(knob);
                    }
                    else {
                        addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    }
                    break;
```

```cpp
                case WK_LATCH:
                    if (c.id == ENGINE_A || c.id == ENGINE_B)
                        addParam(createParamCentered<EngineCycleLatch>(pos, module, c.id));
                    else if (c.id == REC_A || c.id == REC_B) {
                        auto* pad = createParamCentered<SlotVisible<VCVLatch>>(
                            pos, module, c.id);
                        pad->spotymod = module;
                        pad->ctlId = c.id;
                        addParam(pad);
                    }
                    else
                        addParam(createParamCentered<VCVLatch>(pos, module, c.id));
                    break;
```

`engineId` and `bbdOnly` disappear with `EngineExclusiveTrimpot`; the id alone
now selects the rule.

- [ ] **Step 4: Update the older guard that named the retired struct**

`attack_pitch_wiring_issues` looks up `cpp_scope(cpp, "struct
EngineExclusiveTrimpot : Trimpot")`, which no longer exists. Change that lookup
to `cpp_scope(cpp, "struct SlotVisible : W")` and its scope-loop label from
`"EngineExclusiveTrimpot"` to `"SlotVisible"`. Run the suite and fix whatever
else that helper pins to the old struct's body, guided by the failure text.

- [ ] **Step 5: Build and run the suite**

Run from `host/vcv/`:

```bash
./build-local.sh
python res/test_panel.py
```

Expected: a clean build, then `PASS -- panel guards ok`.

- [ ] **Step 6: Check it by eye in Rack**

Flip deck A's `ENG` between Sampler and Synth. The `REC` pad and its caption
appear and disappear together; the REC LED beside it stays dark on Synth. Deck
B's REC is unaffected.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -F - <<'EOF'
feat(vcv): REC appears only where it records

pushParams has always gated REC on the exact Sampler engine id, and the LED
beside it has always gone dark elsewhere. The pad and its caption were the
last things still offering a control that does nothing.

The engine-exclusive trimpot becomes a mixin over any widget, asking the
same ctlVisible the caption loop asks -- so a control can no longer be
hidden while its word is still drawn. Its engineId/bbdOnly fields go: the
control's own id now selects the rule.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 5: MELODY gives up its second job in the Sampler

The one functional change. `MELODY` drives `set_variation` on every engine and
*additionally* `sampler_scan` on the Sampler. One knob, two axes, one caption
slot — which is why `SCAN` had to be printed permanently in the first place.

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp:424` (delete), `:594-622` (the gated push)
- Modify: `host/vcv/res/test_panel.py`

- [ ] **Step 1: Write the failing test**

Add to `host/vcv/res/test_panel.py`:

```python
def test_variation_is_gated_off_the_sampler():
    """MELODY is one knob with one meaning per engine: VARY off the Sampler,
    SCAN on it. Variation parks at LOOP there -- the same shape as the
    LANE_SIZE gate that parks at 0.5f off the Sampler."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "Spotymod.cpp")) as f:
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
    check(n.index("constboolsamplerPart=") < n.index("inst.set_variation("),
          "set_variation must run after samplerPart resolves this tick's engine")
    check("if(samplerPart)inst.sampler_scan(p,pp(MELODY_A,p));" in n,
          "sampler_scan must stay gated on the same samplerPart")
```

- [ ] **Step 2: Run test to verify it fails**

Run from `host/vcv/`: `python res/test_panel.py`

Expected:

```
  - set_variation must park at LOOP on a Sampler deck
  - set_variation must run after samplerPart resolves this tick's engine
```

- [ ] **Step 3: Move the push behind the engine gate**

In `host/vcv/src/Spotymod.cpp`, delete this line from the top of the per-deck
block in `pushParams()`:

```cpp
            inst.set_variation(p, pp(MELODY_A, p));          // -1..+1 RENEW<-LOOP->GROW
```

It cannot stay where it is: `samplerPart` is resolved from `inst.engine_id(p)`
further down, *after* this tick's `inst.set_engine(p, id)` has run, so at the
old position the gate would read last tick's engine.

Replace the existing `if (samplerPart) inst.sampler_scan(...)` line further
down with the pair, keeping the long German comment above it in place:

```cpp
            // MELODY is one knob with one meaning per engine (spec 2026-08-03
            // vcv-engine-aware-captions): VARY off the Sampler, SCAN on it.
            // Both jobs at once is why SCAN had to be printed permanently
            // beside MELO. Variation parks at 0 (LOOP) here, the same shape
            // as the LANE_SIZE gate below, which parks at 0.5f off the
            // Sampler. The cost is deliberate and recorded in the spec: a
            // Sampler deck no longer renews its phrases on its own, and NEW
            // is the gesture that asks for a fresh pair.
            inst.set_variation(p, samplerPart ? 0.f : pp(MELODY_A, p));
            if (samplerPart) inst.sampler_scan(p, pp(MELODY_A, p));
```

Also amend the comment further up that reads *"set_variation and set_density
above keep firing unconditionally"* — `set_density` still does, `set_variation`
no longer does:

```cpp
            // set_density above keeps firing unconditionally -- the "push to
            // both, let the inactive side ignore it" pattern the voice row
            // already uses. set_variation left that pattern when MELODY became
            // SCAN-only on a Sampler deck (spec 2026-08-03); it is pushed
            // below, behind the same samplerPart gate. DENS is the one knob
            // that genuinely does two things in sampler STEP mode: it still
            // thins the groove gate AND now sets grain overlap. Both point the
            // same direction (sparser), so this is left as-is.
```

- [ ] **Step 4: Build and run the suite**

Run from `host/vcv/`:

```bash
./build-local.sh
python res/test_panel.py
```

Expected: a clean build, then `PASS -- panel guards ok`.

- [ ] **Step 5: Prove the guard can fail**

Temporarily change the push back to `inst.set_variation(p, pp(MELODY_A, p));`,
run `python res/test_panel.py`, and confirm `set_variation must park at LOOP on
a Sampler deck`. Revert and re-run; confirm `PASS`.

- [ ] **Step 6: Check it by ear**

On a Sampler deck, turn `MELO` (now captioned `SCAN`) and confirm the tape head
moves and nothing about the phrase changes. Flip to Synth and confirm the same
knob is `VARY` again and moves the phrase. This is the audible half of the
change and the only step in this plan that a test cannot cover.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -F - <<'EOF'
feat(vcv): MELODY does one thing per engine

The knob drove set_variation on every engine and sampler_scan on top of it
in the Sampler -- two unrelated axes on one control, which is the reason
SCAN had to be printed permanently beside MELO. Variation now parks at LOOP
on a Sampler deck, the same shape as the LANE_SIZE gate beside it.

The push moves down past set_engine: samplerPart is resolved from this
tick's engine_id, and at its old position the gate would have read the
previous tick's engine.

A Sampler deck stops renewing its phrases on its own; NEW is the gesture
that asks for a fresh pair. That is the accepted price of one knob having
one honest word.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 6: The README describes the panel that now exists

**Files:**
- Modify: `host/vcv/README.md`
- Modify: `host/vcv/res/test_panel.py`

- [ ] **Step 1: Write the failing test**

Add to `host/vcv/res/test_panel.py`:

```python
def test_readme_matches_the_caption_table():
    """Every word the generator prints must be findable in the manual, and no
    retired word may still be presented as current."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "README.md"), encoding="utf-8") as f:
        readme = f.read()
    for _target, _driver, words in g.DYNAMIC_CAPTIONS:
        for word in set(words):
            check(word in readme, f"README never mentions the caption {word!r}")
    for word in ("BEND", "DIV", "MULT", "SEND", "PUSH"):
        check(word in readme, f"README never mentions the caption {word!r}")
    for stale in ("`STGS`", "`MELO` / `SCAN`", "`SUB` / `LEN`"):
        check(stale not in readme, f"README still presents the retired {stale}")
```

- [ ] **Step 2: Run test to verify it fails**

Run from `host/vcv/`: `python res/test_panel.py`

Expected failures naming the words the README has never carried, at minimum
`BEND`, `DIV`, `MULT`, `SEND`, `PUSH`, `HIT`, `DAMP`, `CHAR`, `EXCIT`,
`BRITE`, `TAIL`, `TILT`, `FEED`, `LOSS`, `VARY`, `SAT`, `CRSH`.

- [ ] **Step 3: Update the README**

Four edits in `host/vcv/README.md`:

**(a)** The Sampler section's remapped-controls table. `MELODY` is no longer a
merge, and the slash notation goes:

```markdown
| Control | Sampler caption | Sampler meaning | Range |
|---|---|---|---|
| MELODY | `SCAN` | tape-head advance; the deck's VARY job is parked at LOOP here | centre is a true dead zone; linear out to real time at three-quarters of travel, then linear up to 4×; sign is direction |
| DENSITY | `DENS` | groove density *and* grain overlap — one word because both mean sparser | 1…8, continuous; the MOTION lane modulates around it |
| SUB | `LEN` | grain length | 1 ms…42 s |
| SOURCE | `ORG` | read position in the material | full material length |
```

Add a sentence under it recording the loss: *"MELODY drove variation and scan
at once until 2026-08-03. It now drives scan alone on a Sampler deck, so the
deck's phrases stop renewing by themselves — `NEW` is the gesture that asks for
a fresh pair."*

**(b)** The SOURCE section: replace the hand-listed caption states with a
pointer to the generator, and add the four other engine-dependent VOICE
captions, since they are new information the manual has never carried:

```markdown
## Engine-dependent captions

Six controls change their caption with the deck's `ENG`. The words live in
`DYNAMIC_CAPTIONS` in `res/gen_panel.py` and reach both the SVG and Rack from
there — the C++ holds no caption word at all.

| Control | Synth | Sampler | Wave | Body | BBD |
|---|---|---|---|---|---|
| MELODY | `VARY` | `SCAN` | `VARY` | `VARY` | `VARY` |
| ATTACK | `ATK` | `ATK` | `ATK` | `HIT` | — (`BEND` occupies the slot) |
| DECAY | `DEC` | `DEC` | `DEC` | `DAMP` | `TAIL` |
| RES | `RES` | `RES` | `RES` | `CHAR` | `TILT` |
| SUB | `SUB` | `LEN` | `SUB` | `EXCIT` | `FEED` |
| FILT | `FILT` | `FILT` | `FILT` | `BRITE` | `LOSS` |
| SOURCE | `TIMB` | `ORG` | `FRAME` | `MATL` | `DRIVE` |

The GRIT mode pad captions itself the same way, from its own value rather than
from `ENG`: `SAT` for Drive, `CRSH` for Reduce.

`REC` is drawn only on a Sampler deck; it has never done anything on the other
four.
```

**(c)** The BBD section's "BBD PITCH and tape TIME surface" subsection — both
its heading and its body name renamed controls. Rename the heading to **"BBD
BEND and the tape multiplier"**, replace `PITCH` with `BEND` throughout it, and
replace the FX-row line with:

```markdown
The FX bottom row is `LINK MULT GRIT COMP`; it contains no BBD control.
`DIV` selects the synchronized tape division. `MULT` multiplies that division
from `x0.25`, through neutral `x1`, to `x4`; its intentional 30 ms slew gives
smooth tape/Doppler motion. At the longest divisions, the existing delay-buffer
limit still clamps the absolute delay.
```

Keep the existing `STGS` migration sentence — `test_bbd_pitch_and_tape_time_
user_documentation` still requires it — but update the word it migrates *to*:
*"the visible `STGS` label is gone and its faceplate caption changes with the
selected engine, reading `BEND` on a BBD deck."*

**(d)** The BBD control-mapping paragraph, which currently spells the four
renamed controls out in prose. Give each its panel word:
`DECAY` → `TAIL`, `RES` → `TILT`, `SUB` → `FEED`, `FILT` → `LOSS`.

- [ ] **Step 4: Fix the two existing README tests the renames break**

`test_bbd_pitch_and_tape_time_user_documentation` asserts `"BBD PITCH" in
readme` and `"TIME" in readme`. Both now describe a panel that no longer
exists:

```python
    check("BBD BEND" in readme, "README omits the BBD BEND faceplate slot")
    check("Freeze Attack" in readme, "README omits menu-only BBD Freeze Attack")
    check("MULT" in readme and "x0.25" in readme and "x4" in readme,
          "README omits the tape multiplier")
```

Run the suite and repair whatever else the README edits break, guided by the
failure text — `test_source_and_detune_user_documentation` and
`test_source_and_detune_documentation_has_no_legacy_surface_contract` both read
the SOURCE section that step 3(b) rewrites.

- [ ] **Step 5: Run the suite**

Run from `host/vcv/`: `python res/test_panel.py`

Expected: `PASS -- panel guards ok`.

- [ ] **Step 6: Prove the guard can fail**

Temporarily delete the word `EXCIT` from the README, run
`python res/test_panel.py`, and confirm `README never mentions the caption
'EXCIT'`. Restore it and re-run; confirm `PASS`.

- [ ] **Step 7: Final full check**

Run from `host/vcv/`:

```bash
python res/gen_panel.py
python res/test_panel.py
./build-local.sh
```

Then from the repo root, confirm the shared engine is untouched:

```bash
git diff --stat HEAD~5 -- engine/
```

Expected: empty output. No task in this plan changes engine code; if this
prints anything, something went wrong and must be reported before committing.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/README.md host/vcv/res/test_panel.py
git commit -F - <<'EOF'
docs(vcv): the manual describes the panel that exists

The README documented ATK DEC RES SUB FILT as though they meant one thing,
named a BBD PITCH slot that is now BEND, and carried a tape TIME multiplier
that is now MULT. The engine-dependent captions get a table of their own --
seventeen words the manual had never carried, because until now they were
not on the plate either.

A test holds the two together: every word the generator prints must be
findable in the manual, and no retired word may still be presented as
current.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Notes for the implementer

**The engine is not in scope.** Every change lives in `host/vcv/`. If a task
seems to need an `engine/` edit, stop and report — it means the plan is wrong,
not that the engine needs changing.

**`compact_cpp` strips whitespace** before comparing, which is why the guard
strings in the tests above look like `caseREC_A:returnroundedEngineState(m,ENGINE_A)==1;`.
Write the C++ readably; the helper handles the rest. `cpp_scope(cpp, header)`
returns the brace-balanced body that follows a declaration line, or `None`.

**The test runner is not pytest.** `check(cond, msg)` appends to a module-level
`FAILS` list and `main()` runs every `test_*` in sorted order, so one failing
assertion does not stop the others — a single run shows you everything at once.
There is no way to run one test in isolation; run the file.

**If a caption geometry test fails**, do not move a coordinate to satisfy it.
Report the word and the overrun. The captions in this plan are at most five
characters, matching the longest word already on the plate (`FRAME`, `SMEAR`,
`SHUFL`), so a genuine overrun would mean the existing layout was already at
its limit — a finding worth surfacing, not papering over.
