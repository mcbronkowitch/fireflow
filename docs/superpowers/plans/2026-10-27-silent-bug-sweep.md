# Silent bug sweep (2026-10-27) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix seven silent logic bugs found in the 2026-10-27 sweep — defects that pass every test and are clearly wrong.

**Architecture:** Seven independent, small fixes in seven areas (BODY voice, engine swap, Center, phrase generator, GRIT, texture-lane sampling, firmware excitation flag). Each task carries its own RED test and touches its own files; any order works. Finding 6 of the sweep (GROW edge pile-up) was re-measured end to end and is **not** in this plan (§"Finding 6").

**Tech Stack:** C++17 engine (`engine/`), doctest (`tests/`), clang + Ninja, Release build.

**Spec:** none. The sweep's probes are the evidence; each task quotes its measurement. Probe sources (session scratch, may be gone later):
`C:\Users\bernd\AppData\Local\Temp\claude\C--Users-bernd-Documents-AI-FireFlow\c7109d4c-7b19-486b-8cc9-05bcb3de433e\scratchpad\{mod,voices,ctrl,fx,g6}\`.

## Global Constraints

- Build: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, then `ctest --test-dir build --output-on-failure -E "^reva_"`. **ctest does not build** — always `cmake --build build` first.
- Never `cd` in a shell command; no shell writes into the repo (Edit/Write tools only).
- Every new test is proven RED once against the unfixed code before the fix lands (sabotage by reverting the fix line with the Edit tool, never `git checkout <file>`).
- Render hashes in `tests/check_render_hash.cmake` will move for Tasks 1, 5 and 6. They are sanity checks, not gates: re-bake them in the task that moves them, and say in the commit which scenarios moved.
- Probe seeds are Part-style `0x1234abcd + (k + 2)·2654435761`; never small seeds.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`. All repo text English.
- Last step of the plan: `host/vcv/build-local.sh install`, then "restart Rack".

## Review Focus

- **FLOW on BODY after Task 1:** a bowed BODY deck must keep its level when the strike carries the velocity — Task 1 pins it with its FLOW test.
- **FLOW decks across an engine swap after Task 2:** a FLOW drone must come back audible, not silenced — Task 2 pins it.
- **A SAMPLER or BODY deck that never visited BBD after Task 7:** the excitation flag must stay off — Task 7's test starts from a non-BBD deck.
- **STEPS 13/15 under GROW after Task 4:** GROW mutates slots, RENEW regenerates units; the tail must now take part in RENEW without changing what GROW does — Task 4's test runs VARY −1 only, and the existing GROW tests must stay green.
- **Texture lanes with 3+ slots after Task 6:** must be bit-identical to before — Task 6 asserts it.

---

### Task 1: BODY applies velocity once

Finding: `BodyVoice::trigger` calls `_exciter.strike(_vel_target)` and `BodyVoice::process` multiplies the result by `_vel` again (`s_raw = (...) * _vel`). STEP accent 0 vs 1 measures −21 to −24 dB on BODY against −10.5 dB on SYNTH/FEED (`kAccentVelFloor` 0.3). Because `_vel` slews (~10 ms) on a ringing voice while the strike lands in the first samples, a weak note after a strong one is +7.7 dB louder than after a weak one.

Decision: the velocity lives in the strike only. The exciter's `_env` already carries it, in STEP (decaying) and in FLOW (frozen at the last strike's value).

**Files:**
- Modify: `engine/body/body_voice.cpp` (`process`, `update_control`, `set_vel`, `init`), `engine/body/body_voice.h` (drop `_vel`)
- Test: `tests/test_body_voice.cpp`

**Interfaces:** none new. `BodyVoice::set_vel(float)` keeps its signature and only stores `_vel_target`.

- [ ] **Step 1: Write the failing tests** in `tests/test_body_voice.cpp`:
  - `"BODY: a STEP accent scales the strike once"` — two fresh voices, same seed and params, `set_vel(0.3f)` vs `set_vel(1.f)`, `trigger(220.f)`, run 0.5 s with `update_control` every 96 samples. `CHECK(peak_weak / peak_strong)` in `[0.27, 0.33]`. Run it at two exciter characters: the click zone and the ping zone.
  - `"BODY: a strike's level does not depend on the previous note"` — voice X: `set_vel(1)`, strike, 0.25 s, `set_vel(0.3)`, strike. Voice Y: same with 0.3 then 0.3. Voice Z / Z': the same first strike only, no second strike. Isolate each second strike as X − Z and Y − Z'. `CHECK(20·log10(peak(X−Z) / peak(Y−Z')))` within ±0.5 dB.
  - `"BODY: FLOW level follows the strike velocity once"` — `set_sustaining(true)`, `set_vel(0.3)` vs `set_vel(1)`, strike, RMS over 1..2 s. Ratio in `[0.27, 0.33]`.
- [ ] **Step 2: Run, expect RED:** the first two must fail (ratio ≈ 0.09; +7.7 dB). The FLOW test fails too if FLOW strikes go through the same double path; either way, record all three numbers in the commit message.
- [ ] **Step 3: Implement:** remove the `* _vel` factor from `s_raw`, the `_vel` slew in `update_control`, and the `_vel` member and its two writes. Keep the `strike(_vel_target)` call.
- [ ] **Step 4: Run all tests, expect GREEN.** Re-bake any moved BODY render hashes.
- [ ] **Step 5: Commit** `fix(body): velocity scales the strike once, not twice`.

### Task 2: An engine swapped back in STEP does not resume its old note

Finding: an unselected engine is not processed, so its voices freeze. Back on the deck, SYNTH/WAVE resume at envelope 0.9065 (was 0.9086 before leaving) after 30 s or 60 s away, at about −9 dB. Only BBD is reset on swap-in (`if (_engine_id == ENGINE_BBD) _bbd.reset();` in `Part::_engine_swap`).

Decision: on swap-in **in STEP**, Part silences the incoming engine. In FLOW the drone resuming is the intended behaviour and stays.

**Files:**
- Modify: `engine/parts/engine_iface.h`, `engine/synth/env.h`, `engine/synth/voice.h/.cpp`, `engine/synth/synth_engine.h/.cpp`, `engine/body/body_voice.h/.cpp`, `engine/body/exciter.h`, `engine/feed/feed_engine.h/.cpp`, `engine/parts/part.cpp`
- Test: `tests/test_part.cpp`

**Interfaces:**
- Produces: `virtual void IPartEngine::silence() {}` — "drop every sounding voice to zero now; Part calls it on a STEP swap-in".
- `void Env::kill()` — `_level = 0`, `_stage = Stage::Idle`.
- `void Exciter::stop()` — `_env = 0`.
- `void BodyVoice::silence()` — `_exciter.stop()`, `_str_a.reset()`, `_str_b.reset()`, `_bank.reset()`, `_follower = _peak = 0`, `_hold_samples = 0`.
- `SynthEngineT::silence()` (overrides): every voice's env `kill()` (BODY: `silence()`), plus clear any pending strewn chord tones.
- `FeedEngine::silence()` (overrides): `_env.kill()`.

- [ ] **Step 1: Write the failing tests** in `tests/test_part.cpp`, one `SUBCASE` per engine SYNTH, WAVE, BODY, FEED:
  - `"Part: a STEP deck swapped away and back does not resume its old note"` — STEP, 8 steps, DECAY 1, ATTACK 0, rate 0.15 Hz. Swap to another engine 200 ms after the 3rd PITCH fire, run 2 s, swap back. Over the window from the swap landing to the next PITCH fire (at least 100 ms), `CHECK(rms_back < 1e-3f * rms_note)`, where `rms_note` is the RMS 200 ms into the original note.
  - `"Part: a FLOW deck swapped away and back keeps sounding"` — the same in FLOW. `CHECK(rms over the first 500 ms back > 0.1f * rms before leaving)`.
- [ ] **Step 2: Run, expect RED** on the STEP case for all four engines (SYNTH ratio ≈ 0.36). The FLOW case is green already: it is the guard.
- [ ] **Step 3: Implement** the interfaces above. In `Part::_engine_swap`, call `if (_step_on) _engine->silence();` right after the BBD reset line, with a comment that names the BBD reset as the same reason.
- [ ] **Step 4: Run all tests, expect GREEN.**
- [ ] **Step 5: Commit** `fix(part): a STEP deck swapped back in starts silent`.

### Task 3: `Center::init` resets the grid servo

Finding: `init()` leaves `_grid_off[2]` and `_grid_cs[2]` alone (`reset_transport()` zeroes only `_grid_off`). After a re-init (VCV: patch open, Initialize, sample-rate change), a SYNC deck with unchanged STEPS keeps the stale offset — `_rebase_grid` returns early on `cs == _grid_cs[i]` — and runs at 0.650× its rate for the pull (`kLockCap` 0.35).

**Files:**
- Modify: `engine/center/center.cpp` (`Center::init`)
- Test: `tests/test_instrument.cpp`

- [ ] **Step 1: Write the failing test** `"Instrument: a re-init leaves no stale grid offset"` — the `p_center2.cpp` setup: Instrument A at 48 kHz, `set_sync(true)`, deck 0 STEP 8, deck 1 STEP 8; 1000 blocks of 96; deck 0 STEP 5; 1000 blocks; `A.init(48000)`, re-push SYNC and STEP 5 / 8. Fresh Instrument B with the same pushes. Over 1000 blocks, compare `part(0).mod().pitch_phase()` advance per block. `CHECK(min_ratio > 0.999f)` and `CHECK(max_ratio < 1.001f)`.
- [ ] **Step 2: Run, expect RED** (min 0.650).
- [ ] **Step 3: Implement:** in `Center::init`, set `_grid_off[i] = 0.f` and `_grid_cs[i] = 1.f` for both decks — the member initialisers' values.
- [ ] **Step 4: Run all tests, expect GREEN.**
- [ ] **Step 5: Commit** `fix(center): init resets the grid servo`.

### Task 4: RENEW renews the tail slot

Finding: `regenerate_unit` (`engine/mod/phrase_gen.h`) regenerates instance slots only. The `r`-slot tail that `generate_phrase` writes under its own motif id is never touched again. Measured at VARY −1, SONG off, 60 s: the last slot never changes at STEPS 13 (k=2, L=6, r=1) and 15 (k=2, L=7, r=1). STEPS 13 and 15 are the only panel step counts with a tail.

Decision: the tail belongs to the last renewal unit (`unit == layout.motif_count - 1`). When that unit is regenerated, the tail is regenerated after it, with `pg_gen_motif(p, rng, n_ids, r, …)` exactly as `generate_phrase` does, where `n_ids` is the arrangement's `max motif id + 1`. No extra dice draw.

**Files:**
- Modify: `engine/mod/phrase_gen.h` (`regenerate_unit`; the header comment above it)
- Test: `tests/test_phrase_gen.cpp`

- [ ] **Step 1: Write the failing test** `"regenerate_unit: the last unit renews the tail"` — for STEPS 13 and 15 and every `Principle`: `generate_phrase`, copy, `regenerate_unit` for the last unit with a fresh Part-style seed, repeated 32 times with different seeds. `CHECK` that the tail slot `n - 1` changed in at least one of the 32 (pitch or gate). Add a second `CHECK`: for STEPS 12 and 16 (no tail), the result is bit-identical to today's (snapshot the `pitch[]` from the unmodified code in Step 2 and paste it in).
- [ ] **Step 2: Run, expect RED** on the tail check.
- [ ] **Step 3: Implement** the decision above.
- [ ] **Step 4: Run all tests, expect GREEN.** The GROW tests must not move: GROW goes through `_mutate_slot`, not `regenerate_unit`.
- [ ] **Step 5: Commit** `fix(mod): RENEW renews the phrase tail with the last unit`.

### Task 5: GRIT Reduce keeps both channels

Finding: one `daisysp::Decimator` and one `SampleRateReducer` serve L and R in turn. The Decimator latches when `inc_ > threshold` (`threshold = floor(i²·96)`). When `threshold + 1` is even, every latch lands on the R call, so the wet path never sees L. Measured, mix 1, 110 Hz sine into one channel: intensity 0.55 (the default) and 0.80 give silence on both outputs for L-only input and −9 dB for R-only input.

Decision: one Decimator and one SampleRateReducer per channel. Same call count, so the Daisy cost does not change. `grit.h`'s "kept as-is" note is replaced with the reason the objects were split. **This changes a sound inherited from the original firmware — Bastian listens before merge** (listening list below).

**Files:**
- Modify: `engine/fx/grit.h` (`GritReduce` members and comment), `engine/fx/grit.cpp` (`GritReduce::init`, `apply`, `process`)
- Test: `tests/test_grit.cpp`

- [ ] **Step 1: Write the failing test** `"GRIT Reduce: an input on one channel reaches the output"` — Reduce mode, mix 1, intensity ∈ {0, 0.3, 0.55, 0.8, 1}, 110 Hz sine at 0.5 into L only, then into R only, 1 s each. `CHECK` output RMS > −30 dBFS on the fed side in both cases; `CHECK` the L-only and R-only results mirror each other within 0.5 dB.
- [ ] **Step 2: Run, expect RED** at 0.55 and 0.8.
- [ ] **Step 3: Implement:** `_decimator[2]`, `_reducer[2]`, both initialised and parameterised identically in `init` and `apply`; `process` runs L through `[0]` and R through `[1]`. Keep the probe marks 17–20 at the same stages.
- [ ] **Step 4: Run all tests, expect GREEN.** Re-bake moved render hashes.
- [ ] **Step 5: Commit** `fix(grit): Reduce runs one decimator per channel`.

### Task 6: A two-slot texture lane is not silent at sine

Finding: a STEP follower lane evaluates its boundary target at the grid phase `k / slots` (`ModLane::follow`, `shuffle_phase_for_position(slot, slots, shuffle)`). With `slots == 2` (`kLaneSlotsMin` in `engine/mod/lane_len.h`), the sine is sampled at phase 0 and 0.5, which are both 0. Measured, SHAPE 0, SMOOTH 0, VARY 0, SHUFFLE 0: SOURCE at STEPS 2–4 and LEVEL at STEPS 2–3 have p2p 0.0000. The ramp on 2 slots gives −1 and 0, a −0.5 lean. `lane_len.h` says the minimum of 2 exists so that a lane "would not emit a constant value".

Decision: only when `slots == 2`, the boundary target is evaluated at the slot's centre (position `slot + 0.5`): sine ±1, ramp ±0.5, no lean, rhythm unchanged. Lanes with 3 or more slots stay bit-identical.

**Files:**
- Modify: `engine/mod/lane.cpp` (`ModLane::follow`: the boundary-evaluation calls inside the walk, the `land_only` branch and the `_follow_jumped` branch — not the final "park at the live position" call), `engine/mod/lane.h` (one private helper), `engine/mod/lane_len.h` (comment on `kLaneSlotsMin`)
- Test: `tests/test_lane_follow.cpp`

**Interfaces:**
- `float ModLane::_boundary_pos(int slot, int slots) const` — returns `slot + (slots == 2 ? 0.5f : 0.f)`, passed to `shuffle_phase_for_position` at those three sites.

- [ ] **Step 1: Write the failing tests** in `tests/test_lane_follow.cpp`:
  - `"follow: a two-slot lane swings at every SHAPE"` — a `SuperModulator` (construction as in `SuperModulator::init`, Part-style seed k = 0), STEP, TIDE 0.5, SMOOTH 0, VARY 0, SHUFFLE 0, STEPS 2; SOURCE lane, SHAPE ∈ {0, 0.25, 0.5}, 30 s. `CHECK(p2p > 1.5f)` at each SHAPE.
  - `"follow: lanes with three or more slots are unchanged"` — SOURCE at STEPS 8 and LEVEL at STEPS 16, SHAPE 0 and 0.45: snapshot 64 consecutive fired targets from the unmodified code and `CHECK` them bit for bit.
- [ ] **Step 2: Run, expect RED** on the first test (p2p 0 at SHAPE 0).
- [ ] **Step 3: Implement** `_boundary_pos` and use it at the three sites.
- [ ] **Step 4: Run all tests, expect GREEN.** Re-bake moved render hashes.
- [ ] **Step 5: Commit** `fix(mod): a two-slot lane samples its slot centres`.

### Task 7: The firmware clears "excite other deck" when a deck leaves BBD

Finding: `shell/control_tick.cpp` sets `g_opt.deck[p].excite_other_deck = true` on `ev.bbd_edge[p]`, and nothing on the hardware ever clears it (its own comment says so). After one BBD visit, a SAMPLER deck records the other deck too, and two BODY decks couple — which `part.h`'s excitation comment says must never happen unasked. Measured: FEED → BBD → BODY leaves the flag at 1 (0 without the BBD visit). The audible effect measured so far is 0.2–0.6 dB. VCV is not affected the same way: there the flag is a checkbox the player can see and clear, so VCV stays as it is.

Decision: on the firmware the flag is on exactly while the deck's engine choice is BBD — set by the entry edge as today, cleared on every tick the law reports the deck off BBD.

**Files:**
- Modify: `control/control_law.h` (`Events`: add `bool bbd_part[2] = {};`, filled with this tick's `bbdPart`), `shell/panel_step.cpp` + its header (new helper), `shell/control_tick.cpp` (call the helper instead of the inline loop; update the comment above `g_opt`)
- Test: `tests/test_panel_step.cpp`

**Interfaces:**
- `void shell::apply_excite_events(const control::Events& ev, control::Options& opt)` — for each deck: `bbd_edge` → `excite_other_deck = true`; else `!bbd_part` → `excite_other_deck = false`.

- [ ] **Step 1: Write the failing test** `"panel step: excite_other_deck follows the BBD visit"` — Events sequence for deck 0: `{bbd_edge, bbd_part}` → flag true; `{bbd_part}` → still true; `{}` (left BBD) → false. Deck 1 untouched throughout. A deck that never saw BBD stays false.
- [ ] **Step 2: Run, expect RED** (the helper does not exist: compile failure counts; then with a stub that only sets, the "left BBD" check fails).
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Run all tests, expect GREEN.** Build the firmware too: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"` in a shell that has **not** sourced `env.sh`, then `make -C shell -j8 images`.
- [ ] **Step 5: Commit** `fix(shell): excite-other-deck ends when the deck leaves BBD`.

### Task 8: Finish

- [ ] Full Release build and `ctest --test-dir build --output-on-failure -E "^reva_"`, read the output yourself.
- [ ] `host/vcv/build-local.sh install`; tell Bastian to restart Rack.
- [ ] Add each fix as one line to `docs/roadmap.md`'s status, and put Task 5 and Task 6 on the listening list below.

## Listening list for Bastian (before merge)

- **BODY, STEP with accents** (Task 1): accents are now about 10 dB deep instead of about 22 dB, and no longer smeared by the note before.
- **GRIT Reduce at the default 0.55** (Task 5): it was mono-from-R before and is stereo now. Only stereo is new: GRIT keeps its old crush rate (for L = R the output equals the old one sample for sample).
- **SOURCE/LEVEL at STEPS 2–4 with SHAPE near sine** (Task 6): they move now, where they stood still. On two-slot lanes every pure SHAPE changes: sine 0 → 1.42 p2p, triangle 2.0 → 1.0, ramp lean −0.5 → −0.25, pulse stays 2.0; blends near SHAPE ~0.15 null.

## Finding 6 (GROW edge pile-up) — re-measured, not in this plan

The sweep's lane probe showed GROW's random walk (`_mutate_slot`: step up to 2 at VARY 1, pull `kGravity` 0.10) piling up at the clamp: 23.6 % of notes at |t| = 1 at VARY 1. Re-measured end to end on 2026-10-27, through `ControlLaw::tick` on the noon frame, both decks, 600 s each, SONG nudged every 8 s (probe `g6/probe_melody2.cpp`):

| MELODY | notes at the lane edge (A / B) | flat phrases (A / B) | distinct notes per phrase (A / B) |
|---|---|---|---|
| 0 (LOOP) | 0.4 % / 0.4 % | 22.0 % / 2.0 % | 2.20 / 2.94 |
| 0.5 | 1.9 % / 2.0 % | 8.5 % / 3.0 % | 2.63 / 3.29 |
| 0.75 | 6.4 % / 8.5 % | 1.5 % / 0.5 % | 3.07 / 3.49 |
| 1.0 | 14.2 % / 21.7 % | 0.5 % / 0.0 % | 3.40 / 3.70 |

The pile-up is real, but it is not the RANGE bug's shape: GROW is the setting with the *most* movement. Flat phrases drop from 22 % to under 1 %. The two commonest notes take less share than at LOOP (deck A at RANGE noon: 49.9 % → 25.5 %), or the same (deck B at RANGE 1: 26.4 % → 26.8 %). The table is at the noon frame's RANGE; the edge shares are the same at RANGE 1. The edge notes split over two pitches (centre ± RANGE). Up to MELODY 0.75 (the init patch sits at 0.69) the edge share stays under 9 %. `kGravity` is tuned by ear. Verdict: no code change. Only if a listening pass at MELODY 1 finds the line "sticking to the walls" is it worth reopening, with a stronger pull or a reflection like the pitch chain's.

## Amendments during execution

- **Task 6:** the slot-centre rule (slot + 0.5) killed the triangle (SHAPE 0.25), and its p2p > 1.5 threshold contradicted its own ramp prediction (±0.5). A probe over offsets 0.10–0.40 and SHAPE 0..1 found no offset that keeps every SHAPE blend alive (blend nulls; S&H ends near 0.13 at any offset). Shipped: slot + 0.25 on two-slot lanes, gated at the pure SHAPEs {0, 0.25, 0.5, 0.75} with p2p > 0.9. Effects: sine 0 → 1.42, triangle 2.0 → 1.0, ramp lean −0.5 → −0.25, pulse 2.0; blend nulls near SHAPE ~0.15.
- **Task 5:** per-channel objects halved the crush rate (the shared pair was stepped twice per frame). The rate was restored by stepping each per-channel object twice per frame; a downsample-factor mapping could not match it (whole-sample holds) and the reducer clamps its frequency at 1.0.
- **Task 2:** `silence()` runs only when the engine id changes, so ENGINE A → B → A inside the fade keeps A's note; it also left the `probe_swap.bbd` timing window.
