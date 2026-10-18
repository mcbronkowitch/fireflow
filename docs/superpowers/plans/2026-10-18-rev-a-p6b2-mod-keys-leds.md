# Rev A P6b-2 — knob ownership, keys, LED law — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Rev A firmware plays the init patch at boot whatever the knobs show, a pot takes its parameter by being moved, the four keys work (MOD, REC A/B; SHFT reserved), and the 15 lamps show their 2026-10-07 meanings in the firmware and in `FireflowHW`.

**Architecture:** Two pure, host-tested layers go into `shell/`: pot ownership (`pot_owner.*`) between `pot_filter()` and the knob vector, and the LED output pattern (`led_out.*`) between the LED law and the 595 chain. The LED law moves from `host/vcv/src/` to `control/led_law.hpp` and both hosts call it; it gains new meanings fed by new `const` observers on `spky::Instrument`. Keys and MOD-aware pot targets extend `shell/controls.*`.

**Tech Stack:** C++17, doctest (`build/spky_tests.exe`), clang+Ninja desktop build, ARM GCC `make` for `shell/`, VCV Rack SDK via `host/vcv/build-local.sh`, Python generators (`host/vcv/res/gen_panel.py`, `shell/gen_panel_map.py`).

**Spec:** `docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md` — read it before any task; section numbers below refer to it.

## Global Constraints

- Desktop build: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. Release is mandatory. **ctest does not build** — always `cmake --build build` first.
- Firmware: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, `make -C shell -j8 images`. Never `source env.sh` in that shell.
- VCV: only `host/vcv/build-local.sh` (finish with `host/vcv/build-local.sh install`, then "restart Rack"). Panel generators run from `host/vcv/`; guards `python res/test_panel.py` and `python res/test_hw_panel.py` (pytest is not installed).
- Never prefix a shell command with `cd`; no shell writes behind `&&` (memory `subagent-shell-auto-approval`).
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Every new test is proven red once (break the code, see it fail, restore by re-editing — never `git checkout <file>`, see memory `fireflow-scripted-edits-crlf`).
- No runtime claim in docs without a probe printing it (CLAUDE.md, the probe rule).
- Observers must not change audio: the render-hash gates in `tests/check_render_hash.cmake` stay green.
- The LED crosstalk rule (P2 §4): LED bits change **only** in the latch that carries the mux address — i.e. only through `MuxScan::set_leds()`, once per block.
- SHFT has no function. `ROOT_A`, `ROOT_B`, `REV_MOD` stay without a parameter.
- New lamp constants are by-eye candidates; mark them so in code and list them in `docs/by-ear-decisions.md`'s unconfirmed section.

## Review Focus

1. **A pot sitting in a snap region at boot** (fully left/right) and nudged: no ownership until the raw move exceeds the touch threshold — the snap must not fake a move. Test in Task 5.
2. **MOD toggled while a pot is mid-turn:** the pot is released, and the depth only jumps once the pot moves past the threshold from where it stood at the toggle. Test in Task 5 (via target change) and Task 6.
3. **The span is not yet valid at boot:** no reference is taken, nothing is written, the init patch plays. Test in Task 5.
4. **ENGINE turned into BBD while MOD is latched:** ATTACK's target goes from its depth to "nothing" and the pot is released; leaving BBD releases it again. Test in Task 6.
5. **A BBD edge while the FLUX pot is being turned:** FLUX goes to 0 and stays there until the pot moves past the threshold again. Test in Task 6.

---

### Task 1: Coupon probe — LED step count and pot noise

Two numbers the rest of the plan needs are measured, not chosen (§2, §4.3). This task needs Bastian at the coupon.

**Files:**
- Modify: `shell/panel_scan.cpp` (coupon branch only, `#if SHELL_COUPON_PROBE`)
- Create: `docs/hardware/p6b2-led-and-touch-measured.md`

**Interfaces:**
- Produces: `kLedSteps` (an int, the step count Bastian picks) and `kPotTouch` (raw counts) — written into the measurement doc; Task 2 and Task 5 copy them into code.

- [ ] **Step 1: Add a temporary dim demo to the coupon play image.** Each key press advances a preset: LED_1 at duty 1 of N for N ∈ {4, 6, 8, 16}, then full on, then off, wrapping. The duty pattern is Task 7's spread rule (`on in block f iff floor((f+1)·d/(N−1)) > floor(f·d/(N−1))`), written inline here. The report prints `SHELL_DIM n=<N> d=<d>` on every change. The demo is marked `// P6b-2 Task 1 probe, removed in Task 7`.
- [ ] **Step 2: Add a raw-spread line to the coupon report:** per coupon pot, `max − min` of the raw word over the last second, printed as `SHELL_SPREAD rv2=… rv4=… rv6=…` once a second, measured while the dim demo switches LED_1.
- [ ] **Step 3: Build** `make -C shell -j8 images` with the coupon play switches as `shell/README.md` documents them; `cmp` against the previous image so a stale object cannot fake a rebuild (memory `fireflow-bench-stale-object-trap`).
- [ ] **Step 4: Session (Bastian).** Flash over DFU. For each preset, Bastian says whether the dim LED flickers when looked at directly and in peripheral vision. Pots untouched for 60 s per preset; record the largest `SHELL_SPREAD` value seen.
- [ ] **Step 5: Record** in `docs/hardware/p6b2-led-and-touch-measured.md`: the per-preset verdicts, the chosen `kLedSteps` (the largest N without visible flicker), the worst spread, and `kPotTouch = max(2 × worst spread, 3 × kPotHysteresis)` rounded up to a multiple of 16 — the rule is fixed here, before the number is known.
- [ ] **Step 6: Commit** the doc and the probe code: `test(shell): P6b-2 probe -- LED step count and pot touch threshold`.

### Task 2: The LED law moves to `control/`

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (emit `ffctl::LightId` into `control/params.hpp`), regenerate `control/params.hpp`
- Move: `host/vcv/src/led_law.hpp` → `control/led_law.hpp`
- Modify: `host/vcv/src/Fireflow.cpp` (include path, `kLedSteps`), `tests/test_led_law.cpp` (include path)
- Test: `tests/test_led_law.cpp`, `host/vcv/res/test_panel.py`

**Interfaces:**
- Produces: `ffctl::LightId` (same names and order as `spkyvcv::LightId`, ending in `NUM_LIGHTS`); `control/led_law.hpp` in namespace `spkyled`, using `ffctl::` ids; `constexpr int spkyled::kLedSteps` (value from Task 1); `int spkyled::duty(float intens, int steps)` and `int spkyled::duty_from_light(float light, int steps)` without `std::pow` per call.

- [ ] **Step 1: Write the failing test** `led: kLedSteps is the coupon's measured step count` — `CHECK(spkyled::kLedSteps == <Task 1 value>);` and `led: duty matches the gamma reference at every step` — for `steps` in {4, 6, 8, 16} and 1000 intensities in (0, 1], `duty(v, steps)` equals the reference `max(1, int(pow(v, 2.2) * (steps − 1) + 0.5))`, and `duty_from_light(l, steps)` equals `max(1, int(l * (steps − 1) + 0.5))` for `l > 0` (the old pow round trip, written out).
- [ ] **Step 2: Run** `build/spky_tests.exe -tc="led*"` — expect a compile failure (no `kLedSteps` in the new header).
- [ ] **Step 3: Emit `LightId` from `gen_panel.py`** into `control/params.hpp` beside `ParamId`; add `static_assert((int)ffctl::NUM_LIGHTS == (int)NUM_LIGHTS, "control/params.hpp is stale");` next to the existing one at `Fireflow.cpp:316`.
- [ ] **Step 4: Move the header** (`git mv`), switch its include to `control/params.hpp` and its ids to `ffctl::`. Replace `duty()`'s `pow` by a comparison against thresholds `t_k = ((k − 0.5)/(steps − 1))^(1/2.2)`, k = 1 … steps − 1, computed once per `steps` value into a small static table (the step count is fixed per host, so one table suffices; rebuild it when `steps` differs from the cached one). `duty_from_light()` becomes the direct rounding above. Move VCV's `kLedSteps = 16` from `Fireflow.cpp:376` into the header with Task 1's value and update its comment: the brightness raster is now the block-rate latch, not the mux.
- [ ] **Step 5: Run** `cmake --build build` and `build/spky_tests.exe -tc="led*"` — PASS, all old `led G*` cases included. Run `python res/test_panel.py` from `host/vcv/` — PASS.
- [ ] **Step 6: Commit** `refactor(control): the LED law moves to control/ -- one raster, no pow per lamp`.

### Task 3: Engine observers for the new lamps

**Files:**
- Modify: `engine/instrument.h`, `engine/instrument.cpp`, `engine/fx/flux.h`, `engine/parts/part.h`
- Create: `tests/test_led_observers.cpp` (add to `spky_tests` in `CMakeLists.txt`)

**Interfaces:**
- Produces, all `const` on `spky::Instrument`:
  - `float deck_out_level(int p) const` — peak follower of the deck's contribution to the mix, `max(|pl[p]|, |prr[p]|) × (p ? gb : ga)` read in the mix loop (`instrument.cpp`, after the deck's `process()`), instant attack, one-pole release `kDeckLevelRelease = 0.02 f` seconds (enough that one audio cycle does not flicker the lamp, §4.2).
  - `float input_peak() const` — the same follower over `max(|in_l|, |in_r|)`.
  - `bool flux_engaged(int p) const` — `Flux::engaged()`.
  - `float flux_repeat_phase(int p) const` — `_repeat_phase_samples / _repeat_period_samples` in [0, 1), 0 while the period is 0.
  - `uint32_t engine_epoch(int p) const` — increments in `Part::_engine_swap()`, i.e. when the new engine takes effect, not at `set_engine()`.
  - `uint32_t scale_epoch() const` — increments in `set_scale()` only when the index differs from the last one.

- [ ] **Step 1: Write the failing tests** in `tests/test_led_observers.cpp`:
  - `observers: a playing deck's level sits above a silent one's` — init patch values as `test_seed_audition_init.cpp` sets them; settle 1 s; `deck_out_level(0) > 0.01f`; then `set_part_level(0, 0.f)`, settle 0.5 s, `deck_out_level(0) < 1e-4f`.
  - `observers: the input peak follows the input` — 0.5-amplitude 440 Hz sine on both inputs for 0.2 s: `input_peak()` in [0.45, 0.55]; silence for 0.5 s: below 1e-4.
  - `observers: the FLUX phase wraps once per repeat period` — FLUX engaged at a known rate; count wraps (phase decreasing) over 10 s; equals `10 / delay_time()` ± 1.
  - `observers: the engine epoch moves when the new engine sounds, not at the call` — `set_engine(0, …)`, epoch unchanged right after; run until the switch fade is idle; epoch +1.
  - `observers: the scale epoch ignores a repeated scale` — `set_scale(3)` twice: +1; `set_scale(4)`: +1 more.
  - `observers: reading them changes no sample` — two instances, same calls, one reads every observer every sample; outputs bit-equal over 2 s.
- [ ] **Step 2: Run** — compile failure on the missing observers.
- [ ] **Step 3: Implement** the observers as the Interfaces block says. Followers update per sample inside the existing loop; no branch on them affects audio.
- [ ] **Step 4: Probe the LVL floor (§4.2).** A throwaway `tests/probe_lvl_floor.cpp` (not added to CMake; built by hand per `docs/engine-map.md` §6) prints `deck_out_level(0)` percentiles for: init patch playing; `set_part_level(0, 0.f)`; cutoff fully closed; Sampler deck empty; a STEP deck at low DENSITY between notes. Paste the printout into the commit message body and pick `kLvlFloor` between the "silent" maxima and the "playing" minima.
- [ ] **Step 5: Run** `cmake --build build` and `ctest --test-dir build --output-on-failure` — all green, render hashes included.
- [ ] **Step 6: Commit** `feat(engine): const observers for the lamp law -- deck level, input peak, FLUX phase, switch epochs`.

### Task 4: The LED law's new meanings, both hosts

**Files:**
- Modify: `control/led_law.hpp`, `host/vcv/src/Fireflow.cpp`, `docs/by-ear-decisions.md`
- Test: `tests/test_led_law.cpp`

**Interfaces:**
- Consumes: Task 3's observers, Task 2's header.
- Produces:
  ```cpp
  struct LedInputs {
      bool mod_latched   = false;
      bool shift_latched = false;   // always false this round (SHFT reserved)
      bool clock_pulse   = false;   // a CLOCK edge since the last fill()
      bool reset_pulse   = false;   // a RESET edge since the last fill()
  };
  void fill(const spky::Instrument&, Panel&, float dt, int steps,
            const LedInputs&, int* duty_out);
  ```
  Candidates (by eye, §4.2): `kLvlFloor` (Task 3's probe), `kFtimeFlash = 0.05f` s, `kSwitchPulse = 0.25f` s (one long flash), `kJackFlash = 0.04f` s.

- [ ] **Step 1: Write the failing tests:**
  - `led: LVL is lit exactly while the deck delivers signal` — playing deck → `duty_out[LVL_A_L] == steps − 1`; `set_part_level(0, 0.f)` → 0; on a STEP deck it goes dark between notes (sample 2 s, see both states).
  - `led: FTIME flashes once per FLUX period, only while FLUX is engaged` — engaged: flash count over 10 s = wraps ± 1; FLUX MIX 0: never lit.
  - `led: the SHIFT lamp shows the input level` — input 0.5 sine → `duty_out[SHIFTBTN_L] == duty(input_peak(), steps)`; silence → 0.
  - `led: an engine switch pulses the SHIFT lamp once it sounds` — full for `kSwitchPulse` after the epoch moves, then back to the input level; no pulse on the first `fill()` (arming, like SONG's S2).
  - `led: a scale change pulses the SHIFT lamp` — same, via `scale_epoch()`.
  - `led: CLK and RST flash on their pulses` — `clock_pulse = true` once → lit for `kJackFlash`, then dark; same for RST.
  - Update `led G6` (every light written) to the new signature.
- [ ] **Step 2: Run** — FAIL (signature, meanings).
- [ ] **Step 3: Implement.** `Panel` gains the per-lamp state the tests imply (last epochs, armed flags, remaining flash times, last FLUX phase). LVL drops its `Lamp` envelope and compares `deck_out_level(p)` with `kLvlFloor`. SHIFT: `shift_latched` → full; else `duty(input_peak(), steps)`, overridden full while a switch pulse runs.
- [ ] **Step 4: Wire VCV.** In `Fireflow.cpp` set `clockSeen` / `resetSeen` where `clockTrig` / `resetTrig` fire (`:715`, `:722`), hand them to `fill()` through `LedInputs`, clear them after. `mod_latched` as today.
- [ ] **Step 5: Run** `cmake --build build`, `ctest --test-dir build --output-on-failure` — green. `host/vcv/build-local.sh install`.
- [ ] **Step 6: Add the four candidates** to `docs/by-ear-decisions.md`'s unconfirmed section.
- [ ] **Step 7: Commit** `feat(led): the 2026-10-07 lamp meanings -- LVL, FTIME, SHIFT, CLK, RST`.
- [ ] **Step 8: Rack check (Bastian),** after "restart Rack", on `FireflowHW`: LVL lit while a deck plays and dark with the deck's level all the way down (the LVL side of LVL/COMP); FTIME flashes with FLUX up and stays dark at FLUX 0; the SHFT lamp follows a signal at IN and pulses once on an ENGINE or SCALE change; CLK/RST flash with a clock patched; MOD and REC as before. Record the verdict in the roadmap entry (Task 8).

### Task 5: Pot ownership

**Files:**
- Create: `shell/pot_owner.h`, `shell/pot_owner.cpp`, `tests/test_pot_owner.cpp` (both added to `spky_tests`)

**Interfaces:**
- Produces:
  ```cpp
  namespace shell {
  inline constexpr int kPotTouch = /* Task 1 */;
  struct PotOwner { uint16_t ref = 0; int target = -2; bool has_ref = false; bool owned = false; };
  enum class Own { Idle, Took, Owned };
  void pot_release(PotOwner& o);                       // owned = has_ref = false
  // span_valid false -> Idle, nothing taken. A target different from o.target
  // releases first and records the new target. Without a reference the raw
  // word becomes it (Idle). Owned stays Owned. |raw - ref| > touch -> Took.
  Own pot_own(PotOwner& o, uint16_t raw, int target, bool span_valid, int touch);
  }
  ```

- [ ] **Step 1: Write the failing tests** (`kT = shell::kPotTouch`):
  - `owner: boot takes a reference and writes nothing` — first valid call → `Idle`, `has_ref`.
  - `owner: an invalid span takes no reference` — `span_valid = false` → `Idle`, `!has_ref`; then valid → reference is that later raw.
  - `owner: jitter inside the threshold never takes` — ±(kT − 1) around ref for 1000 calls → always `Idle`.
  - `owner: a move past the threshold takes, then owns` — ref + kT + 1 → `Took`; any later raw → `Owned`.
  - `owner: a snap region is not a move` — ref 10 (inside the zero snap), raw 10 + kPotHysteresis + 1 (≤ kT) → `Idle`.
  - `owner: a new target releases and re-references` — owned at target 5; call with target 6 at raw r → `Idle`, ref == r; r + kT + 1 → `Took`.
  - `owner: release re-references at the next reading` — `pot_release`, next call → `Idle` with ref = that raw.
- [ ] **Step 2: Run** `build/spky_tests.exe -tc="owner*"` — compile failure.
- [ ] **Step 3: Implement** `pot_own` and `pot_release` as the comment says.
- [ ] **Step 4: Run** — PASS; prove one RED (e.g. `>` → `>=`).
- [ ] **Step 5: Commit** `feat(shell): pot ownership -- a pot takes its parameter by being moved`.

### Task 6: Keys, MOD-aware targets, events

**Files:**
- Modify: `shell/controls.h`, `shell/controls.cpp`, `shell/gen_panel_map.py` (emit the key functions and the LED bit table), regenerate `shell/generated_panel_map.h`, `shell/test_gen_panel_map.py`
- Test: `tests/test_controls_map.cpp`

**Interfaces:**
- Consumes: `ffctl::kModLayer` (`soundId`, `depthId`), `knob_target()`.
- Produces:
  ```cpp
  namespace shell {
  enum class KeyFn : int8_t { RecA, RecB, Mod, Shift };
  struct KeyMap { int count; KeyFn fn[kMaxKeys]; };
  // kRevaKeyMap {4, {RecA, RecB, Mod, Shift}} (165 D0..D3, P2 §4) -- generated;
  // kCouponKeyMap {1, {Mod}} -- hand-written beside kCouponKeys (spec §7).
  // The id a pot writes now: not latched -> knob_target(); latched and the
  // row's `param` has a kModLayer row -> that row's depthId while
  // knob_target() == param, else -1; latched and no depth row -> knob_target().
  int pot_target(const ControlEntry& e, const float* knobs);
  // Debounced press edges (pressed & ~last) toggle REC_A / REC_B / MODBTN
  // between 0 and 1; Shift does nothing. Returns true if MODBTN changed.
  bool apply_key_edges(uint8_t edges, const KeyMap& map, float* knobs);
  // LightId -> bit in MuxScan's LED field, -1 when the lamp is not on the plate.
  // kRevaLedBit from panel-map.json's "leds" order; kCouponLedBit: MODBTN_L = 0.
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/test_controls_map.cpp`:
  - `controls: MOD latched sends a wreathed pot to its depth` — FILT_A row, `knobs[MODBTN] = 1` → `pot_target == depthId of FILT_A`; `= 0` → `FILT_A`.
  - `controls: MOD latched leaves an unwreathed pot on its sound` — pick a row whose param has no `kModLayer` row (ENGINE_A) → its own id both ways.
  - `controls: ATTACK on a BBD deck writes nothing under MOD` — ENGINE_A at `kEngineSlotBbd`, latched → -1; not latched → STAGES_A.
  - `controls: a press toggles, a release does nothing, SHFT does nothing` — edges bit for Mod → MODBTN 0→1, again → 0; RecA → REC_A toggles; Shift → no knob changes; `edges = 0` → nothing.
  - `controls: the Rev A LED bits follow panel-map.json` — `kRevaLedBit[SHIFTBTN_L] == 0`, `[MODBTN_L] == 14`, `[FLOW_A_L] == -1`, the 15 placed lamps are a permutation of 0..14.
  - In `shell/test_gen_panel_map.py`: the emitted key map is RecA, RecB, Mod, Shift.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement** `pot_target`, `apply_key_edges`, the generated tables.
- [ ] **Step 4: Run** `cmake --build build`, `build/spky_tests.exe -tc="controls*"`, `python shell/test_gen_panel_map.py` — PASS.
- [ ] **Step 5: Commit** `feat(shell): the four keys and MOD-aware pot targets`.

### Task 7: Firmware wiring — ownership, keys, events, lamps

**Files:**
- Create: `shell/led_out.h`, `shell/led_out.cpp`, `tests/test_led_out.cpp` (added to `spky_tests`)
- Modify: `shell/panel_scan.cpp`, `shell/control_tick.h`, `shell/control_tick.cpp`, `shell/Makefile` (new sources), `shell/README.md`
- Remove: Task 1's dim demo

**Interfaces:**
- Consumes: Tasks 2, 4, 5, 6.
- Produces:
  ```cpp
  // Bit i set = lamp with kRevaLedBit/kCouponLedBit i lit in block `frame`.
  // A lamp of duty d (0..steps-1) is on in block f iff
  // floor((f+1)*d/(steps-1)) > floor(f*d/(steps-1)): d = 0 never, d = steps-1
  // always, d on-blocks spread evenly over each steps-1 blocks.
  uint32_t led_word(const int* duty, int steps, uint32_t frame, const int8_t* led_bit);
  // control_tick.h:
  control::Events control_tick(const float* knobs, spky::Instrument& inst); // returns the law's events
  void led_tick(const spky::Instrument& inst, bool mod_latched, float dt, int* duty_out);
  ```

- [ ] **Step 1: Write the failing tests** in `tests/test_led_out.cpp`: `led_out: duty 0 is never on, full is always on`; `led_out: duty d is on d times per period` (all d, steps = `kLedSteps`); `led_out: on-blocks are spread, no run longer than ceil((steps-1)/d)` for d ≥ 1; `led_out: a lamp without a bit sets nothing`.
- [ ] **Step 2: Run** — FAIL; **Step 3: implement** `led_word`; **Step 4: run** — PASS.
- [ ] **Step 5: Wire `panel_scan_tick()`.** Per row: `target = pot_target(*e, g_knobs)`; `own = pot_own(g_owner[idx], raw, target, g_span.valid, kPotTouch)`; on `Took` reset `g_filter[idx].emitted = false` so `pot_filter()` emits this reading; write `g_knobs[target] = knob_from_pot(target, v)` only when `own != Idle`, `target >= 0` and the filter emits. Keys: `edges = g_keys.pressed & ~g_last_pressed`, `apply_key_edges(edges, kKeyMap, g_knobs)`. After `control_tick()`: on `ev.bbd_edge[p]` set `g_knobs[p ? FLUX_B : FLUX_A] = 0`, `pot_release()` the FLUX row's owner, and set `deck[p].excite_other_deck = true` in a persistent `control::Options` inside `control_tick.cpp`. `ev.rec_started` is ignored (no sample path on the hardware, spec §3). Then `led_tick()` and `g_scan.set_leds(led_word(...))` with a block counter as `frame`. Count the LED law's cycles into its own `g_led_cycles_max` (same warm-up rule as the law's) and print it on the `SHELL_PLAY_LAW` line as `led_max=`.
- [ ] **Step 6: Remove** Task 1's dim demo and spread line (the measurement doc keeps the result).
- [ ] **Step 7: Build** all three images (`make -C shell -j8 images`, each switch set per `shell/README.md`), `cmp` each against its predecessor; QSPI placement guard green; SRAM_EXEC margin printed.
- [ ] **Step 8: Update `shell/README.md`'s divergences:** drop the FLUX line; add "CLK/RST lamps dark until CLOCK/RESET are wired; SHFT has no function".
- [ ] **Step 9: Run** `cmake --build build`, `ctest --test-dir build --output-on-failure` — green.
- [ ] **Step 10: Commit** `feat(shell): P6b-2 on the board -- ownership, keys, events, lamps`.

### Task 8: Coupon session and the record

**Files:**
- Modify: `docs/roadmap.md` (new M6 entry `2026-10-…` and the M6 table row), `docs/engine-map.md` (the observers' measured facts from Task 3), `docs/hardware/p6b2-led-and-touch-measured.md` (session results)

- [ ] **Step 1: Session (Bastian),** coupon, playing image. Checks, each answered yes/no in the doc: (1) boot plays the init patch with RV2/RV4/RV6 deliberately away from their init positions; (2) a small nudge below the threshold changes nothing audible; a real turn jumps and then follows; (3) the key latches MOD — LED_1 double-pulses — and turning RV6 (FILT_A) changes the filter's modulation depth audibly while the filter's base stays; (4) unlatching: no jump until a pot is moved; (5) LED_1 shows the limiter outside MOD without visible flicker; (6) `SHELL_PLAY_LAW` `cyc_max` and `led_max` read after 60 s of playing.
- [ ] **Step 2: Apply the CPU rule (§6):** if `cyc_max + led_max` exceeds the reserve of P6b-1 §6, move `led_tick()` to every second block (frame counter still per block), rebuild, re-measure; record either outcome.
- [ ] **Step 3: Write the roadmap entry:** what changed, the measured `kLedSteps`, `kPotTouch`, cycle figures, the Rack verdict (Task 4 Step 8), the session verdicts, what P7 still has to check (all 73 pots, four keys, 15 lamps). Update the M6 table row: P6b-2 done, bring-up next.
- [ ] **Step 4: Commit** `docs: P6b-2 coupon session -- ownership, keys and lamps on the board`.
