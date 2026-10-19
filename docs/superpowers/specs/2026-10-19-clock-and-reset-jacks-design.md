# CLOCK and RESET jacks — one clock law for VCV and the firmware

Date: 2026-10-19. Status: approved by Bastian 2026-10-19 (after a builder
check and a completeness review); plan
`docs/superpowers/plans/2026-10-19-clock-and-reset-jacks.md`.
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

- **RESET restarts the song only on decks in STEP** (Bastian: "nur in STEP").
  STEP and FLOW are set **per deck** (`control_law.h:825-826`:
  `set_step(p, steps > 0, steps)`). A STEP deck with SYNC off (COUPLE in its
  free zone) restarts too, as any sequencer restarts its pattern on a reset.
- **Both decks' lanes return to phase 0, as today** (Bastian took the
  recommendation, 2026-10-19). A FLOW deck is coupled to the bar or to the
  other deck whenever COUPLE > 0. If only the STEP deck's lanes reset, the
  coupling drags the FLOW deck for minutes afterwards (measured, §3). So a reset
  keeps today's behaviour for the lanes of both decks (`reset_transport()` →
  `reset_phases()` on both) and adds the song restart only on STEP decks. A
  FLOW deck keeps its song position.
- **With no deck in STEP, RESET does nothing** (the lamp still flashes). *(My
  call: resetting the bar alone would drag synced FLOW decks the same way.)*
- **RESET restarts the music, not only the bar.** Every STEP deck restarts at
  phrase 0, pattern A, step 1, as a sequencer restarts its pattern. Bastian:
  *"sonst würde man nur Clock ohne Reset patchen"*. Today a reset only snaps the
  lanes to the bar, and the song keeps its position (§3).
- **"From the top" plays the pattern as it has evolved** (Bastian took the
  recommendation). With VARY ≠ 0 the patterns are mutated in place at phrase
  wraps (`lane.cpp` ~790-822); the factory patch is on GROW. A restart does not
  rewind those mutations or the EVOLVE walk; it plays the current state from
  phrase 0.
- **With a running clock, a reset arms and the next clock pulse is the 1.**
  Pamela's, like most sequencers, sends RESET shortly before the 1. A reset that
  arrives a few milliseconds **after** a clock pulse still counts for that pulse
  (window in §6), so either ordering of the two edges lands on the same 1.
- **Without a clock, the reset trigger itself is the 1.** FireFlow restarts at
  once, in the running tempo. Without an outside grid there is nothing to be
  early or late against. Bastian dropped the earlier proposal of jumping,
  braking or snapping depending on the distance to the internal 1 (*"passt"*).
- **Known limitation, accepted:** a source that sends RESET on every loop keeps
  the song near phrase 0. In the factory patch one phrase of deck B is 64
  beats, so a reset every 4 bars plays 2 of its 8 steps and never reaches
  pattern B (computed from the code, not measured). Bastian's decision to
  restart the music stands. A "bar only" reset is a candidate for a later SHIFT
  option, not this round.

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
- **Restarting only one deck's lanes drags the other** (completeness review,
  probe `rv_flow_drag.cpp`; method: transport plus the STEP deck's lanes
  reset, the FLOW deck's phase kept, compared against a run without reset).
  - Cause: `Center::update` servos both decks onto the transport in the GRID
    world with no STEP check (`center.cpp:166-181`), and couples them by
    Kuramoto and grid gravity in the FREE world (`center.cpp:185-236`).
  - Factory patch (COUPLE 1.0, deck A FLOW/FEED, deck B STEP 8, PACE ×1/16):
    deck A's pitch scale sits at the 0.35 cap and is still 0.21 cycles off
    after 120 s.
  - PACE ×1, 120 BPM: up to 0.32 for about 4 s.
  - FREE zone: 0.375 at COUPLE 0.45, 0.795 at 0.4999, 0.10 at 0.2, and 0 at
    COUPLE 0.
- **Pulses outside 20..400 BPM still phase-snap** (probe `rv_range.cpp`).
  `Transport::clock_pulse` rounds to the nearest pace multiple
  (`transport.h:36-41`), whatever the period. 480 BPM pulses (80 in 10 s)
  leave the transport at **0.000 beats**, at PACE ×1 and ×1/16 alike: it stands
  still. 15 BPM pulses make it lurch about 0.3 beat backwards on every pulse.
- **Block-exact test tempos hide quantization** (computed). 60, 120 and 240 BPM
  are exactly 500, 250 and 125 blocks of 96 samples. 130 BPM is 230.77 blocks,
  so a single-period measurement alternates 130.43 / 129.87 BPM (±0.43 %).
  Every `set_tempo_bpm` change moves FLUX's delay target (`flux.cpp:41-67`),
  which slews with 30 ms (`flux.cpp:22`, `:205`). That is a pitch blip of
  roughly 15 cents at FLUXRATE 1/32 and over two semitones at 1/2, on every
  beat.

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
  no pull resistor (`gatein.cpp:7`). `GateIn` inverts by default, so a rising
  edge at the jack is a falling edge at the pin.
- **libDaisy's EXTI layer cannot serve both jacks** (completeness review).
  B10 is PG13 and B9 is PG14 (`daisy_patch_sm.h:282-283`), and both share
  `EXTI15_10`.
  - `GPIOInterruptHandler` keeps one pin per EXTI channel
    (`lib/libDaisy/src/per/gpio.cpp:30-39`, `:56-61`), so a second
    `RegisterPin` overwrites the first.
  - `HandleInterrupt` clears only the stored pin (`:63-74`), and the IRQ runs
    at priority 0,0 (`:158-159`).
  - A custom `EXTI15_10_IRQHandler` would collide with libDaisy's
    (`gpio.cpp:259`).
- **Trigger vs. tempo lamp.** The TEMPO lamp reads `beat_phase()`, which
  carries PACE (`led_law.hpp:274`). At the factory PACE ×1/16 it flashes every
  16 clock beats.
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
2. Under a steady external clock, the transport counts **pace × one beat per
   pulse** (the §3 regression: at PACE ×1, 16 pulses → 16 beats, in VCV too).
3. Armed reset: with a clock running, RESET followed by a clock pulse restarts
   on that pulse. RESET within the post-edge window after a pulse restarts on
   that pulse as well.
4. Without a clock, RESET restarts at once.
5. A restart means: the transport bar goes to 0, both decks' lanes go to
   phase 0 (today's `reset_transport`), and **every STEP deck's song goes to
   phrase 0 and pattern A**.
6. A FLOW deck keeps its song position. With no deck in STEP, RESET does
   nothing but flash its lamp.
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
- **Tempo from the period, smoothed across pulses.**
  - BPM comes from the time between edges, accepted in 20..400 BPM as today
    (`control_law.h:57-58`).
  - The measurement spans several periods (the time across the last N valid
    edges, divided by N−1), so the 2 ms quantization does not turn into a
    per-beat tempo step (§3: ±0.43 % at 130 BPM, audible on FLUX).
  - The tempo the engine gets changes **only at a pulse**, never as a per-tick
    glide. A glide would defeat the setters' early-outs
    (`super_modulator.h:28-31`) on the coupon budget.
  - *(The value of N is the builder's, derived from a desktop test with a
    non-block-exact tempo, swing and a tempo step. The goal: the per-pulse
    tempo change at a steady 130 BPM stays below a stated bound, and a tempo
    step settles within a stated number of pulses.)*
- **Out-of-range pulses are no clock.** A pulse whose period is outside
  20..400 BPM is ignored completely: no phase snap, no arming, and it does not
  count as "clock running" for the timeout. Otherwise a 480 BPM train freezes
  the transport (§3). *(My call.)*
- **The first interval after a discontinuity is not a period.**
  - It is discarded after boot, after a clock loss, after a VCV bypass, and
    after a sample-rate change.
  - The law has a reset entry the hosts call there (VCV `reinit()` and
    un-bypass; firmware boot).
  - The clock counts as running only after two valid edges, so the first edge
    alone neither arms a reset nor starts the timeout. *(My call.)*
- **Clock-loss timeout: 2 × the last measured period.** *(My call: long enough
  that one late pulse does not drop the clock, short enough that a reset after
  Pamela's stops acts soon.)* After the timeout the clock is "gone": the tempo
  stays at the last measured value, and RESET acts immediately (§2.2).
- **Knob takeover.**
  - While the clock is gone, the TEMPO knob value at the moment of loss is
    remembered. Any later value that differs gives the knob the tempo back,
    and the tempo **jumps** to the knob on that first move (no pickup), as
    pots do elsewhere on the instrument.
  - The knob reaches 40..240 BPM only, while a held clock tempo can be anything
    in 20..400.
  - In VCV a knob changes only when it is turned. On the board an owned pot
    passes noise above 16 counts (§3), so **the firmware releases the TEMPO
    pot's ownership when the clock is lost**, the same release the BBD/FLUX
    event already uses (`shell/panel_step.cpp:21-30`). The pot must then move
    past `kPotTouch` before its value changes at all.
  - The shared law stays threshold-free.
  - *(My call: reuse the measured mechanism rather than invent a second
    threshold.)*
- **Edges on the board are latched in the EXTI pending bits, with no
  interrupt.**
  - Polling `GateIn` once per 2 ms block can miss a short trigger, and a
    missed RESET is a wrong bar.
  - libDaisy's EXTI layer cannot serve both jacks (§3), so the firmware sets
    up EXTI edge detection on lines 13 and 14 with the NVIC interrupt left
    disabled. The block reads and clears the pending bits.
  - No ISR, no handler conflict, and the cost stays bounded under a noisy or
    audio-rate input. Any trigger that makes an edge is caught, 1 ms included.
  - The edge time is the block it lands in (±2 ms). Sub-block stamping (DWT
    `CYCCNT` in an ISR) is not part of this round.
  - The edge direction follows the inverting input: falling at the pin is
    rising at the jack, and P7 confirms it.
  - Pending bits are cleared just before `StartAudio`, so boot glitches do not
    arrive as edges.
  - *(My call, from the completeness review.)*
- **The coupon image does not read the gate pins.** B9/B10 float on the coupon
  (§1, no pull resistor), and random edges would set wild tempos and restart
  songs on the only board in the house. The gate read is compiled into the
  Rev A play image only. *(My call.)*
- **Post-edge reset window: 10 ms, back-dated.** A reset up to 10 ms after a
  clock edge restarts as if it had come just before that edge. The restart
  happens at the reset, and the transport beats and the lane phases are then
  advanced by the samples elapsed since the edge, so nothing runs behind and
  the next pulse does not have to pull it back. *(My call: Pamela's sends reset
  before the 1, so the window is a safety net for other sources and for the
  firmware's ±2 ms edge quantization. It is short enough that it cannot catch
  a reset meant for the next beat at any tempo up to 400 BPM, whose beat is
  150 ms.)*
- **Reset and clock in the same call** (possible on the board, where both
  latch into one block): the reset counts as arriving first, so that pulse is
  the 1.
- **Armed reset without a following pulse.** If the clock is lost while a reset
  is armed, the restart happens on FireFlow's own next beat at the held tempo,
  not at the timeout. *(My call: it stays on the running grid.)*
- **The RST lamp shows the jack.** It flashes when the pulse arrives, not when
  an armed restart fires, and there is no separate "armed" display. *(My
  call.)*
- **Both hosts use the same law and no connection test.** VCV stops asking
  `inputs[CLOCK].isConnected()` for the tempo. An unplugged jack reads 0 V,
  never edges, and times out exactly like the hardware. *(My call: identical
  behaviour in both hosts beats a VCV-only shortcut.)* The input thresholds
  stay as they are: VCV's Schmitt trigger at 0.1 / 1 V, and on the board
  whatever `GateIn` and Rev A's input give (measured at P7).
- **Gate polarity on the board.** Use `DaisyPatchSM`'s `gate_in_1/2` as its
  `Init()` configures them. Whether the edge reads the right way round is a
  P7 check, not a guess here.
- **Engine.** A new restart that does today's `reset_transport()` (transport
  bar to 0, both decks' lanes and deck clocks to 0) and in addition restarts
  the song of every deck in STEP at phrase 0 and pattern A.
  - At phrase 0 no cadence binding is needed: `song_symbol_at(mode, 0)` is 0
    for every song mode (`song_form.h:75-104`), and the binding only fires for
    pattern B (`lane.cpp:333-336`).
  - It does not rewind VARY's mutations or the EVOLVE walk.
  - *(Name and placement are the builder's. The menu's "Resync to bar" keeps
    calling plain `reset_transport()`.)*
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
   - **Tempo:**
     - Steady pulses at 60, 120 and 240 BPM, and at the non-block-exact
       130 BPM, give that BPM within a tolerance the builder derives and states.
     - At a steady 130 BPM the per-pulse tempo change stays below the stated
       bound.
     - A swung source and a tempo step settle within the stated number of
       pulses.
     - The tempo changes only at pulses.
   - **Out-of-range pulses** (480 BPM, 15 BPM) neither snap nor arm.
   - **First interval:** the first interval after boot, after a loss and after
     a host discontinuity is not taken as a period. One edge alone does not
     count as a running clock.
   - **Reset timing:**
     - Reset before a pulse restarts on the pulse.
     - Reset and pulse in the same call restart on that pulse.
     - Reset 5 ms after a pulse restarts on that pulse, back-dated. At 15 ms it
       waits for the next one.
     - Reset with no clock restarts at once.
   - **Clock loss and takeover:**
     - Clock loss holds the tempo after 2 × the period.
     - A stop/start shorter than the timeout keeps the clock and gives no
       tempo excursion.
     - The knob takes over only when it moves, and returning pulses win again.
     - An armed reset with a lost clock fires on the next internal beat.
2. **Transport regression:** an `Instrument` at **PACE ×1**, driven through the
   shared law with 16 pulses at 120 BPM, counts 16 ± 0.5 beats. This is the
   §3 bug, RED against today's VCV formula.
3. **Restart:**
   - After a restart, every STEP deck reports song position 0 and pattern A,
     and both decks' lanes stand at phase ≈ 0.
   - A FLOW deck beside it keeps its song position. Afterwards its pitch-rate
     correction stays within ε of a run whose lanes were reset the same way.
     That pins "no drag" in GRID and in the FREE zone with COUPLE > 0, the case
     §3 measured.
   - With no deck in STEP, a reset changes nothing.
   - With pending NEW work, the restart applies it (one rebuild); with nothing
     pending, no rebuild happens.
4. **VCV:** a source guard in `host/vcv/res/test_panel.py` asserts three things
   about `Fireflow.cpp`:
   - `clkSamples` and `60.f * curSr / clkSamples` are gone;
   - the shared law is called per sample;
   - its reset entry is called from `reinit()`.
5. **Firmware:**
   - `make images` links (Daisy toolchain, never mixed).
   - The shared law is the only clock logic in `shell/`.
   - The coupon image contains no gate read.
   - The TEMPO pot is released on clock loss (a host test or source guard).
6. `ctest` Release `-E "^reva_"` green; `host/vcv/build-local.sh install`.
7. **Bastian in Rack:** Pamela's clock and reset into FireFlow, with PACE ×1
   for the check. At the factory PACE ×1/16 the TEMPO lamp flashes only every
   16 beats (§3). Listen for four things:
   - the tempo matches Pamela's, not double;
   - the 1 lands with Pamela's 1;
   - a stop keeps the tempo;
   - turning TEMPO then takes over.
8. **P7, not a merge gate:** on Rev A, clock and reset from a real module. Check
   the threshold, the polarity, a 1 ms trigger caught, and the 2 ms edge jitter
   by ear and on the CLK lamp.

## 9. Left to the builder

- Names and file layout of the shared law and the engine restart.
- The averaging window N and the bounds in acceptance 1 (derived from the
  tests).
- Where in the firmware's block the pending bits are read, and how the VCV
  bypass is detected.
- The exact tolerance in acceptance 1 and 2, derived and written down.
