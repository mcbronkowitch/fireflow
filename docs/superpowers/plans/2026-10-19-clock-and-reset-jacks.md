# CLOCK and RESET jacks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One shared clock-and-reset law. VCV and the Rev A firmware both call it, it is tested on the desktop, and it fixes VCV's double-tempo clock. Through it RESET restarts the song on STEP decks, landing on the clock's 1.

**Architecture:**
- A header-only `control::ClockLaw` (beside `control_law.h` and `led_law.hpp`) turns jack edges plus a sample count into events: a valid pulse, a restart (with its age for back-dating), clock lost, the lamp pulses, and the `measured_bpm` the control law already consumes.
- The engine gains `Instrument::restart(age)`: today's `reset_transport()` plus a song restart on every STEP deck.
- VCV calls the law per sample. The firmware calls it per block, from EXTI pending bits on PG13/PG14.

**Tech Stack:**
- C++17 engine and doctest (clang + Ninja, Release).
- VCV Rack plugin via `host/vcv/build-local.sh`.
- Daisy firmware `shell/` (ARM GCC, `make images`).

**Spec:** `docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md` (approved 2026-10-19). It is the authority for every behaviour; this plan names files, interfaces and tests.

## Global Constraints

**Clock**
- One pulse per quarter. No PPQN, no SHIFT function, no other jacks.
- Tempo comes from the time across the last N valid edges divided by N−1. It is accepted only in 20..400 BPM.
- The tempo handed to the engine changes only at a pulse.
- A pulse outside 20..400 BPM is ignored completely: no snap, no arming, and it does not count as running.
- The clock is running only after two valid edges. The first interval after `init()`/`discontinuity()` and after a loss is not a period.
- Clock-loss timeout: 2 × the last measured period.
- After a loss the tempo holds the last measured value, until the TEMPO knob value differs from its value at the moment of loss. Then the knob takes over with a jump. Returning valid pulses take over again.

**Reset**
- With the clock running, RESET arms, and the next valid pulse restarts.
- A RESET up to 10 ms (`0.010 * sr` samples) after a valid pulse restarts for that pulse, back-dated by the samples since the pulse.
- RESET and a pulse in the same call: the reset counts first, and that pulse restarts.
- Without a running clock, RESET restarts at once (age 0).
- Armed when the clock is lost: restart on FireFlow's next internal beat (the `beat_phase()` wrap).

**Restart**
- The transport bar goes to 0, and both decks' lanes and deck clocks go to 0 (today's `reset_transport()`).
- Every deck in STEP (`step_on(p)`) gets phrase 0 and pattern A.
- With no deck in STEP the restart does nothing.
- A FLOW deck keeps its song position.
- No `new_phrase`, and no rewind of VARY or EVOLVE. Pending song work is applied by the existing preroll.
- VCV's "Resync to bar" keeps calling plain `reset_transport()`.

**Lamps**
- CLK and RST flash per edge at the jack (any edge, valid or not).

**Hosts**
- No connection test in either host: VCV stops using `isConnected()` for the clock.
- Firmware: EXTI edge detection on lines 13/14 with the NVIC IRQ disabled. The block reads and clears the pending bits. Bits are cleared just before `StartAudio`.
- Firmware: the edge direction is falling at the pin (the input inverts), and P7 confirms it.
- The coupon image contains no gate read.
- Firmware: on `lost`, the TEMPO pot's ownership is released (`pot_release`).

**House rules**
- `source env.sh` + Release for engine/tests. Never mix toolchains: the `shell/` build runs in a shell without `env.sh`.
- Never `cd`.
- Never run `reva_*` ctest guards.
- Panels and generated files are never hand-edited.
- Everything in the repo is English.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Every new test is proven RED once.
- Use absolute float bounds, not `doctest::Approx`.
- No render hash in `CMakeLists.txt` may be re-cut: `ctrl_identity` has no clock input.

## Review Focus

1. **The FLOW deck beside a STEP deck after a reset, with COUPLE > 0.** It must not be dragged (spec §3, measured). This is pinned in Task 2, `restart: a FLOW deck is not dragged`, in GRID and in FREE at COUPLE 0.45.
2. **The factory PACE ×1/16.** Pulses move the transport by pace × pulses, not by pulses. Pinned in Task 2, `clock law drives the transport one pace step per pulse`, at PACE ×1 and ×1/16.
3. **A clock that stops and restarts within the timeout** (Pamela's stop/start). There must be no tempo excursion and no lost running state. Pinned in Task 1, `clock law: a short pause keeps the clock`.
4. **VCV sample-rate change mid-clock.** `reinit()` must call `discontinuity()`, otherwise the first period is counted at the old rate. Pinned in Task 3's source guard.
5. **Pending bits set while booting.** They must not arrive as a first edge. Pinned in Task 4: the clear sits after all init and immediately before `StartAudio` in the Rev A path (source guard), and the law needs two valid edges anyway.

---

### Task 1: The shared clock law

**Files:**
- Create: `control/clock_law.hpp`
- Create: `tests/test_clock_law.cpp`
- Modify: `CMakeLists.txt` (add `tests/test_clock_law.cpp` to `spky_tests`)

**Interfaces:**
- Produces (namespace `control`):

```cpp
struct ClockOut {
    bool     pulse        = false;  // a valid pulse: the host calls inst.clock_pulse()
    bool     restart      = false;  // the host calls inst.restart(restart_age)
    uint32_t restart_age  = 0;      // samples since the edge the restart belongs to
    bool     lost         = false;  // running -> gone on this call (firmware releases TEMPO)
    bool     clock_lamp   = false;  // any CLOCK edge at the jack
    bool     reset_lamp   = false;  // any RESET edge at the jack
    float    measured_bpm = 0.f;    // for Options::measured_bpm; 0 = the knob sets the tempo
};
class ClockLaw {
public:
    void init(float sample_rate);   // clean state; also what boot calls
    void discontinuity();           // drop the period history (reinit, un-bypass); keeps a held tempo
    // samples: elapsed since the previous call (1 in VCV, 96 per block on the board).
    // tempo_knob: raw TEMPO param 0..1, compared for change only.
    // beat_phase: inst.beat_phase(), for the armed-at-loss restart on the next wrap.
    ClockOut step(uint32_t samples, bool clock_edge, bool reset_edge,
                  float tempo_knob, float beat_phase);
    bool running() const;
};
```

- **Edge timing:** an edge seen in a call is stamped at the end of that call's samples.
- `measured_bpm` is the smoothed tempo while running, the held tempo while lost and the knob is unmoved, and 0 once the knob has taken over. The value only changes on a call with `pulse == true`, or on the knob-takeover call (to 0).

- [ ] **Step 1: Write the failing tests** in `tests/test_clock_law.cpp`.
  - A helper drives a pulse train at a given BPM with a chosen call size (1 and 96 samples at 48 kHz) and collects the outputs.
  - Use absolute bounds; derive each tolerance from the 96-sample quantization and N, and state the derivation in a comment.
  - **Tempo cases:**
    - `clock law: steady tempo`: 60, 120, 240, and the non-block-exact 130 BPM, at both call sizes. After the clock is running, `measured_bpm` stays within the tolerance.
    - `clock law: the tempo changes only at pulses`: a call without `pulse` never changes `measured_bpm`, except on the knob-takeover call.
    - `clock law: per-pulse step at 130 BPM is bounded`: at call size 96, |Δmeasured_bpm| between consecutive pulses stays below a stated bound. It must be strictly better than the single-period ±0.43 %, and that bound is the reason N > 2.
    - `clock law: swing and tempo steps settle`: alternating 55/45 % periods at 120 BPM stay within tolerance of 120. A step from 120 to 90 BPM settles within the stated number of pulses.
  - **Running-state cases:**
    - `clock law: out-of-range pulses are no clock`: 480 BPM and 15 BPM trains give no `pulse`, `running()` false, and a reset during them restarts at once.
    - `clock law: two valid edges before running`: the first edge gives no `pulse` and does not arm; the second does. The same holds after `discontinuity()` and after a loss.
    - `clock law: a short pause keeps the clock`: at 120 BPM, a pause of 1.5 periods (under the 2× timeout) neither loses the clock nor produces a `measured_bpm` outside tolerance after it.
  - **Reset cases:**
    - `clock law: reset before a pulse restarts on that pulse`: `restart` comes with the pulse, `restart_age` 0.
    - `clock law: reset and pulse in one call`: same as above.
    - `clock law: reset 5 ms after a pulse is back-dated`: `restart` is on the reset call, and `restart_age` ≈ 240 samples (±1 call size). At 15 ms it waits for the next pulse.
    - `clock law: reset without a clock restarts at once`: age 0.
  - **Loss and takeover cases:**
    - `clock law: loss holds the tempo`: after 2 × the period without an edge, `lost` is true exactly once and `measured_bpm` keeps the last value.
    - `clock law: knob takeover`: an unchanged `tempo_knob` keeps the held tempo. The first differing value sets `measured_bpm` to 0. Valid pulses afterwards make it measured again.
    - `clock law: armed reset at loss fires on the next beat wrap`: arm, lose the clock, feed a decreasing `beat_phase` → `restart` on that call.
  - **Lamp cases:**
    - `clock law: lamps show every edge`: an out-of-range edge still sets `clock_lamp`, and every reset edge sets `reset_lamp`.
- [ ] **Step 2: Run, see RED.** `source env.sh && cmake --build build && build/spky_tests.exe -tc="clock law*"`. Expected: compile error (`control/clock_law.hpp` missing). Stub `step()` to return `ClockOut{}` and see every case fail.
- [ ] **Step 3: Implement** `control/clock_law.hpp`.
  - Header-only, no libDaisy or Rack types, no heap.
  - Choose N from the step-1 tests and write it as a named constant with the derivation in a comment.
- [ ] **Step 4: Run, see GREEN.** First `build/spky_tests.exe -tc="clock law*"`, then `ctest --test-dir build --output-on-failure -E "^reva_"`. Expected: all green.
- [ ] **Step 5: Commit** `feat(control): one clock-and-reset law for both hosts`.

### Task 2: Engine restart

**Files:**
- Modify: `engine/mod/lane.h` and `engine/mod/lane.cpp` (song restart)
- Modify: `engine/mod/super_modulator.h` (phase reset with age, song restart pass-through)
- Modify: `engine/instrument.h` (`restart`)
- Create: `tests/test_restart.cpp`; Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `control::ClockLaw` (Task 1) in the transport regression test only.
- Produces:
  - `void ModLane::restart_song()`.
    - Melodic lanes only.
    - Sets `_song.phrase_index = 0` and `_song.active_pattern = song_symbol_at(_song.selected_song, 0)`, which is 0 for every mode (spec §6).
    - Touches no pending flag and no pattern.
  - `void SuperModulator::reset_phases(uint32_t age)`. Today's `reset_phases()` becomes the `age == 0` case: each lane is reset to the phase it would have reached `age` samples after 0, at its own current increment.
  - `void SuperModulator::restart_song()` → `_lanes[LANE_PITCH].restart_song()`.
  - `bool Instrument::restart(uint32_t age)`.
    - If no deck has `step_on(p)`: returns false and changes nothing.
    - Otherwise:
      - does `_center.reset_transport()`;
      - advances the transport by `age` samples at its bpm;
      - calls `reset_phases(age)` on both decks;
      - calls `restart_song()` on every STEP deck;
      - returns true.
  - `reset_transport()` itself stays as it is, for "Resync to bar".

- [ ] **Step 1: Write the failing tests** in `tests/test_restart.cpp`. Rig: `Instrument::init(48000.f)`, SYNTH on both decks, as the existing transport tests set it up.
  - `restart: a STEP deck restarts its song`
    - Run until `song_position_for_test(PART_B) > 2` with deck B in STEP.
    - `restart(0)` → true. Then `song_position_for_test(PART_B) == 0`, `active_pattern_for_test(PART_B) == 0`, and deck B's pitch-lane phase < 0.01 after one block.
  - `restart: a FLOW deck keeps its song`: deck A in FLOW beside it keeps its song position across the restart.
  - `restart: no STEP deck, no change`: both decks in FLOW → `restart(0)` returns false, and the song positions and lane phases are unchanged.
  - `restart: a FLOW deck is not dragged`
    - Two instruments, deck A FLOW and deck B STEP, COUPLE GRID. Use `set_sync(true)` and the couple value the law uses for GRID; read `control_law.h`'s COUPLE zone split.
    - One calls `restart(0)`; the reference calls `reset_transport()` at the same sample.
    - For 10 s afterwards, deck A's pitch-lane phase in the two instruments stays within 1e-3. A one-deck-only reset makes this fail (spec §3: 0.35 rate correction).
    - Repeat in the FREE zone (sync off, couple 0.45).
  - `restart: pending work is applied, nothing pending is not`: with `new_phrase(PART_B)` pending, `phrase_builds` rises by one after the restart; with nothing pending, it does not. Use `SuperModulator::pitch_phrase_builds()` through an observer if one is needed.
  - `restart: back-dating`: `restart(240)` at 120 BPM puts the transport at 240 samples' worth of beats (±1 tick), and deck B's pitch-lane phase equals its increment × 240 (±1e-4).
  - `clock law drives the transport one pace step per pulse`
    - Drive an `Instrument` through `control::ClockLaw` in 96-sample blocks: on `pulse`, call `inst.clock_pulse()`; set the tempo from `measured_bpm` exactly as `control_law.h:955-957` does.
    - At PACE ×1 (`set_pace` with the ×1 value; see `divisions.h`), 16 pulses at 120 BPM → transport beats 16 ± 0.5. At PACE ×1/16, 1 ± 0.1.
    - This is RED against today's VCV formula: replay that formula in the test as a second rig, which must fail the same assertion. That proves the test can see the bug.
- [ ] **Step 2: Run, see RED.** `build/spky_tests.exe -tc="restart*,clock law drives*"`. Expected: compile errors for `restart`/`restart_song`. The VCV-formula replay fails its pace assertion.
- [ ] **Step 3: Implement the interfaces above.**
  - Comment at `restart` citing spec §2.2: why both decks' lanes reset, and why only STEP decks restart the song.
- [ ] **Step 4: Run, see GREEN.** Run `ctest --test-dir build --output-on-failure -E "^reva_"`. Expected: all green, with `ctrl_identity` and `wave_formant_sweep` unchanged.
- [ ] **Step 5: Commit** `feat(engine): RESET restarts the song on STEP decks`.

### Task 3: VCV on the shared law

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp`:
  - `:370-372` (`clkSamples` / `clockTrig` / `resetTrig` members)
  - `:629-641` (`reinit`)
  - `:712-720` (`pushParams`: `measured_bpm`)
  - `:734-754` (`process`: edges, pulse, reset)
  - bypass handling
- Test: `host/vcv/res/test_panel.py` (source guard)

**Interfaces:**
- Consumes:
  - `control::ClockLaw` (Task 1).
  - `Instrument::restart(uint32_t)` and `Instrument::clock_pulse()` (Task 2).
  - `inst.beat_phase()`.
- Produces: per sample, the following sequence, with the Schmitt triggers kept at 0.1 / 1 V and no `isConnected()` gate on CLOCK:
  1. `clockTrig.process(...)` / `resetTrig.process(...)` give the two edges.
  2. `ClockOut o = clockLaw.step(1, clockEdge, resetEdge, params[TEMPO].getValue(), inst.beat_phase())`.
  3. On `o.pulse` call `inst.clock_pulse()`; on `o.restart` call `inst.restart(o.restart_age)`.
  4. `clockSeen`/`resetSeen` come from the lamp flags.
  5. `pushParams` sets `opt.measured_bpm = lastClockOut.measured_bpm` (latched).
  6. `reinit()` calls `clockLaw.init(sr)`. Un-bypass calls `clockLaw.discontinuity()`: use Rack 2's bypass event if the module API offers one, else detect a sample-counter gap. Builder's call; state it in the report.
  7. "Resync to bar" keeps calling `inst.reset_transport()`.

- [ ] **Step 1: Write the failing guard** in `test_panel.py`, in the style of its existing `Fireflow.cpp` source checks (see `test_reinit_pushes_the_root_before_init`). It asserts:
  - `clkSamples` and `60.f * curSr / clkSamples` are absent;
  - `clockLaw.step(1,` appears inside `process`;
  - `clockLaw.init(` appears inside `reinit(float sr)`;
  - `inst.restart(` is reached from the law's `restart` flag;
  - `inputs[CLOCK].isConnected()` no longer gates the clock.
- [ ] **Step 2: Run, see RED.** `ctest --test-dir build -R panel_guard --output-on-failure`. Expected: the new guard fails on the old code.
- [ ] **Step 3: Implement.**
  - Keep the CLK/RST lamp wiring through `LedInputs`.
  - Comment at the law call: why no connection test (spec §6).
- [ ] **Step 4: Run, see GREEN.**
  - Run `cmake --build build && ctest --test-dir build --output-on-failure -E "^reva_"`.
  - Then `host/vcv/build-local.sh` (build). If it is not tracked, copy it from the main checkout.
- [ ] **Step 5: Commit** `fix(vcv): CLOCK and RESET through the shared clock law`.

### Task 4: Firmware: gate latch, law, lamps, TEMPO release

**Files:**
- Create: `shell/gate_latch.h` and `shell/gate_latch.cpp`
  - `void gate_latch_init()`: EXTI lines 13/14 on PG13/PG14, falling edge, NVIC IRQ disabled.
  - `void gate_latch_arm()`: clear the pending bits, called immediately before `StartAudio`.
  - `GateEdges gate_latch_take()`: read and clear, once per block.
- Modify:
  - `shell/panel_scan.cpp` (`panel_scan_init`, `panel_scan_tick` before `control_tick`)
  - `shell/control_tick.h` / `.cpp`: `control_tick(knobs, inst, measured_bpm)`; `led_tick(..., clock_lamp, reset_lamp)`
  - `shell/panel_step.h` / `.cpp`: release TEMPO on `lost`, and find TEMPO's channel the way `flux_channels` finds FLUX's
  - `shell/main.cpp` (call `gate_latch_arm()` before `hw.StartAudio(AudioCallback)` on the Rev A play path only)
  - `shell/Makefile` (new source, Rev A play image only)
- Test: `tests/test_panel_step.cpp` (TEMPO release, TEMPO channel) and `host/vcv/res/test_panel.py` or a shell guard (coupon exclusion, arm-before-StartAudio order).

**Interfaces:**
- Consumes: `control::ClockLaw` (Task 1), `Instrument::restart` / `clock_pulse` (Task 2), `pot_release` (`shell/pot_owner.h:43`).
- Produces:
  - `struct GateEdges { bool clock; bool reset; };`
  - `int tempo_channel(const ControlTable&, const ChainProfile&)` returns −1 if absent.
  - `void apply_clock_events(const control::ClockOut&, PotOwner* owners, int tempo_idx)`, which releases TEMPO on `lost`.
- Block order in `panel_scan_tick`:
  1. `gate_latch_take()`;
  2. `g_clock.step(96, ...)` with `g_knobs[ffctl::TEMPO]` and `inst.beat_phase()`;
  3. `inst.clock_pulse()` / `inst.restart(age)`;
  4. `control_tick(g_knobs, inst, o.measured_bpm)`;
  5. `apply_clock_events`;
  6. `led_tick` with the lamp flags.
- The block size comes from `hw.AudioBlockSize()`, not a literal.
- All of this is compiled only when `SHELL_PANEL_SCAN && !SHELL_COUPON_PROBE`.

- [ ] **Step 1: Write the failing host tests** in `tests/test_panel_step.cpp`:
  - `panel step: TEMPO channel found` against `kRevaTable`, the way the FLUX channel test does.
  - `panel step: clock loss releases the TEMPO pot`: an owned TEMPO `PotOwner` is unowned after `apply_clock_events` with `lost = true` and untouched with `lost = false`.
  - Plus a source guard that:
    - `gate_latch` is referenced only under `!SHELL_COUPON_PROBE`;
    - `gate_latch_arm()` precedes `StartAudio` in the Rev A path.
- [ ] **Step 2: Run, see RED.** `build/spky_tests.exe -tc="panel step*"`, plus the guard's ctest. Expected: compile errors and a failed guard.
- [ ] **Step 3: Implement.**
  - For EXTI on the STM32H7, set `SYSCFG_EXTICR4` for port G lines 13/14, `EXTI_FTSR1` bits 13/14, and leave `EXTI_IMR1` and the NVIC so the IRQ is not taken. Then read and clear `EXTI_PR1` (H7: `EXTI->PR1`, write-1-to-clear).
  - First check against libDaisy that nothing else configures EXTI line 13/14 and that `GPIOInterruptHandler` is not used for these pins. Write the finding into the report.
  - If pending bits cannot latch with the IRQ masked on this part, fall back to `EXTI_IMR1` enabled with an empty, non-libDaisy-conflicting path. **Stop and report BLOCKED rather than define `EXTI15_10_IRQHandler`.**
- [ ] **Step 4: Run, see GREEN.**
  - `cmake --build build && ctest --test-dir build --output-on-failure -E "^reva_"`.
  - In a fresh shell without `env.sh`: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images`. It must link all images; report the Rev A SRAM_EXEC use against the 60 656 B free (CPU headroom record).
  - `python shell/gen_panel_map.py --check`.
- [ ] **Step 5: Commit** `feat(shell): CLOCK and RESET on Gate In 1/2 through the shared clock law`.

### Task 5: Docs, install, handoff

**Files:**
- Modify:
  - `docs/roadmap.md`: an M6 entry for this round, and the stale "not merged yet" in the 2026-10-18 P6b-2 entry (it was merged at `8c1a37f1`)
  - `docs/engine-map.md` §6: the spec §3 measured facts, with setups (reset jumps hard, FLOW drag under one-deck reset, the out-of-range freeze, VCV's formula replay)
  - `docs/gotchas.md`: libDaisy's one-pin-per-EXTI-channel, and PG13/PG14 sharing `EXTI15_10`
  - `host/vcv/README.md` and `shell/README.md`: the jacks' behaviour
- Modify: `docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md`: status line only, set to "built".

- [ ] **Step 1: Write the doc edits.** Claim no sound result.
- [ ] **Step 2: Verify.** `ctest --test-dir build --output-on-failure -E "^reva_"` is green, and `host/vcv/build-local.sh install` reports the install path.
- [ ] **Step 3: Commit** `docs: CLOCK and RESET jacks`.
- [ ] **Step 4: Hand off to Bastian** for the Rack check in spec §8.7:
  - Pamela's clock and reset, PACE ×1.
  - The tempo matches and is not double.
  - The 1 lands with Pamela's 1.
  - A stop keeps the tempo; turning TEMPO takes over.

  Spec §8.8 is P7 on Rev A and is not a gate.
