# Sampler Preset Init Patch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make fresh Spotymod VCV modules and Rack **Initialize** reproduce the approved `sampler.vcvm` state while loading the existing bundled `factory.wav` into sampler Part B.

**Architecture:** Store the complete 80-value panel snapshot in a small VCV-host header indexed by the stable `ParamId` order, and make every parameter configuration branch consume that table. Keep non-parameter principle defaults beside the snapshot, reapply them after every engine reinitialization, and clear sampler edit/audio state during Rack **Initialize** so the existing factory-autoload path can deterministically refill Part B.

**Tech Stack:** C++17, VCV Rack SDK 2.6.6, Python source-level panel guards, CMake/CTest, MSYS2/MinGW local VCV build script.

## Global Constraints

- Use the existing `host/vcv/res/factory.wav`; do not modify or replace it.
- Do not bundle or read `sampler.vcvm` at runtime.
- Do not copy or retain the preset's absolute `D:\...` WAV path.
- Preserve all existing `ParamId` numeric values and saved-patch compatibility.
- Saved patches and presets continue to override init parameters, principles, and sampler persistence state.
- Do not push commits or tags.

---

### Task 1: Centralize the complete parameter snapshot

**Files:**
- Create: `host/vcv/src/init_patch.hpp`
- Modify: `host/vcv/src/Spotymod.cpp:1-260`
- Test: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `spkyvcv::ParamId`, `NUM_PARAMS`, and `PARAM_ORDER` in their existing stable order.
- Produces: `spkyvcv::kInitParamDefaults`, `spkyvcv::kInitPrinciple`, and `spkyvcv::initParamDefault(int id)`.

- [ ] **Step 1: Write the failing snapshot test**

Add `re` to the imports in `host/vcv/res/test_panel.py`, then add this guard:

```python
def test_sampler_preset_init_snapshot():
    here = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(here, "..", "src", "init_patch.hpp")
    if not os.path.isfile(header_path):
        check(False, "init_patch.hpp missing")
        return
    with open(header_path) as f:
        header = f.read()

    match = re.search(
        r"kInitParamDefaults\[\]\s*=\s*\{(.*?)\};", header, re.DOTALL)
    check(match is not None, "kInitParamDefaults array missing")
    if match is None:
        return
    actual = []
    for raw in match.group(1).splitlines():
        value = raw.split("//", 1)[0].strip().rstrip(",")
        if value:
            actual.append(float(value.removesuffix("f")))

    expected = [
        0.20466864109039307, 0.61599999666213989, 0.69518107175827026,
        1.0, 0.84406059980392456, -0.76896363496780396,
        0.6036144495010376, 0.5, 0.0, 0.32266658544540405,
        0.3190000057220459, 0.45866644382476807, 0.0,
        0.6773335337638855, 0.0, 0.62966680526733398, 16.0, 0.0,
        1.0, 1.0, 0.0, 0.0, 0.0,
        0.18674719333648682, 0.60000002384185791,
        0.31939762830734253, 0.30000001192092896,
        0.26144576072692871, -0.69156646728515625,
        0.34457823634147644, 0.5, 0.0, 0.4506666362285614,
        0.37900000810623169, 0.53633320331573486,
        0.087999999523162842, 0.46266642212867737,
        0.057000085711479187, 0.71099996566772461, 8.0, 1.0, 0.0,
        1.0, 0.0, 0.0, 0.0,
        0.49277070164680481, 1.0, 0.5, 1.0, 4.0, 0.0, 0.0,
        0.79066669940948486, 0.0, 0.64266586303710938,
        0.66399866342544556, 0.76133310794830322,
        0.86299997568130493, 0.48400050401687622,
        0.2370000034570694, 0.0, 0.064333423972129822,
        -0.2460000067949295, 0.5, 0.39272749423980713,
        0.36363637447357178, 0.28566798567771912,
        0.43933644890785217, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
        0.0, 0.0, 0.43066525459289551, 0.21200035512447357, 1.0,
    ]
    check(len(actual) == len(PARAM_ORDER) == len(expected),
          f"init snapshot has {len(actual)} values, want {len(PARAM_ORDER)}")
    for i, (got, want) in enumerate(zip(actual, expected)):
        check(math.isclose(got, want, rel_tol=0.0, abs_tol=1e-7),
              f"{PARAM_ORDER[i]} init {got}, want {want}")

    check("static constexpr int kInitPrinciple[] = {2, 0};" in header,
          "init principle must be [2, 0]")

    cpp_path = os.path.join(here, "..", "src", "Spotymod.cpp")
    with open(cpp_path) as f:
        cpp = f.read()
    check('#include "init_patch.hpp"' in cpp,
          "Spotymod.cpp does not include the init snapshot")
    check("const float init = initParamDefault(c.id);" in cpp,
          "configControls does not read the indexed init snapshot")
    check("defaultFor(" not in cpp,
          "legacy split defaultFor table still exists")

    makefile_path = os.path.join(here, "..", "Makefile")
    with open(makefile_path) as f:
        makefile = f.read()
    check("res/factory.wav" in makefile,
          "factory.wav is not included in the VCV distribution")
```

- [ ] **Step 2: Run the panel guard and verify RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: exit 1 with `init_patch.hpp missing`.

- [ ] **Step 3: Add the indexed init snapshot**

Create `host/vcv/src/init_patch.hpp`:

```cpp
#pragma once

namespace spkyvcv {

// Panel snapshot from sampler.vcvm (2026-07-24), in stable ParamId order.
static constexpr float kInitParamDefaults[] = {
     0.204668641f, // RATE_A
     0.615999997f, // SHAPE_A
     0.695181072f, // DENSITY_A
     1.000000000f, // SMOOTH_A
     0.844060600f, // RANGE_A
    -0.768963635f, // MELODY_A
     0.603614450f, // MOD_A
     0.500000000f, // TUNE_A
     0.000000000f, // ATTACK_A
     0.322666585f, // DECAY_A
     0.319000006f, // RES_A
     0.458666444f, // SUB_A
     0.000000000f, // DETUNE_A
     0.677333534f, // FLUX_A
     0.000000000f, // GRIT_A
     0.629666805f, // COMP_A
    16.000000000f, // STEPS_A
     0.000000000f, // ENGINE_A
     1.000000000f, // GRITMODE_A
     1.000000000f, // STEP_A
     0.000000000f, // PRINCIPLE_A
     0.000000000f, // NEWPHRASE_A
     0.000000000f, // TRIGGER_A
     0.186747193f, // RATE_B
     0.600000024f, // SHAPE_B
     0.319397628f, // DENSITY_B
     0.300000012f, // SMOOTH_B
     0.261445761f, // RANGE_B
    -0.691566467f, // MELODY_B
     0.344578236f, // MOD_B
     0.500000000f, // TUNE_B
     0.000000000f, // ATTACK_B
     0.450666636f, // DECAY_B
     0.379000008f, // RES_B
     0.536333203f, // SUB_B
     0.087999999f, // DETUNE_B
     0.462666422f, // FLUX_B
     0.057000086f, // GRIT_B
     0.710999966f, // COMP_B
     8.000000000f, // STEPS_B
     1.000000000f, // ENGINE_B
     0.000000000f, // GRITMODE_B
     1.000000000f, // STEP_B
     0.000000000f, // PRINCIPLE_B
     0.000000000f, // NEWPHRASE_B
     0.000000000f, // TRIGGER_B
     0.492770702f, // MORPH
     1.000000000f, // SYNC
     0.500000000f, // TEMPO
     1.000000000f, // COUPLE
     4.000000000f, // SCALE
     0.000000000f, // DRIFT
     0.000000000f, // SPOT
     0.790666699f, // MASTER_DRIVE
     0.000000000f, // SETTLE
     0.642665863f, // REV_SIZE
     0.663998663f, // REV_DECAY
     0.761333108f, // REV_TONE
     0.862999976f, // REV_DIFF
     0.484000504f, // REV_SMEAR
     0.237000003f, // REV_MOD
     0.000000000f, // CHOKE
     0.064333424f, // FILT_A
    -0.246000007f, // FILT_B
     0.500000000f, // TIDE
     0.392727494f, // FLUXRATE_A
     0.363636374f, // FLUXRATE_B
     0.285667986f, // FLUXFB_A
     0.439336449f, // FLUXFB_B
     0.000000000f, // COLOR_A
     0.000000000f, // COLOR_B
     1.000000000f, // DUST_A
     1.000000000f, // DUST_B
     1.000000000f, // ROT_A
     1.000000000f, // ROT_B
     0.000000000f, // REC_A
     0.000000000f, // REC_B
     0.430665255f, // REV_MIX_A
     0.212000355f, // REV_MIX_B
     1.000000000f, // SHUFFLE
};

static_assert(sizeof(kInitParamDefaults) / sizeof(kInitParamDefaults[0])
                  == NUM_PARAMS,
              "init snapshot must contain one value for every ParamId");

static constexpr int kInitPrinciple[] = {2, 0};
static_assert(sizeof(kInitPrinciple) / sizeof(kInitPrinciple[0]) == 2,
              "init principle must contain one value per part");

inline float initParamDefault(int id) {
    return kInitParamDefaults[id];
}

} // namespace spkyvcv
```

In `Spotymod.cpp`, include the header after `generated_panel.hpp`. At the top of
the `kParamCtls` loop, define:

```cpp
const float init = initParamDefault(c.id);
```

Use `init` as the default argument for every `configParam` and `configSwitch`
branch. Keep ranges, quantity types, labels, choices, and snap behavior
unchanged. `configButton` remains unchanged because all three momentary
snapshot values are zero. Delete `defaultFor()` and its obsolete init-patch
comment.

- [ ] **Step 4: Run the panel guard and verify GREEN**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: `PASS -- panel guards ok`.

- [ ] **Step 5: Commit the snapshot implementation**

```powershell
git add host/vcv/src/init_patch.hpp host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -m "feat(vcv): use sampler preset as init patch"
```

### Task 2: Make fresh instances and Initialize restore non-parameter state

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp:130-340,632-637`
- Test: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `kInitPrinciple`, `SamplerPartState`, `Instrument::set_principle()`, and `Instrument::sampler_clear()`.
- Produces: deterministic principle and factory-sample reset behavior for construction, sample-rate reinitialization, and Rack **Initialize**.

- [ ] **Step 1: Extend the source guard for reset semantics**

Append these checks to `test_sampler_preset_init_snapshot()`:

```python
check("int principleIdx[2] = {kInitPrinciple[0], kInitPrinciple[1]};" in cpp,
      "fresh module principle state is not [2, 0]")
check("inst.set_principle(p, principleIdx[p]);" in cpp,
      "reinit does not restore the current principle after inst.init")
check("principleIdx[p] = kInitPrinciple[p];" in cpp,
      "Initialize does not restore principle defaults")
check("smp[p] = SamplerPartState{};" in cpp,
      "Initialize does not reset sampler edit state")
check("inst.sampler_clear(p);" in cpp,
      "Initialize does not empty sampler audio before factory autoload")
```

- [ ] **Step 2: Run the panel guard and verify RED**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: exit 1 with messages for fresh principle, reset principle, sampler
edit state, and sampler audio.

- [ ] **Step 3: Implement deterministic principle and sampler reset**

Initialize the member from the shared constants:

```cpp
int principleIdx[2] = {kInitPrinciple[0], kInitPrinciple[1]};
```

Immediately after `inst.init(sr, fxmem);` in `reinit()`, restore the current
non-parameter state:

```cpp
for (int p = 0; p < spky::PART_COUNT; ++p)
    inst.set_principle(p, principleIdx[p]);
```

Replace the body setup in `onReset()` with:

```cpp
for (int p = 0; p < spky::PART_COUNT; ++p) {
    principleIdx[p] = kInitPrinciple[p];
    smp[p] = SamplerPartState{};
    inst.sampler_clear(p);
    factoryTried[p] = false;
}
reinit(curSr > 0.f ? curSr : 48000.f);
```

This clears any user/external sample before `reinit()` can snapshot it. On the
next parameter push, the default `ENGINE_B == 1`, empty Part B, and
`factoryTried[B] == false` drive the existing cached `factory.wav` autoload.

- [ ] **Step 4: Run the panel and C++ tests and verify GREEN**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: panel guard passes, build exits 0, and CTest reports `100% tests
passed`.

- [ ] **Step 5: Commit reset semantics**

```powershell
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py
git commit -m "fix(vcv): restore sampler init state on initialize"
```

### Task 3: Build, install, and verify the live plugin

**Files:**
- Build input: `host/vcv/`
- Installed output: `C:\Users\bernd\AppData\Local\Rack2\plugins-win-x64\Spotymod\`

**Interfaces:**
- Consumes: `host/vcv/build-local.sh install`.
- Produces: a freshly compiled and installed `plugin.dll`, packaged
  `.vcvplugin`, and unchanged installed `res/factory.wav`.

- [ ] **Step 1: Run all pre-install verification**

Run:

```powershell
python host/vcv/res/test_panel.py
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

Expected: all commands exit 0; panel guard passes; CTest reports zero failures;
Git reports no whitespace errors.

- [ ] **Step 2: Build and install via the machine-specific script**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' `
  'C:/Users/bernd/Documents/AI/Spotykach/host/vcv/build-local.sh' install
```

Expected: MinGW build and packaging succeed; the script reports the archive,
the unpacked DLL path, and `(identical to the just-built plugin.dll)`.

- [ ] **Step 3: Verify installed bytes and the unchanged factory asset**

Run:

```powershell
$builtDll = 'host/vcv/plugin.dll'
$liveDll = "$env:LOCALAPPDATA\Rack2\plugins-win-x64\Spotymod\plugin.dll"
$sourceFactory = 'host/vcv/res/factory.wav'
$liveFactory = "$env:LOCALAPPDATA\Rack2\plugins-win-x64\Spotymod\res\factory.wav"

if ((Get-FileHash $builtDll).Hash -ne (Get-FileHash $liveDll).Hash) {
    throw 'Installed plugin.dll differs from the fresh build'
}
if ((Get-FileHash $sourceFactory).Hash -ne (Get-FileHash $liveFactory).Hash) {
    throw 'Installed factory.wav differs from the bundled factory asset'
}
Get-Item $builtDll,$liveDll,$sourceFactory,$liveFactory |
    Select-Object FullName,Length,LastWriteTime
git status --short --branch
```

Expected: no exception; matching DLL hashes; matching factory WAV hashes; Git
shows only the intended local commits and no untracked build output.

- [ ] **Step 4: Report the handoff**

Report the two implementation commit hashes, tests run, install paths, and that
Rack must be restarted to load the new DLL. Explicitly state that
`factory.wav` was unchanged and nothing was pushed.
