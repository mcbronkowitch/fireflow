# Pitch RANGE Law Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On a deck that plays notes, RANGE becomes the melody's width around a
fixed centre, the pitch chain reflects instead of clamping, and a deck that
visited BBD comes back with its PITCH base at 0.5.

**Architecture:** Three independent mechanisms, one per task: a centred
RANGE law selected per lane by `_note_lane()` (`engine/mod/`), one reflection
in `Part::pitch_pre_quant()` gated by `_note_deck()` (`engine/parts/`), and a
PITCH base write in the control law (`control/control_law.h`). Then the render
scenarios get their base compensation, and the docs and the two other builds
follow.

**Tech Stack:** C++17, doctest, CMake + Ninja + clang (engine, tests, render
host), ARM GCC (`shell/`), `host/vcv/build-local.sh` (Rack).

**Spec:** `docs/superpowers/specs/2026-10-26-pitch-range-law-design.md`
(accepted 2026-10-26, D1 = semitone 18, D2 = no floor). Section numbers below
(§4.1, A3, …) are the spec's.

## Global Constraints

- Desktop build: `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  and `cmake --build build`. Release is mandatory; never MSVC. **ctest does not
  build**: build first, every time.
- Test run: `ctest --test-dir build -E "^reva_" --output-on-failure` (nothing
  under `hardware/` is touched; the reva guards orphan kicad-cli).
- Never prefix a shell command with `cd`; never `source env.sh` in the shell
  that builds `shell/`.
- The gate is the engine class: `_note_lane()` in the lane, `_note_deck()` in
  the part. Never `slot == LANE_PITCH`, never `_melody_engine_on()`, never
  `_melodic` alone (spec §8, invariant I3: a BBD's PITCH lane is the delay
  clock).
- No libm call (`fmodf`, `floorf`, …) in the reflection: it runs on every
  Part control tick on the Daisy (spec §9).
- `apply_range` and the existing cases of `tests/test_range.cpp` stay as they
  are.
- Every new test is shown RED once against the build it guards against; the
  red run's output goes into the task report.
- Every number written into a test comment or a threshold comes from a run of
  that test, never from the spec's P1/P2 tables (they are a different path).
- Everything written into the repo is English. Commit trailer:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Work on a branch `pitch-range-law` in this checkout, not in a worktree
  (worktrees lack `env.sh` and silently pick the wrong compiler).
- The controller re-runs the build and ctest itself at every task boundary;
  an implementer's "green" is not evidence.

## Review Focus

1. **TUNE and the song offset at their extremes on a note deck** (TUNE 0 or 1,
   offset ±max, RANGE 1, base + mod at its own extreme): the reflection must
   land inside [0, 1] for every reachable sum. Pinned by Task 2's bound test.
2. **The S&H (stepped) reading of the PITCH lane** that FLUX_A/B and TUNE_A/B
   take with a negative MOD depth (spec §4.4): it must carry the centred law
   too. Pinned by a stepped-output check in Task 1's A1 test.
3. **Leaving BBD mid-crossfade**: while the old engine still sounds,
   `engine_id(p)` is BBD and STAGES must keep owning the base; 0.5 arrives
   once the note engine has landed. Pinned by A10 sampling the base only after
   the engine id reads back as the init engine (Task 3).
4. **The control-law cache**: the new per-tick PITCH base write must go through
   the same cached setter path as `LANE_SIZE`, so a repeated tick still sends
   only uncached setters. Guarded by the existing `test_law_cache.cpp` and
   `test_skip_unchanged.cpp`, which must stay green in Task 3.
5. **STAGES on a BBD deck**: the new write must not take the base away from
   STAGES while the deck is BBD — the delay clock is played from it. Pinned by
   A10 checking `target_raw(LANE_PITCH)` equals each deck's STAGES (1.0 / 0.0)
   during the BBD phase (Task 3).

---

### Task 1: The centred law on a note lane (§4.1, A1, A2, A3, A6)

**Files:**
- Modify: `engine/mod/range.h` (new function + header comment)
- Modify: `engine/mod/lane.h` (private helper declaration, near `_note_lane()` at `:331`)
- Modify: `engine/mod/lane.cpp:656`, `:1001`, `:1252`
- Modify: `tests/test_range.cpp` (direct cases)
- Modify: `tests/test_melody_reachable.cpp:94-113` (A2)
- Modify: `tests/test_param_impact.cpp:373-429` (A6)
- Create: `tests/test_pitch_range_law.cpp` (A1, A3); add it to the `spky_tests`
  source list in `CMakeLists.txt`, after `tests/test_song_offset.cpp`

**Interfaces:**
- Produces: `inline float apply_range_centred(float v, float r)` in
  `engine/mod/range.h`, namespace `spky`. r clamped to [0, 1] like
  `apply_range`; returns `0.f` for r ≤ 0, else `v * r`.
- Produces: `float ModLane::_ranged(float v) const` (private) —
  `_note_lane() ? apply_range_centred(v, _range) : apply_range(v, _range)`.
  All three call sites go through it.
- Produces: `tests/test_pitch_range_law.cpp` with a file-local helper
  `std::vector<float> lane_stream(bool note_engine, bool step, float range, uint32_t seed)`
  built like `stream()` in `test_melody_reachable.cpp` (set_melodic before
  init, then `set_flow_melody(note_engine)`, 8 steps, 0.5 Hz, SMOOTH 0, VARY 0),
  but with `set_range(range)`. Task 2 adds tests to this file.

- [ ] **Step 0: Keep a stock render binary for A7**

Build Release on the branch point and copy `build/render.exe` to
`build/render_stock.exe`. Task 2 renders against it.

- [ ] **Step 1: Write the failing tests**

`tests/test_range.cpp`, new cases (exact equality — the law is one multiply):
- `"range: centred law is off at zero"` — `apply_range_centred(±1, 0) == 0`,
  and `apply_range_centred(1, -0.2f) == 0`.
- `"range: centred law is a pure width"` — for v ∈ {−1, −0.5, 0.3, 1} and
  r ∈ {0.1, 0.25, 0.5, 0.75}: `apply_range_centred(v, r) == v * r`;
  and `apply_range_centred(0, 0.5f) == 0` (today's law gives 0.5 there).
- `"range: both laws agree at full range"` — for 2001 v over [−1, 1]:
  `memcmp` of `apply_range_centred(v, 1)` and `apply_range(v, 1)` is 0.

`tests/test_pitch_range_law.cpp`:
- **A1** `"pitch range law: a note lane emits r times its RANGE-1 stream"` —
  for step ∈ {false, true}, r ∈ {0.1, 0.25, 0.5, 0.75}, seed
  `0x1234abcdu + 2u * 2654435761u`: `lane_stream(true, step, r, seed)[i] ==
  r * lane_stream(true, step, 1.f, seed)[i]` for every i (exact), and the
  r = 0 stream is all zeros. The same comparison on `ModLane::stepped_output()`
  (`lane.h:211`, the S&H twin `_latch_stepped` writes), read after every
  `process()`: r times the RANGE-1 lane's, exact (Review Focus 2).
- **A3** `"pitch range law: noon plays a melody"` — an `Instrument` on deck A,
  SYNTH, `set_sync(true)`, `set_step(PART_A, true, 8)`, `set_scale(SCALE_DORIAN)`,
  `set_root(PART_A, 0)`, `set_tune(PART_A, 0.5f)`, `set_variation(PART_A, 0.f)`,
  `set_smooth(PART_A, 0.f)`, `set_range(PART_A, 0.5f)`. 200 phrases, each
  started with `new_phrase(PART_A)`; one note per PITCH fire
  (`lane_fired(PART_A, LANE_PITCH)`), read as
  `lround(target_value(PART_A, LANE_PITCH) * 36)` on the sample before the next
  fire. Two shares: one-note phrases, rail notes (0 or 36). CHECK each below its
  threshold.

  Thresholds: run the test once on stock (`git stash` the lane change, keep the
  test) and once on the new build, write both readings into the test comment,
  and set each threshold so both readings clear it by at least a factor of two
  (spec A3). If the two readings are not a factor of four apart, stop and
  report — the path does not show the effect.

`tests/test_melody_reachable.cpp`, **A2**: extend `"SAMPLER and BBD PITCH lanes
are untouched"` with r ∈ {0.1, 0.25, 0.5, 0.75}: `stream` at that RANGE equals
`apply_range(v, r)` of the RANGE-1 stream, sample for sample, both modes
(`stream()` gets a `range` parameter defaulting to 1). Add the same check to
`"non-melodic lanes are untouched"` as a boundary pin; its comment already says
why it cannot go red.

- [ ] **Step 2: Run them and see them fail**

Add `apply_range_centred` as a stub returning `apply_range(v, r)` so the tests
compile. Build; run `build/spky_tests.exe -tc="range:*,pitch range law:*,melody-reachable:*"`.
Expected: `"centred law is a pure width"`, A1 and A3 FAIL; A2 passes (it pins
what must not change).

A2's RED: temporarily gate `_ranged` on `_melodic` alone; A2 must fail; revert.

- [ ] **Step 3: Implement `apply_range_centred` and `ModLane::_ranged`, and route
  `lane.cpp:656`, `:1001`, `:1252` through `_ranged`**

`range.h`'s header comment says which lanes use which law and why (one
paragraph: on a note lane RANGE is a width around the PITCH base; elsewhere the
amplitude law; both agree at r = 0 and r = 1).

- [ ] **Step 4: Retire FORM_A and FORM_B from `expected_sample_bound` (A6)**

In `test_param_impact.cpp`, empty the `expected_sample_bound` group (or remove
it with its uses — spec §12 leaves the choice) and replace the FORM_A/FORM_B
comment block with a dated note: 2026-10-26, spec `pitch-range-law`; under the
centred law both move audio in both modes; FORM_A's dead FLOW reading was the
lane sitting on the top of the span ("pre-quantize 36.0 throughout"), which the
upward shift of the old law produced. Keep the ban text ("Nothing may be added
here without a measurement…") if the group stays.

- [ ] **Step 5: Build, run the full suite**

Run: `ctest --test-dir build -E "^reva_" --output-on-failure`
Expected: all green, `ctrl_identity` and `wave_formant_sweep` included (the
law alone moves neither: `ctrl_identity` never sets RANGE). If
`param impact: a live parameter works in both operating modes` still fails,
read its message before changing anything else.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/range.h engine/mod/lane.h engine/mod/lane.cpp tests/test_range.cpp tests/test_melody_reachable.cpp tests/test_param_impact.cpp tests/test_pitch_range_law.cpp CMakeLists.txt
git commit   # "feat(engine): RANGE is a width around the centre on a note lane"
```

---

### Task 2: One reflection on a note deck (§4.2, A4, A7)

**Files:**
- Modify: `engine/util/math.h` (new helper)
- Modify: `engine/parts/part.cpp:175-189` (`pitch_pre_quant()` and its comment)
- Modify: `tests/test_pitch_range_law.cpp` (A4, bound test)
- Modify: `tests/test_song_offset.cpp:78-81`, `:276-290` (words only)
- Modify: `CMakeLists.txt:338-358` (`ctrl_identity` EXPECTED + reason)

**Interfaces:**
- Consumes: Task 1's test file and its Instrument setup for A3.
- Produces: `inline float reflect_unit(float x)` in `engine/util/math.h`,
  namespace `spky`: folds x into [0, 1] by mirroring at 0 and 1, exact for
  x ∈ [−2, 3] with a fixed number of branch-only passes (two suffice for that
  interval: `x < 0 → −x`, `x > 1 → 2 − x`, repeated once), then `clampf(x, 0, 1)`
  as a NaN-free backstop. No libm.

- [ ] **Step 1: Write the failing tests**

In `tests/test_pitch_range_law.cpp`:
- `"pitch range law: reflect_unit folds the reachable interval"` — for 5001 x
  over [−2, 3]: result in [0, 1], and equal (within 1e-6 absolute) to the
  triangle fold `t = x mod 2 (into [0, 2)); t > 1 ? 2 − t : t` computed in
  double in the test. Plus identities: `reflect_unit(x) == x` bit for bit for
  1001 x over [0, 1].
- `"pitch range law: the pitch sum stays inside the fold's interval"` — the
  bound (spec §9, §12): an Instrument deck A on SYNTH with PITCH base 1 and 0
  (`set_target_base`), MOD depth at its maximum, RANGE 1, TUNE 0 and 1, root
  turned to the offset's extremes (find the latch range in `part.h` near
  `_song_offset`); render 30 s per corner and CHECK
  `base + mod + (tune − 0.5) + offset` (recompute from the part's accessors, or
  expose the unreflected sum under `SPKY_TESTING`) never leaves [−2, 3].
  Write the measured extremes into the comment.
- **A4** `"pitch range law: TUNE away from the centre does not pile notes"` —
  A3's setup with `set_scale(<Minor pent index, SCALE_NAMES[6]>)`,
  `set_tune(PART_A, 0.1747f)`, `set_variation(PART_A, 0.671f)`,
  `set_range(PART_A, 1.f)`: rail-note share below a threshold, set by the A3
  rule from three readings — stock, Task 1 alone (law, no reflection) and
  the new build — all three in the comment. RED against stock **and** against
  Task 1's build; that second red is the proof that the reflection exists.

- [ ] **Step 2: Run them and see A4 and the fold test fail**

Run: `build/spky_tests.exe -tc="pitch range law:*"` on Task 1's build with a
stub `reflect_unit` that returns `clampf(x, 0, 1)`.
Expected: the fold test and A4 FAIL; the bound test passes or reports the
extremes.

- [ ] **Step 3: Implement `reflect_unit` and the note-deck branch of
  `Part::pitch_pre_quant()`**

On `_note_deck()`:
`reflect_unit(_base[LANE_PITCH] + _mod_term(LANE_PITCH) + (_tune - 0.5f) + _offset_norm())`
— the additions in exactly this order, so a sum that today's clamps leave alone
is bit-identical. No D1 constant (D1 = 18 adds 0.0f; spec §12). Off a note
deck, today's expression unchanged. Rewrite the comment above it (`:175-186`):
the offset and TUNE now act before the bound, and a note that leaves the span
is mirrored at the edge.

- [ ] **Step 4: Fix the words in `test_song_offset.cpp`**

`:78-81` ("at the edges an offset song folds back onto the nearest in-range
note") → it is mirrored at the edge. Rename `"song offset: clamp holds at the
span edge"` to `"song offset: the pitch stays inside the span at the edge"`;
its in-code comment says the reflection, not a clamp, keeps it inside.
Assertions unchanged.

- [ ] **Step 5: A7 — compare all 35 renders against stock, on unchanged
  scenario files**

For every `host/render/scenarios/*.json`, render with `build/render_stock.exe`
and `build/render.exe` into the scratchpad and compare `sha256sum`s. Expected
moved set, exactly these fifteen: `ambient_wash`, `chord_bloom`,
`flux_mono_dry`, `flux_mono_verb`, `flux_tape_echo`, `reverb_delay`,
`reverb_wash`, `assets_percussive_source`, `body_sympathetic`, `ctrl_identity`,
`demo_density_sweep`, `demo_song_aaab`, `demo_song_modes`, `demo_step_melody`,
`pace_sweep`. Any other scenario moving — `wave_formant_sweep` or any
`sampler_*` above all — is a finding: stop and report.

- [ ] **Step 6: Re-baseline `ctrl_identity`**

By the procedure in `tests/check_render_hash.cmake`: the new hash from the
render and from the failing ctest's "got" must agree. The comment above
`EXPECTED` says: moved by spec 2026-10-26 pitch-range-law §4.2; the scenario
never sets RANGE, so the centred law leaves it bit-identical, and the
reflection mirrors the pitch sums that leave the span instead of clamping them.

- [ ] **Step 7: Build, run the full suite**

Run: `ctest --test-dir build -E "^reva_" --output-on-failure`
Expected: all green, `wave_formant_sweep` unchanged.

- [ ] **Step 8: Commit**

```bash
git add engine/util/math.h engine/parts/part.cpp tests/test_pitch_range_law.cpp tests/test_song_offset.cpp CMakeLists.txt
git commit   # "feat(engine): a note deck reflects its pitch sum instead of clamping"
```

---

### Task 3: A note deck's PITCH base returns to 0.5 (§4.5, A5, A10)

**Files:**
- Modify: `control/control_law.h:670-699`
- Modify: `tests/test_pitch_range_law.cpp` (A5, A10)
- CMake: `tests/test_pitch_range_law.cpp` now includes `control/control_law.h`
  and `control/params.hpp`; check the `spky_tests` target already has their
  include paths (`test_controls_map.cpp` uses both with a real
  `spky::Instrument`).

**Interfaces:**
- Consumes: nothing from Tasks 1–2 beyond the test file.
- Produces: no new API.

- [ ] **Step 1: Write the failing tests**

Rig as in `test_controls_map.cpp:125-126`: `spky::Instrument` (heap, it is
large) + `control::ControlLaw law; law.on_restore();`, knobs from
`ffctl::kInitParamDefaults`, `law.tick()` every 96 samples, `process()` in
96-sample blocks.

- **A10** `"pitch range law: a deck that visited BBD comes back centred"` —
  4 s on the init knobs; `ENGINE_A = ENGINE_B = 4` (BBD) for 4 s, REQUIRE both
  `engine_id(p) == spky::ENGINE_BBD` and CHECK `target_raw(LANE_PITCH)` is
  1.0 on deck A and 0.0 on deck B (STAGES still owns it, Review Focus 5); back to the init ENGINE values for 6 s,
  REQUIRE both engine ids equal their init-phase ids (Review Focus 3). Then
  CHECK `part(p).target_raw(LANE_PITCH) == 0.5f` on both decks and
  `lround(pitch_cv(p) * 36) == 19` (deck A) and `== 7` (deck B).
  Stock reads 1.0 / 0.0 and 36 / 0 (spec P5).
- **A5** `"pitch range law: the factory patch keeps its drones"` — init knobs
  only, 4 s settle, then 60 s: on every block deck A's
  `lround(pitch_cv(0) * 36) == 19` and deck B's `== 7`. Green on stock; its red
  is against a centre of 12 or 24 — show it by temporarily adding
  `(24.f - 18.f) / 36.f` to the note-deck sum in `pitch_pre_quant()`; revert.

- [ ] **Step 2: Run them; A10 fails, A5 passes**

Run: `build/spky_tests.exe -tc="pitch range law:*"`
Expected: A10 FAIL with 1.0 / 0.0; A5 PASS; then A5 FAIL under the centre-24
sabotage, PASS after reverting (revert by editing back, not `git checkout` of
the file — the sabotage proof must not throw away the task's own edits).

- [ ] **Step 3: Write the base on every tick**

In the ledger block: `set_target_base(p, LANE_PITCH, …)` gets STAGES on a BBD
deck and `0.5f` on every other engine, every tick, through the same call (and
the same cache) the BBD write uses today. Ledger comment for LANE_PITCH:
BBD-only STAGES, restored to 0.5f when the deck is not BBD — the LANE_SIZE
pattern — and why (spec §3.6: a BBD visit used to leave STAGES as the note
deck's melody centre).

- [ ] **Step 4: Build, run the full suite**

Run: `ctest --test-dir build -E "^reva_" --output-on-failure`
Expected: all green, including `test_law_cache.cpp`'s `"law cache: a repeated
tick sends only the uncached setters"` and `test_skip_unchanged.cpp`. If the
cache test counts the new call as uncached, route the write through the cache
like `LANE_SIZE`'s — do not loosen the test.

- [ ] **Step 5: Commit**

```bash
git add control/control_law.h tests/test_pitch_range_law.cpp
git commit   # "fix(control): a note deck's PITCH base returns to 0.5 after BBD"
```

---

### Task 4: Render scenarios keep their register (§4.6)

**Files:**
- Modify: `host/render/scenarios/{ambient_wash,chord_bloom,reverb_delay,reverb_wash,flux_mono_dry,flux_mono_verb,flux_tape_echo}.json`
- Modify: `docs/gotchas.md:203-207`

**Interfaces:** none.

- [ ] **Step 1: Raise each note deck's PITCH base by min(r, 1 − r)**

New `set_target_base` value on slot 2 (PITCH), from the files as they stand
today (base = 0.5 where none is set):

| Scenario, deck | RANGE | base today | new base |
|---|---|---|---|
| `ambient_wash` A | 0.45 | 0.55 | 1.0 |
| `ambient_wash` B | 0.2 | 0.2 | 0.4 |
| `chord_bloom` A | 0.3 | 0.25 | 0.55 |
| `reverb_delay` A | 0.4 | 0.3 | 0.7 |
| `reverb_wash` A | 0.4 | 0.3 | 0.7 |
| `flux_mono_dry` A | 0.4 | — (0.5) | 0.9 |
| `flux_mono_verb` A | 0.4 | — (0.5) | 0.9 |
| `flux_tape_echo` A | 0.4 | — (0.5) | 0.9 |

Re-read each file before editing; if a base or RANGE differs from this table,
stop and report. Where a scenario sets no base, add a `set_target_base` line
on slot 2 next to its `set_range` (and `set_target_active` slot 2 if the file
pattern needs it — check what the deck's PITCH slot does by default first).
Each changed line carries a `_comment`: base raised by min(RANGE, 1 − RANGE)
for spec 2026-10-26 pitch-range-law §4.6, which keeps today's centre.

- [ ] **Step 2: Retire the gotcha**

`docs/gotchas.md:203-207`: the advice to keep a PITCH base at about 0.15–0.3
against the upward shift goes. In its place, one line: on a note deck the
PITCH base is the melody's centre at every RANGE (spec 2026-10-26).

- [ ] **Step 3: Render the seven and check them**

Render each with `build/render.exe`; expected: no crash, and a quick peak/RMS
read shows audio. Neither hash-gated scenario is among them, so ctest is
unaffected; run it anyway.

Run: `ctest --test-dir build -E "^reva_" --output-on-failure` — all green.

- [ ] **Step 4: Commit**

```bash
git add host/render/scenarios docs/gotchas.md
git commit   # "test(render): scenarios keep their register under the centred RANGE law"
```

---

### Task 5: Docs, firmware and Rack (§13, A8)

**Files:**
- Modify: `docs/engine-map.md` (§1, §6, §7)
- Modify: `docs/superpowers/specs/2026-08-14-melody-reachable-design.md` (§1.3, §2.2, gates G3/G4)
- Modify: `docs/roadmap.md` (`:3427`, `:362`, `:700`, `:2861` — re-find by
  text, the numbers drift)
- Modify: `docs/by-ear-decisions.md`

**Interfaces:** none.

- [ ] **Step 1: Engine map**

§6: the seed rule — probe with Part-style seeds `0x1234abcd + k·2654435761` —
with the measured bias of small seeds (spec §3.4: 7 of the 10 probe seeds draw
−0.86 or lower first), and the skeleton's seed changed to match. §7, "The
melody's second gate": replace the RANGE table (seeds 12345 / 777 / 4242) with
spec §5.1's numbers under the new law, and close the "unsolved" paragraph with
a pointer to the spec. §1: mark its per-state numbers as small-seed
measurements (not re-measured here).

- [ ] **Step 2: melody-reachable spec**

A "Superseded (2026-10-26)" note at §1.3 and §2.2, shaped like the one already
in its §1.2, pointing to the new spec. G3 and G4 retire: G3 measured a drone
band of the deleted terrain generator (D2 is that question now, ruled "no
floor"); G4 counted clamped samples, which the reflection makes zero by
construction — A4 carries its intent.

- [ ] **Step 3: Roadmap and by-ear decisions**

Roadmap: the open thread "the pitch RANGE law" becomes this change's status
line (spec, branch, what landed); the three mentions point to the spec.
`by-ear-decisions.md`: D1 = semitone 18 and D2 = no floor, accepted with the
spec on 2026-10-26 as its defaults, **not yet confirmed by a listening pass** —
A9 is that pass; say so, so nobody later reads them as tuned.

- [ ] **Step 4: Firmware**

In a shell without `env.sh`:
`PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images`.
Expected: both images link. Flashing is Bastian's, as a pair, QSPI first
(`dfu-util -a 0 -s 0x90100000 -D build/shell-qspi.bin`, then
`dfu-util -a 0 -s 0x90040000:leave -D build/shell-sram.bin`). `bench/` is not
rebuilt now, but its next run must rebuild it (stale-object trap).

- [ ] **Step 5: Rack**

Run: `host/vcv/build-local.sh install`
Expected: build and install succeed; tell Bastian to restart Rack.

- [ ] **Step 6: Commit**

```bash
git add docs
git commit   # "docs: the pitch RANGE law -- engine map, roadmap, superseded gates"
```

- [ ] **Step 7: Hand over A9**

Bastian, in Rack or on the board, on the init patch: RANGE swept 0 → full;
noon plays a melody, and no deck sits on one note above RANGE 0.3. His verdict
on D1/D2 by ear goes into `by-ear-decisions.md` afterwards.
