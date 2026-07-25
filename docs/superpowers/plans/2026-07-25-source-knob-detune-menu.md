# SOURCE Knob and Detune Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn each visible DTUN control into the direct SOURCE base for Synth TIMB, Wave FRAME, and Sampler ORG, while moving independent 0–35 ct per-part detune controls into the VCV context menu with a 6 ct default.

**Architecture:** Keep SOURCE as one normalized per-part parameter routed through the existing `Part::_base[LANE_SOURCE]` path, so the existing lane and MOD equations continue to work. Add two widgetless Rack parameters for persistent Detune A/B menu controls, and change the shared melodic-engine implementation so their configured spread is independent of SOURCE. Make the panel caption a live view of the corresponding ENG parameter rather than adding more static alias text.

**Tech Stack:** C++17 portable DSP engine, VCV Rack 2 C++ API, Python panel generator and guard tests, doctest, CMake/CTest, GNU Make/MinGW local Rack build.

## Global Constraints

- SOURCE range is normalized `0..1`; both parts initialize to `0.5`.
- The visible control means Synth `TIMB`, Wave `FRAME`, and Sampler `ORG`.
- The existing SOURCE lane remains active and `MOD` continues to control its modulation depth.
- Detune A/B are independent widgetless Rack parameters with a displayed range of `0..35 ct` and a default of exactly `6 ct`.
- A `6 ct` setting always means a total oscillator spread of `6 ct`, split approximately `+3 ct` / `-3 ct`, regardless of SOURCE.
- Detune reaches Synth and Wave but not Sampler.
- Exactly one live caption is visible per SOURCE knob and follows the selected ENG parameter immediately.
- The stable Rack parameter name is `SOURCE A/B`; its tooltip explains all three meanings.
- No legacy patch/automation migration, separate per-engine SOURCE memory, soft takeover, detune MIDI-learn surface, or hardware detune gesture.
- Generated files must be regenerated from `host/vcv/res/gen_panel.py`; never edit `generated_panel.hpp` or `Spotymod.svg` by hand.

---

## File Structure

- `engine/synth/synth_engine.h` — define detune as a configured total spread and expose a test observer for the spread applied at the control tick.
- `engine/synth/synth_engine.cpp` — remove the SOURCE-squared multiplier before detune is pushed to voices.
- `tests/synth_engine_contract.h` — shared Synth/Wave contract proving SOURCE changes cannot change detune.
- `tests/test_synth_engine.cpp`, `tests/test_wave_engine.cpp`, `tests/test_part.cpp`, `tests/test_sampler_part.cpp` — invoke the shared contract and update observer terminology.
- `host/vcv/res/gen_panel.py` — separate visible panel controls from widgetless parameters, rename the physical control to SOURCE, and remove the static ORG alias at that position.
- `host/vcv/res/test_panel.py` — pin parameter order, hidden/visible separation, geometry, initialization, routing, menu configuration, and dynamic caption behavior.
- `host/vcv/src/generated_panel.hpp`, `host/vcv/res/Spotymod.svg` — regenerated artifacts.
- `host/vcv/src/init_patch.hpp` — SOURCE A/B defaults at `0.5`; appended Detune A/B defaults at `6 / 35`.
- `host/vcv/src/Spotymod.cpp` — configure/push hidden detune parameters, route SOURCE for every engine, add menu controls/reset, and render the live caption.
- `host/vcv/README.md`, `docs/roadmap.md` — describe the final live control semantics without claiming menu automation.

---

### Task 1: Make melodic detune independent of SOURCE

**Files:**
- Modify: `engine/synth/synth_engine.h`
- Modify: `engine/synth/synth_engine.cpp`
- Modify: `tests/synth_engine_contract.h`
- Modify: `tests/test_synth_engine.cpp`
- Modify: `tests/test_wave_engine.cpp`
- Modify: `tests/test_part.cpp`
- Modify: `tests/test_sampler_part.cpp`

**Interfaces:**
- Consumes: `SynthEngineT<OscT>::set_detune(float normalized)` with normalized `0..1`.
- Produces: `float detune_spread_ct() const` for the configured total spread and `float applied_detune_ct() const` for the value last pushed to voices.
- Invariant: both accessors report `normalized * kDetuneCeilCt`; SOURCE target values do not alter `applied_detune_ct()`.

- [ ] **Step 1: Add the failing shared Synth/Wave detune contract**

Add this helper to `tests/synth_engine_contract.h`:

```cpp
template <class EngineT>
void contract_detune_is_independent_of_source() {
    using namespace spky_contract;

    EngineT e;
    e.init(48000.f);
    e.set_detune(6.f / EngineT::kDetuneCeilCt);

    feed(e, 0.5f, 0.f);
    render_l(e, EngineT::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(6.f));

    feed(e, 0.5f, 1.f);
    render_l(e, EngineT::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(6.f));
    CHECK(e.detune_spread_ct() == doctest::Approx(6.f));
}
```

Call `contract_detune_is_independent_of_source<SynthEngine>();` from the Synth shared-contract test and `contract_detune_is_independent_of_source<WaveEngine>();` from the Wave shared-contract test.

- [ ] **Step 2: Run the native test target and verify the new contract fails**

Run:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R spky_tests --output-on-failure
```

Expected: compilation fails because `applied_detune_ct()` and `detune_spread_ct()` do not exist.

- [ ] **Step 3: Implement constant total-spread semantics**

In `engine/synth/synth_engine.h`:

```cpp
float detune_spread_ct() const { return _detune_spread_ct; }
float applied_detune_ct() const { return _applied_detune_ct; }
```

Replace `_detune_max_ct` with:

```cpp
float _detune_spread_ct = 18.f;
float _applied_detune_ct = 18.f;
```

In `SynthEngineT<OscT>::_update_control()` replace the SOURCE-dependent expression with:

```cpp
const float timbre = _targets[LANE_SOURCE];
_applied_detune_ct = _detune_spread_ct;
```

Push `_applied_detune_ct` to every voice. In `set_detune(float n)`, store:

```cpp
_detune_spread_ct = clampf(n, 0.f, 1.f) * kDetuneCeilCt;
```

Update comments so TIMBRE controls oscillator morph/frame only and detune is an independent symmetric spread.

- [ ] **Step 4: Update existing observer assertions**

Change existing `detune_max_ct()` checks in `tests/test_part.cpp` and `tests/test_sampler_part.cpp` to `detune_spread_ct()` without weakening their expected normalized-to-35-cent assertions.

- [ ] **Step 5: Run the native suite**

Run:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R spky_tests --output-on-failure
```

Expected: `spky_tests` passes, including the shared contract for both `SynthEngine` and `WaveEngine`.

- [ ] **Step 6: Commit the independent-detune engine change**

```powershell
git add engine/synth/synth_engine.h engine/synth/synth_engine.cpp tests/synth_engine_contract.h tests/test_synth_engine.cpp tests/test_wave_engine.cpp tests/test_part.cpp tests/test_sampler_part.cpp
git commit -m "feat(synth): decouple detune from source"
```

---

### Task 2: Generate visible SOURCE and widgetless Detune parameters

**Files:**
- Modify: `host/vcv/res/gen_panel.py`
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/src/init_patch.hpp`
- Regenerate: `host/vcv/src/generated_panel.hpp`
- Regenerate: `host/vcv/res/Spotymod.svg`

**Interfaces:**
- Produces visible `SOURCE_A` and `SOURCE_B` IDs in the old physical DTUN slots inside the unchanged 23-parameter part stride.
- Produces trailing widgetless `DETUNE_A` and `DETUNE_B` IDs after `SHUFFLE`.
- Produces `PANEL_PARAMS` containing only controls that receive widgets and `PARAMS = PANEL_PARAMS + HIDDEN_PARAMS` as the complete enum order.
- Defaults: SOURCE A/B `0.5`; Detune A/B `6.f / 35.f`.

- [ ] **Step 1: Change the panel-order guards first**

In `host/vcv/res/test_panel.py`, change the old strided names from `DETUNE_A/B` to `SOURCE_A/B`, append `DETUNE_A/B` after `SHUFFLE`, and change their tips to:

```python
'SOURCE',  # visible SOURCE_A / SOURCE_B slots
'Detune A', 'Detune B',  # trailing hidden params
```

Add a guard equivalent to:

```python
def test_source_and_hidden_detune_partition():
    visible = [c.enum for c in g.PANEL_PARAMS]
    hidden = [c.enum for c in g.HIDDEN_PARAMS]
    check("SOURCE_A" in visible and "SOURCE_B" in visible,
          "SOURCE controls must stay visible")
    check(hidden == ["DETUNE_A", "DETUNE_B"],
          f"hidden params are {hidden!r}")
    check(not any(e in visible for e in hidden),
          "widgetless detune leaked into panel controls")
    check([c.enum for c in g.PARAMS] == visible + hidden,
          "complete ParamId order must end with hidden detune")
```

Update geometry helpers to inspect `g.PANEL_PARAMS`, not widgetless parameters.

- [ ] **Step 2: Run the panel guard and verify it fails**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: failure because `SOURCE_A/B`, `PANEL_PARAMS`, and `HIDDEN_PARAMS` do not exist.

- [ ] **Step 3: Split the generator's parameter model**

In `part_controls()`, replace the visible tuple:

```python
("DETUNE", "DTUN", VOICE_X[2], ROW_V2)
```

with:

```python
("SOURCE", "TIMB", VOICE_X[2], ROW_V2)
```

Give the visible control the stable tip `SOURCE`. Define:

```python
PANEL_PARAMS = PART_A + PART_B + SHARED + [
    # existing appended visible controls, ending with SHUFFLE
]
HIDDEN_PARAMS = [
    Ctl("DETUNE_A", SMKNOB, 0.0, 0.0, "", "Detune A"),
    Ctl("DETUNE_B", SMKNOB, 0.0, 0.0, "", "Detune B"),
]
PARAMS = PANEL_PARAMS + HIDDEN_PARAMS
```

Use `PANEL_PARAMS` for SVG glyphs, labels, geometry, and `kParamCtls`. Continue using complete `PARAMS` only for the `ParamId` enum and `NUM_PARAMS`.

Remove `("DETUNE", "ORG")` from `SAMPLER_LBL`; ORG will be drawn dynamically at runtime. Preserve the existing MELO/SCAN and SUB/LEN aliases.

- [ ] **Step 4: Update initialization defaults**

In `host/vcv/src/init_patch.hpp`, replace the two former visible detune snapshot values with:

```cpp
0.500000000f, // SOURCE_A
0.500000000f, // SOURCE_B
```

Append:

```cpp
0.171428576f, // DETUNE_A = 6 / 35
0.171428576f, // DETUNE_B = 6 / 35
```

Update the exact expected list in `test_sampler_preset_init_snapshot()` to match the new complete enum order.

- [ ] **Step 5: Regenerate the artifacts**

Run:

```powershell
python host/vcv/res/gen_panel.py
```

Expected: `generated_panel.hpp` contains visible `SOURCE_A/B`, trailing `DETUNE_A/B`, `NUM_PARAMS` includes both hidden parameters, and `kParamCtls` contains no `DETUNE_A/B` rows.

- [ ] **Step 6: Run generator and panel checks**

Run:

```powershell
python host/vcv/res/test_panel.py
git diff --check
```

Expected: panel guard passes and no whitespace errors are reported.

- [ ] **Step 7: Commit the generated parameter model**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/init_patch.hpp host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg
git commit -m "feat(vcv): replace dtun knobs with source controls"
```

---

### Task 3: Route SOURCE and add persistent Detune menu controls

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp`
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `SOURCE_A/B`, trailing `DETUNE_A/B`, `kInitParamDefaults`.
- Produces: `DetuneQuantity`, normalized `0..1` with cents display; independent Detune A/B context-menu sliders and reset actions.
- Routing: `set_target_base(p, LANE_SOURCE, pp(SOURCE_A, p))` for every engine; `set_voice_detune(p, params[p ? DETUNE_B : DETUNE_A].getValue())`.

- [ ] **Step 1: Add failing host-wiring guards**

Extend `host/vcv/res/test_panel.py` to require these normalized source fragments:

```cpp
inst.set_target_base(p, spky::LANE_SOURCE, pp(SOURCE_A, p));
inst.set_voice_detune(
    p, params[p ? DETUNE_B : DETUNE_A].getValue());
```

Also require:

```cpp
configParam<DetuneQuantity>(
    DETUNE_A, 0.f, 1.f, initParamDefault(DETUNE_A), "Detune A");
configParam<DetuneQuantity>(
    DETUNE_B, 0.f, 1.f, initParamDefault(DETUNE_B), "Detune B");
```

and two menu subitems/reset actions. Reject any remaining call that feeds `pp(SOURCE_A, p)` into `set_voice_detune()` or gates SOURCE base assignment on `samplerPart`.

- [ ] **Step 2: Run the panel guard and verify it fails**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: host-wiring failures because the old DTUN routing remains and the menu quantities are absent.

- [ ] **Step 3: Add a cents-display Rack quantity**

Near the existing custom `ParamQuantity` types in `Spotymod.cpp`, add:

```cpp
static constexpr float kDefaultDetune = 6.f / spky::SynthEngine::kDetuneCeilCt;

struct DetuneQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return string::f("%.1f ct",
            getValue() * spky::SynthEngine::kDetuneCeilCt);
    }
};
```

After the visible `kParamCtls` configuration loop, configure both hidden parameters with normalized `0..1`, the exact default, and stable A/B names.

Special-case visible `SOURCE_A/B` in the same configuration path so Rack names
them `SOURCE A` and `SOURCE B`; do not expose the engine-dependent short
caption as the stable parameter name.

- [ ] **Step 4: Replace the old host routing**

In `pushParams()`:

```cpp
inst.set_voice_detune(
    p, params[p ? DETUNE_B : DETUNE_A].getValue());
```

Replace the engine-dependent SOURCE-base block with:

```cpp
inst.set_target_base(p, spky::LANE_SOURCE, pp(SOURCE_A, p));
```

Keep Sampler SIZE/LEN routing engine-dependent. Do not change PITCH activation, SCAN, overlap, or any FX route.

- [ ] **Step 5: Add menu sliders and exact reset**

Add a non-owning slider for an existing Rack parameter:

```cpp
struct ParamMenuSlider : ui::Slider {
    explicit ParamMenuSlider(ParamQuantity* pq) {
        box.size.x = 180.f;
        quantity = pq;
    }
};
```

In `appendContextMenu()`, add independent `Detune A` and `Detune B` submenus. Each submenu contains a `ParamMenuSlider` bound to `m->getParamQuantity(id)` and:

```cpp
createMenuItem("Reset to 6.0 ct", "", [m, id]() {
    m->params[id].setValue(kDefaultDetune);
});
```

Do not add custom JSON, MIDI-learn claims, or synthetic undo history.

- [ ] **Step 6: Run panel and native regression tests**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: panel guard and all CTest tests pass.

- [ ] **Step 7: Commit routing and menu behavior**

```powershell
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -m "feat(vcv): move detune to the context menu"
```

---

### Task 4: Render one live TIMB, FRAME, or ORG caption

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp`
- Modify: `host/vcv/res/gen_panel.py`
- Modify: `host/vcv/res/test_panel.py`
- Regenerate: `host/vcv/src/generated_panel.hpp`
- Regenerate: `host/vcv/res/Spotymod.svg`

**Interfaces:**
- Consumes: corresponding `ENGINE_A/B` Rack parameter value, rounded to `0`, `1`, or `2`.
- Produces: `sourceCaption(int engineState)` returning `TIMB`, `ORG`, or `FRAME`.
- Rendering rule: skip the static SOURCE row in the generic caption loop and draw exactly one resolved caption at the SOURCE control's generated label coordinates.

- [ ] **Step 1: Add failing caption-state and geometry guards**

In `host/vcv/res/test_panel.py`, require the mapping:

```python
{0: "TIMB", 1: "ORG", 2: "FRAME"}
```

Check both SOURCE label positions against all three strings using the existing monospace `text_w()` geometry helpers; require the text bounds to remain inside the VOICE group and not overlap the SUB/RES controls or labels.

Add a source guard that requires `PanelText` to hold `Spotymod* module`, skip `SOURCE_A/B` in the generic caption loop, read `module->params[ENGINE_A/B]`, and call one dynamic-caption draw per part.

- [ ] **Step 2: Run the panel guard and verify it fails**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: failures for absent engine-aware caption mapping and static `PanelText`.

- [ ] **Step 3: Make generated static output represent the default state**

Keep `TIMB` as the SOURCE label in the generated SVG preview because Synth is ENG state `0`. Do not generate `FRAME` or `ORG` as static `kPanelTexts` aliases. If `FRAME` requires the existing smaller alias size to fit, encode one shared SOURCE caption size/position in the SOURCE `PanelCtl` so all runtime states use the same collision-tested box.

Regenerate:

```powershell
python host/vcv/res/gen_panel.py
```

- [ ] **Step 4: Implement live runtime caption rendering**

Add:

```cpp
static const char* sourceCaption(int state) {
    return state == 1 ? "ORG" : state == 2 ? "FRAME" : "TIMB";
}
```

Change `PanelText` to accept and retain `Spotymod*`. In its generic parameter-caption loop, skip `SOURCE_A` and `SOURCE_B`. After that loop, find the two SOURCE `PanelCtl` rows, round the corresponding `ENGINE_A/B` parameter values, and draw exactly one `sourceCaption(state)` at each generated label coordinate.

Construct it with:

```cpp
auto* labels = new PanelText(module);
```

When `module == nullptr` in the module browser, render `TIMB` for both sides.

- [ ] **Step 5: Run caption and generation checks**

Run:

```powershell
python host/vcv/res/test_panel.py
python host/vcv/res/gen_panel.py
git diff --exit-code -- host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg
```

Expected: panel tests pass and a second generation produces no diff.

- [ ] **Step 6: Build the VCV plugin**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd /c/Users/bernd/Documents/AI/Spotykach/host/vcv && ./build-local.sh'
```

Expected: `plugin.dll` and the Spotymod distribution build successfully.

- [ ] **Step 7: Commit dynamic captions**

```powershell
git add host/vcv/src/Spotymod.cpp host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg
git commit -m "feat(vcv): show engine-aware source captions"
```

---

### Task 5: Document semantics and run the release-level verification

**Files:**
- Modify: `host/vcv/README.md`
- Modify: `docs/roadmap.md`
- Modify when intentional hashes change: `CMakeLists.txt`
- Verify: all files changed in Tasks 1–4

**Interfaces:**
- Documents SOURCE as one contextual live control and Detune as a constant menu-set spread.
- Does not promise patch compatibility, menu automation, MIDI learn, or undo behavior.

- [ ] **Step 1: Add documentation assertions to the panel guard**

Extend `host/vcv/res/test_panel.py` to read `host/vcv/README.md` and require the terms `TIMB`, `FRAME`, `ORG`, `6 ct`, and a statement that detune is independent of SOURCE. This test must fail against the current README.

- [ ] **Step 2: Run the documentation guard and verify it fails**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: failure identifying the missing SOURCE/Detune documentation.

- [ ] **Step 3: Update user-facing documentation**

In `host/vcv/README.md`, replace the old DTUN description with:

- one physical SOURCE control per part;
- Synth `TIMB`, Wave `FRAME`, Sampler `ORG`;
- SOURCE lane modulation around the knob base;
- per-part Detune A/B in the context menu;
- constant `0..35 ct` spread, default `6 ct`, independent of SOURCE.

In `docs/roadmap.md`, update the completed voice/control-surface description so it no longer says DTUN is a visible Synth/Wave control or implies TIMBRE scales detune.

- [ ] **Step 4: Run the full verification matrix**

Run:

```powershell
python host/vcv/res/test_panel.py
python host/vcv/res/test_factory_wav.py
python tools/bake_wavetables.py --check
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd /c/Users/bernd/Documents/AI/Spotykach/host/vcv && ./build-local.sh'
git diff --check
git status --short
```

Expected:

- panel and factory-WAV guards pass;
- wavetable bank is fresh;
- all four CTest entries pass, including unchanged Synth and Wave render hashes unless the intentional detune-default change requires a separately reviewed golden update;
- VCV local build succeeds;
- no whitespace errors;
- only the intended documentation files remain uncommitted at this step.

The `ctrl_identity` and `wave_formant_sweep` hashes are expected candidates
to change because one uses the melodic-engine boot detune and the other
explicitly sets non-zero detune. For every failing render gate:

1. run the same render twice into separate output directories;
2. compute SHA-256 for both WAV files and require identical hashes;
3. confirm the scenario reaches non-zero detune;
4. update only that scenario's `EXPECTED=` value in `CMakeLists.txt` to the
   twice-reproduced hash;
5. rerun the CTest gate.

A scenario that explicitly sets detune to zero must remain bit-identical.
Never bless an unexplained hash change.

- [ ] **Step 5: Commit documentation**

```powershell
git add host/vcv/README.md docs/roadmap.md host/vcv/res/test_panel.py CMakeLists.txt
git commit -m "docs: describe contextual source controls"
```

- [ ] **Step 6: Install and perform the VCV live-control smoke test**

Build and install:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd /c/Users/bernd/Documents/AI/Spotykach/host/vcv && ./build-local.sh install'
```

Restart Rack, add a fresh Spotymod, and verify this exact checklist:

1. ENG Synth shows only `TIMB`; turning SOURCE holds a fixed timbre when MOD is zero.
2. ENG Wave shows only `FRAME`; the full knob reaches the first and last bank regions without changing the 6 ct spread.
3. ENG Sampler shows only `ORG`; loaded material moves from beginning to end.
4. Automating ENG changes the caption immediately and never stacks captions.
5. Detune A and B show `6.0 ct`, move independently, and each reset action returns exactly to `6.0 ct`.
6. Save a patch with unequal Detune A/B values, reopen it, and confirm both values restore.

Record the manual result in the implementation handoff; do not add a claim to
the README that was not observed.

- [ ] **Step 7: Perform the desktop clean-tree verification**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git status --short --branch
```

Expected: all tests pass and the working tree is clean.

---

### Task 6: Re-run the real Daisy WAVE performance gate

**Files:**
- Create through the runner: `docs/bench/YYYY-MM-DD-<githash>.md`
- Create through the runner: `docs/bench/YYYY-MM-DD-<githash>.csv`
- Verify: `bench/build/bench.elf`, `bench/build/bench-sram.elf`, and the unchanged QSPI payload

**Interfaces:**
- Consumes: the committed clean tree after Tasks 1–5, a powered Daisy Seed,
  and the ST-Link V3 attached over SWD.
- Produces: two complete deterministic hardware runs with accepted Synth/Wave
  rows and persisted evidence.
- Acceptance: `wave_2x4 avg <= synth_2x4 avg`, `wave_2x4 max <= synth_2x4 max`,
  and `wave_2x4 max < 960000` on both runs.

- [ ] **Step 1: Run bench host contracts before touching hardware**

Run:

```powershell
python -m unittest discover -s bench -p "test_*.py"
```

Expected: all bench runner, QSPI, arena, and Task-8 contracts pass.

- [ ] **Step 2: Confirm the hardware checkpoint**

Before continuing, confirm:

- the Daisy is powered;
- the ST-Link V3 SWD cable is attached;
- monitor volume is low because the anchor rows buzz twice;
- `git status --short` is empty.

Do not attempt DFU; this bench uses the split SRAM/QSPI OpenOCD path.

- [ ] **Step 3: Build and refresh the QSPI verification receipt**

Run from `bench/`:

```powershell
python run.py --build-only
python run.py --no-build --program-qspi --build-only
```

Expected: the helper verifies exactly 65,024 bytes at `0x90040000` and writes a receipt bound to the new bench ELF, QSPI digest, and connected Seed.

- [ ] **Step 4: Run the fail-closed two-pass hardware measurement**

Run from `bench/`:

```powershell
python run.py --repeat 2
```

Expected:

- both captures reach `BENCH_END`;
- row sets and checksums are identical across runs;
- QSPI digest and device fingerprint remain stable;
- both Synth/Wave comparisons satisfy the acceptance values above;
- one Markdown and one CSV evidence file are created under `docs/bench/`.

- [ ] **Step 5: Inspect and commit hardware evidence**

Check the new evidence contains two runs, 136 CSV rows, the current commit
hash, no raw MCU UID, and explicit PASS text for the matched WAVE/SYNTH gate.
Then commit:

```powershell
git add docs/bench
git commit -m "docs(bench): record source-detune hardware gate"
```

- [ ] **Step 6: Final verification**

Run:

```powershell
python host/vcv/res/test_panel.py
python tools/bake_wavetables.py --check
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git status --short --branch
```

Expected: every check passes and the tree is clean.
