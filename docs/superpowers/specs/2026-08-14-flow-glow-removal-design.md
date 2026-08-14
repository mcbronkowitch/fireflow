# Removing the flow layer and Glow — design

**Date:** 2026-08-14
**Status:** revision 2, approved in conversation, not yet implemented
**Supersedes:** the "Glow rework" and the flow-layer half of the M6 entry in
`docs/roadmap.md`

**Revision note.** Revision 1 was reviewed by two independent agents and did not
survive. Four of its instructions were build-breaking, one whole tracked
directory was outside its scope, its central description of `flow_params.h` was
wrong, and its verification criterion could not pass. What each review found is
folded in below rather than appended; the errors worth remembering are named at
§9 so the next reader does not re-make them.

## 1. Why

The Synthux residency closed on 2026-08-14 with a final rejection, three weeks
before the self-issued decision deadline of 4 September. Glow existed as the
small macro box to turn up with; there is nothing to turn up to. Its terrain and
macro layer, `engine/flow/`, existed to feed Glow.

This is **Variante B** of
`docs/superpowers/specs/2026-08-07-fireflow-hardware-roadmap-design.md`
§"Die Glow-Frage" — with the module dropped rather than kept as a VCV module.
That spec's timetable (Aug 2026 – Apr 2027, Phases 0–3) is unaffected and stays
the plan of record. Its largest named risk, "zwei Instrumente gleichzeitig",
disappears with this removal.

Measured 2026-08-14 on `feat/melody-reachable`: of the 156 commits made since the
place was offered on 10 August, **67** touch the flow layer or the Glow sources —
**7,158 insertions across 32 hand-written files**, plus the 12-file
`hardware/glow-faceplate/` KiCad tree. None of it ships.

## 2. Scope

**Goal:** `engine/flow/` and everything Glow leave the tree entirely, with no
dead code left behind and no test coverage silently lost.

### 2.1 The naming trap

Two unrelated things are spelled *flow* here. `engine/flow/` is the terrain and
macro layer, and it goes. FLOW as the free-running counterpart to STEP —
`_flow_melody`, `Instrument::set_flow`, `set_flow_melody`, `_flow_melody_on()` —
is instrument core and stays untouched. A cleanup driven by filenames removes the
wrong files.

Survivors that look like targets:

| File | Why it stays |
|---|---|
| `tests/test_flow_melody.cpp` | includes `mod/lane.h` only; tests the lane run mode |
| `tests/test_flow_melody_wiring.cpp` | includes `instrument.h` only; same |
| `host/render/scenarios/flow_melody.json` | contains no `flow_*` action; the FLOW-melody listening render accepted by ear 2026-08-13 |

Targets that look like survivors:

| File | Why it goes |
|---|---|
| `tests/test_flow_gesture.cpp` | includes `flow/gesture.h`, `flow/taste.h` |
| `tests/test_flow_patch_bridge.cpp` | includes `vcv/src/flow_patch_bridge.hpp` |
| `tests/test_pad_geometry.cpp` | includes `vcv/src/generated_flow_panel.hpp`; `pad_geometry.hpp` hit-tests Glow's touch pads and has no other consumer |
| `host/vcv/src/Fireflow.cpp` | **survives, but not unchanged** — see §2.2 |

### 2.2 The `Fireflow` module survives, minus one feature

The long-term intent is that `FireflowHW` is the only module in the plugin, but
while the 60 HP layout is still moving, `Fireflow` is the only surface that
reaches parameters the hardware panel does not carry. Its removal gets its own
spec once the panel is final.

It is **not** untouched by this work. `Fireflow.cpp:10` includes
`flow_patch_bridge.hpp`, and `Fireflow.cpp:1806-1820` is a context-menu action
"carry this patch onto a Glow pad" calling `to_flow_base()` at `:1838`. The
menu action, the include and the `TransferReport` handling come out with the
bridge. Without that edit the VCV build does not link.

## 3. What is deleted

**Engine** — all of `engine/flow/` except `flow_params.h`, which moves in §4.1:
`flow.cpp`, `flow.h`, `flow_ids.h`, `flow_rng.h`, `gesture.h`, `taste.h`,
`terrain.cpp`, `terrain.h`, `terrain_code.h`.

**VCV host** — `src/Glow.cpp`, `src/glow_panel.hpp`, `src/glow_panel.cpp`,
`src/glow_ui.hpp`, `src/touch_pads.hpp`, `src/pad_geometry.hpp`,
`src/generated_flow_panel.hpp`, `src/flow_patch_bridge.hpp`;
`res/gen_flow_panel.py`, `res/test_flow_panel.py`, `res/validate_glow_assets.py`,
`res/test_glow_assets.py`, `res/touch2_geometry.py`, `res/test_touch2_geometry.py`;
`res/Glow.svg`, `res/GlowRear.png`, `res/GlowFaceplate.png`, `res/GlowTouch.png`,
`res/GlowSwitch{Down,Center,Up}.png`. The `Glow` model registration in
`src/plugin.cpp` and `src/plugin.hpp`, and the `Glow` entry in `plugin.json`.

`touch2_geometry.py` is on the list because after `gen_flow_panel.py` and
`validate_glow_assets.py` go, its only remaining consumer is its own guard.
`gen_hw_panel.py:14` imports `gen_panel`, not it.

**Hardware** — `hardware/glow-faceplate/`, all 12 tracked files. The owner's
decision, 2026-08-14: delete rather than attic, because git holds it. §5's
`README.md` records the commit hash it was deleted in, so recovering it is a
`git show` rather than a `git log --all --diff-filter=D` hunt.

**Render host** — the `flow::Flow` instance and its control-rate call in
`main.cpp`; `Scenario::has_flow`, `Scenario::flow_code` and the `flow_*` action
branch in `scenario.{h,cpp}`; scenarios `flow_calm_corner.json`,
`flow_new_ride.json`, `flow_pace_sweep.json`, `flow_pace_sweep_sampler.json`,
`flow_smoke.json`.

**Tests (17)** — `test_flow_audio`, `test_flow_chord_reach`, `test_flow_gesture`,
`test_flow_mode`, `test_flow_new`, `test_flow_overlay`, `test_flow_patch_bridge`,
`test_flow_rng`, `test_flow_runtime`, `test_flow_taste`, `test_flow_terrain`,
`test_flow_terrain_code`, `test_flow_transfer_diff`, `test_flow_veto`,
`test_glow_panel_manifest`, `test_glow_ui`, `test_touch_pads`,
`test_pad_geometry`.

`test_flow_params.cpp` is deliberately absent — see §3.1.

**CMake, by line** — `:143` (`glow_panel.cpp` in `spky_tests`), the test-target
entries above including `:170` (`test_flow_rng`) and `:142`
(`test_pad_geometry`), the two `engine/flow/*.cpp` source entries at `:174` and
`:181`/`:218-219`, `:293-297` (`add_custom_target(glow_assets_guard ALL …)`),
**`:299` (`add_dependencies(render glow_assets_guard)`)**, `:313-317`
(`touch2_geometry_guard`), `:319-323` (`glow_assets_guard` CTest), `:328-333`
(`flow_panel_guard` and its `DEPENDS`).

Line `:299` is the one that bites. It is not a CTest guard but a build-graph edge
from the **render executable** to the Glow PNG validator: delete the custom
target without it and `cmake` fails to configure, taking both render-hash gates
(`ctrl_identity`, `wave_formant_sweep`) with it.

**`host/vcv/Makefile`, by line** — `:45-46` (`engine/flow/*.cpp` sources),
`:89-91` (Glow distributables), **`:96-100`** (`.PHONY: validate_glow_assets` and
the `all dist: validate_glow_assets` prerequisite). Missing `:96-100` breaks
`make` once the script is gone — which is exactly the manual check §7 relies on
to prove the VCV host still builds.

**Working tree** — the stale worktree `.worktrees/glow-hardware-panel-design/`,
a second checkout of `codex/glow-hardware-panel-design`, whose head `3b6a27d` is
an ancestor of `main`. Verified clean.

### 3.1 Survivors that need edits, not deletion

- **`tests/test_flow_params.cpp` → `tests/test_param_table.cpp`.** Its two cases
  assert nothing Glow-specific and carry over with the include, namespace and
  case names changed. Deleting the only test of a header that survives §4.1
  would leave `apply_param` uncovered. It additionally gains the two table-row
  assertions rescued from `test_flow_mode.cpp` (§4.4).
- **`tests/test_scenario.cpp`** — drop the `flow_*` action cases *and* the
  file-scope `#include "flow/flow.h"`, `#include "flow/terrain_code.h"` and
  `using namespace spky::flow;` at `:7-10`.
- **`host/vcv/src/Fireflow.cpp`** — remove the Glow-pad menu action (§2.2).
- **`host/render/scenarios/`** — PACE loses both its demo scenarios. PACE is an
  engine feature and survives, so one replacement scenario exercising
  `set_pace()` without the flow layer is written in the same commit. Neither old
  scenario is gated in CTest; they were hand-run listening material.

## 4. What moves out, and what has to be rebuilt

### 4.1 `engine/flow/flow_params.h` → `engine/param_table.h`

This file is not a Glow feature. It is a reflection table over `Instrument`'s
setters: an X-macro whose rows are `X(id, lo, hi, steps)`, expanded into the
`ParamId` enum, into `constexpr ParamInfo kParams[P_COUNT]`
(`{ const char* name; float lo, hi; int steps; }`), and accompanied by
`clamp_to()` and `apply_param(Instrument&, int, float)`.

**There is no story-ownership column.** Revision 1 claimed the fourth macro
argument was story ownership and instructed the implementer to drop it; it is the
discrete-step count, and dropping it breaks `ParamInfo`, `kParams` and
`apply_param`'s rounding. Nothing in the file is Glow-specific except two prose
comments and one unused include.

The complete edit:

1. drop `#include "flow/flow_ids.h"` at `:3` — nothing in the file uses it, and
   the header is deleted by §3;
2. rename `SPKY_FLOW_PARAMS` → `SPKY_PARAMS` and its two expander macros to match;
3. `namespace spky::flow` → `namespace spky`;
4. strip the story-overlay rationale from `P_MODE`'s comment (the stream-key
   paragraph governed terrain draws) and from `P_PACE`'s, keeping in both the
   statements that are about the instrument: `Instrument::set_sync` is global,
   and `P_PACE` carries a live offset.

Its consumers afterwards are `tests/test_param_impact.cpp` and
`tests/test_param_table.cpp`. It stays in `engine/` because it is the
machine-readable mirror of `instrument.h` and M6 step 2 needs this dispatch to
map panel positions onto setters — subject to §4.2.

### 4.2 The hole in `apply_param`, named rather than inherited

`flow_params.h:213-217` refuses three parameters:

```
// P_MODE, P_STEPS_A and P_STEPS_B are deliberately NOT handled here.
// set_step() takes mode AND count together and set_sync() is global, so
// routing them needs all three values at once … Flow::push_mode_and_steps() owns them.
case P_MODE: case P_STEPS_A: case P_STEPS_B: break;
```

`Flow::push_mode_and_steps()` was the compensating mechanism and dies with
`flow.cpp`. A panel driven through `apply_param` alone could not set the
operating mode or either step count — silently, via a `break`.

So the move adds a free function beside it:

```
void apply_mode_and_steps(Instrument&, bool step_mode, int steps_a, int steps_b);
```

taking all three at once and issuing `set_sync` and both `set_step` calls, which
is what `test_param_impact.cpp`'s `apply_patch()` already does by hand at
`:161-165`. This is not scope creep: without it, §4.1's justification for keeping
the header in `engine/` is false, and M6 inherits a trap of exactly the shape the
memory index already catalogues twice.

### 4.3 The operating points → `tests/param_impact_points.h`

`tests/test_param_impact.cpp` — the gate written so a parameter cannot silently
go dead, the one that would have caught the DIRT macro — draws its operating
points from `TerrainState`: `pick_terrains()` at `:234-250` takes the first two
terrains per mode that pass its filters out of masters 1..**399**.

**Exactly those four points are frozen**, not a wider or better-chosen set. The
owner's decision, 2026-08-14, and it is the cheap one for a specific reason:
`:411-423` pins the `P_FORM_B` sample-bound tolerance to *"masters 3 and 8"* by
name, and `:391-393` marks `P_SONG_A` untraced under the same draw. Moving the
points means re-measuring three hand-maintained expectation lists in the same
commit or the commit lands red.

Revision 1 proposed greedy selection over eight points per mode and called it
"stronger, not weaker". That is true for gate 1 (the dead-parameter set) and
**false for gate 2**, which compares `moves_audio(flow)` against
`moves_audio(step)` at `:436` — a difference between two samples, where
maximising coverage independently per mode manufactures exactly the sample-bound
false positives the file already spends 29 lines apologising for. It also costs
roughly 6,300 four-second stereo render pairs to compute, a figure revision 1
did not state. Widening the gate is a separate round, and §8 records that after
this deletion it can no longer draw candidates and would have to author them.

The frozen header holds, per point: a comment naming the master seed and mode it
came from, the full `float v[P_COUNT]` vector, and the `step` / `steps_a` /
`steps_b` triple. A sparse `{ParamId, float}` list — revision 1's format — cannot
represent the triple, which exists precisely because of the `apply_param` hole in
§4.2.

**`deck_audible()` stays, as a live `REQUIRE` per point.** Today `pick_terrains()`
re-runs it on both decks on every build; it is a precondition about the
instrument's current state, not about the terrain. Frozen without it, the next
change that leaves a deck near-silent at a frozen point reports that deck's
entire parameter set as dead and reads as an instrument regression — and the
file's own header at `:116-121` records that this rig already made that exact
mistake once. Two renders per point converts a silent wrong answer into a loud
failure.

**Stated cost, now complete:** the gate stops sweeping. A parameter alive only in
a corner these four points do not reach stays invisible — which is true of the
gate today as well, since it draws four points; what is lost is that a future
build could have drawn *different* four. The `expected_sample_bound` entry's
written instruction ("remove the entry if the terrain sample ever moves onto one
of the nine") becomes permanently unexecutable and should be reworded to say so.

### 4.4 Coverage that must be rebuilt before `test_flow_mode.cpp` is deleted

Of the 17 deleted tests, one takes core-engine coverage with it. Checked
individually: `test_flow_chord_reach.cpp`'s mechanism is covered better by
`tests/test_chord.cpp:33-47`, and `test_flow_audio.cpp`'s only core case (extreme
PACE renders finite audio) is covered by `tests/test_instrument.cpp:1643` and
`tests/test_transport.cpp:44-56`. Both are clean losses.

`tests/test_flow_mode.cpp` is not:

- **`:125-152`** is the only place in the suite that reads
  `Instrument::deck_steps()` per deck and compares it against what was pushed.
  Its own comment says so, and it verifies: `deck_steps` appears in `tests/` only
  there and in `test_flow_transfer_diff.cpp:273`, also deleted. After the removal,
  "`set_step(part, mode, count)` routes the count to the right deck" is asserted
  nowhere — and §4.2 makes the panel the next thing to drive it.
- **`:38-45`** asserts the `kParams` rows for `P_MODE` (`steps == 2`, range) and
  `P_PACE` (continuous, 0..1). `test_param_table`'s "table is sane" case is
  generic (`lo < hi`, `steps >= 2`) and does not replace them.

So the deletion commit also adds per-deck `set_step` → `deck_steps` assertions to
`tests/test_instrument.cpp`, and the two table-row assertions to
`tests/test_param_table.cpp`. Without this, §2's "no test coverage silently lost"
is not true.

### 4.5 `engine/flow/taste.h` → `docs/attic/taste-by-ear-notes.md`

`taste.h` holds rules Bastian set by ear: vetoes, redrawn curves, the archetype
window, musical weights, the COMP band move, the drone SHAPE cap
`P_SHAPE_A/B {0,.25}`, reverb MOD held low and TONE held high. Formally terrain
draw rules; in substance, claims about where this instrument sounds good.

A prose note records which values were chosen by ear, what each asserts
musically, and where it should resurface — hardware panel ranges, and the Marbles
round's question of what VARY means at each end. No code, no dependency.

These values are **not** promoted into the boot patch by this work. That changes
the factory sound, and the control-merge init trap has already cost four
regressions on exactly that move. It needs its own listening round.

## 5. The paper trail → `docs/attic/`

A new directory with a `README.md` saying in two sentences what it is —
discontinued work, kept for its reasoning rather than its code — and carrying the
commit hash in which `hardware/glow-faceplate/` was deleted.

Moved in with `git mv`, so `git log --follow` still resolves: the flow and Glow
specs and plans under `docs/superpowers/`, `docs/2026-08-13-glow-macro-audit.md`,
`docs/flow-fireflow-param-map.md`, and
`docs/superpowers/specs/2026-08-05-flow-listening-notes.md`.

## 6. Documentation

**`docs/roadmap.md`** — remove the Planned entry "Glow rework" and the ordering
paragraph above it; remove from "Done" the entries Flow patch transfer, the
Touch-2 panel rebuild and the taste tables; remove the four flow-layer paragraphs
from the M6 block. A dated line at the top records the removal, why, and where
the reasoning went. Three further entries carry premises this removal
invalidates and must be edited, not left:

- **SHAPE + SMOOTH rework**, third falsified-claim bullet, cites the terrain cap
  `P_SHAPE_A/B = {0,.25}` as a live constraint. With `taste.h` gone the
  constraint exists nowhere in the tree; the bullet is rewritten to cite the
  attic note from §4.5.
- **Marbles round**, third open question, cites the Glow macro audit's decision 7
  for its ownership model. The answer is now the panel; the citation moves to the
  attic path.
- **M6** names *preset persistence* in its preamble. `terrain_code.h` was the
  repo's only whole-patch serialiser and `flow_patch_bridge.hpp` its only patch
  transfer. Whether M6's persistence inherits anything from them is a decision
  this spec defers — §8 records it rather than leaving it implied.

**`docs/engine-map.md`** — the Glow content is **not** in the fan-in tables
(`:171-176`, `:186-190` have no Glow column). It is in §4, the write-side index:
`:272`, `:274`, and the whole subsection `:277-291` "Settled: does a Sampler
deck's PITCH lane modulate under Glow?". Restate rather than delete — the
`_active[slot]` finding that `Fireflow.cpp:881` is its only writer is core-engine
truth, and `tests/test_param_impact.cpp:178-185` depends on it. Also: the
citation rows at `:43-44`, §7 ("Reachability: what a terrain actually draws"),
and the probe recipe at `:376-381`, which compiles `terrain.cpp`.

**Dangling citations in surviving core files** — three, verified 2026-08-14:
`engine/mod/lane.h:208` (`flow/taste.h:586-588`), `engine/mod/song_ladder.h:11`
(`engine/flow/taste.h:891`) and `:40` (`engine/flow/flow.cpp:234`). All three
files survive the removal and must either cite the attic note or restate the fact
without the reference. `lane.h:266` looks like a fourth and is not — it cites
`Fireflow.cpp:892-893`, which survives.

**`CLAUDE.md`** — remove the `engine/flow/` and param-map rows. Line `:20` is the
VCV module row and names **both** `Fireflow` and `Glow`: edit it, do not delete
it, or the surviving module loses its map entry. The build section at `:63`
documents Glow's panel generator and is prose, not a row — it goes too.

**`README.md`** `:152-155` describes Glow driving `engine/flow/`.
**`host/vcv/README.md`** — its "FireFlow Glow" section.
**`docs/release-notes.md`** — the entire current body `:11-56` is the Glow Touch-2
release. Per the release process it is rewritten in a bump commit; this spec does
not bump, so it states that the file describes a module that no longer exists and
must be rewritten before the next tag.

## 7. Order and verification

Six commits. The order is forced from both ends: the two harvests need the
generator alive, and the namespace move must come **after** the deletion, because
`kParams` / `spky::flow` are consumed by roughly twenty files that live until
then. Revision 1 put the move first and was unbuildable at four of its six steps.

1. **Freeze the operating points** (§4.3). Adds `tests/param_impact_points.h` and
   switches `test_param_impact.cpp` onto it, dropping `#include "flow/flow.h"`
   but still including `flow/flow_params.h` for `ParamId`. The generator is still
   present; the expectation lists are untouched because the points are unchanged.
2. **Harvest the taste notes** (§4.5).
3. **`git mv` the paper trail into `docs/attic/`** (§5).
4. **The deletion** (§3) — including the `Fireflow.cpp` menu action (§2.2), the
   three survivor edits (§3.1), the replacement PACE scenario, the rebuilt
   `deck_steps` coverage (§4.4), and `hardware/glow-faceplate/`.
5. **`engine/param_table.h`** (§4.1) plus `apply_mode_and_steps` (§4.2), with
   `tests/test_param_table.cpp` and its two rescued row assertions.
6. **Documentation** (§6).

After each: a Release build and `ctest --test-dir build --output-on-failure`
green. Debug fails the render-hash gates and is not a valid run.

Commit 4 additionally requires two manual checks no test covers:
`host/vcv/build-local.sh` completes, and Rack loads the plugin showing exactly
two modules, `Fireflow` and `FireflowHW`.

**Completeness check**, run after commit 6 and recorded in its message. Scoped to
`engine/ host/ tests/ shell/ bench/ CMakeLists.txt` — an unscoped grep cannot
pass, because `Glow` and `terrain` legitimately survive in `docs/roadmap.md`'s
untouched Done entries, `docs/milestone-history.md`, and `docs/attic/`. Within
that scope, `engine/flow`, `spky::flow`, `Glow` and `terrain` may return only:

- the two survivors named in §2.1, whose comments mention Glow;
- the provenance comments in `tests/param_impact_points.h`;
- `kColGlow` / `GLOW` in `host/vcv/res/gen_panel.py` and
  `src/generated_panel.hpp` — a panel *colour*, unrelated;
- `glow` as an LED brightness term in `Fireflow.cpp:1421-1476,1917`;
- `ModGlow` in `src/core/config.h:96` and `src/ui/core.ui.cpp:676` — the upstream
  Spotykach firmware, which is history and out of scope (`docs/upstream-firmware.md`).

This is a review step, not a test. A test asserting the absence of a string is
the kind of gate that cannot go red for the right reason.

## 8. What this spec deliberately does not do

- It does not remove the `Fireflow` VCV module, only its Glow-pad menu action
  (§2.2).
- It does not promote any `taste.h` value into the boot patch (§4.5).
- It does not widen the parameter-impact gate. After commit 4 the generator can
  no longer draw candidate operating points, so widening means authoring them by
  hand — the cost of that is real and is the price of the clean deletion (§4.3).
- It does not decide what M6's preset persistence inherits from `terrain_code.h`
  and `flow_patch_bridge.hpp`, the repo's only whole-patch serialiser and only
  patch-transfer implementation. That decision belongs to M6 step 2 and is
  recorded here so it is not discovered late.
- It does not bump the version or rewrite `docs/release-notes.md` beyond noting
  that it must be rewritten before the next tag (§6).
- It does not write the Marbles round's spec, though it removes that round's
  ordering dependency and answers its ownership question.
- It does not address the six open follow-ups recorded for the flow patch
  transfer. They are moot; the roadmap entry carrying them goes.

## 9. Errors revision 1 made, kept as warnings

Four are worth carrying, because each was plausible from a partial read:

1. **`flow_params.h`'s fourth column is the discrete-step count, not story
   ownership.** There is no story column in that file. The two `story` hits are
   prose comments.
2. **The `Fireflow` module is not independent of the flow layer.** Grepping for
   `Glow` in filenames finds the module; the coupling was a menu action in the
   surviving module's source.
3. **`glow_assets_guard` is a build target before it is a CTest guard**, and the
   `render` executable depends on it. Reading the CTest registrations alone
   misses `CMakeLists.txt:299`.
4. **Ordering a header move first "because it is the smallest change" is
   backwards** when the header serves the thing being deleted. The move is last.
