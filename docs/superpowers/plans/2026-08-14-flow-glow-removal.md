# Flow layer and Glow removal — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete `engine/flow/` and everything Glow from the repository, without
losing test coverage and without leaving dead references behind.

**Architecture:** Six commits in a forced order. The two harvests (frozen
operating points, by-ear notes) must run while the terrain generator still
exists; the `flow_params.h` move must run *after* the deletion, because
`kParams` and `spky::flow` are consumed by roughly twenty files that live until
then. Everything else is subtraction.

**Tech Stack:** C++17, CMake + Ninja + clang (never MSVC), doctest, Python 3 for
the VCV panel generators.

**Spec:** `docs/superpowers/specs/2026-08-14-flow-glow-removal-design.md` — read
it before starting. This plan argues from it and does not repeat its reasoning.

## Global Constraints

- **Build**: `source env.sh` first. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.
  **Release is not optional** — a Debug build fails `spky_tests` and
  `ctrl_identity` with "SYNTH reference moved", because the render hashes in
  `tests/check_render_hash.cmake` are byte-identity gates built from Release.
- **Verification after every task**: `cmake --build build && ctest --test-dir build --output-on-failure`, green.
- **Never** `source env.sh` in a shell used for `shell/` or `bench/` — those are
  ARM GCC via `make` and the two toolchains must not mix. This plan does not
  touch either tree.
- **VCV builds** only via `host/vcv/build-local.sh`. The system `g++` on this
  machine is the ARM cross-compiler and fails with "MinGW not found".
- **Everything written into the repo is English** — code, comments, commit
  messages, docs. The conversation is German; the files are not.
- **Commit trailer**: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Branch**: `2026-08-14-flow-glow-removal`, already created, spec already
  committed on it.
- **A test that cannot go red gets fixed.** Where this plan says "prove the RED",
  do it and paste the failure into the commit message or discard the step.
- **The naming trap**: `engine/flow/` is the terrain layer and goes. FLOW as a
  lane run mode (`_flow_melody`, `set_flow_melody`, `Instrument::set_flow`) is
  instrument core and stays. Never delete by filename alone.

---

### Task 1: Freeze the parameter-impact operating points

Today `pick_terrains()` draws four operating points from the terrain generator on
every build. The generator is about to be deleted, so the four points it draws
today get written down. Exactly those four — not a wider or better-chosen set;
`tests/test_param_impact.cpp:411-423` pins the `P_FORM_B` tolerance to masters 3
and 8 by name, and moving the points means re-measuring three hand-maintained
expectation lists.

**Files:**
- Create: `tests/param_impact_points.h`
- Create (throwaway, not committed): `tools/dump_points.cpp`
- Modify: `tests/test_param_impact.cpp` — `ReferencePatch`, `reference_patch()`,
  `pick_terrains()`, the includes at `:31,41`
- Modify: `CMakeLists.txt` — nothing yet; the test target is unchanged

**Interfaces:**
- Consumes: `ParamId`, `P_COUNT`, `kParams`, `clamp_to`, `apply_param` from
  `flow/flow_params.h` (still at its old path in this task)
- Produces: `spky::FrozenPoint`, `kFlowPoints`, `kStepPoints`, `kPer` — Task 5
  re-points their include from `flow/flow_params.h` to `param_table.h`

- [ ] **Step 1: Write the dump program**

Create `tools/dump_points.cpp`. It reproduces `pick_terrains()` exactly and
prints the header. It is deleted at the end of this task and never committed.

```cpp
// THROWAWAY. Prints tests/param_impact_points.h. Deleted at the end of
// task 1 of docs/superpowers/plans/2026-08-14-flow-glow-removal.md.
#include "flow/flow.h"
#include "instrument.h"
#include <cstdio>
using namespace spky;
using namespace spky::flow;

// Copy these VERBATIM from tests/test_param_impact.cpp so the dump
// reproduces the live rig: the constants kSr, kBlock, kCtrlHz, kDur, kSkip,
// kPer, kMoved, kDeckAudible; the FxMem statics and pi_fx_mem(); struct
// ReferencePatch; has_sampler(); reference_patch(); apply_patch(); compare();
// deck_audible(). Do not paraphrase any of them -- a dump that draws
// different points than the rig does silently freezes the wrong instrument.

int main() {
    std::printf("// GENERATED once on 2026-08-14 by tools/dump_points.cpp,\n");
    std::printf("// which was then deleted. See the removal plan, task 1.\n");
    std::printf("//\n");
    std::printf("// These are the four operating points tests/test_param_impact.cpp\n");
    std::printf("// used to draw from the terrain generator on every build. The\n");
    std::printf("// generator is gone; the points are not. deck_audible() still runs\n");
    std::printf("// live against each one -- see load_points() in the test.\n");
    std::printf("#pragma once\n#include \"flow/flow_params.h\"\n\n");
    std::printf("namespace spky {\n\nconstexpr int kPer = 2;\n\n");
    std::printf("struct FrozenPoint {\n");
    std::printf("    const char* origin;\n");
    std::printf("    float v[P_COUNT];\n");
    std::printf("    bool  step;\n");
    std::printf("    int   steps_a, steps_b;\n};\n\n");

    ReferencePatch flow_pts[kPer], step_pts[kPer];
    uint32_t flow_m[kPer] = {0}, step_m[kPer] = {0};
    int nf = 0, ns = 0;
    for (uint32_t m = 1; m < 400 && (nf < kPer || ns < kPer); ++m) {
        TerrainState st; st.master = m;
        Terrain t = generate(st);
        if (has_sampler(t)) continue;
        const ReferencePatch rp = reference_patch(m);
        if (!deck_audible(rp, PART_A) || !deck_audible(rp, PART_B)) continue;
        if (rp.step && ns < kPer)  { step_m[ns] = m; step_pts[ns++] = rp; }
        if (!rp.step && nf < kPer) { flow_m[nf] = m; flow_pts[nf++] = rp; }
    }
    if (nf != kPer || ns != kPer) { std::fprintf(stderr, "draw failed\n"); return 1; }

    auto emit = [](const char* name, const ReferencePatch* p, const uint32_t* ms) {
        std::printf("inline constexpr FrozenPoint %s[kPer] = {\n", name);
        for (int i = 0; i < kPer; ++i) {
            std::printf("  { \"master %u, %s\", {\n", ms[i], p[i].step ? "STEP" : "FLOW");
            for (int q = 0; q < P_COUNT; ++q)
                std::printf("      %.9gf, // %s\n", p[i].v[q], kParams[q].name);
            std::printf("    }, %s, %d, %d },\n",
                        p[i].step ? "true" : "false", p[i].steps_a, p[i].steps_b);
        }
        std::printf("};\n\n");
    };
    emit("kFlowPoints", flow_pts, flow_m);
    emit("kStepPoints", step_pts, step_m);
    std::printf("}  // namespace spky\n");
    return 0;
}
```

- [ ] **Step 2: Build and run the dump**

```bash
source env.sh
clang++ -std=c++17 -O1 -I engine -I third_party -I third_party/DaisySP/Source \
    -o dump_points.exe tools/dump_points.cpp engine/flow/terrain.cpp \
    engine/flow/flow.cpp $(ls engine/**/*.cpp)
./dump_points.exe > tests/param_impact_points.h
```

If the link line is wrong, take the object list from the `spky_tests` target in
`CMakeLists.txt` rather than guessing. Expected: a header of four blocks, each
naming its master seed.

- [ ] **Step 3: Verify the frozen points match what the rig draws**

Read the generated header. Expected: two `kStepPoints` entries whose origin
comments read `master 3, STEP` and `master 8, STEP` — those are the two masters
`tests/test_param_impact.cpp:411-420` names by hand. If they are different
masters, **stop**: the rig has changed since that comment was written, and the
expectation lists in the test must be re-measured before anything is frozen.

- [ ] **Step 4: Switch the test onto the frozen points**

In `tests/test_param_impact.cpp`:

Replace `#include "flow/flow.h"` with `#include "flow/flow_params.h"` and
`#include "param_impact_points.h"`. Delete `using namespace spky::flow;`.

Delete `has_sampler()`, `reference_patch()`, `struct ReferencePatch` and
`pick_terrains()`. Replace `ReferencePatch` at every use site with
`FrozenPoint`.

**Delete `constexpr int kPer = 2;` at `:69`** — the generated header now owns
that name, and both are in `namespace spky` once the file's `using namespace
spky;` is in effect. Leaving both is a redefinition error, not a warning.

Then add:

```cpp
struct Terrains { FrozenPoint flow[kPer], step[kPer]; };

// The generator that drew these is gone (removal spec 4.3). What it did on
// every build and this cannot is re-check that both decks still carry the
// mix -- so that check runs here instead, live, on each frozen point. A
// change that leaves a deck near-silent at one of these points would
// otherwise report that deck's whole parameter set as dead, which is the
// mistake this rig already made once (see the ReferencePatch note above).
Terrains load_points() {
    Terrains out{};
    for (int i = 0; i < kPer; ++i) {
        out.flow[i] = kFlowPoints[i];
        out.step[i] = kStepPoints[i];
    }
    for (int i = 0; i < kPer; ++i) {
        REQUIRE_MESSAGE(deck_audible(out.flow[i], PART_A), out.flow[i].origin);
        REQUIRE_MESSAGE(deck_audible(out.flow[i], PART_B), out.flow[i].origin);
        REQUIRE_MESSAGE(deck_audible(out.step[i], PART_A), out.step[i].origin);
        REQUIRE_MESSAGE(deck_audible(out.step[i], PART_B), out.step[i].origin);
    }
    return out;
}
```

Replace the single call to `pick_terrains()` with `load_points()`.

- [ ] **Step 5: Rewrite the now-unexecutable instruction at `:411-423`**

That comment tells the reader to "remove the entry if the terrain sample ever
moves onto one of the nine". The sample cannot move any more. Replace that
sentence with: `The sample is frozen (removal spec 4.3), so this entry is now
permanent unless the frozen points are re-authored by hand.` Leave the rest of
the block, including the measured numbers, untouched.

- [ ] **Step 6: Build and run**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R param_impact
```
Expected: PASS.

- [ ] **Step 7: Prove the RED**

Edit one float in `tests/param_impact_points.h` — set the `P_LEVEL_A` entry of
the first flow point to `0.0f` — and re-run.

Expected: FAIL, and specifically the `deck_audible` `REQUIRE` for
`kFlowPoints[0]` / `PART_A`, printing that point's origin string. If it fails
somewhere else, or passes, the `load_points()` guard is not wired. Revert the
edit afterwards.

- [ ] **Step 8: Delete the dump program and commit**

```bash
rm tools/dump_points.cpp dump_points.exe
git add tests/param_impact_points.h tests/test_param_impact.cpp
git commit -m "test(impact): freeze the four operating points the terrain drew

The generator that drew them is being deleted. deck_audible() moves into
load_points() and runs live on each frozen point, because it checks the
instrument's current state rather than the terrain's -- without it the next
change that silences a deck reports that deck's whole parameter set as dead.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Harvest the by-ear rules from `taste.h`

`engine/flow/taste.h` holds values Bastian set by listening. Formally they are
terrain draw rules and they die with the layer; in substance they are claims
about where this instrument sounds good, and the hardware panel ranges and the
Marbles round will both want them.

**Files:**
- Create: `docs/attic/taste-by-ear-notes.md`
- Read: `engine/flow/taste.h` (whole file)

**Interfaces:** none — prose only, no code, no build change.

- [ ] **Step 1: Read `taste.h` end to end**

Every by-ear value in it carries a header comment explaining itself. Collect at
minimum: the vetoes, the redrawn curves, the archetype window, the musical
weights, the COMP band move, the per-domain adventure draw, the drone SHAPE cap
`P_SHAPE_A/B {0,.25}`, reverb MOD held low, TONE held high, and both
`kCalmCornerRmsMin` / `kCalmCornerRmsMax` comment blocks with their per-commit
isolation.

- [ ] **Step 2: Write the note**

`docs/attic/taste-by-ear-notes.md`. For each value: what it was, what it asserts
musically, and where it should resurface. Structure it by that third column —
"wants a hardware panel range", "input to the Marbles round", "closed, recorded
only so it is not re-litigated" — because that is how it will be read.

Carry the coupling finding verbatim: the drone SHAPE cap is what retired the
`kCalmCornerRmsMax` ceiling breach at master 0x707 *and* what mutes master
0x404. Anyone reinstating one gets the other.

State at the top that these values are **not** in the boot patch and that
promoting them there changes the factory sound — the control-merge init trap has
cost four regressions on exactly that move, so it needs its own listening round.

- [ ] **Step 3: Commit**

```bash
git add docs/attic/taste-by-ear-notes.md
git commit -m "docs(attic): harvest the by-ear rules before taste.h goes

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Move the paper trail to `docs/attic/`

**Files:**
- Create: `docs/attic/README.md`
- Move: twelve specs and plans, the macro audit, the param map, the listening notes

**Interfaces:** none.

- [ ] **Step 1: Write the attic README**

Two sentences on what the directory is — discontinued work, kept for its
reasoning rather than its code — plus a line reserving a slot for the
`hardware/glow-faceplate/` deletion hash, which Task 4 fills in.

- [ ] **Step 2: Move the documents**

```bash
git mv docs/2026-08-13-glow-macro-audit.md docs/attic/
git mv docs/flow-fireflow-param-map.md docs/attic/
git mv docs/superpowers/specs/2026-08-05-flow-listening-notes.md docs/attic/
git mv docs/superpowers/specs/2026-08-05-flow-machine-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-06-glow-taste-structure-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-07-glow-genre-and-scale-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-07-glow-tonality-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-10-fireflow-touch-curated-places-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md docs/attic/
git mv docs/superpowers/specs/2026-08-11-flow-patch-transfer-design.md docs/attic/
git mv docs/superpowers/plans/2026-08-05-flow-engine-layer.md docs/attic/
git mv docs/superpowers/plans/2026-08-05-flow-glow-vcv-module.md docs/attic/
git mv docs/superpowers/plans/2026-08-06-glow-operating-mode.md docs/attic/
git mv docs/superpowers/plans/2026-08-06-glow-taste-tables.md docs/attic/
git mv docs/superpowers/plans/2026-08-07-glow-genre-and-scale.md docs/attic/
git mv docs/superpowers/plans/2026-08-07-glow-tonality.md docs/attic/
git mv docs/superpowers/plans/2026-08-11-flow-patch-transfer.md docs/attic/
git mv docs/superpowers/plans/2026-08-11-glow-touch-2-panel.md docs/attic/
git mv docs/superpowers/plans/2026-08-13-glow-hardware-faithful-hybrid-panel.md docs/attic/
```

Then `ls docs/superpowers/specs docs/superpowers/plans | grep -i "glow\|flow"` —
what remains must be only the flow-*melody* documents (`2026-08-13-flow-melody-engine-*`,
`2026-08-14-melody-reachable*`) and this removal's own spec and plan. Those
describe the lane run mode and stay.

- [ ] **Step 3: Verify history survived the move**

```bash
git log --follow --oneline -- docs/attic/2026-08-05-flow-machine-design.md | tail -3
```
Expected: commits from 5 August, not a single "moved" commit.

- [ ] **Step 4: Commit**

```bash
git add -A docs/
git commit -m "docs(attic): move the flow and Glow paper trail

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The deletion

The big one. Everything in one commit, because the tree does not build between
its halves.

**Files:**
- Delete: nine `engine/flow/*` (all but `flow_params.h`), eighteen VCV sources
  and assets, `hardware/glow-faceplate/`, five render scenarios, eighteen tests
- Modify: `CMakeLists.txt`, `host/vcv/Makefile`, `host/vcv/plugin.json`,
  `host/vcv/src/plugin.{cpp,hpp}`, `host/vcv/src/Fireflow.cpp`,
  `host/render/main.cpp`, `host/render/scenario.{h,cpp}`,
  `tests/test_scenario.cpp`, `tests/test_instrument.cpp`
- Create: `host/render/scenarios/pace_sweep.json`

**Interfaces:**
- Consumes: nothing from earlier tasks except that Task 1 already removed
  `test_param_impact.cpp`'s dependency on `flow/flow.h`
- Produces: a tree where `engine/flow/flow_params.h` is the only survivor of the
  directory — Task 5 moves it

- [ ] **Step 1: Rebuild the `deck_steps` coverage first, and prove it red**

`tests/test_flow_mode.cpp:125-152` is the suite's only assertion that
`set_step(part, mode, count)` routes the count to the right deck. Its own comment
says so, and `deck_steps` appears in `tests/` nowhere else that survives. Add to
`tests/test_instrument.cpp`:

```cpp
// The only assertion in the suite that a step count reaches the deck it was
// addressed to. Inherited from tests/test_flow_mode.cpp, deleted with the flow
// layer (removal spec 4.4); its own comment observed that swapping the two
// arguments would otherwise leave the whole suite green.
TEST_CASE("instrument: set_step routes each count to its own deck") {
    Instrument in;
    in.init(48000.f);          // engine only, no FX chain needed -- the idiom
                               // the neighbouring cases in this file use
    in.set_sync(true);
    in.set_step(PART_A, true, 6);
    in.set_step(PART_B, true, 11);
    CHECK(in.deck_steps(PART_A) == 6);
    CHECK(in.deck_steps(PART_B) == 11);
    in.set_step(PART_A, true, 16);
    CHECK(in.deck_steps(PART_A) == 16);
    CHECK(in.deck_steps(PART_B) == 11);   // untouched by A's change
}
```

Prove the RED: in `engine/`'s `set_step` implementation, temporarily swap the two
decks. Run `ctest -R instrument`. Expected: FAIL on `deck_steps(PART_A) == 6`.
Revert.

- [ ] **Step 2: Remove the Glow-pad menu action from `Fireflow.cpp`**

Delete `#include "flow_patch_bridge.hpp"` at `:10`, and the context-menu action
at `:1806-1820` together with the `to_flow_base()` call at `:1838` and its
`TransferReport` handling. Read the surrounding menu construction before cutting
— the neighbouring items must keep their separators.

- [ ] **Step 3: Delete the engine, VCV, hardware and scenario files**

```bash
git rm engine/flow/flow.cpp engine/flow/flow.h engine/flow/flow_ids.h \
       engine/flow/flow_rng.h engine/flow/gesture.h engine/flow/taste.h \
       engine/flow/terrain.cpp engine/flow/terrain.h engine/flow/terrain_code.h
git rm host/vcv/src/Glow.cpp host/vcv/src/glow_panel.hpp host/vcv/src/glow_panel.cpp \
       host/vcv/src/glow_ui.hpp host/vcv/src/touch_pads.hpp host/vcv/src/pad_geometry.hpp \
       host/vcv/src/generated_flow_panel.hpp host/vcv/src/flow_patch_bridge.hpp
git rm host/vcv/res/gen_flow_panel.py host/vcv/res/test_flow_panel.py \
       host/vcv/res/validate_glow_assets.py host/vcv/res/test_glow_assets.py \
       host/vcv/res/touch2_geometry.py host/vcv/res/test_touch2_geometry.py
git rm host/vcv/res/Glow.svg host/vcv/res/GlowRear.png host/vcv/res/GlowFaceplate.png \
       host/vcv/res/GlowTouch.png host/vcv/res/GlowSwitchDown.png \
       host/vcv/res/GlowSwitchCenter.png host/vcv/res/GlowSwitchUp.png
git rm -r hardware/glow-faceplate
git rm host/render/scenarios/flow_calm_corner.json host/render/scenarios/flow_new_ride.json \
       host/render/scenarios/flow_pace_sweep.json host/render/scenarios/flow_pace_sweep_sampler.json \
       host/render/scenarios/flow_smoke.json
git rm tests/test_flow_audio.cpp tests/test_flow_chord_reach.cpp tests/test_flow_gesture.cpp \
       tests/test_flow_mode.cpp tests/test_flow_new.cpp tests/test_flow_overlay.cpp \
       tests/test_flow_patch_bridge.cpp tests/test_flow_rng.cpp tests/test_flow_runtime.cpp \
       tests/test_flow_taste.cpp tests/test_flow_terrain.cpp tests/test_flow_terrain_code.cpp \
       tests/test_flow_transfer_diff.cpp tests/test_flow_veto.cpp \
       tests/test_glow_panel_manifest.cpp tests/test_glow_ui.cpp tests/test_touch_pads.cpp \
       tests/test_pad_geometry.cpp
```

`host/render/scenarios/flow_melody.json`, `tests/test_flow_melody.cpp` and
`tests/test_flow_melody_wiring.cpp` are **not** in that list and must survive.
Confirm with `ls` before continuing.

- [ ] **Step 4: Strip the render host**

`host/render/main.cpp`: delete `#include "flow/flow.h"`, the `flow::Flow
flow_obj` / `flow_ptr` declarations at `:75-76`, and the control-rate tick at
`:67-72`. `host/render/scenario.h`: delete `has_flow` and `flow_code` and the
`flow_*` paragraphs of the dispatch comment. `host/render/scenario.cpp`: delete
the `flow_` early-outs at `:47,84,225` and the whole `flow_wake` / `flow_macro` /
`flow_cv` / `flow_new` / `flow_new_partial` / `flow_undo` / `flow_lock` branch.

- [ ] **Step 5: Write the replacement PACE scenario**

`host/render/scenarios/pace_sweep.json`, modelled on the deleted
`flow_pace_sweep.json` but driven through plain scenario actions. It must sweep
`set_pace` across its range on a note engine and produce audible tempo change.
Copy the deleted file's timeline structure from `git show HEAD:host/render/scenarios/flow_pace_sweep.json`
and replace every `flow_*` action with the equivalent direct setter action.

```bash
./build/render.exe host/render/scenarios/pace_sweep.json /tmp/pace.wav /tmp/pace.csv
```
Expected: exit 0, a non-silent WAV.

- [ ] **Step 6: Strip `tests/test_scenario.cpp`**

Delete the `flow_*` action cases *and* the file-scope
`#include "flow/flow.h"`, `#include "flow/terrain_code.h"` and
`using namespace spky::flow;` at `:7-10`.

- [ ] **Step 7: Strip CMake**

Remove, by line: `:142` (`test_pad_geometry`), `:143` (`glow_panel.cpp` in
`spky_tests`), `:170` (`test_flow_rng`), the other deleted test entries, `:174`
and `:181` and `:218-219` (`engine/flow/*.cpp` sources), `:293-297`
(`add_custom_target(glow_assets_guard ALL …)`), **`:299`
(`add_dependencies(render glow_assets_guard)`)**, `:313-317`
(`touch2_geometry_guard`), `:319-323`, `:328-333`.

`:299` is the trap: it is a build-graph edge from the **render executable** to
the Glow PNG validator, not a CTest guard. Leaving it while the custom target
goes makes `cmake` fail to configure, which takes both render-hash gates with it.

- [ ] **Step 8: Strip the VCV Makefile, plugin manifest and registration**

`host/vcv/Makefile`: remove `:45-46` (flow sources), `:89-91` (Glow
distributables), **`:96-100`** (the `.PHONY: validate_glow_assets` target and the
`all dist: validate_glow_assets` prerequisite). `host/vcv/plugin.json`: remove
the `Glow` module entry. `host/vcv/src/plugin.cpp` and `plugin.hpp`: remove the
`modelGlow` declaration and `addModel` call.

- [ ] **Step 9: Configure from scratch, build, test**

A stale CMake cache hides exactly the configure error step 7 is about.

```bash
rm -rf build
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: configure succeeds, all tests pass.

- [ ] **Step 10: Build the VCV plugin — the check no test covers**

```bash
cd host/vcv && ./build-local.sh
```
Expected: completes. Then load it in Rack and confirm the plugin shows exactly
two modules, `Fireflow` and `FireflowHW`. **This is a manual step; do not mark it
done without having looked.**

- [ ] **Step 11: Commit, and record the faceplate hash**

```bash
git add -A
git commit -m "feat(repo): delete the flow layer and Glow

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

Then put this commit's hash into the slot Task 3 reserved in
`docs/attic/README.md`, so recovering `hardware/glow-faceplate/` is a `git show
<hash>^:hardware/glow-faceplate/...` rather than a `--diff-filter=D` hunt.
Amend it into the same commit.

---

### Task 5: `flow_params.h` → `engine/param_table.h`

The file is a reflection table over `Instrument`'s setters, not a Glow feature.
Its X-macro rows are `X(id, lo, hi, steps)` — **the fourth argument is the
discrete-step count, not story ownership.** Dropping it breaks `ParamInfo`,
`kParams` and `apply_param`'s rounding.

**Files:**
- Move: `engine/flow/flow_params.h` → `engine/param_table.h`
- Move: `tests/test_flow_params.cpp` → `tests/test_param_table.cpp`
- Modify: `tests/param_impact_points.h`, `tests/test_param_impact.cpp` — includes
- Modify: `CMakeLists.txt` — the renamed test target

**Interfaces:**
- Produces: `spky::kParams`, `spky::clamp_to`, `spky::apply_param`,
  `spky::apply_mode_and_steps`, `spky::ParamId`, `spky::P_COUNT` — all formerly
  `spky::flow::`

- [ ] **Step 1: Move the header and strip it**

```bash
git mv engine/flow/flow_params.h engine/param_table.h
rmdir engine/flow
```

Then edit `engine/param_table.h`:
1. delete `#include "flow/flow_ids.h"` at `:3` — nothing in the file uses it and
   the header is gone;
2. rename `SPKY_FLOW_PARAMS` → `SPKY_PARAMS`, `SPKY_FLOW_ENUM` → `SPKY_ENUM`,
   `SPKY_FLOW_INFO` → `SPKY_INFO`;
3. `namespace spky::flow` → `namespace spky`;
4. strip the story-overlay rationale from `P_MODE`'s and `P_PACE`'s comments —
   the stream-key paragraph governed terrain draws. **Keep** the two statements
   that are about the instrument: `Instrument::set_sync` is global, and `P_PACE`
   carries a live offset rather than a base value.

Leave the `X(id, lo, hi, steps)` rows and `struct ParamInfo` untouched.

- [ ] **Step 2: Write the failing test for `apply_mode_and_steps`**

`apply_param` refuses `P_MODE`, `P_STEPS_A` and `P_STEPS_B` via a deliberate
`break`, because `set_step()` needs mode and count together and `set_sync()` is
global. `Flow::push_mode_and_steps()` used to compensate and is gone. Without a
replacement, §4.1's claim that this header is M6's dispatch table is false.

In `tests/test_param_table.cpp`:

```cpp
TEST_CASE("param table: apply_mode_and_steps reaches what apply_param refuses") {
    Instrument in;
    in.init(48000.f);          // as the file's existing "apply routes to the
                               // engine" case does -- no FX chain needed
    // apply_param cannot route these three -- it is per-parameter and stateless.
    apply_param(in, P_STEPS_A, 6.f);
    CHECK(in.deck_steps(PART_A) != 6);      // the documented refusal
    apply_mode_and_steps(in, true, 6, 11);
    CHECK(in.deck_steps(PART_A) == 6);
    CHECK(in.deck_steps(PART_B) == 11);
}
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R param_table
```
Expected: FAIL, `apply_mode_and_steps` not declared.

- [ ] **Step 4: Implement it**

In `engine/param_table.h`, beside `apply_param`:

```cpp
// The three parameters apply_param() refuses, issued as the one unit they
// have to be: set_step() takes mode and count together and set_sync() is
// global, so a per-parameter, stateless setter cannot see all three at once.
// Flow::push_mode_and_steps() used to own this and was deleted with the flow
// layer (removal spec 4.2); a panel driving the instrument through kParams
// needs it or it cannot set the operating mode or either step count.
inline void apply_mode_and_steps(Instrument& in, bool step_mode,
                                 int steps_a, int steps_b) {
    in.set_sync(step_mode);
    in.set_step(PART_A, step_mode, steps_a);
    in.set_step(PART_B, step_mode, steps_b);
}
```

- [ ] **Step 5: Run it and watch it pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R param_table
```
Expected: PASS.

- [ ] **Step 6: Move the test file and rescue the two table-row assertions**

```bash
git mv tests/test_flow_params.cpp tests/test_param_table.cpp
```

Change its include to `param_table.h`, drop `using namespace spky::flow;`, and
rename its two cases from `"flow params: …"` to `"param table: …"`. Then add the
two row assertions rescued from the deleted `test_flow_mode.cpp:38-45` — the
generic "table is sane" case does not cover them:

```cpp
TEST_CASE("param table: the two rows other code reads by hand") {
    // P_MODE is discrete and binary; P_PACE is continuous 0..1 with 0.5 = x1.
    // Inherited from tests/test_flow_mode.cpp (removal spec 4.4).
    CHECK(kParams[P_MODE].steps == 2);
    CHECK(kParams[P_MODE].lo == doctest::Approx(0.f));
    CHECK(kParams[P_MODE].hi == doctest::Approx(1.f));
    CHECK(kParams[P_PACE].steps == 0);
    CHECK(kParams[P_PACE].lo == doctest::Approx(0.f));
    CHECK(kParams[P_PACE].hi == doctest::Approx(1.f));
}
```

- [ ] **Step 7: Re-point the two remaining consumers**

`tests/param_impact_points.h`: `#include "flow/flow_params.h"` →
`#include "param_table.h"`. `tests/test_param_impact.cpp`: same, and drop any
`spky::flow` qualification. `CMakeLists.txt`: rename the `test_flow_params.cpp`
entry to `test_param_table.cpp`.

- [ ] **Step 8: Full build and test, then commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add -A
git commit -m "refactor(engine): flow_params.h becomes engine/param_table.h

It was never a Glow feature -- it is the reflection table over Instrument's
setters. The fourth X-macro column is the discrete-step count, not story
ownership. apply_mode_and_steps() is added because apply_param() refuses
P_MODE and both step counts via a silent break that Flow::push_mode_and_steps
used to compensate for; M6's panel would have inherited the trap.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Documentation

**Files:**
- Modify: `docs/roadmap.md`, `docs/engine-map.md`, `CLAUDE.md`, `README.md`,
  `host/vcv/README.md`, `docs/release-notes.md`, `engine/mod/lane.h`,
  `engine/mod/song_ladder.h`

**Interfaces:** none.

- [ ] **Step 1: Fix the three dangling citations in surviving core files**

Verified 2026-08-14: `engine/mod/lane.h:208` cites `flow/taste.h:586-588`;
`engine/mod/song_ladder.h:11` cites `engine/flow/taste.h:891`; `:40` cites
`engine/flow/flow.cpp:234`. All three files survive; all three targets are gone.
Rewrite each to cite `docs/attic/taste-by-ear-notes.md` or to restate the fact
without the reference.

`lane.h:266` looks like a fourth and is not — it cites `Fireflow.cpp:892-893`,
which survives. Leave it.

- [ ] **Step 2: Edit `docs/roadmap.md`**

Remove the Planned entry "Glow rework" and the ordering paragraph above it;
remove from "Done" the entries Flow patch transfer, the Touch-2 panel rebuild and
the taste tables; remove the four flow-layer paragraphs from the M6 block. Add a
dated line at the top recording the removal, why, and that the reasoning is in
`docs/attic/`.

Then three entries whose premises this invalidates:

- **SHAPE + SMOOTH rework**, third falsified-claim bullet, cites the terrain cap
  `P_SHAPE_A/B = {0,.25}` as a live constraint. It is not one any more — rewrite
  the bullet to cite the attic note.
- **Marbles round**, third open question, cites the Glow macro audit's decision 7
  for its ownership model. The answer is now the panel; repoint the citation to
  `docs/attic/2026-08-13-glow-macro-audit.md` and say so.
- **M6** names *preset persistence*. `terrain_code.h` was the repo's only
  whole-patch serialiser and `flow_patch_bridge.hpp` its only patch transfer;
  both are gone. Note that M6 step 2 now starts from nothing there.

- [ ] **Step 3: Edit `docs/engine-map.md`**

The Glow content is **not** in the fan-in tables — `:171-176` and `:186-190` have
no Glow column. It is in §4, the write-side index: `:272`, `:274`, and the whole
subsection `:277-291` "Settled: does a Sampler deck's PITCH lane modulate under
Glow?".

**Restate, do not delete.** The `_active[slot]` finding — that `Fireflow.cpp:881`
is its only writer — is core-engine truth, and `tests/test_param_impact.cpp:178-185`
depends on it. Rewrite the subsection as a statement about the `Fireflow` module
without the Glow comparison.

Also: the citation rows at `:43-44`, §7 ("Reachability: what a terrain actually
draws") in full, and the probe recipe at `:376-381`, which compiles
`terrain.cpp`.

- [ ] **Step 4: Edit `CLAUDE.md`, `README.md`, `host/vcv/README.md`**

`CLAUDE.md`: remove the `engine/flow/` and param-map rows, and the Glow panel
paragraph at `:63`. Line `:20` is the VCV module row and names **both**
`Fireflow` and `Glow` — **edit it, do not delete it**, or the surviving module
loses its map entry.

`README.md:152-155` describes Glow driving `engine/flow/`. `host/vcv/README.md`:
its whole "FireFlow Glow" section.

- [ ] **Step 5: Note the stale release notes**

`docs/release-notes.md:11-56` is entirely the Glow Touch-2 release, and per the
release process its body becomes the next GitHub release. This plan does not
bump the version, so add one line at the top of that file stating it describes a
module that no longer exists and must be rewritten before the next tag.

- [ ] **Step 6: Remove the stale worktree**

```bash
git worktree remove .worktrees/glow-hardware-panel-design
git worktree prune
```
It is a second checkout of `codex/glow-hardware-panel-design`, whose head
`3b6a27d` is an ancestor of `main`. If `remove` refuses because of local changes,
stop and report rather than forcing.

- [ ] **Step 7: Run the completeness check**

```bash
grep -rn "engine/flow\|spky::flow\|Glow\|terrain" \
    engine/ host/ tests/ shell/ bench/ CMakeLists.txt
```

Scoped deliberately — an unscoped grep cannot pass, because `Glow` and `terrain`
legitimately survive in `docs/roadmap.md`'s untouched Done entries,
`docs/milestone-history.md` and `docs/attic/`.

Within that scope, only these may appear. Anything else is a miss:

- `tests/test_flow_melody.cpp` and `tests/test_flow_melody_wiring.cpp` — comments
  mentioning Glow
- `tests/param_impact_points.h` — the provenance comments
- `kColGlow` / `GLOW` in `host/vcv/res/gen_panel.py` and
  `host/vcv/src/generated_panel.hpp` — a panel *colour*
- `glow` as an LED brightness term in `host/vcv/src/Fireflow.cpp`
- `ModGlow` in `src/core/config.h` and `src/ui/core.ui.cpp` — the upstream
  Spotykach firmware, history, out of scope

This is a review step, not a test. A test asserting the absence of a string is
the kind of gate that cannot go red for the right reason.

- [ ] **Step 8: Full build, test, commit**

```bash
rm -rf build && source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
git add -A
git commit -m "docs: the tree stops describing a layer it no longer has

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

Paste the completeness-check output into the commit message body.
