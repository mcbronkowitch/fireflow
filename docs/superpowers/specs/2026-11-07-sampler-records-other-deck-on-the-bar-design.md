# Sampler records the other deck, on the bar

Date: 2026-11-07. Status: design, not built; revised the same day after an
independent review (§9). Engine: `engine/sampler/`, `engine/instrument.h`.
Control: `control/control_law.h`, `control/led_law.hpp`. Firmware:
`shell/panel_step.cpp`. Hosts: Rev A play image first; VCV and the render host
get the engine part behind a switch that defaults off.

## 1. Intent

Bastian, 2026-11-07: a deck on SAMPLER should record the other deck again, and
REC should land in time. Pressed shortly before the end of a bar, a start or a
stop waits for the bar line; pressed shortly after it, it counts as if it had
been pressed on the line.

Today on Rev A a SAMPLER deck records only the audio input. With nothing
plugged in, REC records silence: the lamp pulses, then shows content, and the
take is mute. That is what Bastian heard on 2026-11-07 (deck A SAMPLER, deck B
playing a bass, two REC presses, no sound).

## 2. Decisions taken (Bastian, 2026-11-07)

- **Source:** the other deck **and** the audio input, summed. No new control.
- **Snap:** both ways. In the last beat before a bar line the press waits for
  the line; in the first beat after it the press counts from the line;
  anywhere else it acts at once. The same rule applies to start and stop.
- **Slow settings:** no snap where it makes no sense (§4.2).
- **Monitor:** while recording, the monitor plays the audio input only, not
  the other deck (§4.1, after the probe in §3).
- **TEMPO lamp:** beat 1 of the bar brighter (§4.3).
- A late overdub stop that cannot snap may stop at once (asked before the
  revision; the revised design snaps it anyway, §4.2).

## 3. What is there (measured or read, with the source)

- **The engine can already do it.** `Part::process` sums the sibling deck into
  the sampler's record input when the deck's `excite_other_deck` source is on
  (`part.h:466-472`: `fast_tanh(in + _deck_in)`, `SPKY_DECK_BUS` is 1 by
  default). The tap is the sibling's output after GRIT / FLUX / COMP, before
  CHOKE, MORPH, the reverb return and the limiter, one sample late
  (`instrument.h:756-769`, `instrument.cpp:414-416`, `579-582`).
  Probe `scratchpad/probe_deckrec.cpp` (desktop, `-O2`, A = SAMPLER, B = SYNTH
  init, 120 BPM, no audio input, record A for 4 s after 2 s): source off →
  take RMS **0.00000**, deck B tap RMS 0.0667; source on → take RMS
  **0.06932**, B tap RMS 0.0697. rec_size 192 000 frames in both runs.
- **The audio input changes path too.** The sampler always gets the audio
  input; `excite_audio_in` feeds only BODY's bus (`part.cpp:546`). With the
  deck source on, the input goes through `fast_tanh(in + deck_in)` even while
  the other deck is silent (`part.h:463-472`). Probe
  `scratchpad/probe_deckrec2.cpp` (fresh `Instrument` per case, B an empty
  SAMPLER, a 220 Hz sine at the input, 2 s take), take RMS / peak with the
  source off → on:
  - amplitude 0.1: 0.0707 / 0.100 → 0.0705 / 0.0997, unchanged;
  - amplitude 0.5: 0.3535 / 0.500 → 0.3332 / 0.462, −0.5 dB;
  - amplitude 0.9: 0.6364 / 0.900 → 0.5355 / 0.716, −1.5 dB, peaks rounded.
- **The monitor doubles the other deck.** Same probe, A = SAMPLER, B = SYNTH
  init, A records from 3 s:
  - source off: master RMS 0.0425 before REC, 0.0344 during;
  - source on: master RMS 0.0425 before REC, **0.0706** during.

  With the monitor turned off by hand (`probe_deckrec3.cpp`), source on reads
  0.0387 during REC, and A's tap carries only the grains of the take (0.0285).
- **Feedback.** Both decks SAMPLER, both recording each other, 0.5 s of noise
  at 0.9 into the input and then silence, 20 s. Nothing goes non-finite.
  - Monitor on: the loop holds itself at full scale, deck taps peak 2.59 and
    the output peaks at 1.000.
  - Monitor off: the loop still sustains through the grains, taps peak 1.41.

  A SAMPLER deck recording a BBD deck that hears it: taps peak 1.68 with the
  monitor on and 0.99 with it off. All bounded, none decays.
- **A side find.** `Instrument::init()` does not reset the sampler's monitor
  flag. The probe's first draft reused one `Instrument`, and the second case
  inherited a monitor left on by the first. No host re-inits while
  recording today as far as this spec checked; noted, not fixed here.
- **Why the board lost it.** `apply_excite_events()` (`shell/panel_step.cpp:39-40`,
  commit `2f78f17a`, 2026-10-27) turns `excite_other_deck` on at a deck's entry
  into BBD and off on every tick the deck is not on BBD. Before that commit
  nothing turned it off, so a deck that had once visited BBD went on recording
  the other deck on SAMPLER — the behaviour Bastian remembers. VCV keeps it as
  a menu checkbox ("Route: other deck (BODY excite, SAMPLER feed+rec, BBD
  feed)", `Fireflow.cpp:1582`), default off.
- **The transport.** It counts beats at `bpm × pace` (`instrument.cpp:247`,
  `transport.h:26`), one tick per 96-sample control block (`center.cpp:86`).
  The TEMPO lamp blinks every one of these beats (`led_law.hpp:284`) and shows
  no downbeat. The engine has **no bar**: "a bar is 4 beats" is this spec's
  choice, matching the RATE ladder's labels ("2 bars" = 8 steps at one step
  per beat, engine-map §6). The beat count is not continuous: an external
  clock edge snaps it forwards or backwards (`transport.h:35-41`), RST sets it
  to 0 and RESET to a small value (`instrument.h:597-624`).
- **Tempo range.** TEMPO knob 40..240 BPM (`control_law.h:963`); an external
  clock 20..400 (`control_law.h:62`). One beat at 40 BPM, PACE ×1, is 1.5 s.
- **REC reaches the engine** as a latch pushed every control tick: the law
  calls `sampler_record(p, want)` whenever `want != sampler_is_recording(p)`
  (`control_law.h:640-649`), and raises `ev.rec_started` each time.
  `is_recording()` stays true through the 192-frame fade-out.
- **The record state machine** (`sample_buffer.cpp:51-206`): idle → fade-in
  (192 frames) → sustain → fade-out → idle. A stop of a first take rewinds the
  write head to 0 and fades **new live input** onto the head of the take
  (`sample_buffer.cpp:106-112`); `cut()` then locks the loop. An overdub writes
  `old × feedback + new` into the locked loop from frame 0. The slice detector
  marks frames as the write head covers them (`sampler_engine.cpp:162-176`).
- **REC lamp** (`led_law.hpp:404-436`): 2 Hz pulse while recording, steady at
  the fill level with content, dark when empty.
- **CPU.** `cb_max` on Bastian's patch is about 690 000 of 960 000 cycles per
  block (`docs/hardware/takes-measured.md` §6).
- **Memory.** The last build in `shell/build/shell.map` has `.sdram_bss` at
  0x22E3000 B (36.6 MB) of 64 MB. Which flags that build had is not recorded;
  the plan re-reads it on its own build (§6 P4).

## 4. Design

### 4.1 Source: a SAMPLER deck hears the other deck

`apply_excite_events()` sets `excite_other_deck` while the running engine is
BBD **or SAMPLER**, and clears it on every other engine. The Events gain a
`sampler_part[p]` level beside `bbd_part[p]`; BBD's entry edge rule stays.

- The bit follows the running engine, which lands about 100 ms after boot
  (`panel_scan.cpp:235-238`). REC is not persisted (`persist_policy.cpp:11`),
  so no take can start before it. `apply_excite_hold()` and
  `excite_after_take` stay BBD-only; their comments say so already
  (`persist_policy.h:40`) and get one line about SAMPLER.
- VCV: unchanged. Its checkbox stays the player's choice.
- **Monitoring: the audio input only** (Bastian, 2026-11-07, after the probe
  in §3). `sampler_record()` still turns the monitor on with REC
  (`instrument.h:425-430`), but the monitor plays the raw audio input, not
  the summed record input. The other deck is not doubled, and the
  one-sample loop between two monitoring decks is gone. An external source
  being recorded is still heard. Part hands the sampler the raw input for the
  monitor beside the summed input for the buffer; with the deck source off
  the two are the same samples, so nothing moves there.
- **Feedback paths.** Two SAMPLER decks recording each other, or a SAMPLER
  deck recording a BBD deck that hears it, still loop through the grains
  (§3: bounded, sustained). Not prevented; a listening item (§7 L2).

### 4.2 Snap: REC on the bar, by writing one beat late

**The idea.** The record buffer is written from a short ring of the deck's
input, a fixed delay `D` behind live. Every start and stop is turned into a
target **frame index** on a sample counter. The delayed writer acts when it
reaches that index. A bar line that has just passed is still ahead of the
writer, so a late press needs no trimming and no copying back. The take is
built in order, exactly as an on-time press would have built it. The fade-in,
the fade-out onto the head, the overdub feedback and the slice detector all
run unchanged on the writer's path.

An engine part per deck (`engine/sampler/rec_snap.h`), off by default,
switched on by `Instrument::set_rec_snap(bool)`. The shell turns it on; VCV
gets a menu item, default off. **Off means `D = 0` and no ring**: the record
path is the one of today, so no render, test or render hash moves.

**Sample counter.** A per-deck 64-bit frame count, advanced in
`Part::process`. `now` is its value at the current sample.

**Bar lines.** Each control tick compares `floor(beats / 4)` with the last
one. When it rises by one, a bar line was crossed inside the block; its frame
index is interpolated from the beat count before and after the tick. When the
count jumps (RST, RESET, or an external clock edge that snaps backwards or
skips), the new position counts as a bar line only for RST and RESET (beat 0
is a downbeat); a clock snap re-aims nothing and logs nothing. The part keeps
the last bar line's frame index.

**The window and the delay.** `w` is one transport beat in frames at the
press. `D` is set when the writer leaves idle and stays fixed until it is idle
again: `D = w` at that moment, so the writer runs one beat behind live. A stop
press uses `min(w_now, D)` as its late window, since a writer `D` behind
cannot reach further back.

**Slow settings: no snap** (Bastian, 2026-11-07). When one transport beat is
longer than 1.5 s (72 000 frames: slower than 40 BPM at PACE ×1, or 120 BPM
at PACE ×1/4), the press acts at once and a writer that starts from idle gets
`D = 0`. A bar is then over 6 s, a seventh of the 42 s buffer. Fast settings
keep the snap: at 240 BPM and PACE ×4 the window is 62 ms.

**The rule.** For a press at `now`, with `L` the last bar line:

| where the press lands | target |
|---|---|
| `now − L < window` (late) | `L` |
| the next bar line comes within `w` (early) | held; the target is that line's frame index once it is crossed |
| elsewhere, or slow settings | `now` |

"The next bar line comes within `w`" is read from the beat count at the press
(`4 − beats mod 4 ≤ 1`). If the transport jumps before the line is crossed
(RST, RESET), the held request takes the jump's bar line as its target.

The writer applies a start or a stop on the sample where its index equals the
target. For an overdub stop this is the case Q1 asked about. It now snaps both
ways like every other press.

**The ring.** Per deck, `D_max` + one control block + 32 frames:
72 000 + 96 + 32 = 72 128 frames, 577 KB of SDRAM, injected through `FxMem`
like the sample buffers (engine "no heap", `instrument.h:20-34`). The ring is
written on every sample while the deck is on SAMPLER and the snap is on, and
it holds the same summed input the buffer would write. It keeps a count of
the frames written since the deck turned onto SAMPLER; a read below that
count is silence. Ring frames pass the buffer's existing non-finite guard on
write (`sample_buffer.cpp:192-198`).

**Pending and cancel.**
- A second REC press while a start is held cancels the start. A second press
  while a stop is held cancels the stop.
- `sampler_clear()` (SHIFT + REC goes straight there, `panel_scan.cpp:1067`)
  and `begin_load()` cancel everything held and stop the writer. The clear
  and the load already empty the buffer.
- An engine change away from SAMPLER stops the writer **at once**. The take
  ends `D` short of the change; a held start is dropped.
- `set_recording(true)` stays locked out while a take loads
  (`sampler_engine.cpp:180`), held or not.

**The law.** It compares REC against a new observer
`sampler_rec_requested(p)` instead of `sampler_is_recording(p)`. The observer
is true while a start is held or the writer is in fade-in or sustain, and no
stop is held. The fade-out does not count. A stop request with nothing held,
in fade-in or in sustain is a no-op, so the law's re-push during a fade-out
cannot start a held stop. `rec_started` then fires once per press. In VCV it
also fires on a held press that is cancelled later and drops the sample path
for nothing (`Fireflow.cpp:761`). Accepted.

**Monitoring** follows the request: on at a start press, off at the stop
press. It stays live; the monitor never goes through the ring, and it plays
the audio input only (§4.1).

**Takes on the SD card.** A held start is not recording; the saver may begin
a save, which the start then aborts as today (`take_policy.cpp:78`,
`take_still_valid`). The writer is still recording for `D` after a stop press,
and the saver waits for it through `is_recording()`.

### 4.3 The lamps

- **REC**: a held request blinks fast (8 Hz, the record pulse's brightness).
  By eye (§7 L3).
- **TEMPO**: the pulse on beat 1 of each bar is brighter than the other three;
  the other three get dimmer to make room, because the lamp is at full duty
  today. Beat 1 is the beat after the bar line of §4.2. By eye (§7 L4). With
  the snap off (slow settings) the lamp is unchanged.

### 4.4 What it costs (to be measured, §6)

While the snap is on and the deck is on SAMPLER, each sample costs:
- one ring write;
- one ring read while the writer runs;
- one 64-bit compare of the writer index against a target.

Each control tick adds one `floor` of the beat count. All of it shows in
`cb_max` against the 690 000 baseline before the play image takes it.

## 5. Proof

- **Host tests** (`tests/`):
  - the rule table for start and stop, first take and overdub, early, late
    and elsewhere, at 120 BPM and at 40 BPM PACE ×1 (1.5 s, the slowest beat
    that snaps);
  - no snap at 39 BPM PACE ×1 and at 120 BPM PACE ×1/4;
  - **equivalence:** a late or early press yields frame for frame the same
    buffer, the same `rec_size` and the same slice markers as an on-time
    press on a snap-off instrument, fed the same input, compared after both
    writers are idle;
  - cancel by a second press, by `sampler_clear()`, by `begin_load()`, and
    the engine change;
  - RST and RESET while a start is held;
  - an external clock edge that snaps backwards across a bar line;
  - a fresh ring right after the deck turns onto SAMPLER reads silence;
  - snap off leaves every existing render and test unchanged, with the render
    hash gate green.
- **RED once** for each: break the bar-line interpolation, the ring index and
  the target compare, and see the matching test fail.
- **Board:** `cb_max` over 10 min on Bastian's patch, snap on, deck A SAMPLER
  recording repeatedly; then Bastian by hand records B's bass on A, presses
  late and early, and hears the take land on the bar.

## 6. Probes

- **P1.** Through `apply_excite_events()` with the deck on SAMPLER: that is
  the plan's first host test, since the function cannot do it before the
  change.
- **P2.** Done, 2026-11-07, results in §3: the audio-in level, the doubled
  deck, the feedback paths.
- **P3.** The cost of the ring per sample is measured on the board, not on
  the desktop: desktop nanoseconds do not size the H7's cycles (the
  control-cost lesson in the bench notes). The `cb_max` run of §5 is the
  measurement.
- **P4.** Done: `shell/build/shell_git_hash.h` reads `0db106f`, with
  `SHELL_PANEL_SCAN=1`, `SHELL_KEYS_NC=1`, `SHELL_PERSIST=1` and
  `SHELL_TAKES=0`. That play image's `.sdram_bss` is 36.6 MB of 64 MB. The two
  rings add 1.15 MB, and the takes' 4 KB header comes on top.

## 7. Listening items

- **L1.** Recording B on A with the audio-input-only monitor: B stays at its
  own level, and the take shows up once REC stops.
- **L2.** Both decks on SAMPLER recording each other; SAMPLER recording a BBD
  deck (§3: sustained loops).
- **L3.** The held-request blink.
- **L4.** The TEMPO lamp's bright beat 1 against the dimmer three.
- **L5.** An audio-in take through the new `tanh` path, against today's.
- **L6.** The take becomes playable one beat after the stop press (the writer
  is `D` behind). Audible as a short wait at fast tempos, up to 1.5 s at slow
  ones.

## 8. Non-goals and limits

- No new panel control; no change to VCV's routing checkbox.
- No resampling of the master mix or the reverb return: the tap stays the
  deck's own output before MORPH and the reverb.
- No quantised loop length beyond what the snap gives: a take started and
  stopped outside the windows keeps its own length.
- After a live STEPS turn the decks free-run against the transport until RST
  (`center.cpp:261-273`); the snap then lands on the transport's bar, not on
  what the deck plays.

## 9. Review, 2026-11-07

An independent review read the first draft against the code. Its three
blockers were all in the late side, which trimmed and copied after the fact:
- a trimmed take's slice markers past the new end made a saved take fail its
  restore;
- the trim's fade-out needs live input that does not exist yet;
- a pre-roll copy into a short overdub loop wraps out of order.

Its should-fix findings covered:
- stale frames during the copy;
- the ring sizing and uninitialised SDRAM;
- the law's re-push during a fade-out;
- the discontinuous beat count;
- the engine-change cancel;
- the audio-input `tanh`;
- the per-sample compare.

The delayed writer of §4.2 replaces trim and copy, and with them the first
four findings. The others are answered in §3, §4.2 and §6. Unmeasured claims
from the first draft are now probes (§6).
