# VCV BBD PITCH and FLUX TIME Surface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move BBD PITCH into the engine-aware VOICE ATK slot, replace the obsolete FX STGS control with tape TIME, and preserve every existing VCV patch parameter id and value.

**Architecture:** Split generator data into persistent parameter order, runtime widgets, and static SVG controls. Rack keeps separate ATTACK and STAGES parameter widgets at one coordinate and switches their visibility from the rounded ENG parameter, while a new trailing FLUXTIME pair routes through the existing `FXT_FLUX_TIME` path. Source-contract tests in `test_panel.py` pin the Rack-only behavior without introducing Rack SDK dependencies into desktop unit tests.

**Tech Stack:** Python 3 panel generator/contracts, C++17 VCV Rack plugin, generated SVG/C++ header, CMake/CTest, Git Bash `build-local.sh`.

## Global Constraints

- Scope is VCV Rack panel and host wiring only; do not change the Daisy Seed hardware panel.
- Keep `PART_STRIDE == 23`.
- Keep every existing ParamId through `DRIVE_B` at its current numeric id.
- Append `FLUXTIME_A` and `FLUXTIME_B` after `DRIVE_B`; do not reuse `STAGES_A/B`.
- Keep `ATTACK_A/B` and `STAGES_A/B` ranges and saved values unchanged.
- Set both Rack configuration and init snapshot defaults for `FLUXTIME_A/B` to exactly `0.5f`.
- Use the rounded Rack `ENGINE_A/B` parameter; value `4` alone means BBD. Do not use `inst.engine_id()` for UI or menu state.
- The module-browser preview is the default Synth surface: visible `ATK`, hidden `PITCH` widget.
- The generated static SVG must contain `ATK` and `TIME`, and no visible `STGS` or overlapping static `PITCH`.
- The only permitted exact runtime parameter overlaps are `ATTACK_A`/`STAGES_A` and `ATTACK_B`/`STAGES_B`.
- Keep RATE as the synchronized division and map TIME through the existing `tape_time_mult()` curve (`0 -> x0.25`, `0.5 -> x1`, `1 -> x4`) and existing 30 ms slew.
- No JSON migration, new DSP setter, tape buffer change, or reassignment of an existing parameter id.

---

## File Structure

- `host/vcv/res/gen_panel.py`: owns persistent ParamId order, runtime widget geometry, static SVG membership, and generated header/SVG output.
- `host/vcv/res/test_panel.py`: owns schema, geometry, generated-artifact, routing, visibility, caption, context-menu, and init-default contracts.
- `host/vcv/src/generated_panel.hpp`: generated ParamId and runtime control tables; never edit by hand.
- `host/vcv/res/Spotymod.svg`: generated default-Synth preview; never edit by hand.
- `host/vcv/src/init_patch.hpp`: append-only factory defaults indexed by ParamId.
- `host/vcv/src/Spotymod.cpp`: parameter quantities/configuration, control-tick routing, dynamic caption, overlapping-widget visibility, and BBD Freeze Attack menu access.
- `host/vcv/README.md`: documents the final BBD VOICE and tape FX surface.

---

### Task 1: Separate persistent, runtime, and static panel collections

**Files:**
- Modify: `host/vcv/res/test_panel.py:29-103,181-228,260-305,530-553,1250-1318`
- Modify: `host/vcv/res/gen_panel.py:190-205,381-453,700-709,775-792`
- Modify: `host/vcv/src/init_patch.hpp:3-20,103-108`
- Regenerate: `host/vcv/src/generated_panel.hpp`
- Regenerate: `host/vcv/res/Spotymod.svg`

**Interfaces:**
- Produces generator collections `PANEL_PARAMS`, `APPENDED_PANEL_PARAMS`, `RUNTIME_PANEL_PARAMS`, `STATIC_PANEL_PARAMS`, `HIDDEN_PARAMS`, and `PARAMS`.
- Produces trailing ids `FLUXTIME_A` and `FLUXTIME_B` and runtime `PanelCtl` rows with tips `Tape Time`.
- Preserves `PARAMS[:-2]` byte-for-byte in semantic order and `PART_STRIDE == 23`.
- Later tasks consume `FLUXTIME_A/B`, moved `STAGES_A/B` geometry, and both overlapping runtime table rows.

- [ ] **Step 1: Write failing schema, geometry, static-render, and init-default contracts**

Update `ctl()` to search `g.RUNTIME_PANEL_PARAMS`. In the existing `PARAM_ORDER`
literal, append `FLUXTIME_A` and `FLUXTIME_B` immediately after `DRIVE_B`. In
the existing `PARAM_TIPS` literal, replace the two `STGS` entries with two
`BBD Pitch` entries, then append two `Tape Time` entries after `Drive B`:

```python
PARAM_ORDER[-4:] == [
    'DRIVE_A', 'DRIVE_B', 'FLUXTIME_A', 'FLUXTIME_B'
]

PARAM_TIPS[71:75] == ['LINK', 'LINK', 'BBD Pitch', 'BBD Pitch']
PARAM_TIPS[-6:] == [
    'Detune A', 'Detune B', 'Drive A', 'Drive B',
    'Tape Time', 'Tape Time',
]
```

Add exact collection/order contracts:

```python
def test_bbd_pitch_flux_time_collections():
    persistent = [c.enum for c in g.PARAMS]
    runtime = [c.enum for c in g.RUNTIME_PANEL_PARAMS]
    static = [c.enum for c in g.STATIC_PANEL_PARAMS]
    check(persistent[-7:] == [
        'SHUFFLE', 'DETUNE_A', 'DETUNE_B', 'DRIVE_A', 'DRIVE_B',
        'FLUXTIME_A', 'FLUXTIME_B'
    ], "FLUXTIME must follow the old hidden tail")
    check(persistent[-2:] == ['FLUXTIME_A', 'FLUXTIME_B'],
          "FLUXTIME ids are not the trailing pair")
    check(all(e in runtime for e in ('STAGES_A', 'STAGES_B',
                                      'FLUXTIME_A', 'FLUXTIME_B')),
          "runtime table lacks PITCH or TIME widgets")
    check('STAGES_A' not in static and 'STAGES_B' not in static,
          "static preview contains the BBD-only PITCH widgets")
    check(all(e in static for e in ('ATTACK_A', 'ATTACK_B',
                                     'FLUXTIME_A', 'FLUXTIME_B')),
          "static Synth preview lacks ATK or TIME")
    check(g.PARAMS == g.PANEL_PARAMS + g.HIDDEN_PARAMS
                      + g.APPENDED_PANEL_PARAMS,
          "persistent ParamId order no longer matches the declared partitions")
    check(not any(c.enum in runtime for c in g.HIDDEN_PARAMS),
          "menu-only DETUNE/DRIVE leaked into runtime widgets")
```

Audit every existing `g.PANEL_PARAMS` use in `test_panel.py`: schema partition
checks use `PANEL_PARAMS + HIDDEN_PARAMS + APPENDED_PANEL_PARAMS`; bounds,
position, kind, and overlap checks use `RUNTIME_PANEL_PARAMS`; SVG glyph and
render-order checks use `STATIC_PANEL_PARAMS`. Ring/section tests that address
legacy controls by name may continue using `PANEL_PARAMS`.

Replace `LOWER_A` entries and add overlap/static checks:

```python
'STAGES_A': (9.25, 77.30),
'FLUXTIME_A': (54.75, 89.40),
```

```python
for suffix in ('_A', '_B'):
    attack, pitch = ctl('ATTACK' + suffix), ctl('STAGES' + suffix)
    check((attack.x, attack.y) == (pitch.x, pitch.y),
          f"{suffix}: ATK/PITCH do not share coordinates")
    time = ctl('FLUXTIME' + suffix)
    check(time.label == 'TIME' and time.tip == 'Tape Time',
          f"{suffix}: TIME caption/tooltip drifted")

svg = g.svg()
check('>STGS</text>' not in svg, "static SVG still exposes STGS")
check(svg.count('>ATK</text>') == 2, "static preview must show two ATK captions")
check(svg.count('>TIME</text>') == 2, "static preview must show two TIME captions")
check('>PITCH</text>' not in svg, "static preview must not overlay PITCH on ATK")
```

Change `test_no_overlap()` to iterate `g.RUNTIME_PANEL_PARAMS`, skip only exact-id pairs `{ATTACK_A, STAGES_A}` and `{ATTACK_B, STAGES_B}`, and report every other collision.

Extend the init snapshot expectation and add:

```python
check(PARAM_ORDER[-4:] == ['DRIVE_A', 'DRIVE_B', 'FLUXTIME_A', 'FLUXTIME_B'],
      "init snapshot tail ids drifted")
check(actual[-4:] == [0.200000003, 0.200000003, 0.5, 0.5],
      "init snapshot tail defaults drifted")
```

- [ ] **Step 2: Run the panel contracts and confirm RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: FAIL because `RUNTIME_PANEL_PARAMS`, `STATIC_PANEL_PARAMS`, and `FLUXTIME_A/B` do not yet exist, STAGES is still in the FX slot, and the snapshot lacks the TIME defaults.

- [ ] **Step 3: Implement the three generator views without changing legacy ids**

Keep the current legacy visible list in `PANEL_PARAMS`, but move the STAGES widgets to the ATTACK coordinates and rename their runtime caption/tip:

```python
Ctl("STAGES_A", SMKNOB, VOICE_X[0],     ROW_V1, "PITCH", "BBD Pitch"),
Ctl("STAGES_B", SMKNOB, W - VOICE_X[0], ROW_V1, "PITCH", "BBD Pitch"),
```

Define TIME separately so its widgets can be runtime-visible while its ids remain after the old hidden tail:

```python
APPENDED_PANEL_PARAMS = [
    Ctl("FLUXTIME_A", SMKNOB, FX_BOT[1],     ROW_V2, "TIME", "Tape Time"),
    Ctl("FLUXTIME_B", SMKNOB, W - FX_BOT[1], ROW_V2, "TIME", "Tape Time"),
]

RUNTIME_PANEL_PARAMS = PANEL_PARAMS + APPENDED_PANEL_PARAMS
STATIC_PANEL_PARAMS = [
    c for c in RUNTIME_PANEL_PARAMS
    if c.enum not in ("STAGES_A", "STAGES_B")
]
PARAMS = PANEL_PARAMS + HIDDEN_PARAMS + APPENDED_PANEL_PARAMS
```

Use `STATIC_PANEL_PARAMS` in the SVG glyph/label loop and `RUNTIME_PANEL_PARAMS` for `emit_table("kParamCtls", ...)`. Keep `emit_enum("ParamId", PARAMS, ...)` unchanged.

Append to `kInitParamDefaults`:

```cpp
     0.500000000f, // FLUXTIME_A = neutral x1
     0.500000000f, // FLUXTIME_B = neutral x1
```

Update generator comments so FX bottom is `LINK TIME | GRIT COMP` and STAGES is documented as the BBD-only PITCH widget overlapping ATTACK at runtime.

- [ ] **Step 4: Regenerate and run the focused contracts**

Run:

```powershell
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
git diff --check
```

Expected: generator reports 86 params, panel guards PASS, generated SVG has TIME and no STGS, generated header contains both ATK/STAGES overlap rows and the trailing TIME ids.

- [ ] **Step 5: Commit the generator/schema slice**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp host/vcv/src/init_patch.hpp
git commit -m "feat(vcv): add compatible BBD pitch and tape time controls"
```

---

### Task 2: Configure and route tape TIME through the existing FX target

**Files:**
- Modify: `host/vcv/res/test_panel.py` near existing quantity, init, and routing source contracts
- Modify: `host/vcv/src/Spotymod.cpp:42-66,234-265,419-440`

**Interfaces:**
- Consumes `FLUXTIME_A/B` and `initParamDefault()` from Task 1.
- Produces `FluxTimeQuantity`, configured `Tape Time` ParamQuantities, and per-control-tick calls to `set_fx_target_base(part, FXT_FLUX_TIME, value)`.
- Reuses `spky::tape_time_mult(float)` from `engine/fx/tape_echo.h`; no DSP API changes.

- [ ] **Step 1: Write failing display, configuration, and routing source contracts**

Add a helper that scopes `Spotymod::configControls()` and `Spotymod::pushParams()`, then require these exact semantic fragments:

```python
def flux_time_wiring_issues(cpp):
    issues = []
    quantity = cpp_scope(cpp, "struct FluxTimeQuantity : ParamQuantity")
    config = cpp_scope(cpp, "void configControls()")
    push = cpp_scope(cpp, "void pushParams()")
    if quantity is None or "spky::tape_time_mult(getValue())" not in quantity:
        issues.append("Tape Time display does not reuse tape_time_mult")
    if config is None or config.count("configParam<FluxTimeQuantity>") != 1:
        issues.append("FLUXTIME is not configured through FluxTimeQuantity")
    expected = """
inst.set_fx_target_base(p, spky::FXT_FLUX_TIME,
    params[p ? FLUXTIME_B : FLUXTIME_A].getValue());
"""
    if push is None or compact_cpp(expected) not in compact_cpp(push):
        issues.append("FLUXTIME does not route to FXT_FLUX_TIME")
    if push and "pp(FLUXTIME_A, p)" in push:
        issues.append("trailing FLUXTIME ids are incorrectly read through pp()")
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
```

Also require `configParam<FluxTimeQuantity>(c.id, 0.f, 1.f, init, lbl)` to sit under an `FLUXTIME_A/B` branch, and add mutation checks that reject wrong deck B id, wrong FX target, `pp()`, or a hard-coded default.

- [ ] **Step 2: Run the panel contracts and confirm RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: FAIL on missing `FluxTimeQuantity`, configuration, and routing.

- [ ] **Step 3: Add the minimal TIME quantity and routing**

Near `FluxRateQuantity`, add:

```cpp
struct FluxTimeQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float mult = spky::tape_time_mult(getValue());
        return std::fabs(mult - 1.f) < 0.005f
            ? "x1"
            : string::f("x%.2f", mult);
    }
};
```

In the knob branch of `configControls()` add:

```cpp
else if (c.id == FLUXTIME_A || c.id == FLUXTIME_B)
    configParam<FluxTimeQuantity>(c.id, 0.f, 1.f, init, lbl);
```

In `pushParams()`, beside FLUXRATE/FLUXFB, add:

```cpp
inst.set_fx_target_base(p, spky::FXT_FLUX_TIME,
    params[p ? FLUXTIME_B : FLUXTIME_A].getValue());
```

Use the explicit ternary because the new ids are trailing and outside `PART_STRIDE`.

- [ ] **Step 4: Run focused and desktop verification**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: panel guards PASS, build succeeds, all desktop tests pass including the existing `tape_time_mult` endpoint tests.

- [ ] **Step 5: Commit the TIME wiring slice**

```powershell
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -m "feat(vcv): route tape time from the FX surface"
```

---

### Task 3: Make the shared VOICE slot engine-aware and expose Freeze Attack

**Files:**
- Modify: `host/vcv/res/test_panel.py` near `source_caption_wiring_issues()` and context-menu contracts
- Modify: `host/vcv/src/Spotymod.cpp:1181-1241,1288-1320,1343-1380`

**Interfaces:**
- Consumes overlapping ATTACK/STAGES runtime table rows from Task 1.
- Produces `roundedEngineState(const Spotymod*, int)`, `isBbdSelected(const Spotymod*, int)`, `EngineExclusiveTrimpot`, and engine-aware VOICE captions.
- Context-menu entries bind the existing matching `ATTACK_A/B` `ParamQuantity`; no new parameter state.

- [ ] **Step 1: Write failing shared-state, visibility, caption, and menu contracts**

Add `attack_pitch_wiring_issues(cpp)` that requires:

```cpp
static int roundedEngineState(const Spotymod* module, int engineId) {
    return module
        ? static_cast<int>(std::round(module->params[engineId].getValue()))
        : 0;
}

static bool isBbdSelected(const Spotymod* module, int engineId) {
    return roundedEngineState(module, engineId) == 4;
}
```

The contract must reject `inst.engine_id()` anywhere in `PanelText`, `EngineExclusiveTrimpot`, or `appendContextMenu()`. It must also require:

```python
for attack_id, stages_id, engine_id in (
        ('ATTACK_A', 'STAGES_A', 'ENGINE_A'),
        ('ATTACK_B', 'STAGES_B', 'ENGINE_B')):
    # both ids are created as EngineExclusiveTrimpot at the same generated pos
    # attack has bbdOnly=false; stages has bbdOnly=true
```

Require the generic caption loop to skip `SOURCE_A/B`, `ATTACK_A/B`, and `STAGES_A/B`; require exactly one caption call per deck and mapping `BBD -> PITCH`, otherwise `ATK`. Require menu strings `BBD A — Freeze Attack` and `BBD B — Freeze Attack`, each guarded by its corresponding `isBbdSelected(m, ENGINE_*)` and bound to matching `ATTACK_*`.

Add representative mutation checks for ENG rounding removed, `ENGINE_B` changed to `ENGINE_A`, `STAGES_B` changed to `ATTACK_B`, preview fallback changed from Synth to BBD, both overlapping widgets made visible, and menu quantity bound to the wrong deck.

- [ ] **Step 2: Run the panel contracts and confirm RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: FAIL because both shared-position widgets are ordinary `Trimpot`s, captions are static/generic, and Freeze Attack is absent.

- [ ] **Step 3: Centralize rounded engine selection and implement exclusive widgets**

Add the two helpers above near `sourceCaption()`. Define:

```cpp
struct EngineExclusiveTrimpot : Trimpot {
    Spotymod* spotymod = nullptr;
    int engineId = ENGINE_A;
    bool bbdOnly = false;

    void step() override {
        visible = isBbdSelected(spotymod, engineId) == bbdOnly;
        Trimpot::step();
    }
};
```

In the `WK_SMKNOB/WK_KNOBI` creation branch, special-case ATTACK/STAGES. Create an `EngineExclusiveTrimpot`, set `spotymod`, `engineId`, and `bbdOnly`, then add it. ATTACK uses `bbdOnly = false`; STAGES uses `true`. Other small knobs keep the current `Trimpot` path.

Do not merely change opacity or draw state: `visible = false` is required so the hidden widget is absent from pointer hit testing, scrolling, and tooltips.

- [ ] **Step 4: Draw exactly one ATK/PITCH caption per deck**

Extend the generic caption skip condition to skip both ATTACK and STAGES pairs. Add a helper shaped like the existing SOURCE helper:

```cpp
auto attackPitchCaptionAt = [&](int attackId, int engineId) {
    const PanelCtl* attack = nullptr;
    for (const auto& c : kParamCtls)
        if (c.id == attackId) { attack = &c; break; }
    if (!attack) return;
    nvgTextAlign(args.vg, alignOf(attack->anchor) | NVG_ALIGN_BASELINE);
    text(attack->lbl.x, attack->lbl.y, attack->lblSize,
         col(attack->lblRgb), isBbdSelected(module, engineId) ? "PITCH" : "ATK");
};
attackPitchCaptionAt(ATTACK_A, ENGINE_A);
attackPitchCaptionAt(ATTACK_B, ENGINE_B);
```

Refactor `sourceCaptionAt()` to call `roundedEngineState(module, engineId)` so visibility, both dynamic caption systems, and context-menu eligibility share one interpretation.

- [ ] **Step 5: Add conditional Freeze Attack menu sliders**

After the existing DRIVE menu block, add:

```cpp
if (isBbdSelected(m, ENGINE_A)) {
    menu->addChild(createSubmenuItem("BBD A — Freeze Attack", "", [m](Menu* sub) {
        sub->addChild(new ParamMenuSlider(m->getParamQuantity(ATTACK_A)));
    }));
}
if (isBbdSelected(m, ENGINE_B)) {
    menu->addChild(createSubmenuItem("BBD B — Freeze Attack", "", [m](Menu* sub) {
        sub->addChild(new ParamMenuSlider(m->getParamQuantity(ATTACK_B)));
    }));
}
```

Keep the existing ATTACK parameters, DSP setters, defaults, and values unchanged.

- [ ] **Step 6: Run contracts and build the VCV plugin**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build
ctest --test-dir build --output-on-failure
```

Then from Git Bash:

```bash
cd host/vcv
./build-local.sh
```

Expected: all contracts/tests pass and the Rack plugin builds. If Rack SDK compilation rejects the widget lifecycle call order or visibility API, apply `superpowers:systematic-debugging`, preserve the public behavior above, and add the corrected source shape to the contract before proceeding.

- [ ] **Step 7: Commit the engine-aware UI slice**

```powershell
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -m "feat(vcv): switch the voice slot between attack and BBD pitch"
```

---

### Task 4: Align documentation, install locally, and verify the full surface

**Files:**
- Modify: `host/vcv/README.md:300-390`
- Verify generated: `host/vcv/res/Spotymod.svg`
- Verify generated: `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes all implementation from Tasks 1–3.
- Produces final user documentation and a locally installed VCV plugin archive/DLL.

- [ ] **Step 1: Add a failing documentation contract**

Extend `test_panel.py`'s README checks to require all of these concepts and forbid stale panel claims:

```python
check("BBD PITCH" in readme, "README omits the BBD PITCH faceplate slot")
check("Freeze Attack" in readme, "README omits menu-only BBD Freeze Attack")
check("TIME" in readme and "x0.25" in readme and "x4" in readme,
      "README omits the tape TIME multiplier")
check("STGS" not in readme, "README still presents STGS as a visible control")
```

- [ ] **Step 2: Run the panel contract and confirm RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: FAIL on stale README surface wording.

- [ ] **Step 3: Update the VCV README**

Document the final controls explicitly:

- BBD uses `PITCH` in the VOICE upper-left slot; other engines use `ATK` there.
- BBD Freeze Attack remains available as `BBD A/B — Freeze Attack` in the module context menu.
- FX bottom row is `LINK TIME GRIT COMP`; no BBD control remains in FX.
- `RATE` selects the synchronized tape division; `TIME` moves from `x0.25` through neutral `x1` to `x4` with intentional slew/Doppler and existing longest-delay buffer clamp.
- Existing `STAGES_A/B` patch state remains BBD pitch state; the visible word `STGS` is gone.

- [ ] **Step 4: Run fresh full verification**

Run:

```powershell
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
git status --short
```

Expected: generator succeeds, panel guards PASS, CMake build succeeds, all CTest tests pass, no whitespace errors, and only intended files are modified.

- [ ] **Step 5: Build and install the local VCV plugin**

From Git Bash run:

```bash
cd host/vcv
./build-local.sh install
```

Expected: the plugin archive builds and the install script copies the current plugin into the local Rack plugins directory. Record the exact archive and installed plugin paths from the script output.

- [ ] **Step 6: Manually verify both decks in Rack**

Restart Rack, insert a fresh Spotymod, and verify:

1. Synth/Sampler/Wave/Body show `ATK`; BBD shows `PITCH` in the same VOICE slot.
2. Switching engines preserves independent ATK and PITCH values.
3. FX always shows `TIME`, never `STGS`.
4. TIME centre is `x1`; endpoints display `x0.25` and `x4` and move the tape smoothly.
5. Only a BBD deck exposes its matching Freeze Attack menu slider.
6. In BBD, drag/scroll changes only STAGES and tooltip says `BBD Pitch`.
7. Outside BBD, drag/scroll changes only ATTACK and the ordinary attack tooltip returns.

If Rack cannot be launched automatically, report these seven checks as pending user listening/UI verification; do not mark them mechanically verified.

- [ ] **Step 7: Commit documentation and any final contract refinements**

```powershell
git add host/vcv/README.md host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git commit -m "docs(vcv): describe BBD pitch and tape time surface"
```

- [ ] **Step 8: Request final code review**

Dispatch a fresh GPT-5.6-sol reviewer using `superpowers:requesting-code-review`. The reviewer must compare the complete diff against `docs/superpowers/specs/2026-08-02-vcv-bbd-pitch-flux-time-surface-design.md`, inspect patch compatibility and hidden-widget interaction semantics, and report file/line evidence for every issue. Resolve findings through `superpowers:receiving-code-review`, rerun Step 4 and the install command, then present the verified commits and any genuinely manual Rack checks still outstanding.
