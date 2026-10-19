# CLOCK and RESET jacks — one clock law for VCV and the firmware

Date: 2026-10-19. Status: draft, awaiting Bastian's approval.
Follows: the Rev A master plan (`2026-09-28-rev-a-master-plan-design.md`, P6 →
P7), P2's pin map (`2026-09-29-rev-a-p2-pin-map-design.md`), and the P6b-1
pattern of one shared control law (`2026-10-09-rev-a-p6b1-shared-control-law-design.md`).

## 1. Intent

Rev A's firmware reads no jack at all today. This round gives the two transport
jacks, CLOCK and RESET, their behaviour **before the board arrives**. All of the
logic goes into shared, desktop-tested code. VCV uses it at once, and the
firmware is wired to it, so the P7 bring-up only has to measure the electrical
side.

Bastian's use case: FireFlow in a rack next to other sequencers, clocked from
**Pamela's Workout**, with CLOCK and RESET coming from the same source. In his
words, the hard part is *"das Timing: wie bekommen wir es hin, dass ein externer
Trigger auch immer die korrekte Bar trifft"*.

There is no board with a jack on a Daisy gate input in the house. The coupon
leaves B5, B6, B9 and B10 unconnected (`hardware/coupon/fab/coupon.net`,
`unconnected-(U_SM-GATE_IN_1-PadB10)` and its three siblings), so no real signal
reaches the pins before Rev A.

## 2. Decided behaviour (Bastian, 2026-10-19)

### 2.1 CLOCK

- **One pulse per quarter note**, as today. PPQN selection (later, perhaps as a
  SHIFT function) is not part of this round.
- While pulses arrive, they set the tempo. The tempo is measured from the
  **period between two consecutive edges** (§3 shows that VCV does not measure
  that today). Each pulse also phase-aligns the beat, as `clock_pulse()` does
  today.
- **When the pulses stop, FireFlow keeps running at the last measured tempo.**
  It does not jump to the TEMPO knob. The knob takes over only once it is
  turned. When pulses return, the clock takes over again.
- The hardware cannot tell whether a cable is plugged in. "The clock has gone"
  is therefore decided by a timeout (§6).

### 2.2 RESET

- **RESET acts only in STEP.** In FLOW it is ignored, because there is no bar
  to land on. STEP and FLOW are set **per deck** (`control_law.h:825-826`:
  `set_step(p, steps > 0, steps)`), so this is a per-deck rule: a reset
  restarts every deck that is in STEP and leaves a FLOW deck untouched. A STEP
  deck with SYNC off (COUPLE in its free zone) restarts too, as any sequencer
  restarts its pattern on a reset. *(My call, read from Bastian's "nur in
  STEP".)*
- **RESET restarts the music, not only the bar.** Both decks restart at phrase 0,
  pattern A, step 1, as a sequencer restarts its pattern. Bastian: *"sonst würde
  man nur Clock ohne Reset patchen"*. Today a reset only snaps the lanes to the
  bar; the song keeps its position (§3).
- **With a running clock, a reset arms and the next clock pulse is the 1.**
  Pamela's, like most sequencers, sends RESET shortly before the 1. A reset that
  arrives a few milliseconds **after** a clock pulse still counts for that pulse
  (window in §6), so either ordering of the two edges lands on the same 1.
- **Without a clock, the reset trigger itself is the 1.** FireFlow restarts at
  once, in the running tempo. Without an outside grid there is nothing to be
  early or late against. Bastian dropped the earlier proposal of jumping,
  braking or snapping depending on the distance to the internal 1 (*"passt"*).

### 2.3 Lamps

The CLK and RST lamps flash per jack pulse, as the LED law already defines
(`control/led_law.hpp:219-220`, `:352-356`). The firmware fills these two inputs
for the first time. RST flashes on every pulse at the jack, in FLOW too, because
the lamp shows the jack, not the effect.

## 3. Measured and read facts

**Measured (probe, 2026-10-19).** Desktop, `clang++ -O2`, main at `18d6fe01`;
probes in the session scratchpad.

- **VCV's tempo measurement is wrong.**
  - What the code does: `Fireflow.cpp:737-745` zeroes `clkSamples` on every
    edge, and `:719-720` computes `measured_bpm = 60 * sr / clkSamples` on every
    control push (every 16 samples, `:396`). That is the time since the last
    edge, not the period.
  - Measured on a 120 BPM clock: 1350 of 4500 law ticks fall outside 20..400 and
    fall back to the TEMPO knob, and the accepted values run from 120 to 400 BPM.
  - The transport replayed with the same formula (`Transport` at 500 Hz,
    `clock_pulse(1.0)` per edge, knob 140 BPM) counts 30.6 beats for 16 pulses
    at a 120 BPM clock, 32.3 at 60 BPM and 15.9 at 240 BPM. So FireFlow in VCV
    on an external clock should run near double tempo except around 240 BPM.
  - This is a replay of the host code, not a run in Rack.
- **What `reset_transport()` does today.**
  - Setup: an `Instrument` in STEP, sync on, 120 BPM, 8 steps, reset at
    10.30 s, 10.55 s and 10.80 s.
  - Each time, deck A's pitch lane stands at phase 0.004 2 ms after the reset,
    against 0.001 wanted. The lanes jump hard to the bar
    (`Instrument::reset_transport` → `SuperModulator::reset_phases`,
    `super_modulator.h:168-195`).
  - The song position keeps counting (20 → 39 over the following ~10 s), so
    the song does not restart.

**Read from the code (not probed).**

- **Pins.** CLOCK is on B10 (Gate In 1) and RESET on B9 (Gate In 2) (P2,
  `2026-09-29-rev-a-p2-pin-map-design.md:71-72`). libDaisy's `DaisyPatchSM`
  exposes them as `gate_in_1` / `gate_in_2` (`daisy_patch_sm.h:256`). `GateIn`
  has `Trig()`, `State()` and an `invert` flag that defaults to true
  (`hid/gatein.h`).
- **Firmware timing.** The firmware runs 96-sample audio blocks at 48 kHz, i.e.
  2 ms (`shell/main.cpp:395`, measured in an earlier round). If the gate inputs
  are read once per block, an edge is known to ±1 block.
- **Firmware control path.** It calls the control law once per block through
  `shell/control_tick.cpp:42-48`, with a static `control::Options` that never
  sets `measured_bpm`. `led_tick` (`:50-55`) never sets `clock_pulse` /
  `reset_pulse`.
- **Engine.** It already has `Instrument::clock_pulse()` (`instrument.h:536`;
  it passes the pace on to `Center::clock_pulse(pace)`, `center.h:65`, and
  `Transport::clock_pulse` rounds the beat count to the nearest pace multiple
  from the anchor) and `reset_transport()` (`instrument.h:540-543`). Nothing in
  it restarts a deck's song: `phrase_index` is set to 0 only at lane init
  (`lane.cpp:83`) and in `_apply_pending_song_work` (`lane.cpp:330-336`).
- **A reset already applies pending song work** (read by the builder check,
  not probed). `ModLane::reset()` sets `_cur_step = -1` (`lane.cpp:603`), and
  the next lane step runs `_apply_preroll_work()` (`lane.cpp:345-350`), which
  applies any pending FORM / NEW / length work — a rebuild, and with it a new
  song-offset latch.
- **Gate inputs are polled.** `GateIn::Trig()` compares the pin level between
  calls (`hid/gatein.cpp:13-19`), so a trigger shorter than the polling
  interval can be missed. `GateIn::Init` configures the pin as an input with
  no pull resistor (`gatein.cpp:7`).
- **Pot ownership after the first turn.** An owned pot passes every change
  above `kPotHysteresis` = 16 counts (`shell/scan_value.h:21`,
  `scan_value.cpp:39-42`). Only an unowned pot needs the larger `kPotTouch`
  move (`shell/pot_owner.h`, measured idle spread up to 67 counts).
- **VCV's "Resync to bar" menu item** (`Fireflow.cpp:385`, `:753`, `:1479`)
  calls `reset_transport()` too.

## 4. Goals

1. One shared clock-and-reset law that VCV and the firmware both call, so the
   two hosts cannot drift apart. It takes edges and a sample clock and produces
   the tempo and the reset/clock events; it decides nothing about pins.
2. Under a steady external clock, the transport counts **one beat per pulse**
   (the §3 regression: 16 pulses → 16 beats, in VCV too).
3. Armed reset: with a clock running, RESET followed by a clock pulse restarts
   both decks on that pulse. RESET within the post-edge window after a pulse
   restarts on that pulse as well.
4. Without a clock, RESET restarts at once.
5. A restart means: the transport bar goes to 0, and every deck in STEP has its
   lanes at phase 0 (today's `reset_transport`) and **its song at phrase 0 and
   pattern A**.
6. RESET does nothing to the music of a deck in FLOW.
7. Clock loss holds the last measured tempo. The TEMPO knob takes over only
   when it moves, and returning pulses take over again.
8. The firmware reads CLOCK and RESET from Gate In 1/2, feeds the shared law,
   fills the CLK/RST lamps, and builds with `make images`.

## 5. Non-goals and anti-goals

- No PPQN setting and no SHIFT function.
- No other jacks (CV, V/Oct, MOD1..4, audio in, the PITCH/GATE outputs).
- No "stop when the clock stops" mode (Bastian chose to keep running).
- No MIDI clock and no clock output.
- No change to `clock_pulse()`'s phase rounding or to the grid servo.
- **The reset is not a re-generation of its own.** A restart raises no
  `new_phrase`. Work that is already pending (a SONG detent, a FORM change, a
  STEPS length change) is applied at the restart, exactly as it would be at the
  next phrase wrap. A restart is a wrap, and the song offset latches there by
  the rule of the ROOT round. With nothing pending, the same song plays from the
  top. *(My call: this is what `reset_transport()` already does (§3), and a
  reset that withheld a SONG change the player just made would feel broken.)*
- **No distance-dependent jump/brake/snap without a clock** (rejected above).
- VCV's "Resync to bar" menu item stays a bar resync and does not restart the
  song. *(My call: it is named "to bar", and moving it would change a working
  feature nobody asked about.)*

## 6. Constraints and my calls

- **Where the law lives.** A header beside `control/control_law.h` and
  `control/led_law.hpp`, desktop-testable with doctest. It must not include a
  libDaisy or Rack type.
- **Time base.** The law advances by a sample count per call. VCV calls it per
  sample (count 1), the firmware per block (count 96). An edge is stamped at the
  call that sees it, so firmware edge times are quantized to 2 ms.
- **Tempo from the period.** BPM = 60 · sr / samples between the last two edges,
  accepted in 20..400 BPM as today (`control_law.h:57-58`); a period outside
  that is ignored. At 120 BPM, a 2 ms quantization error is 0.4 % of one
  period. *(Whether to average several periods is the builder's call, decided
  by a desktop test with ±1-block jitter, not by assumption.)*
- **Clock-loss timeout: 2 × the last measured period.** *(My call: long enough
  that one late pulse does not drop the clock, short enough that a reset after
  Pamela's stops acts at once.)* After the timeout the clock is "gone": the
  tempo stays at the last measured value, and RESET acts immediately (§2.2).
- **Knob takeover.** While the clock is gone, the TEMPO knob value at the moment
  of loss is remembered. Any later value that differs gives the knob the tempo
  back. In VCV a knob changes only when it is turned. On the board an owned pot
  passes noise above 16 counts (§3), so **the firmware releases the TEMPO pot's
  ownership when the clock is lost**, the same release the MOD latch already
  does (P6b-2). The pot must then move past `kPotTouch` before its value
  changes at all. The shared law stays threshold-free. *(My call: reuse the
  measured mechanism rather than invent a second threshold.)*
- **Triggers down to 1 ms must be caught on the board.** Polling `GateIn` once
  per 2 ms block can miss a short trigger (§3), and a missed RESET is a wrong
  bar. The firmware latches edges of B10/B9 with an edge interrupt (EXTI) or an
  equivalent hardware latch, and the block reads and clears the latch. The edge
  time is still the block it lands in (±2 ms). *(My call: 1 ms covers common
  Eurorack triggers. Whether EXTI lines on these pins are free is for the
  builder to check against libDaisy and the existing pin use.)*
- **The coupon image does not read the gate pins.** B9/B10 float on the coupon
  (§1, no pull resistor), and random edges would set wild tempos and restart
  songs on the only board in the house. The gate read is compiled into the
  Rev A play image only. *(My call.)*
- **Post-edge reset window: 10 ms.** A reset up to 10 ms after a clock edge
  restarts as if it had come just before that edge, i.e. the restart happens
  at the reset, up to 10 ms after the beat. *(My call: Pamela's sends reset
  before the 1, so the window is a safety net for other sources and for the
  firmware's ±2 ms edge quantization. It is short enough that it cannot catch
  a reset meant for the next beat at any tempo up to 400 BPM, whose beat is
  150 ms.)*
- **Armed reset without a following pulse.** If the clock is lost while a reset
  is armed, the restart happens at the timeout. *(My call.)*
- **Both hosts use the same law and no connection test.** VCV stops asking
  `inputs[CLOCK].isConnected()` for the tempo. An unplugged jack reads 0 V,
  never edges, and times out exactly like the hardware. *(My call: identical
  behaviour in both hosts beats a VCV-only shortcut.)* The input thresholds
  stay as they are: VCV's Schmitt trigger at 0.1 / 1 V, and on the board
  whatever `GateIn` and Rev A's input give (measured at P7).
- **Gate polarity on the board.** Use `DaisyPatchSM`'s `gate_in_1/2` as its
  `Init()` configures them. Whether the edge reads the right way round is a
  P7 check, not a guess here.
- **Engine.** A new restart that, for each deck in STEP, does what
  `reset_transport()` does today (lanes to phase 0, the deck clock to 0) plus
  restarting that deck's song at phrase 0 and pattern A. At phrase 0 no cadence
  binding is needed: `song_symbol_at(mode, 0)` is 0 for every song mode
  (`song_form.h:75-104`), and the binding only fires for pattern B
  (`lane.cpp:333-336`). The transport's bar is reset whenever any deck
  restarts. A FLOW deck's lanes are not touched. Today's `reset_transport()`
  resets every deck's lanes. *(Name, placement, and whether
  `reset_transport()` itself changes or a new call sits beside it are the
  builder's; the menu's "Resync to bar" keeps its current effect.)*
- House rules: two toolchains never mixed; VCV work ends with
  `host/vcv/build-local.sh install`; English in every file; commit trailer per
  CLAUDE.md; every new test proven RED once.

## 7. Where it touches (read, the builder confirms)

- **New shared law:** `control/` (new header), with doctest coverage in
  `tests/`.
- **Engine:** `engine/instrument.h` (restart), `engine/mod/super_modulator.h`
  and `engine/mod/lane.*` (song restart per deck).
- **VCV:** `host/vcv/src/Fireflow.cpp`
  - `:737-753`: edges into the shared law; its events drive `clock_pulse`,
    restart and the lamps.
  - `:719-720`: `measured_bpm` comes from the law.
  - Whether `FireflowHW` shares this path (it shares the `Fireflow` struct,
    `Fireflow.cpp:1938`).
- **Firmware:**
  - `shell/control_tick.cpp`: feed the law, fill `measured_bpm` and the lamp
    inputs.
  - The gate reads go wherever the block's control work runs (`panel_scan.cpp`
    calls `control_tick` at `:175`).
- **Docs:** `docs/roadmap.md` (M6 / P7 entry), `docs/engine-map.md` (the
  measured reset facts above), `host/vcv/README.md`, `shell/README.md`, and
  `docs/gotchas.md` if the VCV double-tempo finding is confirmed in Rack.

## 8. Acceptance

1. **Shared-law doctests**, run per sample and per 96-sample block. Each one is
   RED once.
   - Steady pulses at 60, 120 and 240 BPM give that BPM (within a tolerance the
     builder derives and states).
   - ±1-block jitter keeps the tempo within that tolerance.
   - Reset before a pulse restarts on the pulse.
   - Reset 5 ms after a pulse restarts on that pulse; at 15 ms it waits for
     the next one.
   - Reset with no clock restarts at once.
   - Clock loss holds the tempo after 2 × the period.
   - The knob takes over only when it moves; returning pulses win again.
   - An armed reset with a lost clock fires at the timeout.
2. **Transport regression:** an `Instrument` driven through the shared law with
   16 pulses at 120 BPM counts 16 ± 0.5 beats. This is the §3 bug, RED against
   today's VCV formula.
3. **Restart:**
   - After a restart, every STEP deck reports song position 0 and pattern A,
     and its lanes stand at phase ≈ 0.
   - A FLOW deck beside it is untouched: same song position, and its lanes do
     not jump.
   - With pending NEW work, the restart applies it (one rebuild).
   - With nothing pending, no rebuild happens.
4. **VCV:** a source guard in `host/vcv/res/test_panel.py` that `Fireflow.cpp`
   no longer computes BPM from time-since-edge and calls the shared law.
5. **Firmware:**
   - `make images` links (Daisy toolchain, never mixed).
   - The shared law is the only clock logic in `shell/`.
   - The coupon image contains no gate read.
   - The TEMPO pot is released on clock loss (a host test or source guard).
6. `ctest` Release `-E "^reva_"` green; `host/vcv/build-local.sh install`.
7. **Bastian in Rack:** Pamela's clock and reset into FireFlow. The tempo
   matches Pamela's (not double), the 1 lands with Pamela's 1, a stop keeps the
   tempo, and turning TEMPO then takes over.
8. **P7, not a merge gate:** on Rev A, clock and reset from a real module. Check
   the threshold, the polarity, a 1 ms trigger caught, and the 2 ms edge jitter
   by ear and on the CLK lamp.

## 9. Left to the builder

- Names and file layout of the shared law and the engine restart.
- Whether the tempo uses one period or a short average (decided by the jitter
  test).
- Where in the firmware's block the gates are read.
- The exact tolerance in acceptance 1 and 2, derived and written down.
