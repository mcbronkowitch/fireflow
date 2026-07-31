# BBD as a part engine — design

**Date:** 2026-07-31 (rev. 2, after a three-reviewer round)
**Status:** design settled; not implemented; cost claim explicitly unproven
**Supersedes:** `2026-07-30-bbd-part-engine-design.md` in the residency repo.

> **Rev. 2 changed the central mechanism.** Rev. 1 claimed that under tempo sync
> the stage count becomes an absolute pitch axis. It does not: a bucket-brigade
> line writes and reads at the same clock, so **its steady-state pitch is always
> unity at every stage count**, and transposition exists only while the clock is
> moving. Two reviewers found this independently and proposed the same fix, which
> rev. 2 adopts — see §5.2. Everything retracted is in **Appendix A**.

---

## 1. What this is

**A deck can be switched to `ENGINE_BBD`. It then has no synth voices: it
processes audio arriving at its input through a bucket-brigade delay that hangs
on the deck's modulation plane and its rhythm lane.**

**The case is musical, not budgetary** (§2). But the musical premise has now been
tested rather than asserted: a reviewer built a line-for-line numerical model of
`BbdLine`, `Compander` and `BbdEcho` and drove it with a semitone pattern at
FEEDBACK 0.75, T = 250 ms:

```
 repeat 1: 377.2 Hz  (expect 377.98)    repeat 3: 377.6 Hz
 repeat 2: 449.9 Hz  (expect 449.5)     repeat 4: 300.0 Hz  (home)
```

**An in-tune semitone melody out of stored charge, with the repeat locked to the
grid.** That is the instrument, and it is worth building. Two properties of it
are not obvious and are load-bearing everywhere below:

- **It only exists with feedback up.** Without recirculation the wet output is
  the first pass, which is always at unity pitch. `LANE_PITCH` is
  multiplicatively gated by `LANE_MOTION`.
- **Each repeat is mostly silence** — measured, ~75–100 ms of audible content per
  250 ms repeat, shrinking with every upward step. The gappy, duty-cycled
  character is the engine's most distinctive trait, not an artefact to remove.

## 2. The budget position, stated honestly

Measured on a Daisy Seed at `d570e47`, block budget 960 000 cycles:

| row | avg % | max % (**the gate**) |
|---|---:|---:|
| `instrument_worst` | 95.55 | **98.92** |
| `instrument_worst_bbd` | 99.09 | 103.13 |
| `instrument_worst_bbd_dtcm` (retained) | 98.65 | **102.64** |

`instrument_worst` is already inside budget. The overrun is **2.64 points**, and
an `-O3`/LTO round with an explicit stop rule is queued ahead of any of this.

**Three reasons the "this saves CPU" claim is not established:**

1. **The per-line operand was measured at an operating point no deck runs.**
   `sweep_flux_lines_2ch` runs at `Flux::init`'s boot defaults — 8192 Hz clock,
   8192 stages, 0.341 ticks/sample. The gate runs `set_stages(1.f)` and rate
   index 11, clamped to `kClockMaxHz`: **1.333 ticks/sample, 3.9× the tick
   rate.** The repo already wrote this critique — at
   **`bench/workloads_instr.cpp:1096-1119`**, with the derivation at
   `bench/workloads_sweep.cpp:124-137` — and the correction is
   **+1.72 pct_max** (14.80 − 13.08). A line at the deck's operating point costs
   **≈5.9–6.3 points, not 4.55**.
2. **The units were mixed.** Every per-deck figure is `pct_avg`; the gate is
   `pct_max`.
3. **The inference has a measured counterexample in this repo.** "Cheaper than
   SYNTH ⇒ never the worst case" was run for the sampler and came out the other
   way (`docs/bench/2026-07-22-8668367.md`): `inst_sampler_worst` is **4.3
   points cheaper on the mean and 13.1 points more expensive on the peak**.

**A BBD deck's peak spread has never been measured**, and §5.5's freeze is a
state no bench row prices at all.

**Movement 3 saves essentially nothing either.** A stereo tape echo measured
**5.91 points/deck** (`fx_flux_sdram` − `fx_none` at `c3c0cdb`) against the
BBD-in-FLUX's ≈6.18. It is in this design because two different delays are
better than the same one twice.

## 3. Scope — three movements, and how they are coupled

| # | movement | what it delivers | needs |
|---|---|---|---|
| 1 | audio-rate cross-deck bus | a deck can hear its neighbour; the sampler gains cross-deck recording | — |
| 2 | `ENGINE_BBD = 5` | the BBD becomes a playable engine | 1 |
| 3 | FLUX reverts to a tape echo | two different delays in series | — |

**They are not independent.** Movements 2 and 3 both grow `FxMem` and therefore
both change `Part::init`'s signature (§6.4), and both touch the hand-written ITCM
hotset (§8.4). Whichever lands second takes a mechanical conflict.

**Order: 1 → 2 → 3, one branch each.** Each gets its own plan, bench evidence and
definition of done (§4.7, §5.13, §6.10).

**Three hazards specific to the 2 → 3 window, when movement 2 has landed and 3
has not:**

- **A BBD deck runs three BBD lines** — the engine's stereo pair plus FLUX's own
  mono BBD behind it. That is not the configuration §5.11 describes and not what
  §2 prices. `inst_bbd_engine_worst` (§8.3) must be defined so that it does not
  silently keep measuring that transitional shape after movement 3 lands.
- **`STAGES_A/B` and `DRIVE_A/B` each carry two live meanings.** §5.10 gives the
  orphaned STAGES knob to the engine *because movement 3 orphans it* — but until
  then it is still FLUX's stage control on every deck. Movement 2 must state
  which wins on a BBD deck in the window.
- **Movement 1's bench A/B must be captured on movement 1's branch**, because
  movement 2 modifies the same `Part::process` body.

---

## 4. Movement 1 — the cross-deck audio bus

### 4.1 Why the existing path does not serve

`_dry_tap` (BODY's excitation bus, `instrument.cpp`) is written once per block
and read once per block, in mono. As a drive envelope for a resonator that is
correct. As an audio source it is a signal decimated to 96 samples — aliased
noise. The new bus runs at audio rate.

### 4.2 The ordering trap

`Instrument::process` runs the decks sequentially within each sample, but the
order comes from CHOKE:

```c
const int pri = _choke > 0.f ? PART_B : PART_A;
const int yld = 1 - pri;
```

A bus built on "whoever runs first feeds whoever runs second" would have a
latency that **inverts when CHOKE crosses zero** — one direction at 0 samples,
the other at 1, silently swapping under the player's hand. In a mutual routing
it would alternate between a 1-sample loop and a 0-sample loop, and a 0-sample
loop is an algebraic loop with no defined value.

### 4.3 The contract, the storage, and the mechanism

**`float _deck_tap[PART_COUNT][2]`** — per deck, stereo. Four floats, which is
what "four stores per sample" means. (Rev. 1 wrote `_deck_tap[2]`, which reads
as one float per deck, i.e. mono, like the existing `_dry_tap[PART_COUNT]` at
`engine/instrument.h:291`.)

**Fixed one-sample latency in both directions, independent of CHOKE**: read at
the top of the sample, written at the bottom. The tap point is the source deck's
output **after** its FX chain (`pl[]` / `prr[]`), which is what the player hears
and is already available there.

**The value reaches `Part` through a setter, not through `Part::process`'s
signature.** `Part::set_deck_in(float l, float r)` is called once per deck per
sample from `Instrument::process`. The alternative — two more arguments on
`Part::process` — touches roughly ninety call sites across `tests/test_part.cpp`,
`test_choke.cpp`, `test_center.cpp`, `test_mod_tide.cpp` and
`bench/workloads_instr.cpp`, and every one of those edits would have to re-argue
bit-exactness. `part.h:224-242` also warns that `Part::process` is
`always_inline` into ten call sites, so statements added to its body are not
free; the setter keeps the new work out of that body when the source is off.

**Cost:** the §4.7 measurement must price the setter calls as well as the stores.
An ISA hand-count would say ≈0.04 points; round 4 made the same kind of count
about the same loop and was falsified by 2–4× in the unfavourable direction, so
no figure is claimed here.

**An observer is required and does not exist.** §4.7's latency check needs
per-deck output; `Instrument::process` only writes the summed `outL[i]`/`outR[i]`
(`instrument.cpp:191-192`), and a two-deck mix cannot distinguish 0 samples from
1. Add `deck_tap(int p)` alongside the existing `tape_tap` / `excitation_bus` /
`stages_for_test` accessors (`instrument.h:117-139`), which is the established
pattern for exactly this.

### 4.4 Source selection

`Part::process` selects what reaches `process_in()` from the **existing**
per-deck `set_excitation_sources(tape, other_deck, audio_in)`. No new panel
control, which the hardware constraint requires.

**Only the `other_deck` flag is shared.** The excitation bus defaults `audio_in`
to **false**, deliberately, so an unmodified BODY deck behaves exactly as Task 9
left it — but the sampler receives audio-in **unconditionally** today. Gating the
audio-rate path on the same flag would silence sampler recording by default.

**`_audio_in_tap` keeps latching the raw input.** `part.h:313` computes
`_audio_in_tap = 0.5f * (inL + inR)` for BODY's excitation bus. It latches
`inL/inR`, **not** the bounded `el/er` of §4.5. Latching the bounded sum would
count the neighbour twice — once through `el`, once through `_other_deck_tap` at
`part.cpp:383` — and stack a second `fast_tanh` under the one at `part.cpp:385`,
silently moving what `bench/workloads_body.cpp` measures.

**`set_engine` does not write patch state.** §5.12's silence cure is a *default
in the init patch and the host's engine-switch handler*, not a mutation of
`set_excitation_sources` from inside the engine swap. A `set_engine` that flipped
`_src_deck` would rewire that deck's BODY excitation bus as a side effect and put
§4.7's neutrality proof at the mercy of engine selection.

**`_src_deck` gates two independent paths today, and only because nothing
consumes both.** The flag feeds the control-rate excitation bus
(`part.cpp:383`, `bus += _other_deck_tap`) *and* the audio-rate bus of §4.5
(`part.h:349`, `el = fast_tanh(el + _deck_in_l)`). Today that is safe because
`SamplerEngine` ignores `set_excitation` (`engine_iface.h`'s no-op default)
and `SynthEngineT<BodyVoice>` doesn't override `process_in` — exactly one of
the two paths is ever live per engine. **Movement 2 must check this before
`ENGINE_BBD` forwards both.** If a BBD deck both implements `process_in` and
forwards `set_excitation`, the neighbour arrives twice through the same
`_src_deck` flag — once through the audio-rate sum, once through the
control-rate bus — which is exactly the double-count this section's
`_audio_in_tap` paragraph above rules out for `audio_in`. Either the two
paths need separate flags at that point (reopening the "no new panel
control" constraint) or the engine must consume only one of them by
construction. Recorded here, where movement 2 will meet it, rather than left
implicit in a comment on `_src_deck`'s declaration.

**A persisted host flag silently changed meaning on this branch, and the
change is deliberate.** The VCV host's "Excite: other deck" checkbox
(`host/vcv/src/Spotymod.cpp`, per-part context menu; JSON key
`exciteOtherDeck`) predates this branch and was previously inert on a
SAMPLER deck — `SamplerEngine::set_excitation` does not exist, so the flag
only ever did anything for BODY. After this branch the same flag also drives
the audio-rate bus above, because it is the one flag `set_excitation_sources`
exposes. Two consequences follow, both correct applications of "bound the
sum" under this movement's no-new-control constraint, and both worth
knowing before touching a saved patch or the host's menu:

- A patch saved with the flag on for a SAMPLER deck behaves differently
  after upgrading to this branch, silently — it now audibly routes and
  records the neighbouring deck, where before it did nothing.
- With the flag on, the engine input becomes `fast_tanh(inL + _deck_in_l)`
  even when the neighbour is silent (`_deck_in_l == 0`) — so toggling this
  one checkbox also puts the sampler's **external audio-in** path through
  `fast_tanh` for the first time. `tanh(0.5) ≈ 0.462`, `tanh(1) ≈ 0.762` —
  the same ≈0.76 figure §4.5's `SamplerEngine` note cites as the reason the
  monitor itself must stay linear. Toggling "Excite: other deck" measurably
  attenuates external-input monitoring and recording, as a side effect of a
  menu item whose name never mentions audio-in.

Neither is a code defect; both are what "one shared flag, no new control"
necessarily buys. The host's menu label and the surrounding documentation
say so now (`host/vcv/src/Spotymod.cpp`'s "Excite: other deck" item,
`host/vcv/README.md`'s Body/Sampler section) — the behaviour itself is
unchanged from what this movement shipped.

### 4.5 Mutual routing is allowed; one bound replaces the exclusivity rule

Both decks may select `other_deck`, exactly as today. Idempotence is untouched
and `bench/workloads_body.cpp` keeps measuring what it measures.

**Sampler ↔ sampler is the one unbounded case.** Both engines monitor their input
through and neither bounds it, so without a fix the recursion grows without
limit. **An earlier draft of this section said the circulation reaches
`inf`/`NaN` rather than merely getting loud. The cross-deck-audio-bus branch
built the case this section warns about and measured it, rather than asserting
it: with a constant exogenous input the per-channel recurrence is
`x[n] = 0.5 + x[n-2]`, which is unbounded *linear* growth, not `inf`/`NaN` —
it does not reach either within a 10 s run** (`tests/test_deck_bus.cpp`, the
sampler↔sampler mutual-routing test's closing comment, which derives all
three orderings — correct, swapped, and absent — side by side). **The
correction changes only the predicted symptom, not the conclusion**: unbounded
linear growth still poisons the record buffers exactly as thoroughly as a
`NaN` would, just more slowly and without ever producing a non-finite sample
for a naive `isfinite()` check to catch — which is itself an argument for
bounding the sum rather than for leaving it be. The master limiter cannot
prevent it either way: `Limiter` is an `Instrument` member applied to the
summed output after MORPH and reverb, **outside** the loop, and there is no
per-deck limiter (per-deck COMP has a bit-exact bypass, `comp.h:17`).

**Fix: bound the cross-deck sum in `Part`, not in the sampler.**
`SamplerEngine`'s monitor is `if (_monitor) { l += _in_l; r += _in_r; }`,
deliberately *"dry input at unity"* (`sampler_engine.cpp:954-955`); a `fast_tanh`
there would move the monitor level for every existing user (tanh(1) ≈ 0.76) and
break the neutrality proof. The bound belongs where the new path is created:

```c
if (_src_deck) { el = fast_tanh(el + _deck_in_l); er = fast_tanh(er + _deck_in_r); }
```

With the source off — the default, and today's behaviour — the path is
**bit-exact unchanged**.

**Note on the BBD's own bound.** `BbdEcho::Process` (`bbd.h:568-577`) is
`comp_.Expand(line_.Process(comp_.Compress(sat)))` with
`sat = fast_tanh(x * sat_in_) * sat_out_`. So `kSatCeil` = 0.9 bounds the
*saturator*, and `Compander::Expand` can multiply by up to 4× afterwards
(`bbd.h:467-474`) — see §5.9, where the engine's real output bound is dealt with.

### 4.6 The sampler

Changes nothing. `SamplerEngine` already implements `process_in` and
`consumes_input`. From the moment the bus exists it records the neighbouring
deck, without the external patch cable that is the only way to do it today.

### 4.7 Definition of done — movement 1

- `_deck_tap[PART_COUNT][2]` and `Instrument::deck_tap(p)` exist; latency is one
  sample and is verified **equal in both directions** across a CHOKE sweep that
  crosses zero.
- With `other_deck` off on both decks, `ctrl_identity` and `wave_formant_sweep`
  are unchanged. **Those are the only two render-hash gates that exist**
  (`CMakeLists.txt:183-203`) — there is none for SAMPLER or BODY, and
  `tests/check_render_hash.cmake:23-24` hashes the WAV only and deletes the CSV
  unread. Bit-exactness for the other engines therefore needs its own assertion,
  not a claim that "the hashes cover it".
- Sampler ↔ sampler mutual routing at full monitor produces finite output over
  10 s — no `inf`, no `NaN`.
- A paired same-source A/B prices the bus, reported as `pct_max`. The A arm needs
  the bus code absent; add a compile-time switch for it, since none exists.

---

## 5. Movement 2 — `ENGINE_BBD = 5`

`BbdEcho` moves out of `Flux` into an `IPartEngine` implementation. The class is
self-contained — injected memory, a clean `Process(in, clock_hz)` signature, no
knowledge of music. What has to be written is the musical layer `Flux` provided.

**Home:** `engine/parts/bbd_engine.{h,cpp}`, class `BbdEngine`, alongside the
other engines. It owns two `BbdEcho`, the clock derivation, the freeze state and
the feedback-path filter; `Part` owns the lane values and pushes them, exactly as
for every other engine.

`EngineId` gains `ENGINE_BBD = 5`, appended — ids go in milestone order and are
never renumbered, because patches persist them. The stability pin already exists
at `tests/test_part.cpp:279-287` and needs one more `CHECK` (it is a runtime
`CHECK`, not a `static_assert`).

`consumes_input()` returns `true`, overridden **together with** `process_in()` as
`engine_iface.h` requires. `Part::_engine_for` (`part.h:415-423`) needs a
`case ENGINE_BBD` — its `default:` silently routes to `_tone`.

### 5.1 The physics, restated so the rest of this section is checkable

From `bbd.h`:

```
    ticks/sample = 2 * f_clk / fs
    cells        = stages / 2
    delay        = stages / (2 * f_clk)
```

Constants: `kClockMaxHz` = 32 000, `kMinStages` = 512, `kMaxStages` = 16 384,
`kFilterHz` = 3600, `kSatCeil` = 0.9, `kLossCoef` = √3 − 1.

**Even ticks WRITE, odd ticks READ, through the same index `imem_`.** Content is
therefore written at `f_clk` and read at `f_clk`: **at a constant clock the line
transposes nothing.** Pitch moves only while the clock is changing, because
charge written at `f₁` is read out at `f₂`. With feedback, the shifted copy is
re-recorded at the new clock, so a circulating grain tracks
`f_now / f_at_entry` — which is how §1's melody is possible.

`SetStages` changes `cells_` and nothing else; the buffer is deliberately not
cleared, "so a stage change drifts in time and pitch instead of clicking."
`SetClock(hz)` with `hz ≤ 0` or non-finite means hold: no ticks, and no output.

### 5.2 The clock is the lane; the stage count is derived

**Decided: `LANE_PITCH` sets `f_clk` directly, and the stage count follows from
it and the grid.**

```
    T      = cycle_seconds * div                              // §5.4
    f_clk  = geometric(LANE_PITCH), clamped to kClockMaxHz    // THE PITCH
    stages = clamp(round(2 * T * f_clk), kMinStages, kMaxStages)
```

`delay = stages / (2·f_clk) = T` still holds, so the repeats stay on the grid.
The difference from rev. 1 is which quantity the lane owns:

- **`LANE_PITCH` moves `f_clk`** → the circulating charge bends, the delay time
  does not move. This is the gesture `docs/roadmap.md:1157-1160` records as
  **confirmed by ear** — *"RATE bending stored pitch"*.
- **`LANE_SIZE` moves `T`**, and `stages` moves with it in exact proportion, so
  `f_clk` is untouched → **the rhythm moves and nothing transposes.**

**The engine syncs to tempo through `set_cycle`**, which `Part` already pushes to
every engine. A free-running clock was rejected: the engine would be the only
thing in the instrument running against the groove.

**Brightness is a consequence, not a control.** The loss pole tracks the clock at
`f_-3dB ≈ f_clk/4`, so raising the pitch brightens and lowering it darkens. That
is the roadmap's *"STAGES is a brightness axis"* — **confirmed by ear** and
**not** retired: it now describes a derived quantity rather than a lane, and the
axis is the same one.

**`bbd_clock_hz` already clamps to `kClockMaxHz`** (`bbd.h:201-206`), and its own
comment states the risk — *"BbdLine must never see a clock it cannot count ticks
from."* The second clamp at `flux.cpp:368-369` exists only because `_time_mult`
multiplies afterwards. The engine needs its own clamp for the same reason if it
applies any post-multiplier, not because the primary clamp is going away.

**Sample-rate independence, worth stating because it is a real advantage:**
`delay = stages/(2·f_clk)` does not involve `fs`. The engine's reachable delay
range is identical at 44.1 kHz and 192 kHz — unlike the tape line (§6.3), whose
buffer is in samples.

### 5.3 Lane mapping

| lane | function class | → BBD |
|---|---|---|
| `LANE_SOURCE` | POSITION / **TIMBRE** | **DRIVE** — the dirt inside the loop |
| `LANE_PITCH` | **PITCH** (master lane) | **`f_clk`** — bends the circulating charge |
| `LANE_SIZE` | **SIZE** | **`div`** — the delay length against the grid |
| `LANE_MOTION` | SHAPE / **MOTION** | **FEEDBACK** — proximity to bloom |
| `LANE_LEVEL` | **LEVEL** | **MIX** — dry against delayed (§5.8) |

**`LANE_PITCH` and `LANE_MOTION` are not orthogonal.** At FEEDBACK 0 the wet
output is the first pass only, which is always at unity pitch, so the PITCH lane
is inaudible. MOTION is the switch that turns PITCH on. This is a property of the
instrument, and it should be stated in the manual rather than engineered away.

**`LANE_PITCH`'s reachable span depends on the division.** `stages` must land in
[512, 16384] and `f_clk ≤ kClockMaxHz`, so `f_clk ∈ [256/T, min(32000, 8192/T)]`
and the span is `min(125·T, 32)`:

| delay time `T` | `f_clk` range | span |
|---:|---|---|
| ≥ 256 ms | 1000…32000 Hz at 256 ms | full 32× — **five octaves** |
| 125 ms | 2048…32000 Hz | 15.6× — ~3.97 octaves |
| 100 ms | 2560…32000 Hz | 12.5× — ~3.64 octaves |
| 50 ms | 5120…32000 Hz | 6.25× — ~2.64 octaves |

At 120 BPM a quarter note is 500 ms, so the full range is available **at 1/4 and
longer** — 1/8 (250 ms) already falls just short.

**Decided: the lane is scaled to what is reachable**, i.e. `LANE_PITCH` 0..1 maps
onto `[256/T, min(32000, 8192/T)]` rather than onto a fixed frequency range with
a dead zone at the top. The lane always spans its full travel; the interval size
changes with the division and the tempo. The alternative — a fixed range that
clamps — puts a silently-moving dead zone in the top of the master lane whose
size depends on a control the player is also moving, which is worse. The cost is
that the pitch interval per lane step is not constant across divisions; on an
instrument where this lane is a bend rather than a keyboard, that is acceptable.

**A consequence to know:** `kMinStages` cannot widen this. The truncation is at
the `kClockMaxHz` **ceiling**; lowering `kMinStages` extends downward, and at
e.g. 214 stages / 107 ms the clock is 1000 Hz — Nyquist 500 Hz, loss corner
250 Hz, and a modelled sweep there is non-monotone because tones fold around the
clock. That may be a good destruction zone; it is not a pitch range.

### 5.4 `div` — the division ladder

`set_cycle` is fed `1.f / _mod.master_hz()` (`part.cpp:423-426`) — the **whole
phrase**, not a beat — and `master_hz` excludes `clock_scale()` and the EVOLVE
walk. So `div` must reach down to a step boundary, which in STEP is `cycle/steps`
(`lane.h:71,102`).

**Decided: eleven rungs, and `LANE_SIZE` snaps to them with hysteresis.**

```
1/32  1/24  1/16  1/12  1/8  1/6  1/4  1/3  1/2  2/3  1
```

Snapping rather than a continuous law, because `T` sets the repeat rhythm and a
continuously-drifting repeat time is not a musical quantity here. Hysteresis is
required, not optional: `LANE_SIZE` is a continuously modulated lane, and a bare
nearest-rung round chatters at every boundary. **One rung of overlap** — a rung
holds until the lane passes the next rung's centre.

`div = 1` is a whole phrase; at 40 BPM (the TEMPO floor) with a long phrase this
can exceed the reachable stage count at any clock, in which case `T` is clamped
to the longest `T` for which `stages ≤ kMaxStages` at the lane's lowest `f_clk`.
That clamp must be reported by the observer (§9), not silent.

### 5.5 The grid, the gate, and the fires

**The deck's own mode switch chooses the pitch grid.**

```c
_pitch_q = (_engine_id == ENGINE_SAMPLER ||
            (_engine_id == ENGINE_BBD && !_step_on)) ? pitch_raw : pitch_quantized;
```

- **STEP → quantized.** The clock lands on scale steps, so the bend is in the key.
- **FLOW → free.** Continuous bends, which is the gesture FLOW exists for.

**`Quantizer::SPAN_SEMIS` is 36, not 60** (`quantizer.h:66`) — the normalized
value spans **three octaves**, and `process()` returns `note / 36`. The engine's
`f_clk` map spans up to five octaves (§5.3), so one quantizer step is **1.667
semitones of clock ratio**, not one semitone. **Decided: the engine re-derives
semitones against its own span**, i.e. it converts the quantized normalized value
back to semitones (`×36`) and applies them as `2^(semis/12)` on the clock,
clamped to the reachable range. Otherwise STEP quantizes to a grid that is not a
scale. At divisions where the reachable span is under three octaves the top of
the scale is unreachable, which the observer must report.

**A step fire latches the clock** and holds it until the next one — the pattern
`SynthEngine` already uses for pitch.

**The latch is gated on `set_flow`.** `engine/mod/lane.cpp:447-452` reads
`bool gated = _step_mode ? _effective_gate(slot) : true;` — *"FLOW has no
per-step gate so it always fires."* A FLOW deck fires once per master-lane cycle;
un-gated, the latch would freeze the clock at the top of every cycle and the
continuous bend FLOW was chosen for would not happen. **In FLOW the engine
ignores fires and follows the plane continuously.**

`trigger_chord` keeps its default fan-out. COLOR takes its content from the
stereo pair instead (§5.7).

### 5.6 Freeze

**Decided: a circulating freeze** — input muted, loop gain held so the content
keeps going round and stays audible. `SetClock(0)` would be free and is already
implemented, but it stops the READ ticks too, so it is a pause in the signal
rather than a freeze of it.

**Rev. 1's scalar-`k` scheme does not work, and its acceptance test could not
fail.** Three measured facts:

- **The compander's round trip is `L²`, not `L`.** With constant inner gain `L`,
  compressor → line → expander measures `L²` above about −40 dBFS, and `L` below
  it — so **quiet content has loop gain `1/L > 1` and grows**. A freeze of near
  silence blooms out of nothing.
- **`L` is a lowpass, so one scalar cannot hold a spectrum.** At 8192 stages /
  T = 250 ms, with `k` tuned for 1 kHz, ten circulations leave 110 Hz at
  **+7.2 dB** and 2.5 kHz at **−48.8 dB**. A "±0.5 dB at 1 kHz over 10
  circulations" test measures the one frequency it tuned and is a tautology.
- **DC is at unity line gain** — the Butterworth sections are normalised
  `H(0) = 1` and the loss pole is unity at DC — so any `k` above unity at 1 kHz
  puts DC strictly above unity, and it grows monotonically until it parks the
  saturator.

**Specified instead:**

1. **A DC blocker in the feedback path.** `daisysp::DcBlock` is in the tree and
   `part.cpp:385` already uses this exact idiom.
2. **The feedback path carries a one-pole high shelf** whose corner tracks
   `f_clk/4`, inverting `kLossCoef`'s tilt. This is the same filter RESONANCE
   plays (§5.8) — the freeze is RESONANCE at its neutral point, not a separate
   mechanism.
3. **The loop gain compensates DRIVE analytically:** `k = k₀ / bbd_drive_gain(d)`.
   `bbd_drive_gain` spans 1.0…3.98 (0…+12 dB) and the small-signal loop gain
   *is* `feedback × g`, so with `LANE_SOURCE` running the plane would otherwise
   swing the loop gain ±12 dB **per circulation**. This term is exactly known;
   leaving it to the ear was wrong.
4. **`k₀` is measured with broadband material** — a noise burst or a chord — and
   the acceptance criterion is **per-octave level within ±1 dB over 10
   circulations**, at `f_clk` mid-range and DRIVE 0. `k` is a tuning constant;
   DECAY (§5.8) trims *below* it.

**The engine also keeps `Flux`'s feedback law on the normal path:**
`fb = norm × 1.2 / bbd_drive_gain(drive)` (`flux.cpp:199`). Without the division
the bloom point slides from 0.57 to 0.14 across DRIVE, and since `LANE_SOURCE`
*is* DRIVE the plane would drive the loop through self-oscillation via a lane
that is not the feedback lane.

**What drives the freeze:**

- **`set_gate()` in STEP** — gate high freezes.
- **FLOW ignores the gate.** A FLOW deck's gate is effectively always on;
  without this rule a FLOW BBD would be permanently frozen. **Consequence: the
  freeze is unreachable in FLOW, so ATTACK and DECAY are inert there.** That is a
  mode-dependent dead knob and it is accepted, but it must be in the manual.
- **`set_hold()` (CHOKE) closes the input** and lets the tail run out.

### 5.7 The engine is stereo, and COLOR is its width

**Two `BbdEcho` lines per BBD deck, one per channel.** A part engine has no dry
path — it *is* the signal path — and its input is stereo at every boundary:
`process_in(float inL, float inR)`, `process(float& outL, float& outR)`, and
§4.3's bus. (The roadmap's standing instruction against restoring stereo FLUX was
withdrawn by the owner on 2026-07-30, commit `886119b`; mono came out of
necessity.)

**Cost.** The mono collapse measured the second line directly:
`instrument_worst_bbd` 125.24 → 112.88 = **12.36 points for two lines ≈ 6.18 per
line** — corroborating §2's corrected 5.9–6.3. A stereo BBD deck is ≈11.8–12.6
against a SYNTH deck's 17.60–18.21, all `pct_avg`; §8.3 measures `pct_max`.

**A mono source through a stereo engine is mono, and that is correct** — with
both lines fed the same signal at the same clock they are bit-identical. With a
genuinely stereo source they differ from the first sample.

**Decided: COLOR is a symmetric, geometric clock spread with the delay time held
on the grid for both lines.**

```
f_L = f_clk · r        stages_L = round(2·T·f_clk·r)
f_R = f_clk / r        stages_R = round(2·T·f_clk/r)
```

Both delays remain `T`. What differs is the **stage count**, hence the bandwidth
and grain (`f_clk/4`) — a stereo image made of two differently-bright copies of
the same rhythm, plus the comb offset from the sub-sample stage rounding.

**This deliberately gives up the two-tone behaviour, and the owner chose that
trade.** Two tones would require the two lines to have different *delay times*,
which splits them rhythmically and cumulatively: at T = 500 ms and 50 cents the
lines are 29 ms apart on the first repeat and **232 ms apart by the eighth**, and
the character changes completely with the division. Width that stays on the grid
at every tempo was judged the better control. Consequence to state plainly:
**at self-oscillation both lines sing at `1/T`, i.e. in unison.**

**The endpoint is left for the ear**, and unlike rev. 1 there is a floor to
reason from: the stage-count ratio is `r²`, so a few cents of `r` already give an
audible brightness split without any rhythmic cost.

**Rejected, recorded so they are not re-proposed:** feedback cross-coupling
(ping-pong — the most recognisable stereo-delay behaviour, but furthest from
COLOR's "how much sounds at once"), and stage-count offset at a common clock
(which is the two-tone case above, re-derived).

### 5.8 The VOICE row

`Part` forwards six VOICE setters (`part.h:138-148`): ATTACK, DECAY, RESONANCE,
SUB, DETUNE, FILT. **Only five are panel knobs** — `DETUNE_A/B` is in
`HIDDEN_PARAMS`, exposed only as a context-menu slider.

| knob | → BBD engine |
|---|---|
| ATTACK | freeze ramp — engage/release time |
| DECAY | freeze decay — trims the loop **below** `k₀`, from a few seconds to infinite |
| RESONANCE | **feedback-path tilt** — how bright the repeats stay |
| SUB | **input level** — how much neighbour / audio-in arrives |
| DETUNE *(menu)* | **slew time** — how far the engine bends when modulated |
| FILT | **the loss-pole corner** — the darkness |

**MIX is on `LANE_LEVEL`, not on a knob.** The old FLUX was send-style
(`l += wet`); an engine has no such signal — it *is* the path, and the dry input
arrived through `process_in()` where `outL/outR` cannot see it.
`Instrument::process` composes `l = al·ga + bl·gb`, so the audio input reaches
the output nowhere. On a wet/dry engine **the mix is the level**, so LEVEL's
function class survives in substance, and the plane can open and close the echo
rhythmically — which is the strongest musical idea available here. Rev. 1 put MIX
on RESONANCE and admitted the assignment was arbitrary; putting the one
indispensable control on the one arbitrary silkscreen was the wrong allocation.

**FILT moves the loss pole, not `kFilterHz`.** Rev. 1 assigned it the Butterworth
corner. That is not implementable: `kFilterHz` is `constexpr` and baked into
`butterworth_poles()` (`bbd.cpp:53`); the coefficients live in **two file-scope
singletons** `g_fin`/`g_fout` that every `BbdLine` holds raw pointers into, so
one deck's knob would retune the whole instrument; a rebuild is 396
`exp`/`cos`/`sin` calls; and `bbd.h:270-281` documents in-place rebuild as a
shared-mutable hazard safe only with the audio callback stopped. The loss pole is
a per-line scalar with no rebuild cost — and it is the pole that actually carries
the darkness: at 16384 stages the fixed chain contributes only **−0.93 dB at
2.5 kHz**.

**RESONANCE plays the feedback-path tilt.** Neutral, it inverts `kLossCoef` so
repeats keep their brightness; left, they darken faster than physics; right, they
brighten and approach the freeze condition. It is the same filter §5.6 needs, so
it costs one biquad that is already being added, and "how the tail colours" is an
honest meaning for a resonance control on a delay.

**ATTACK and DECAY keep their function class** — "the shape of the event in
time", as on every other engine. DECAY trimming *below* `k₀` resolves rev. 1's
contradiction between "`k` is a constant, not a parameter" and "DECAY is a
feedback trim": `k₀` is the unity reference, DECAY scales down from it, and
§5.6's acceptance test applies at DECAY maximum.

**DETUNE → slew time** stays a menu parameter. `flux.h` records the slew as a
deliberate musical value; since `LANE_PITCH` and `LANE_SIZE` are both driven by
the plane, this decides *how* the engine answers modulation. **§7 discusses
whether it should take DRAG's freed panel slot.**

Everything else stays fixed and must **not** be promoted to a knob: the compander
(*"NOT a parameter… the device's character"*), `kFiltOrder` (refused on CPU
grounds), `kSatCeil`, `kLossCoef`'s derivation.

**Note on the guards:** `flux.h:145` records that `set_stages` (a `powf`) and
`set_drive` (a `pow10f`) are protected by unchanged-value guards. Putting DRIVE
on a lane defeats its guard permanently. It is a handful of transcendentals per
block, so it is negligible — but the guards should stop being cited as if they
still fire.

**Implementation trap.** `Part::set_voice_*` forwards to each engine by name, one
hand-written line per knob. A new engine not added to all six lines has silently
dead knobs — the same failure class as the `process_in`/`consumes_input` pairing.
**All six forwards must be extended together.**

### 5.9 Three things the model does not have, and needs

None of these was in rev. 1, and each is cheap.

**A noise floor.** The compander exists to manage *"the BBD's 75 dB noise
floor"* (`bbd.h:432`) — **which is not modelled**. Zero in, zero state, zero out,
forever, at any FEEDBACK. So a player who switches a deck to BBD with nothing
connected and turns FEEDBACK up gets bit-exact silence: the worst possible first
contact with the engine, and §5.12's audio-in default does not fix it when VCV
passes `nullptr`. **Inject a few LSBs of dither at the write tick.** It is one
PRNG call, it makes the engine sing from nothing — the single most compelling
demo of the design — and it is *more* faithful to the part being modelled, not
less.

**A denormal floor.** `BbdEcho` has none; it never needed one behind a
`SoftSwitch`. As an always-on engine it is exactly the "long engaged silence"
case that `comp.cpp:56` and `limiter.h:37` already guard with `< 1e-9f → 0.f`.
`Xin_[]`, `Xout_mem_[]`, `loss_z_`, `ybbd_old_` and `fb_state_` all decay
geometrically, and the buffer fills with denormals — a large, load-dependent
stall on x86. Apply the same floor the repo already uses. The dither above
largely solves this for free.

**A stated output bound.** With the expander's 4× ceiling the engine can return
roughly +8 to +11 dBFS in the self-oscillating regime, and it is the only engine
whose bound is unstated and non-unity. There is no per-deck limiter, and the
reverb send taps before the master `Limiter`. **Normalise the engine's output**
(or `fast_tanh` it, matching §4.5's idiom) and state the bound.

### 5.10 The rest of the panel

**The modulation-plane controls are all functional** — RATE, SHAPE, DENSITY,
SMOOTH, RANGE, MELODY, MOD, STEPS, and the STEP / FORM / NEWPHRASE / SONG
buttons.

**TUNE sets the base clock**, with `LANE_PITCH` modulating around it.
`pitch_pre_quant()` runs TUNE through the same quantizer, so it follows §5.5's
grid decision.

**SOURCE needs no change** — already a lane base, and its caption already changes
with the engine. On a BBD deck it is the DRIVE base.

**STAGES_A/B is orphaned by movement 3** and becomes the `LANE_PITCH` base on a
BBD deck. Re-pointing a knob per engine at host level is not new — the sampler
already moves `SUB_A` to `set_target_base(LANE_SIZE)` as GENE SIZE. See §3 for
the 2 → 3 window, where the knob has two meanings.

`DRIVE_A/B` is menu-only, because *"DRIVE lost its panel slot to DRAG"*. §7 gives
that slot back.

GRIT, GRITMODE, COMP, REV_MIX, FLUX, FLUXRATE, FLUXFB and LINK keep their meaning
behind a tape echo. `REC_A/B` is inert on a BBD deck, but it is equally inert on
a SYNTH deck today.

### 5.11 The FX chain behind the engine

`Part::process` calls `_fx.process()` after `_engine->process()` for every engine
— the chain belongs to the deck. A BBD deck therefore has GRIT → FLUX (tape) →
FX MIX → COMP behind it, plus the reverb send tap.

**Decided: FLUX defaults to disengaged on a deck switched to `ENGINE_BBD`.** The
BBD's output is already six poles at 3600 Hz plus a loss pole at `f_clk/4`
breathing under a compander, and §1's gappy repeats are its most distinctive
trait — which a tape echo behind it will fill in. Dark-and-compressed into
darker-and-smeared is the default outcome. The player can add it.

- **No runaway path.** Series, not a loop, with no route back. Both units are
  bounded.
- **No collision between the target rows.** The deck carries two — five engine
  lanes (`LaneId`) and five FX targets (`FxTargetId`). The engine takes the
  lanes, FLUX the FX targets. **This does not extend to the panel knobs**, which
  do collide (§5.10, §7).
- **Switching the tape off is not free.** `PartFx::process` still runs the five
  `FXT` smoothers, `_comp.process()` and the reverb send tap unconditionally. In
  the build §2 anchors on, that residue is `fx_none` = **26 619 cycles = 2.77
  points per deck** (`docs/bench/2026-07-30-d570e47-system-itcm-hot.md:60`). The
  2.55 figure quoted by rev. 1 is from `a1b1b7a`, a different build.
- **The comparison does not count the chain twice**, because it runs identically
  on a SYNTH deck. It does **not** follow that coupling is zero — §8.2.

### 5.12 Integration with code that assumes notes

**CHOKE stage 1 is not a BBD bug, but rev. 1's reasoning about it was wrong.**
`Instrument::process` opens the inhibit window with
`_parts[pri].gate() || _parts[pri].flow()`, and **every** engine in FLOW as the
priority side already holds it open permanently — `instrument.cpp` documents it
as intended (*"FLOW: a drone is always 'on'"*). Rev. 1 added that `set_inhibit`
"suppresses triggers and gate pulses, not audio". **That is false:**
`Part::set_inhibit` forwards `_engine->set_hold(on)` (`part.h:121-125`), which
`engine_iface.h:35-38` documents as *"a FLOW engine releases its sustaining
drone"* — and §5.6 of this very document makes `set_hold` the thing that closes
the BBD's input. CHOKE does reach audio. What remains open is a design question:
"the priority side is holding a note" is vacuous for a voiceless engine, and
changing it would affect every engine.

**CHOKE stage 2 degrades to stage 1 on a BBD deck, and that stands.**
`Part::max_voice_env()` loops over `SynthEngine::kVoices` and returns 0, so the
decay window never opens. Arguably correct — a frozen delay tail is infinite, so
a BBD deck would otherwise choke its neighbour forever.

**The silence trap** has two halves and both need fixing: no source selected
(→ the init default for a BBD deck is audio in **and** the neighbouring deck),
and no signal present (→ §5.9's dither, which is what makes an unconnected deck
audible at all).

**Clearing on engine switch needs machinery that does not exist.**
`BbdLine::Reset()` is a **private** member of `BbdEcho` (`bbd.h:580`), and
`BbdEcho` exposes no `Reset()`. `IPartEngine` has no swap-away notification —
`Part::_engine_swap` (`part.cpp:402-419`) only pushes state *into* the engine
being swapped in. So this needs a new `BbdEcho::Reset()` (which must also reset
`comp_` and `fb_state_`, not only the line) **and** an init-on-activate
convention. Both are in scope for movement 2.

**The VCV and render surface:**

- `ENGINE_A/B` is `configSwitch(c.id, 0.f, 3.f, …, {"Synth","Sampler","Wave","Body"})`
  — range, labels and the `kEngineShades[]` table all extend.
- The dispatch ternary's own comment says *"anything that isn't 0/2/3 still falls
  through to Sampler"* — **a bare enum append routes BBD to the sampler.**
- `sourceCaption()` covers states 1/2/3 only (`Spotymod.cpp:1066-1068`).
- `host/render/scenario.cpp:85-91`'s `parse_engine` needs a new spelling —
  without it no render scenario can select the engine, and half of §5.13 is
  unmeasurable.
- `bench/audition/init_patch.cpp:59-65` is **already mis-routing**: three arms
  (`0→SYNTH`, `2→WAVE`, else→SAMPLER) against `initParamDefault(ENGINE_B) == 3`,
  so deck B boots as SAMPLER there while `Spotymod.cpp:442` gives it BODY. Fix
  before any BBD audition row means anything.
- Defaults go in `kInitParamDefaults[]` and `configControls`. **`defaultFor()` is
  deleted and `test_panel.py:1289-1290` asserts it stays deleted.** The snapshot
  source is `drone.vcvm`.

**`test_panel.py` is red with 53 failures today**, and the breakdown decides how
to fix it: **3** are ENG wiring (it pins a *three*-engine `configSwitch` and a
dispatch with no BODY arm), **3** are SOURCE-caption drift, and **47** are
init-snapshot value mismatches. **Decided: the code is authoritative and the
pins are rewritten**, because the 47 are BODY-era drift that shipped. Note the
consequence: `bench/audition/init_patch.cpp:16` and
`tests/test_seed_audition_init.cpp:34,36` read the same table, so this re-bases
the audition bench's boot state. Doing it is **movement 2's first task**, on its
own commit, before any engine code.

**Boot state.** All five lane bases boot at 0.5 with every lane active
(`part.h:584-586`), so a deck freshly switched to `ENGINE_BBD` starts at DRIVE
0.5 and FEEDBACK 0.5, modulated. The engine's own init must set a boot state that
neither blooms nor sits silent, and §5.13 tests it.

### 5.13 Definition of done — movement 2

- A deck set to `ENGINE_BBD` passes audio from audio-in and from the neighbour,
  with MIX (`LANE_LEVEL`) at 0 and 1 both correct.
- **With FEEDBACK up**, moving `LANE_PITCH` with content circulating transposes
  the tail, and the repeat interval measured from the render CSV does not move.
- Moving `LANE_SIZE` with content circulating moves the repeat interval, and the
  pitch of the circulating tail does not move. *(Both bullets are measured
  **across** the change with material in flight, not after it settles — the slew
  time is a user parameter, so the settling convention has to be stated or the
  two tests contradict each other.)*
- With FEEDBACK at 0, `LANE_PITCH` produces no pitch change — the documented
  gating, asserted rather than discovered.
- `LANE_PITCH` spans its full travel at every division; no dead zone at the top.
- The freeze holds a **broadband** burst within ±1 dB **per octave** over 10
  circulations at DECAY maximum, with DRIVE swept 0 → 1 during the hold.
- A frozen loop shows no DC growth over 60 s.
- With no input connected and FEEDBACK high, the engine self-oscillates from the
  dither floor rather than outputting silence.
- After 60 s of silence at the input, no denormal stall is measurable on x86.
- The engine's output stays within its stated bound with both decks blooming.
- In FLOW, a lane cycle boundary does not latch the clock.
- COLOR 0 with a mono source is bit-identical between L and R; COLOR opened is
  not, and both channels' repeat intervals stay equal.
- Switching away from and back to `ENGINE_BBD` produces silence, not old charge.
- `test_panel.py` is green.
- The other four engines are bit-identical (see §4.7 on what the hash gates
  actually cover).
- `inst_bbd_engine_worst` exists as a bench row (§8.3).

---

## 6. Movement 3 — FLUX reverts to a tape echo

### 6.1 What actually has to be rebuilt

`21087f2` **deleted** `DeLine`, `TapeBpf` and `EchoDelay` — about **173 lines**
across `flux.h` (154) and `flux.cpp` (19). Rev. 1 said "~230" and misspelled
`TapeBpf`.

**Two corrections to rev. 1's framing, both load-bearing:**

- **`fast_tanh` did *not* change.** `git diff e004a3d^ HEAD -- engine/util/fast_tanh.h`
  is comment-only; the last code change (`deb796f`, 2026-07-19) is an *ancestor*
  of `e004a3d`. There is nothing to pin, and rev. 1's warning sends a round of
  work against a non-problem.
- **The real obstacle is the tap bank.** `git show e004a3d^:engine/fx/flux.h`
  includes `"fx/taps.h"`, holds `TapBank _taps`, declares `set_dust`/`set_rot`/
  `set_tap_offsets`/`taps_active`, and `Flux::process` has a live taps branch.
  §10 forbids restoring it, so the reference must be **surgically stripped**, not
  restored.
- **"Nothing in the tree implements an interpolating delay line" is false.**
  `daisysp::DelayLine` (`lib/DaisySP/Source/Utility/delayline.h:49-92`) is one,
  and BODY's Karplus-Strong string already uses it (`engine/body/ks_string.h:9`).
  What is missing is an interpolating line **over injected memory**, which is
  what `DeLine` was — and that is the honest statement of the work.

**Confirmed non-conflict:** `BbdEcho` lives entirely in `engine/fx/bbd.{h,cpp}`
with no `Flux` dependency, so movement 3 removes a *user*, not the class.

### 6.2 The tape echo is stereo

As it was: `EchoDelay<kMaxSamples> _echo_l, _echo_r`. The owner withdrew the
"FLUX stays mono" instruction on 2026-07-30 — mono came out of necessity, in step
(3) of a CPU programme (*"Erst den Mantel, messen, dann auf mono"*), and the
listening pass accepted a fait accompli. The listening was also done on a
*bucket-brigade* in that slot, which movement 3 replaces. The observation behind
it — *"what remains is carried by the dry path's existing per-voice pan"* —
survives untouched and may produce the same verdict again; it is the verdict
being re-taken, not the argument.

Not reopened: no panning or widening layer, no DRIVE or compander re-tuning.

**`Flux::init`'s signature changes shape, not just count** — today
`init(float, float* buf)`, the reference `init(float, float* buf_l, float* buf_r)`
— so `PartFx::init(float, float* echo)` (`part_fx.cpp:8`) changes with it.

### 6.3 Memory

`21087f2` records it: **`kMaxSamples` dropped from 262 144 to `kMaxStages/2` =
8192.** The tape buffer was 32× the BBD's.

**Specified: the tape line keeps 262 144 floats per channel** — 5.46 s at 48 kHz,
2.73 s at 96 kHz, 1.37 s at 192 kHz. The buffer is in samples, so the reachable
delay shrinks at higher rates, exactly as before. `kTapeSamples` is a new name;
**`Flux::kMaxSamples` becomes it**, and `bench/workloads_bbd.cpp:32,66` — which
size a raw `BbdEcho`/`BbdLine` from `Flux::kMaxSamples` — must be re-pointed at
`kMaxStages/2` or they will silently claim 1 MB of arena for an 8192-cell line.

**`FxMem` gains two buffer families**, because the lengths differ 32×:

```c
struct FxMem {
    float* echo[PART_COUNT][2] = {};   // tape, kTapeSamples each
    float* bbd [PART_COUNT][2] = {};   // BBD engine lines, kMaxStages/2 each
    AmbientReverb* reverb = nullptr;
    SampleBuffer::Frame* sampler_buf[PART_COUNT] = {};
    size_t sampler_frames = 0;
};
```

**The `bbd` family and the null contract belong to movement 2, not here.**
Rev. 1 put both in this section, but movement 2 lands first and needs them:
`Instrument::init(float)` hands an empty `FxMem` (`instrument.cpp:17`) and most
tests use that overload, so `set_engine(p, ENGINE_BBD)` there must not fault.
**Specified: `nullptr` → that line runs silent**, matching `SamplerEngine`'s
documented behaviour rather than `Flux`'s `_buf_ok` guard, because the engine has
no bypass to fall back to.

| what | per line | per deck | instrument |
|---|---:|---:|---:|
| tape echo (stereo) | 1 MB | 2 MB | **4 MB** |
| BBD engine (stereo) | 32 KB | 64 KB | **128 KB** |

On a Daisy with 64 MB of SDRAM that is affordable. **On VCV the tape buffer must
move to the heap** — `Spotymod.cpp:174` holds it by value in the `Module`, so it
would be ~4 MB per instance; the pattern to copy is `samplerMem` at `:182` with
`reinit()` at `:346-351`.

**Three further places the 32× lands** that rev. 1 missed:
`bench/audition/memory.cpp:27-29` has a hard `static_assert(kSdramBytes < 64 MiB)`
including `sizeof(g_echo)`; **eight static arrays in `tests/`** grow to ~20 MB of
BSS in `spky_tests` (`test_instrument.cpp:65,278,279,409,471,511,512,551`, six in
`test_part_fx.cpp`, two in `test_flux.cpp`); and `host/render/main.cpp:13`,
`bench/mem.cpp:37`, `bench/audition/memory.cpp:16-17` are statics.

### 6.4 `Part::init` and its callers

**The signature grows in both movements 2 and 3** — that is §3's coupling.

**Its trailing parameters have defaults** (`part.h:25-27`), and this is the
difference between a ten-site edit and a ninety-site one: the ~85 two-argument
test calls survive a signature growth if the new parameters also default. **Only
the calls that pass `echo` positionally break.**

Actual direct `Part::init` callers, verified: `engine/instrument.cpp:22,25`;
`bench/workloads_instr.cpp:687,691,1465`; `bench/workloads_abl.cpp:74`;
`bench/workloads_mod.cpp:96,97`; `bench/workloads_system.cpp:99,100`; plus the
bare-`Part` tests. Rev. 1 listed `bench/workloads_body.cpp` (it calls
`KsString::init` and `Instrument::init`, not `Part::init`) and `bench/mem.cpp` /
`bench/audition/memory.cpp` (they *populate* `FxMem`), and missed the three
`workloads_*` files above.

**Direct `FxMem::echo` indexers also break when it becomes 2-D:**
`bench/workloads_system.cpp:212`, `bench/workloads_sweep.cpp:252,357,538,571,572`,
`bench/workloads_instr.cpp:1077`.

**Five source lists are hand-synced** (`CMakeLists.txt:144-147` says so), and one
is already broken: `bench/audition/Makefile:35-36` lists `flux.cpp` and `bbd.cpp`
but **not `drag.cpp`**, while `flux.cpp:310` calls `derive_intervals`, defined
only at `drag.cpp:6`. Every other build lists it. It also has to gain movement
2's new engine `.cpp`.

### 6.5 LINK: THIN takes the whole knob, DRAG is dropped

**THIN comes across and gets the whole knob** — a pure gate on the sibling's
rhythm, engine-agnostic and mechanism-free. `LINK_A/B` becomes unipolar THIN over
its full travel.

**DRAG is dropped.** It was designed on the BBD's *clock* (*"the clock moves, so
the stored charge bends in pitch"*); on a tape echo the same gesture is a
read-pointer sweep, i.e. a re-implementation. It has nowhere to live if it
follows the BBD instead, and it was not judged worth the room.

**Removing it is not a clean excision — DRAG and THIN share machinery.**
`flux.cpp:334-336` runs one accumulator for both (*"One accumulator, two
consumers. They are mutually exclusive by construction"*), and `_drag_step_len`
is armed **inside `apply_drag()`** (`flux.cpp:280`:
`_drag_step_len = thinning ? _delay_time * _sr : 0.f;`). Deleting `_drag_phase`,
`_drag_step_len` and `apply_drag()` therefore **breaks THIN**. Extract and rename
the shared timebase first, in its own commit, with THIN's tests green before
anything is deleted.

What goes: `_drag`, `_drag_iv`, `_drag_i`, `_drag_active`, one per-sample branch,
and the uniformity guard (which exists only because *"an even pattern is a
failure for DRAG and a RESULT here"*).

**`engine/fx/drag.h` stays; `drag.cpp` does not.** Rev. 1 kept the pair "because
it houses `link_tuning`" — but `link_tuning` is entirely header-side `constexpr`
(`drag.h:31-45`), and `drag.cpp` contains only `derive_intervals`, which is the
DRAG-only path THIN deliberately bypasses (`flux.cpp:304-307` reads `rv.gap[]`
raw). After DRAG goes, `drag.cpp` has no live caller. Renaming the header to
`link.h` is optional and cosmetic.

**Consequence to accept:** a BBD-engine deck has no rhythmic coupling to its
neighbour of its own. The sibling reaches it through CHOKE, the cross-deck bus,
and THIN on the tape echo behind it.

### 6.6 Patch migration

`LINK` is `configParam<LinkQuantity>(c.id, -1.f, 1.f, …)` (`Spotymod.cpp:248-249`)
with THIN on the **negative** half. Making it unipolar clamps every saved THIN
value to 0 and reinterprets every saved DRAG value as THIN.

**Rev. 1 pointed at the wrong mechanism.** `form_song_migration.hpp` is 36 lines
about a version integer, gated on `>= 1`, and `dataFromJson`
(`Spotymod.cpp:724-747`) migrates FORM/SONG only; Rack restores params itself.
This needs **new `dataFromJson` code**: read the stored LINK value, map `v < 0 →
-v` and `v > 0 → 0`.

Three things to decide with it: whether to bump `formSongVersion` or add a second
key; **what happens to a patch with no version key at all** (it runs the legacy
FORM/SONG migration *and* needs the LINK remap, and the two have no ordering);
and `LinkQuantity::getDisplayValueString` (`Spotymod.cpp:81-88`) plus the
`init_patch.hpp` LINK note, several of which `test_panel.py` pins.

### 6.7 Disposing of the BBD-shaped surface on `Flux`

Rev. 1 listed three leftovers and missed the ones that matter. Today's `Flux`
carries, and the `e004a3d^` reference does **not**:

`set_drive`, `set_stages`, `set_time_mod`, `set_link`, `set_rhythm`, `stages()`,
`clock_hz()`, `drive_norm_for_test()`, `feedback_coef_for_test()`,
`drag_time_s()`, `gate_for_test()`, `thin_n_for_test()`.

These are not dead. `set_drive`/`set_stages` are wired to `Instrument`
(`instrument.h:101-102`), `PartFx` (`part_fx.h:48-49`) and the VCV
`STAGES_A`/`DRIVE_A` params, and are **pinned by four tests**
(`tests/test_instrument.cpp:129-144,146-164`, `tests/test_part_fx.cpp:211`,
`tests/test_flux.cpp:315`). Each needs an explicit disposition in the plan.

**`FXT_FLUX_TIME` has no meaning on a tape echo, and all three options are bad.**
`PartFx::process` pushes `_flux.set_time_mod(v[FXT_FLUX_TIME])` **unguarded,
every sample** (`part_fx.cpp:47`), and the target only became live *because* FLUX
is a BBD. Deleting it breaks `FxTargetId`'s five-slot contract and the
pad-slot-equals-lane-index principle; making it a read-pointer sweep is exactly
the mechanism §6.5 refuses to build for DRAG; making it a no-op creates a dead
modulation target. **This must be decided before the plan is written.**

Also: `Instrument::drag_time_for_test`, `tests/test_drag.cpp`, and
`bench/anchor.cpp`'s anchoring of `instrument_worst_bbd` / `_dtcm` — see §6.9.

### 6.8 Cost expectations

An interpolating delay line's cost does not vary with delay time; the BBD's does.
A reverted tape FLUX is therefore expected to have a flat cost curve.

`instrument_worst_taps` is **not** a forecast for it — `5d53901` says that row
measured DUST/ROT, deleted in `e004a3d`.

### 6.9 The `_bbd` rows do not simply retire

Rev. 1 said they could. **They cannot, for two reasons.** After movement 2 the
BBD is still in the instrument — in the *engine* slot — and its cost still varies
with `ticks/sample`. And `bench/run.py:499-529` uses
`instrument_worst_bbd` / `instrument_worst_bbd_dtcm` as the **DTCM A/B
checksum-equality pair**, with `run.py:663` naming `_dtcm` as *the gate row* and
`bench/anchor.cpp:11-21` listing both. Retiring them deletes the only A/B pair
the harness has. **Movement 3 must re-point them at the engine configuration, not
remove them.**

### 6.10 Definition of done — movement 3

- An interpolating stereo tape echo over injected memory exists behind FLUX's
  public form (`SoftSwitch`, `engaged()`, the bit-exact off path, `set_rate`/
  `set_mix`/`set_feedback`/`set_bpm`, the shared delay-time slew), with every
  symbol in §6.7 either kept with a stated meaning or removed with its tests.
- `FXT_FLUX_TIME` has a decided meaning and a test for it.
- With FLUX disengaged, every render hash is unchanged.
- THIN works over the full LINK travel, with its tests green **before** any DRAG
  symbol is deleted.
- A patch saved before the change loads with its THIN setting intact, including
  one with no version key.
- The tape buffer is heap-allocated on VCV; `spky_tests` BSS stays sane.
- `bench/audition/Makefile` builds.
- The DTCM A/B pair still exists, re-pointed.
- `instrument_worst` is re-measured in the same build, reported as `pct_max`.

---

## 7. The panel after all three movements

| control | before | after |
|---|---|---|
| STAGES_A/B | FLUX stage count | `LANE_PITCH` base on a BBD deck |
| DRIVE_A/B | menu-only (lost its slot to DRAG) | **open — see below** |
| LINK_A/B | bipolar DRAG ↔ THIN | unipolar THIN, full travel |
| COLOR_A/B | chord size / voice count | clock spread between the stereo lines |
| RESONANCE_A/B | filter resonance | feedback-path tilt (tail brightness) |
| ENGINE_A/B | 4 states | 5 states |

**DRAG's freed panel slot is open.** The strongest candidate is DETUNE (§5.8) — a
real performance control currently reachable only through a context menu, whose
promotion would also settle the "six setters, five widgets" awkwardness in the
VOICE row. Against it: the hardware constraint says controls should leave rather
than arrive, and leaving the slot empty is a legitimate outcome.

---

## 8. Budget — what must be measured

**This section makes no claim that the design saves CPU.**

### 8.1 Figures not traceable to a measured row

| figure | § | status |
|---|---|---|
| the bus's cost | §4.3 | no figure claimed; §4.7 measures it |
| the tape echo's in-context cost | §6.8 | unmeasured, and cannot be measured before it exists |
| `k₀` | §5.6 | **defined as a measurement** |
| COLOR's endpoint | §5.7 | left for the ear |

### 8.2 Two structural cautions

- **Component rows do not sum.** The roadmap names this with a figure:
  *"Component rows summed to ~120 % of budget while `instrument_worst` measured
  ~159 % — a ~375k-cycle (39-point) gap with no named owner."*
- **Composition and layout move the gate by points, at an unchanged checksum.**
  The repo records `instrument_worst_bbd` reading 110.78 in one build and 112.79
  in another with the same checksum, and `fx_grit` moving 4.78 → 7.70 across
  builds.

**Rev. 1's version of the second caution was wrong and is withdrawn.** It quoted
*"cache pressure from the BBD buffers merely being resident"* as the live
hypothesis for `sweep_grit_no_bbd_mem` vs `sweep_grit_bare`. That phrase is one
of three candidates listed at `bench/workloads_sweep.cpp:96-100`, and the repo
**ruled it out**: the rows came back 1.53 / 4.92
(`docs/bench/2026-07-29-cd6dafd-sweep.md:75-76`), and
`docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md:505-540` records the
cause as a per-sample `std::pow` behind a stale `_buf_ok` gate plus code-layout
drift. Coupling is still not zero — but it must not be argued from a hypothesis
this repo tested and rejected.

### 8.3 Rows that must exist

1. **`inst_bbd_engine_worst`, as a real worst case** — `LANE_PITCH` at the clock
   ceiling, the shortest division, **freeze engaged**, COLOR at maximum, both
   decks. One row at a comfortable operating point would reproduce §2's error
   under a new name. Define it so it survives movement 3 (§3).
2. **A sampler-engine instrument row in the gate set.** `inst_sampler_worst` at
   106.82 is the standing counterexample and is outside the gate:
   `setup_inst_worst` never calls `set_engine`. WAVE, BODY and the sampler have
   never been in the worst case.
3. **The stereo tape echo's flat cost in context.**
4. **The bus, measured rather than waived** (§4.7).
5. **A second BBD line, same-build.**

All report `pct_max`.

**Where a row is registered:** `bench/run.py:167-303`
(`BENCH_PROTOCOL_ROWS_BY_FAMILY`) **and** the C++ workload array for its family
(e.g. `kInstrWorkloads[]` at `bench/workloads_instr.cpp:1696-1706`). Rev. 1 named
`bench/profiles.py` and `bench/families.cpp`, which both register **families,
never rows** — `profiles.py:4-5` says so in its own docstring — and omitted the
C++ array, which is the one that matters. `bench/anchor.cpp` is separate again:
it pins anchored rows, not the registry.

**Verify new rows against `bench.map`, not the memory table.** The bench build
can silently relink a stale object.

### 8.4 ITCM

The 6-point saving behind the 102.64 gate is a **code-residency** result.
`bench/itcm_hot.lds:16-25` holds a hand-written ten-object hotset —
`instrument.o, part.o, part_fx.o, flux.o, bbd.o, grit.o, reverb.o, comp.o,
synth_engine.o, voice.o` — **41 984 bytes** of code (`docs/roadmap.md:145`),
ending at address 42 240 because the region reserves 256 bytes, with **23 296
free** under a hard `ASSERT` at 64 KiB. Rev. 1 quoted the end address as the code
size.

Movement 2 adds a sixth engine whose kernel must be resident; movement 3 replaces
`flux.o`'s text.

**The failure is loud, and the guard is in `bench/itcm_placement.py:13-24`, not
`bench/test_itcm_link.py`** (which only builds and asserts `hot_size > 0`). Its
`HOT_SYMBOL_FRAGMENTS` requires **both** `spky::Flux::process(` **and**
`spky::BbdLine::Process(` to link inside ITCM — movement 2 relocates the latter's
only user to a new TU, and movement 3 removes it from `flux.o`. Rev. 1 named one
of the two. **Both §5.13 and §6.10 need a "the hotset still fits and both symbols
still resolve" bullet.**

---

## 9. Open questions

- **The cost claim (§2, §8.3)** — unproven; needs peak measurement.
- **`FXT_FLUX_TIME`'s meaning on a tape echo (§6.7)** — must be decided before
  movement 3's plan.
- **DRAG's freed panel slot (§7)** — DETUNE is the candidate; empty is
  legitimate.
- **COLOR's endpoint (§5.7)** — the ear.
- **`k₀` across operating points (§5.6)** — one value and a compensation law are
  specified; whether the residual STAGES-dependence needs its own term is a
  listening question.
- **CHOKE stage 1 for voiceless engines (§5.12)** — changing it touches every
  engine.
- **No engine-contract analogue.** WAVE and BODY both entered through
  `tests/synth_engine_contract.h`. A voiceless non-`SynthEngineT` engine cannot
  satisfy it, and §5.13's list is the raw material for a replacement — which
  still needs writing, with its own test file and `CMakeLists.txt` registration.
- **The observer, which does not exist yet.** A BBD deck would write 0 into
  `a_voices`/`a_v0..3` and expose nothing, so a demo scenario would pass
  vacuously. **At minimum: `f_clk`, the derived stage count, the active `div`
  rung, the freeze state, and both clamp flags** (§5.3's reachable-range clamp
  and §5.4's long-division clamp) — a clamp that is invisible reads as a broken
  knob. Note that rev. 1 cited FLUX's `stages_for_test`/`drive_norm_for_test` as
  precedent for CSV output: `Flux`'s observer is `stages()` (`flux.h:72`),
  `stages_for_test` is an `Instrument` method used only by a test, and **neither
  appears in the CSV header** (`host/render/main.cpp:80-85`). There is no
  precedent; this is new work.
- **Does a BBD deck want a tape echo after it?** §5.11 now defaults it off.
  Whether dark-and-compressed into longer-and-cleaner is worth playing is a
  listening question.

---

## 10. Explicitly not proposed

- **A forced trade** (BBD on → fewer voices, or GRIT locked to Drive).
- **BBD exclusivity** (only one deck may run it).
- **A second, cheap delay alongside the BBD in the FX chain.**
- **A new FEED control in the centre section.** §4.4's existing source selection
  carries it.
- **DRAG, in any layer** (§6.5). Lineage: DUST/ROT → LINK(DRAG|THIN) → THIN.
- **Restoring the DUST/ROT tap bank.** Its function returned as LINK; its knobs
  were renamed and re-pointed; and `instrument_worst_taps` measured it at
  **4.19 points** over `instrument_worst` in **run 1** and **5.16** in **run 2**
  (`docs/bench/2026-07-27-c3c0cdb-body.md:125-126,164-165` — rev. 1 had the runs
  inverted). Either figure is the whole margin this design might win.
- **The multiple-read-point COLOR** (Appendix B).
- **Raising `kClockMaxHz` or lowering `kMinStages`.** `kClockMaxHz` carries a
  physical argument — the clock must not overtake the fixed 3.6 kHz chain.
  `kMinStages` does not: its comment is a modelling one, and it is a floor. But
  §5.3 shows lowering it cannot help the truncation, which is at the ceiling.

---

## Appendix A — superseded claims

Everything retracted, in one place. Rev. 2 items — found after this document
settled, by the `feat/cross-deck-audio-bus` branch that implements movement 1
— are marked ②; rev. 1 items ①; rev. 0 items ⓪.

| claim | why it is wrong |
|---|---|
| ② **"Sampler ↔ sampler circulation reaches `inf`/`NaN` rather than merely getting loud"** (§4.5) | Measured, not merely reasoned about: with a constant exogenous input the recurrence is `x[n] = 0.5 + x[n-2]` — unbounded *linear* growth, not `inf`/`NaN`, within a 10 s run (`tests/test_deck_bus.cpp`). The bound is still required; only the predicted failure mode was wrong. |
| ① **"Under sync, STAGES becomes an absolute five-octave pitch axis"** | A BBD writes and reads at the same clock: **steady-state pitch is unity at every stage count.** Transposition is a transient lasting one delay period, and it is gated on feedback. §5.2 inverts the formula. |
| ① "`LANE_SIZE` moves the rhythm, `LANE_PITCH` moves the pitch, and neither disturbs the other" | Under rev. 1's formula both moved the clock, so `LANE_SIZE` was an equally strong pitch control. True only after §5.2's inversion. |
| ① "The roadmap's *STAGES is a brightness axis* is retired" | It is **confirmed by ear** (`roadmap.md:1044,1157-1160`) and correct. Rev. 1 overrode a listened result with a false inference. |
| ① "In STEP the charge transposes in semitones" | `Quantizer::SPAN_SEMIS` is 36, not 60 — one step was 1.667 semitones (§5.5). |
| ① "FILT moves `kFilterHz`" | `constexpr`, baked, and held in two file-scope singletons every line points into; a rebuild is 396 transcendentals and a documented race. FILT moves the loss pole (§5.8). |
| ① "The freeze is a scalar `k`, tested at ±0.5 dB at 1 kHz over 10 circulations" | The compander's round trip is `L²`; the test is a tautology; DC is at unity and grows. §5.6. |
| ① "DECAY is a freeze feedback trim" *and* "`k` is a constant, not a parameter" | Contradictory as written. §5.8 makes `k₀` the reference and DECAY a trim below it. |
| ① "COLOR opens into two distinguishable pitches" | Only with different delay *times*, which splits the lines rhythmically (232 ms by repeat 8 at 500 ms / 50 ct). §5.7 chooses width on the grid instead. |
| ① "`set_inhibit` suppresses triggers and gate pulses, not audio" | It forwards `set_hold`, which releases a FLOW drone — as §5.6 itself relies on. |
| ① "`fast_tanh` changed underneath the tape echo" | Comment-only since before `e004a3d`. The real obstacle is the tap bank in the reference (§6.1). |
| ① "Nothing in the tree implements an interpolating delay line" | `daisysp::DelayLine`, already used by BODY. The gap is a line over *injected memory*. |
| ① "~230 lines deleted", "`TapBpf`" | ~173 lines; `TapeBpf`. |
| ① "Cache pressure from the BBD buffers is the live hypothesis for a 3-point move" | One of three candidates, and the one the repo **ruled out** (§8.2). |
| ① "Rows are registered in `run.py`, `profiles.py`, `families.cpp`, `anchor.cpp`" | The latter two register families, never rows; the C++ workload array was omitted (§8.3). |
| ① "`test_itcm_link.py` asserts `BbdLine::Process` in ITCM" | That is `itcm_placement.py`, and it requires `Flux::process` too (§8.4). |
| ① "42 240 bytes of hotset" | 41 984 of code; 42 240 is the end address past a 256-byte reserved gap. |
| ① "FLUX's `stages_for_test`/`drive_norm_for_test` print in the render CSV" | Neither appears in the CSV header; `Flux`'s observer is `stages()`. There is no observer precedent (§9). |
| ① "`instrument_worst_taps`: 4.19 is run 2" | 4.19 is run 1, 5.16 is run 2. |
| ① "Full pitch range at and below 1/4" | At 1/4 and **longer**; 1/8 already falls short. |
| ① "`fx_none` = 2.55 points" | 2.55 is `a1b1b7a`; in `d570e47`, which §2 anchors on, it is **2.77**. |
| ① "`drag.{h,cpp}` stays because it houses `link_tuning`" | `link_tuning` is header-side `constexpr`; `drag.cpp` holds only the DRAG-only `derive_intervals` (§6.5). |
| ① "`Part::init`: every caller changes" | Trailing parameters have defaults, so only positional `echo` passers break — ten sites, not ninety (§6.4). Rev. 1's caller list also named three non-callers and missed three callers. |
| ① "`bbd_clock_hz`'s clamp is going away with `flux.cpp:368`" | The primary clamp is in `bbd_clock_hz` and movement 3 does not touch it. |
| ① "The `_bbd` bench rows can retire" | They are the harness's only DTCM A/B pair and its gate row (§6.9). |
| ① "The engine's output is bounded at `kSatCeil`" | That bounds the saturator; the expander adds up to 4× after it (§5.9). |
| ⓪ "The BBD costs 8–9 points, the size of the overrun" | Baseline two merged rounds stale. The overrun is 2.64. |
| ⓪ "Two independent measurements agree" | A gross component cost and a net saving are not the same quantity. |
| ⓪ "A BBD deck is cheaper than SYNTH, so no BBD configuration is the worst case" | Measured counterexample (§2, the sampler). |
| ⓪ "One `BbdEcho` line costs 4.55 points" | Measured at boot defaults, 3.9× below the gate's tick rate. ≈5.9–6.3. |
| ⓪ "Movement 3 is the move that clears the gate" | It saves essentially nothing (§2). |
| ⓪ "`FxMem` gains a field" | Two buffer families differing 32× (§6.3). |
| ⓪ "DRAG's removal is a clean excision" | It shares an accumulator with THIN (§6.5). |
| ⓪ "`form_song_migration.hpp` handles the LINK migration" | 36 lines about a version integer (§6.6). |
| ⓪ "DRIVE / DETUNE are panel knobs" | Both are menu-only. |
| ⓪ "The sampler leaves SUB and DETUNE dead" | SUB is re-pointed; DETUNE is widgetless. There is no dead-knob precedent. |
| ⓪ "`defaultFor()` / `init.vcvm`" | Deleted with a guard; the snapshot is `drone.vcvm`. |
| ⓪ "The three movements are independent" | 2 and 3 both grow `FxMem` and both touch the ITCM hotset (§3). |

## Appendix B — the read-point mechanism, recorded but not proposed

`BbdLine` has no read pointer — *"all charge packets are clocked forward
together, and the delay is a consequence of the clock, not of an index."* So
"taps" is the wrong word and a tap-delay reading of COLOR does not fit the model.

A different reading does. Reading the same line at cells N, N/2, N/3, N/4 gives
delays in the ratio 1 : ½ : ⅓ : ¼ — the first four partials, since delay time is
pitch on a BBD. Below ~8 ms they are simultaneous pitches, not echoes.

Not proposed, for three reasons: each extra read point needs its own `ybbd_old_`
and `Xout_mem_[3]` chain (the reconstruction filter is driven by the step between
consecutive readings), estimated at ≈40 % of a line — **read, not measured**, and
the one measured bracket is a warning (the deleted tap bank priced at ~1.05
points per tap while a BBD read point does strictly more work); the meaning
changes across the time control, from chord tones to rhythmic repeats; and COLOR
now has content (§5.7) that costs nothing.
