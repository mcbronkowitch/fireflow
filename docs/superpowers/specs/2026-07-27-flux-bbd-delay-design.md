# FLUX becomes a bucket-brigade delay — the clock is the instrument

**Date:** 2026-07-27
**Status:** Approved (design review with user)
**Supersedes:** `2026-07-20-rhythm-fed-delay-taps-design.md` entirely. The taps
are deleted, not revised. See "What is cut and why".

## Context

FLUX is a tape echo: an interpolating delay line, one band-pass at 800 Hz, a
`fast_tanh` soft clip in the feedback path, and — since 2026-07-20 — two
read-only taps placed by the other bank's rhythm.

The taps have not earned their keep. The user's verdict:

> Ich finde die 2 taps mittlerweile nicht mehr so spannend. Sie verbrauchen
> Resourcen und 2 Knobs, man hört sie nur wenn Delay weit aufgedreht ist,
> wenig Feedback hat und das andere Deck einen passenden Rhythmus spielt.
> Insgesamt nicht befriedigend.

That is three conditions that must coincide before anything is audible, paid
for in the scarcest two currencies the instrument has: block budget and panel
real estate. `instrument_worst_taps` measures 96.9 % average and 101.8–102.2 %
maximum of the block budget — already over
(`docs/bench/2026-07-26-518f639-system.md:95-96`).

This spec spends that reclaimed budget on a physical model of the
Electro-Harmonix Deluxe Memory Man, and reaches past it into territory no
pedal occupies.

The user's target, verbatim: *"Es kann ruhig richtig dreckig werden, wir wollen
bis ins experimentelle Territorium vordringen."*

## The physics, and where the numbers come from

A bucket-brigade device has **no read pointer**. All 8192 charge packets are
clocked forward together. Four consequences follow, and they are the entire
design:

**1. Delay time is clock rate.**

```
t_d = N / (2 · f_clk)      N = total stages
```

The DMM cascades 2× MN3005 (4096 stages each), so `f_clk = 4096 / t_d`.
Verified two independent ways: the MN3005 datasheet specifies 4096 stages at
10–100 kHz giving 20.48–204.8 ms, and the factory calibration document for the
EH-7850 measures a clock period of 120–140 µs (7.1–8.3 kHz) at maximum delay —
against 7.45 kHz calculated for 550 ms. Two sources, one number.

**2. Changing the clock pitch-shifts what is already stored.** Not a side
effect — EHX documents it as a feature: *"Turning the DELAY knob, while
listening to your echoes, will bend the pitch of your notes."* There is no
crossfade in the physical device and there must be none in the model.

**3. Bandwidth follows the clock.** Charge-transfer inefficiency over 8192
stages puts the corner at

```
f_-3dB ≈ f_clk / 4
```

At 550 ms that is ~1.9 kHz. *This*, not the filter chain, is why long delays
are dark — the filters are fixed at roughly six poles around 3.5–4 kHz and do
not move.

**4. Chorus is clock modulation.** The triangle LFO is injected into the same
CD4047 timing node the DELAY pot sits on. Chorus ≈ 0.9 Hz, vibrato ≈ 4 Hz (the
switch swaps timing capacitors, ratio 4.7:1), depth at full travel ≈ ±10 % of
the clock period. Because depth is *relative*, the pitch excursion scales with
delay time: ~16 cents at 50 ms, ~120 cents at 400 ms.

Around the line sits an NE570 compander (2:1 compressor before, 1:2 expander
after, τ = 10 kΩ × 1 µF = 10 ms) whose job is the BBD's 75 dB noise floor and
whose audible signature is tails that get pulled down harder than a linear
delay would pull them, with the noise breathing along.

The feedback path re-enters *before* the compander, so every repeat pays the
full chain again: bandwidth shrinks multiplicatively, ~2.5 % THD accumulates,
and at the MN3005's 0.9 V_RMS ceiling the loop saturates softly and then
self-oscillates as a thick distorted tone rather than a screech.

### One correction recorded on purpose

An earlier estimate in this project put the model at 8–12 % CPU stereo. That
used `f_clk = 2N/t_d`, which is 4× too high. The datasheet cross-check above is
authoritative. The corrected relation is the one that makes this design
affordable at all:

```
Events/Sample = f_clk / fs        Bandwidth ≈ f_clk / 4
→  Bandwidth [Hz] = Events/Sample × 12000        (at 48 kHz)
```

**You pay CPU per Hz of BBD bandwidth. Delay time does not enter.** A
DMM-typical 3.4 kHz costs 0.28 events per sample.

## What is cut and why

| Deleted | Reason |
|---|---|
| `engine/fx/taps.{h,cpp}` — `TapBank`, `TapeTap`, `derive_offsets` | A BBD has no read pointer. Taps are not being removed for budget alone; they contradict the model. |
| `EchoDelay`, `TapeBpf`, `DeLine` in `flux.h:17-148` | Replaced by `BbdEcho`. Full replacement, not a mode — see below. |
| `Flux::set_tap_offsets`, `Flux::taps_active`, the rhythm push through `super_modulator` → `instrument.cpp` | Nothing left to feed. |
| `tests/test_taps.cpp`, `bench/workloads_taps.cpp` | Replaced by `test_bbd.cpp` and `workloads_bbd.cpp`. |

`src/core/echo.h` and `src/core/deline.h` are pre-fork originals and are **not**
touched. Whether they are still built is a verification item for the plan.

### Full replacement, not a TAPE/BBD mode

FLUX's current tape voicing is tuned by ear: the 800 Hz band-pass at Q 0.1
(`flux.h:76-78`), `fast_tanh` with its hard clamp at 3.65, feedback running to
1.2 (`flux.cpp:55`). The BBD brings its own filter chain, its own saturation
point and the compander — it replaces exactly that tuning.

The user chose full replacement over a switchable mode. A TAPE mode kept beside
it would be a second delay implementation to maintain and test, plus a switch
the real hardware would also have to carry, for a voicing that would quickly
become a museum piece. Existing patches keep the meaning of RATE, MIX and FB
but not their sound. That is accepted and deliberate.

## Architecture

New: **`engine/fx/bbd.{h,cpp}`**, three units with one job each.

**`BbdLine<kMaxStages>`** — the Holters/Parker core (DAFx-18), ported from
jpcima's `bbd_line` / `bbd_filter` (BSL-1.0, MIT-compatible). An N-stage buffer
over injected memory, driven by a clock frequency, with the input and output
filters carried as partial-fraction parallel branches so the resampling
interpolation is done *by* the filters rather than before them.

Interface: `Init(buf, sample_rate)`, `SetClock(hz)`, `SetStages(n)`,
`Process(in)`.

It knows nothing about music — no BPM, no divisions, no feedback. That is what
makes it testable against physics rather than against itself.

**`Compander`** — the NE570 pair, τ = 10 ms, compressor before and expander
after the line. Small, standalone, tuned by ear.

**`BbdEcho<kMaxStages>`** — assembles the chain: DRIVE → compressor →
`BbdLine` → expander → feedback path with per-pass darkening and soft
saturation. Drops into `EchoDelay`'s place with the same `Process(in, …)`
shape.

Unchanged: `Flux` as a class, its name and public form — the `SoftSwitch`,
`engaged()`, the bit-exact off path, `set_rate` / `set_mix` / `set_feedback` /
`set_bpm`, and the shared delay-time slew. `PartFx` changes two setters.

Renamed: `set_dust` → `set_drive`, `set_rot` → `set_stages`.

Newly wired: `v[FXT_FLUX_TIME]`, which `part_fx.cpp:31` already smooths and
then discards — alone among the five targets — now reaches `Flux`.

**The division of labour that holds this together:** `BbdLine` knows only
physics, `Flux` knows only music, and between them sits a free function
`bbd_clock_hz(delay_seconds, stages)` that converts time to clock and enforces
the ceiling. Pure, stateless, separately tested. It is the one formula everyone
would otherwise doubt while debugging.

## The clock law

```
f_clk = stages / (2 · t_d)     clamped to f_max = 32000 Hz
Bandwidth ≈ f_clk / 4          Events/Sample = f_clk / fs
```

**The 32 kHz ceiling is physical, not arbitrary.** The real device's fixed
post-BBD filter chain sits at ~3.5–4 kHz and the clock never overtakes it, so
BBD bandwidth beyond ~8 kHz is inaudible by construction. 32 kHz yields 8 kHz —
twice what is needed — and bounds the worst case at 0.67 events per sample. At
8192 stages the ceiling engages below 128 ms, which is precisely where the
fixed filters dominate anyway. It costs nothing audible and makes the budget
computable.

There is no floor. The mud at the long end is the point.

## Parameters

**RATE** — unchanged ladder: `kFluxRateOffset = 5`, 12 rungs from "1/2" to
"1/32" (`divisions.h:41-46`). The `t_max` buffer-safety clamp at `flux.cpp:47`
is **removed**: delay time is no longer bounded by buffer length, only by how
dark the user is willing to go. RATE is now a tone control as much as a time
control: the ladder spans 16× in time at a fixed tempo, and once the ceiling is
applied that is roughly 8× in brightness at 120 BPM — 8 kHz at "1/32" down to
1.0 kHz at "1/2", and further down as the tempo drops.

**STAGES** (was ROT) — geometric, 512 to 16384, the original at 8192. Five
octaves of brightness at fixed delay time: grainy, dark, image-rich at the
bottom; clean and fast at the top. Physically this is swapping the chip; no
pedal exposes it.

**DRIVE** (was DUST) — 0..1 mapped to −6 … +24 dB into the compressor/BBD
stage, saturation threshold fixed. DRIVE sits **inside** the loop, so each
repeat saturates again: clean at the bottom, harmonics accumulating over the
repeats from roughly two thirds up, and with high FB it tips into thick
self-oscillation.

This is not redundant with GRIT. GRIT runs before FLUX (`part_fx.cpp:40`) and
dirties the input once; DRIVE dirties every pass.

**FEEDBACK** keeps its 1.2 over unity (`flux.cpp:55`) so self-oscillation stays
reachable — documented behaviour of the original, not an accident. The bound
now comes from saturation *within* the loop rather than `fast_tanh` on the read
path.

**The compander is not a parameter.** It is tuned by ear and fixed. It is the
device's character, not a setting of it, and the word "compression" is already
spoken for by COMP on the panel.

## Modulation

`FXT_FLUX_TIME` is reactivated. The 2026-07-17 spec retired it with
*"modulating the delay time makes no musical sense"* — true of a crossfade
delay, false of a BBD, where clock modulation *is* the sound generation.

**Two smoothers, two jobs, and they must not be the same one:**

- The ladder sets base time through the existing 30 ms slew (`flux.cpp:18`).
  It stays, and it now doubles as the VCO slew of the real circuit: division
  changes stay click-free and bend in pitch, like the hardware.
- The lane pulls **multiplicatively on the clock**, downstream of the base,
  through the 2 ms smoother already present at `part_fx.cpp:12`. Had modulation
  gone through the 30 ms path it would have been a ~5 Hz low-pass and a 4 Hz
  vibrato would not have survived.

Lane depth maps geometrically to ×1/4 … ×4. Since pitch tracks the clock ratio
directly, that is ±2 octaves at full depth. The original's ±10 % clock swing is
±1.65 semitones, so the entire historical chorus/vibrato range lands in the
bottom tenth of the control. The historical device is the start of the scale,
not the target.

**STAGES also runs through the 30 ms slew.** Stage count is a buffer length,
not a continuous quantity; changing it means swapping the chip, and that
clicks. Slewing it is not what a physical part does, but it produces exactly
the class of artefact this device already makes — a drift in time and pitch —
which turns STAGES into a playable gesture rather than a setup control.

## Memory

`FxMem::echo` (`instrument.h:19-27`) shrinks from 262144 to `kMaxStages` =
16384 floats per channel: **4.19 MB → 256 KB** across all four lines. SDRAM
occupancy drops from 67.0 % to roughly 61 %.

The injection pattern is unchanged and the lines stay in SDRAM. At 8192 stages
the active window is 32 KB per line and is walked sequentially rather than
streamed, so the 3.29× SDRAM penalty measured for streaming walks is
**expected** to largely disappear here. That is an expectation, not a
measurement; the bench must confirm it.

## CPU — a gate, not a claim

Removed: the taps (~4.7 points of worst case, from `instrument_worst_taps`
102 % against `instrument_worst` 97.5 %, `docs/bench/2026-07-26-518f639-system.md:64-65,95-96`),
plus `TapeBpf` and `fast_tanh` per sample per channel — and `fast_tanh` was
~60 % of the FLUX per-sample delta (`docs/roadmap.md:509-511`).

Added: `BbdLine`'s audio-rate filters, event work at ≤0.67 events per sample,
the compander, and the drive/saturation path. Rough estimate ~260 cycles per
line per sample at the ceiling; ×4 lines ≈ 10 % of the block budget. At a
typical 250 ms and 8192 stages, nearer 7 %.

**Net, a small gain is expected — but the worst case may be a wash.** The taps
pay for the BBD and nothing more. Given that a CPU figure in this project was
already off by 4× once, this is written as a gate rather than an assertion:

> `bench/workloads_bbd.cpp` replaces `workloads_taps.cpp` and measures the
> ceiling case — shortest division, STAGES at maximum, high feedback, both
> decks. The design is not accepted until that number is in.

Two levers are pre-authorised if the measurement disagrees, neither of which
changes the design: drop the clock ceiling from 32 to 24 kHz (6 kHz bandwidth,
still above the filter chain), and drop `kMaxStages` from 16384 to 8192.

`Flux::process` also loses its per-sample `_taps.active()` branch — one path,
strictly cheaper.

## Hosts, panel, patches

**DUST and ROT are renamed in place, not replaced.** Param IDs stay; only
labels and tooltips change. The append-only rule at `gen_panel.py:232-234` is
therefore never engaged — nothing is inserted or shifted, and every B-deck and
SHARED id stays put. Old patches still load; their DUST value lands in DRIVE
and their ROT value in STAGES. It sounds different, which full replacement
already conceded.

The hardware constraint is satisfied without negotiation: control count is
unchanged, and a modulation consumer is removed.

**Defaults.** `init.vcvm` and `defaultFor()` need new values. STAGES defaults
to 8192 — 0.8 on the geometric control — so the instrument ships as a Memory
Man and the rest of the scale asks to be explored. DRIVE starts low.

**API and builds.** `set_dust` / `set_rot` → `set_drive` / `set_stages` on
`Instrument` and `PartFx`. `taps.cpp` out, `bbd.cpp` in, across all three build
lists (the engine and bench source lists in `CMakeLists.txt`,
`host/vcv/Makefile`, `bench/Makefile`).

## Tests

The core is tested against physics, not against itself.

- **`tests/test_bbd.cpp`** (replaces `test_taps.cpp`): delay time matches
  `N / (2·f_clk)` within tolerance; the bandwidth corner demonstrably tracks
  the clock; a clock ramp produces the expected pitch ratio; the 32 kHz ceiling
  holds when ladder and lane push against it together.
- **`bbd_clock_hz`** on its own — the one formula.
- **Compander**: the 10 ms time constant must be measurable.
- **`tests/test_flux.cpp`** extended: the bit-exact off path still holds, and
  `FXT_FLUX_TIME` actually moves the clock — the test that could not exist
  before.
- A render scenario beside `host/render/scenarios/reverb_delay.json`, as a
  listening check. Not a byte gate; that is not this project's practice.

## Risks, in the order they must be faced

1. **The partial-fraction decomposition at very low clock rates.** At 5 s the
   line runs at 819 Hz. Whether Holters/Parker's parallel filter branches stay
   stable and meaningful at that ratio is unknown, and the entire design rests
   on it. **This is step one of the plan** — a spike against the ported core
   before anything else is built, not a surprise discovered in the middle.
2. **The CPU gate above.**
3. **The port.** BSL-1.0 notices stay in the source files; SIMD
   (`SSEComplex` and friends) must be rewritten scalar for the Cortex-M7's
   FPv5; `powf` / `cosf` per event replaced by LUT or polynomial — Holters and
   Parker suggest this themselves, and the desktop build profits too.

## References

**Papers**
- Holters & Parker, *A Combined Model for a Bucket Brigade Device and its Input
  and Output Filters*, DAFx-18 — the core model.
- Raffel & Smith, *Practical Modeling of Bucket-Brigade Device Circuits*,
  DAFx-10 — compander, nonlinearity, filter blocks.

**Code**
- jpcima/bbd-delay-experimental — `bbd_line`, `bbd_filter`. BSL-1.0.
  ChowDSP and Surge carry the same model under GPLv3 and are therefore
  reference reading only; this repository is MIT.

**Device**
- Deluxe Memory Man EH-7850 schematic; MN3005 and NE570 datasheets; EHX
  manuals; the EH-7850 factory calibration document (clock periods, LFO rates,
  ±10 % depth).

**In-repo**
- `engine/fx/flux.{h,cpp}`, `engine/fx/taps.{h,cpp}`, `engine/fx/part_fx.{h,cpp}`,
  `engine/instrument.h`, `engine/mod/divisions.h`
- `docs/bench/2026-07-26-518f639-system.md`, `docs/roadmap.md`
- `docs/superpowers/specs/2026-07-17-flux-synced-delay-design.md`,
  `docs/superpowers/specs/2026-07-20-rhythm-fed-delay-taps-design.md`

## Errata (added post-implementation, Task 12)

Facts discovered or corrected while implementing this spec, kept here rather
than silently rewritten into the sections above so the record of what was
*specified* versus what was *found* stays intact.

1. **Memory: `kMaxStages/2`, not `kMaxStages`.** The line stores one sample
   per two stages because the model's even ticks write and its odd ticks
   read — that alternation *is* the two-phase clock. 128 KB across four
   lines, not 256 KB.
2. **Events per sample are per kind.** The tick rate at the ceiling is
   1.33/sample: 0.67 writes and 0.67 reads. The spec's 0.67 is right per
   kind.
3. **Risk 3's SIMD clause does not apply to the reference actually used.**
   `jpcima/bbd-delay-experimental` is plain scalar `std::complex<double>`;
   `SSEComplex` belongs to the ChowDSP/Surge variants, which are GPLv3 and
   were reference reading only. What the port did need: float, injected
   memory, no `std::complex`, and a single init-time filter build in place
   of the mutex-guarded cache. The `powf`/`cosf`-per-event risk was already
   solved upstream by the `G` interpolation table.
4. **The STAGES endpoint was unreachable without a snap.** `Flux::kMaxStages`
   is a documented endpoint, but the float32 one-pole slew stalls before
   arriving at magnitude 16384 — `daisysp::fonepole`'s coefficient is
   ~6.94e-4 while float32's spacing at 2^14 is ~1.95e-3, so the recurrence
   parks at ~16383.30 and `int(x+0.5)` yields 16383. The fix is a
   snap-to-target within one stage in `Flux::process`. The bottom endpoint
   (512) is unaffected only incidentally, because its spacing is ~32× finer.
5. **Two of the plan's own test parameters did not survive measurement**,
   and both were corrected with data rather than by loosening the
   assertion: the `"DRIVE dirties every pass"` case specified DRIVE 0.9,
   where a 0.4 burst already saturates flat on the first pass and leaves
   nothing to compound — the effect is an inverted U peaking near 0.5
   (measured delta 0.0617 at 0.5 vs 0.0017 at 0.9, and negative at 1.0), so
   the test moved to 0.5. The `"each repeat is darker"` case specified a
   1500 Hz burst sitting below both the fixed 3600 Hz chain and the
   clock-derived 4096 Hz corner, leaving a 0.05 % margin; raising it to
   4200 Hz gives 67 %.
6. **The ear pass (2026-07-27, played live in VCV Rack): three of the four
   design claims are heard, one is open.** Confirmed by ear: RATE bends
   stored pitch while repeats ring; STAGES works as a brightness axis;
   `FXT_FLUX_TIME` works as a live modulation lane. Open: DRIVE produces no
   audible difference at any setting. The leading evidence so far is item 5
   above — the measured inverted-U in DRIVE's effect on output delta,
   peaking near DRIVE ≈ 0.5 and collapsing toward the top of the knob —
   together with the makeup gain around the saturator holding small-signal
   loop gain at unity by construction at every DRIVE setting. Not resolved
   here; a separate measurement investigation is open and its outcome is
   the instrument owner's decision.

   **Superseded by item 7.** The "not resolved here" investigation this item
   describes ran, found a real bug in the makeup-gain law itself, and fixed
   it in two commits landed after this item was written. DRIVE's status is
   no longer "open, no audible difference reported" — see item 7 for what
   changed and what is still outstanding.
7. **DRIVE's silence (item 6) was diagnosed, and fixed — but not yet
   re-heard.** That separate measurement investigation ran, and it found the
   makeup-gain law itself at fault, not the owner's ear or the BBD model.
   The law held small-signal loop gain at unity by shrinking the
   saturator's output ceiling (`sat_out_`) by exactly the dB range's own
   width across the knob — at the range then in force (-6..+24 dB) that is
   30 dB, `0.9/0.501 = 1.796` at DRIVE 0 down to `0.057` at DRIVE 1.
   Measured consequence: a 14.0 dB drop in the actual echo return's peak
   level between DRIVE 0 and DRIVE 1 (`drive-investigation.md`) — DRIVE got
   *quieter* as it was turned
   up, not dirtier, so "no audible difference" read more like "getting
   weaker" than "doing nothing."

   Fixed in `ce07532`: `sat_out_` is now a **fixed** ceiling
   (`bbd_tuning::kSatCeil`), matching the real MN3005 (headroom does not
   recede as you drive it harder). Documented, accepted consequence:
   small-signal loop gain now equals `g` itself, so FEEDBACK moves closer to
   self-oscillation as DRIVE rises — the loop still cannot diverge
   (`fast_tanh` clamps hard at ±1). That consequence broke a different
   design constraint at the range then in force: DRIVE 0's self-oscillation
   FEEDBACK threshold rose to 1.714, *above* `Flux::set_feedback`'s 1.2
   maximum — self-oscillation, required to stay reachable at every DRIVE
   setting, became unreachable at DRIVE 0. Fixed in `3dea01a`: DRIVE's range
   moved from -6..+24 dB to `bbd_tuning::kDriveLoDb/kDriveHiDb` = 0..+12 dB,
   giving final measured self-oscillation thresholds of 0.837 / 0.418 / 0.209
   at DRIVE 0 / 0.5 / 1.0 (`drive-fix-report.md`) — all comfortably below the
   1.2 ceiling, with real FEEDBACK travel left at every setting.

   **Superseded in part by item 8.** DRIVE was re-heard and the level fix
   confirmed by ear; the "accepted consequence" above — FEEDBACK moving with
   DRIVE — was then rejected by the same listening pass.
8. **DRIVE's coupling into FEEDBACK, accepted in item 7, was rejected by ear
   and removed.** The owner's report after listening to the fixed DRIVE: it
   works, *and* "das drive schickt das delay aber früh ins blooming
   feedback." Measured (350 ms, 4096 stages, tail length to −60 dB below the
   tail's own peak): the FEEDBACK knob position producing a 15 s tail slid
   from **0.57** at DRIVE 0 to **0.14** at DRIVE 1 — from a quarter DRIVE
   upward, most of FEEDBACK's travel was runaway territory. This is exactly
   what the plan's own design claim 4 had predicted before item 7 retired it
   ("FEEDBACK means something different at every DRIVE setting and the two
   knobs fight"), and the prediction was correct; what item 7 got right was
   only that the *makeup-gain* remedy was the wrong one.

   Fixed by dividing `bbd_drive_gain()` back out of the coefficient in
   `Flux::set_feedback`, not by touching `sat_out_`. That distinction is the
   whole point: the ceiling stays fixed, so everything item 7 bought
   survives. Measured, identical before and after on the first repeat —
   peak 0.512 / 0.923 / 1.279 at DRIVE 0 / 0.5 / 1.0 — while the knob
   position for a 15 s tail goes flat at 0.57 / 0.56 / 0.56. Accepted
   residual: the fed-back signal enters the saturator `1/g` quieter, so
   repeats compound less distortion than the coupled law gave; the first
   repeat, which carries the audible DRIVE cue, is untouched. The division
   lives in `Flux` rather than `BbdEcho` so the model stays a faithful BBD
   whose loop gain honestly equals `feedback × g`.

   **Confirmed by ear, 2026-07-28**, on the branch build: "jetzt ist das
   feedback stabil, drive klingt gut." That closes the listening pass item 7
   left outstanding — DRIVE's level fix and this decoupling were both heard,
   on the same build, and both hold. The accepted residual above (later
   repeats compounding less dirt) was audible-in-principle and did not
   register as a loss.

   **Consequence for item 7's range argument: it no longer binds.** Every
   trade-off that picked 0..+12 dB weighed DRIVE's dirt against FEEDBACK's
   travel. Decoupled, that trade does not exist, and `kDriveHiDb` is free to
   be chosen for distortion character alone.

   **Separately investigated and found NOT to be a defect.** The same
   listening pass reported that once the echo is blooming, turning DRIVE back
   down does not stop it — only moving FEEDBACK does. Measured with a
   path-dependence probe (same final settings, one run driven hot for 3 s
   first, one never): the two histories converge to the same level at every
   FEEDBACK setting, so there is no latched state. Above a FEEDBACK knob of
   roughly **0.56** the loop self-oscillates at *any* DRIVE including 0 —
   DRIVE was never what held it up, so turning it down cannot stop it.
   That is the design's required "self-oscillation stays reachable" behaviour.
   The reason it read as a bug is item 8's own defect: under the coupled law
   DRIVE really could push an otherwise-safe FEEDBACK setting into runaway,
   so DRIVE was the reasonable suspect. With the coupling gone, any FEEDBACK
   below ~0.56 is safe at every DRIVE.
