# Sampler Records the Other Deck, on the Bar — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Rev A, a deck on SAMPLER records the other deck plus the audio input. Its monitor plays the audio input only. REC start and stop snap to the bar: an early press waits for the line, a late press counts from it. The snap works by writing the buffer one beat behind live from a short ring.

**Architecture:**

The engine gets four portable pieces, each host-tested:
- a raw monitor input beside the summed record input (`Part` → `SamplerEngine`);
- a bar clock in `Instrument`: the frame index of each bar line, interpolated inside the control block;
- a ring plus a delayed writer in `SamplerEngine` (`D` frames behind live; `D = 0` and no ring when the snap is off);
- a pure rule unit `engine/sampler/rec_snap.h` that turns a press into a target frame (now, the last bar line, or the next one, held).

`Instrument` wires them behind `set_rec_snap(bool)`, default off, so every desktop render and the render hashes stay byte-identical.

The control law compares REC against a new `sampler_rec_requested()`, and the LED law gains a held-REC blink and a brighter beat 1 on TEMPO.

On the hardware, `apply_excite_events()` opens the deck route on SAMPLER as well as on BBD. A parse-time switch `SHELL_REC_SNAP` (default 1) turns the snap on and allocates the two rings in SDRAM. VCV gets the rings and a menu item, default off.

**Tech Stack:** C++17, doctest (host, clang + Ninja via `env.sh`, Release), ARM GCC + libDaisy (`shell/`), the VCV toolchain (`host/vcv/build-local.sh`), Python generators.

**Spec:** `docs/superpowers/specs/2026-11-07-sampler-records-other-deck-on-the-bar-design.md` (revised after review §9; probes §3 and §6).

## Global Constraints

- Host build: `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build`. **ctest does not build**: always `cmake --build build` first. Run `ctest --test-dir build --output-on-failure -E "^reva_"` (`hardware/` is untouched).
- Firmware shell: never `source env.sh` there. `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, then `make -C shell -j8 images`.
- Never prefix a command with `cd`. Never write files with shell redirection; use the Edit/Write tools. Long compounds go into a scratchpad script.
- Everything in the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Do not edit `lib/libDaisy` or `lib/DaisySP`.**
- **The render hash gates stay green unchanged** (`spky_tests`, `ctrl_identity`, `tests/check_render_hash.cmake`). The snap is off by default, the monitor change is a no-op while the deck source is off, and neither may move a desktop render. A moved hash is a bug in the task, not a hash to re-bake.
- A test that cannot go red gets fixed. Prove each new gate's RED once by sabotaging the implementation with Edit, then restore it with Edit (never `git checkout <file>`).
- Scripted edits eat CRLF. Edit existing files with the Edit tool only.
- No runtime claim enters docs until a probe or the board printed it.
- **Parallel implementer etiquette** (memory `fireflow-parallel-implementer-etiquette`): one implementer writes into the tree at a time.
- Engine "no heap": the rings are injected through `FxMem`, like the sample buffers.
- Constants (48 kHz, block = 96 samples = 2 ms):

| constant | value | meaning |
|---|---|---|
| `kSnapBeatsPerBar` | 4 | a bar, the spec's choice (§3) |
| `kSnapMaxBeatFrames` | 72 000 | 1.5 s; a longer beat turns the snap off |
| `kRecRingFrames` | 72 128 | `kSnapMaxBeatFrames` + one block + 32 |
| held-REC blink | 8 Hz | by eye (spec L3) |
| TEMPO beat 1 / other beats | full / dimmer | by eye (spec L4); start at light 1.0 and 0.35 |

## Review Focus

1. **Equivalence:** a late or early press must yield exactly the buffer an on-time press yields: the same frames, the same `rec_size()` and the same slice markers. This covers first takes and overdubs.
   - *Task 4:* `writer: delayed writer equals live writer`.
   - *Task 6:* `snap: late start / early start / late stop / early stop equal on-time`, plus the overdub cases.
2. **Snap off is today, bit for bit.** Render hashes unchanged. `writer: snap off has no ring and D = 0`.
3. **The law and the fade-out:** a stop pushed again during the fade-out is a no-op, never a held stop (Task 6, `law: re-push in fade-out is not a press`).
4. **The beat count jumps:** RST, RESET and a backwards clock snap must neither double a bar line nor strand a held request (Task 3, Task 6).
5. **No bulk work in the callback:** one ring write, one ring read and one compare per sample. No copy, no memset.

---

### Task 1: The monitor plays the audio input only (engine)

**Files:**
- Modify: `engine/sampler/sampler_engine.h/.cpp`: a monitor input separate from the record input.
- Modify: `engine/parts/part.h`: hand the raw input to the sampler's monitor when the deck source is on.
- Modify: `tests/test_instrument.cpp`

**Interfaces:**
```cpp
// SamplerEngine
void process_in(float l, float r);          // record input; also sets the monitor input (as today)
void set_monitor_in(float l, float r);       // overrides the monitor input for this sample
// the monitor (sampler_engine.cpp:1005) reads _mon_l/_mon_r instead of _in_l/_in_r
```
In `Part::process` (`part.h:458-476`), when `_src_deck` is on and the engine is SAMPLER, call `set_monitor_in(inL, inR)` after `process_in(el, er)`. With `_src_deck` off nothing changes, and the monitor reads the same samples as today.

- [ ] **Step 1: Write the failing tests** in `tests/test_instrument.cpp`. Setup: a static 10 s sample buffer per deck, A = SAMPLER, B = SYNTH init, 120 BPM, `set_excitation_sources(A, true, true, false)`.
  - `monitor: the other deck is not doubled`:
    - Record A from 1 s, with no audio input.
    - Over 1.5..3 s, A's `deck_tap` must equal A's grain output alone: compare against the same run with `sampler_monitor(A, false)` forced after REC. Bitwise.
  - `monitor: the audio input is still heard`:
    - Feed a 0.5-amplitude 220 Hz sine; B is an empty SAMPLER.
    - A's tap during REC minus the same run with the monitor forced off equals the raw input sample for sample, within 1e-6. It must not equal the `tanh`'d sum.
- [ ] **Step 2:** `cmake --build build` → FAIL (both tests).
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Build, then ctest → PASS, hashes unchanged. RED: monitor `_in_l` again → `not doubled` fails. Restore.
- [ ] **Step 5: Commit** `feat(sampler): the monitor plays the audio input, not the other deck`.

### Task 2: A SAMPLER deck hears the other deck on the hardware (control + shell)

**Files:**
- Modify: `control/control_law.h`: `Events::sampler_part[2]` (level: this tick's engine choice is SAMPLER), set where `bbd_part` is set.
- Modify: `shell/panel_step.cpp/.h`: `apply_excite_events()` keeps the bit on while `bbd_part || sampler_part` and clears it otherwise. The BBD entry edge stays.
- Modify: `shell/persist_policy.h`: one line in the `excite_after_take` comment saying SAMPLER is derived per tick and not held.
- Modify: `tests/test_panel_step.cpp`

- [ ] **Step 1: Write the failing tests** in `tests/test_panel_step.cpp`:
  - `excite: on while the deck is on SAMPLER`.
  - `excite: off when the deck leaves SAMPLER for SYNTH`.
  - `excite: BBD → SAMPLER keeps it on, SAMPLER → BODY clears it`.
  - The existing BBD cases stay green unchanged.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.** VCV is untouched: its checkbox drives the bit there, and VCV never calls `apply_excite_events()`. Confirm with a grep and say so in the commit.
- [ ] **Step 4:** Build, then ctest → PASS. RED: drop `|| sampler_part` → `on while SAMPLER` fails. Restore.
- [ ] **Step 5: Commit** `fix(shell): a SAMPLER deck records the other deck again`.

### Task 3: The bar clock (engine)

**Files:**
- Modify: `engine/instrument.h/.cpp`
- Create: `tests/test_bar_clock.cpp`, registered in `CMakeLists.txt` with the other `spky_tests` sources.

**Interfaces:**
```cpp
// Instrument (observers)
uint64_t frame_now() const;          // samples processed since init
uint64_t last_bar_frame() const;     // frame index of the last bar line (beats crossed a multiple of 4)
int      beat_in_bar() const;        // floor(beats) mod 4, 0 = beat 1 -- for the TEMPO lamp
float    frames_per_beat() const;    // 48000 * 60 / (bpm * pace)
```
- On each control tick, compare `floor(beats / 4)` before and after the tick.
  - When it rose by exactly one, a line was crossed. Its frame is `tick_frame - 96 + 96 * (4k - beats_before) / (beats_after - beats_before)`, rounded down.
  - When it jumps for any other reason, nothing is logged, except after `reset_transport()` and `restart(age)`. Those log the jump's frame as a bar line, because beat 0 is a downbeat.
  - A `clock_pulse` that snaps backwards across a line logs nothing. A forward snap across a line logs the snap's frame.
- One bar clock for the instrument, not one per deck.

- [ ] **Step 1: Write the failing tests:**
  - `bar clock: the line frame matches a per-sample reference`. Run 120 BPM and 130 BPM (130 is not block-exact, spec §3) for 20 s. Track the reference from `beats()` per sample in the test, as `bpm*pace/(60*48000)` per sample from init. Every logged line lies within ±1 frame of it.
  - `bar clock: PACE x1/4 and x4 move the lines`.
  - `bar clock: RST logs a line at the reset frame`, and `restart(age)` does the same.
  - `bar clock: a backwards clock snap logs nothing and doubles nothing`. Use clock pulses at 120 BPM with jitter that snaps beats back across a line, and assert exactly one line per bar.
  - `bar clock: beat_in_bar counts 0..3`.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.** The tick is in the per-sample loop at `_ctrl_ctr == 0` (`instrument.cpp`, `_center.update`). Read `beats()` before and after it.
- [ ] **Step 4:** Build, then ctest → PASS, hashes unchanged (observers only). RED: drop the interpolation (log `tick_frame`) → the 130 BPM case fails. Restore.
- [ ] **Step 5: Commit** `feat(engine): a bar clock with frame-exact bar lines`.

### Task 4: Ring and delayed writer (engine)

**Files:**
- Modify: `engine/instrument.h`: `FxMem::rec_ring[PART_COUNT]` (`SampleBuffer::Frame*`), `FxMem::rec_ring_frames`.
- Modify: `engine/sampler/sampler_engine.h/.cpp`, and `engine/parts/part.cpp` (reset the ring's valid count when the swap lands on SAMPLER).
- Modify: `tests/test_sampler_engine.cpp`

**Interfaces:**
```cpp
// SamplerEngine
void     set_ring(SampleBuffer::Frame* mem, size_t frames);  // nullptr: no ring, D stays 0
uint64_t frame_now() const;              // this deck's sample counter (advances in process_in)
void     set_delay(uint32_t d);          // only while the writer is idle; d <= ring - 128
uint32_t delay() const;
void     rec_at(uint64_t frame, bool on); // the writer starts/stops when its index reaches frame
                                          // (frame >= frame_now() - delay(), asserted)
void     rec_cancel_pending();
```
- `process_in(l, r)`:
  - Write the ring at `now % ring`. Count the ring's valid frames since the deck turned onto SAMPLER; a read below that count is silence.
  - The writer index is `now - D`. Where it equals a pending target, call `_buf.set_recording(on)`.
  - Feed `_buf.write()` the ring frame at the writer index, and run the slice detector on it as today (`sampler_engine.cpp:162-176`).
  - `D == 0` is exactly today's path, with no ring read.
- `is_recording()` is unchanged: it reports the buffer's state, so it stays true for `D` after a stop target.

- [ ] **Step 1: Write the failing tests** in `tests/test_sampler_engine.cpp`:
  - `writer: delayed writer equals live writer`. Two engines are fed the same 6 s of noise plus transients.
    - Engine 1: `D = 0`, `set_recording` at frames 48 000 / 192 000, then an overdub 240 000 / 260 000.
    - Engine 2: `D = 24 000`, `rec_at` the same frames.
    - Once both writers are idle, the buffers, `rec_size()` and the exported slice markers are equal bitwise.
  - `writer: snap off has no ring and D = 0`. With no `set_ring`, `set_delay(100)` is refused, and the output is bitwise equal to today's engine over a record/overdub script.
  - `writer: a fresh ring reads silence`. Turn onto SAMPLER, `D = 24 000`, start at once. The first 24 000 frames of the take are zero.
  - `writer: cancel drops a pending target`.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Build, then ctest → PASS, hashes unchanged. RED: read the ring at `now - D + 1` → `equals live writer` fails. Restore.
- [ ] **Step 5: Commit** `feat(sampler): a ring and a delayed writer`.

### Task 5: The snap rule (engine, pure)

**Files:**
- Create: `engine/sampler/rec_snap.h` (header-only, no engine types beyond `<cstdint>`).
- Create: `tests/test_rec_snap.cpp` (register in `CMakeLists.txt`).

**Interfaces:**
```cpp
namespace spky {
struct SnapIn {
    uint64_t now, last_bar;
    double   beats;          // transport beats at the press
    float    frames_per_beat;
    uint32_t delay;          // the writer's D (stop presses: late window = min(beat, D))
    bool     start;          // start or stop press
};
enum class SnapKind : uint8_t { Now, Past, HeldNext };
struct SnapOut { SnapKind kind; uint64_t target; };   // target valid for Now / Past
SnapOut rec_snap(const SnapIn& in);
inline constexpr float kSnapMaxBeatFrames = 72000.f;
inline constexpr int   kSnapBeatsPerBar   = 4;
}
```
- If `frames_per_beat > kSnapMaxBeatFrames`, the result is `Now`.
- A late press, with `now - last_bar < window`, gives `Past` with target `last_bar`. The window is one beat for a start and `min(beat, delay)` for a stop.
- An early press, with `4 - fmod(beats, 4) <= 1`, gives `HeldNext`.
- Anything else gives `Now`.

- [ ] **Step 1: Write the failing tests:**
  - The spec's rule table, row by row, at 120 BPM and at 40 BPM PACE x1 (exactly 72 000 frames: it snaps).
  - No snap at 39 BPM and at 120 BPM PACE x1/4.
  - A stop's late window is clamped to `delay`.
  - The edges: exactly one beat after the line gives `Now`; exactly one beat before it gives `HeldNext`.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Build, then ctest → PASS. RED: `<` → `<=` on the late window → the edge test fails. Restore.
- [ ] **Step 5: Commit** `feat(sampler): the REC snap rule`.

### Task 6: Wire the snap into `Instrument` and the law (engine + control)

**Files:**
- Modify: `engine/instrument.h/.cpp`. In `init(sr, mem)`, the rings from `FxMem` go to the decks.
- Modify: `control/control_law.h`: the adapter gains `sampler_rec_requested`, and the REC push compares against it (`control_law.h:640-649`).
- Create: `tests/test_rec_snap_instrument.cpp` (register in `CMakeLists.txt`).

**Interfaces:**
```cpp
void set_rec_snap(bool on);              // refused (stays off) without both rings
bool rec_snap() const;
bool sampler_rec_requested(int p) const; // start held, or writer in fadein/sustain, and no stop held
bool sampler_rec_held(int p) const;      // a start or stop waits for the next bar line
```
`sampler_record(p, on)` with the snap on:
- **Start from idle.** Set `D = min(frames_per_beat, kSnapMaxBeatFrames)`, or 0 when the snap rule's slow case applies. Monitor on. Then:
  - `Now` → `rec_at(now - D + D)`, which is the writer's index `D` later, i.e. the frame of the press;
  - `Past` → `rec_at(last_bar)`;
  - `HeldNext` → held. When the bar clock logs the next line, `rec_at(that line)`.
- **Stop** while the writer is in fade-in or sustain, or a start is held: the same rule with `start = false`. Monitor off at the press. A stop with nothing held, in fade-in or in sustain is a no-op; this is the fade-out re-push.
- **A second press of the same kind while held** cancels the held request.
- **`sampler_clear()` and `sampler_begin_load()`** cancel everything held and pending first.
- **An engine change away from SAMPLER** (`set_engine`): `rec_cancel_pending()` and an immediate buffer stop.
- **The load lockout:** `set_recording(true)` stays refused while loading.
- **RST and RESET while a start is held:** the logged line becomes the target.

- [ ] **Step 1: Write the failing tests.** Equivalence is the core. Two `Instrument`s are fed the same input, at 120 BPM:
  - (a) snap off, REC pushed exactly on the bar-line frames (from `last_bar_frame()` of a dry run);
  - (b) snap on, REC pushed 0.3 beat late, and in another case 0.3 beat early.

  After both writers are idle, the buffer, `rec_size()` and the markers are equal. The cases:
  - `snap: late start`, `snap: early start`;
  - `snap: late stop`, `snap: early stop` (first take);
  - `snap: overdub late start / late stop`;
  - `snap: elsewhere acts at the press` (D later, same frames);
  - `snap: second press cancels a held start`, and the same for a held stop;
  - `snap: clear and begin_load cancel`, `snap: engine change stops at once`;
  - `snap: RST while a start is held starts on the reset line`;
  - `law: re-push in fade-out is not a press`. Drive the real law with REC held low for 3 blocks after a stop; `sampler_rec_held` stays false throughout.
  - `snap: set_rec_snap refused without rings`.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Build, then ctest → PASS, hashes unchanged (snap off everywhere it is not set). Two REDs:
  - Make a stop in fade-out count as a press → `law: re-push` fails.
  - Ignore `Past` (use `now`) → `late start` fails.

  Restore both.
- [ ] **Step 5: Commit** `feat(sampler): REC snaps to the bar`.

### Task 7: Lamps (control)

**Files:**
- Modify: `control/led_law.hpp`, `tests/test_led_law.cpp`

The REC lamp:
- `sampler_rec_held(p)` → an 8 Hz even blink at the record pulse's light (1.0 / dark), from `p.blink`.
- The held state wins over "recording".

The TEMPO lamp:
- When `inst.rec_snap()` is on and the beat is shorter than `kSnapMaxBeatFrames`, the pulse on `beat_in_bar() == 0` gets light 1.0 and the other three 0.35, through `duty_from_light`.
- Otherwise, today's full-duty pulse on every beat.

- [ ] **Step 1: Write the failing tests:**
  - `rec lamp: held blinks at 8 Hz`;
  - `tempo lamp: beat 1 brighter with snap on`;
  - `tempo lamp: unchanged with snap off`. Every existing TEMPO test stays green.
- [ ] **Step 2:** Build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Build, then ctest → PASS. RED: use `beat_in_bar() == 1` → the beat-1 test fails. Restore.
- [ ] **Step 5: Commit** `feat(control): held-REC blink and a bright beat 1`.

### Task 8: Firmware: rings and `SHELL_REC_SNAP` (shell)

**Files:**
- Modify: `shell/sdram_mem.cpp`: `g_rec_ring[2][kRecRingFrames]`, `DSY_SDRAM_BSS`, `alignas(32)`, with a size static_assert.
- Modify: wherever `shell/` fills `FxMem` (grep `sampler_buf`): add the rings. After `init`, call `inst.set_rec_snap(SHELL_REC_SNAP)`.
- Create: `shell/write_shell_rec_snap.py`, modelled on `write_shell_takes.py` (the parse-time switch; it deletes stale objects).
- Modify: `shell/Makefile`:
  - `SHELL_REC_SNAP ?= 1`;
  - its `SWITCH_HEADERS` entry;
  - the stale-object list (`main.o sdram_mem.o` and any file that reads the header).
- Modify: `shell/check_images_build.py`: `reva_play` builds with the snap on (default), and a `reva_play_no_snap` image with `SHELL_REC_SNAP=0` for the board's A/B.
- Modify: `shell/README.md`: the switch row.

- [ ] **Step 1:** `make -C shell -j8 images`, plus `python shell/check_images_build.py` → green. Record `.sdram_bss` from `shell/build/shell.map` for the play image; it must stay below 64 MB with margin (spec P4: about 37.8 MB).
- [ ] **Step 2:** The two images differ: `cmp` the `reva_play` and `reva_play_no_snap` bins. They must differ, which proves the switch resolves at parse time (memory `fireflow-bench-stale-object-trap`).
- [ ] **Step 3: Commit** `feat(shell): REC snaps to the bar on Rev A (SHELL_REC_SNAP)`.

### Task 9: VCV (host)

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` (and `FireflowHW` if it builds its own `FxMem`):
  - rings on the heap, next to the sampler buffers;
  - a menu item "REC snaps to bar", default off;
  - the JSON key `recSnap`, absent → off.

- [ ] **Step 1:** Run `host/vcv/build-local.sh`. The panel guards (`res/test_panel.py`, `res/test_hw_panel.py`) run as plain scripts and stay green.
- [ ] **Step 2:** Run `host/vcv/build-local.sh install`, and tell Bastian to restart Rack.
- [ ] **Step 3: Commit** `feat(vcv): REC snaps to bar, as a menu option`.

### Task 10: Board session (with Bastian's go)

**Files:** Create `docs/hardware/rec-snap-measured.md`.

- [ ] **Step 1: The callback, hands off.** Flash a clean `reva_play_no_snap`, capture 10 min with `read_play.py`, and record `cb_max`. Repeat with `reva_play`. Bastian's persisted patch loads in both: these are play images, not stress images, so they write no QSPI without a panel change. The gate: the snap costs less than 2 % of a block (19 200 cycles) over the no-snap image.
- [ ] **Step 2: By hand** (Bastian; Claude reads the log):
  1. Deck A SAMPLER, deck B a bass. Press REC on A, then press again: the take holds B's bass.
  2. Press late, just after beat 1, then stop early, just before beat 1: the take starts and ends on the bar, and the loop sits in time with B.
  3. During a held start, REC blinks fast. TEMPO shows beat 1 brighter.
  4. An overdub snaps the same way.
  5. Turn TEMPO below 40 BPM, and PACE down: REC acts at once, and TEMPO pulses evenly.
  6. An external source at the input is recorded and heard while recording; B is not doubled.
  7. Listening items L1–L6 from the spec; Bastian's verdict on each.
- [ ] **Step 3:** Write the numbers and the verdicts into `rec-snap-measured.md`. Commit `docs(hardware): REC snap on Rev A`.
- [ ] **Step 4:** Leave the board on the normal play image. If the takes branch is merged by then, use its play flags.

### Task 11: Docs

- `docs/engine-map.md`:
  - the bar clock;
  - the deck route on SAMPLER, with the probe numbers from spec §3;
  - the `init()` monitor side find.
- `docs/gotchas.md`:
  - the writer runs `D` behind, so `is_recording()` stays true for one beat after a stop press;
  - snap off is `D = 0`.
- `docs/by-ear-decisions.md`: the blink rate and the TEMPO levels, once Bastian confirmed them.
- `docs/roadmap.md`: an M6 entry, and "Last updated".
- The spec's status line: built, with a pointer to `rec-snap-measured.md`.

- [ ] Commit `docs: REC snaps to the bar`.

## Branch

Work on a new branch `feat/rec-snap` cut from `feat/sampler-takes-sd`. Tasks 6 and 8 touch the same REC paths that the takes work reads (`sampler_is_recording`, `take_block`), and the takes branch is not merged yet. If the takes branch is merged before Task 1 starts, cut from `main` instead.
