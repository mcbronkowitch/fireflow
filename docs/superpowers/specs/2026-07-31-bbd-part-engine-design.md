# BBD as a part engine — design

**Date:** 2026-07-31
**Status:** design settled; not implemented; cost claim explicitly unproven
**Supersedes:** `2026-07-30-bbd-part-engine-design.md` (written in the residency
repo while this one was locked). That document was patched through two review
rounds and ended internally inconsistent in about a dozen places. This is a
rewrite from the settled decisions, not a patch. Everything it said that turned
out to be wrong is in **Appendix A**, once, instead of scattered through the
text.

---

## 1. What this is

**A deck can be switched to `ENGINE_BBD`. It then has no synth voices: it
processes audio arriving at its input through a bucket-brigade delay that hangs
on the deck's modulation plane and its rhythm lane.**

Three things have to happen for that, and they are listed in §3.

**The case is musical, not budgetary.** An earlier draft argued the move pays
for itself in CPU. That argument does not survive — see §2. Nothing here should
be built on the assumption that it saves cycles until §8.3's rows exist.

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
   rate.** The repo already wrote this critique of the sister row
   (`bench/workloads_instr.cpp:252-254`) and prices the correction at
   **+1.72 pct_max**. A line at the deck's operating point costs **≈5.9–6.3
   points, not 4.55**.
2. **The units were mixed.** Every per-deck figure is `pct_avg`; the gate is
   `pct_max`.
3. **The inference has a measured counterexample in this repo.** "Cheaper than
   SYNTH ⇒ never the worst case" was run for the sampler and came out the other
   way (`docs/bench/2026-07-22-8668367.md`): `inst_sampler_worst` is **4.3
   points cheaper on the mean and 13.1 points more expensive on the peak**. That
   report's own verdict: *"The mean is the cheaper of the two; the peak is
   not."*

**A BBD deck's peak spread has never been measured**, and §5.4's freeze is a
state no bench row prices at all.

**Movement 3 saves essentially nothing either.** A stereo tape echo measured
**5.91 points/deck** (`fx_flux_sdram` − `fx_none` at `c3c0cdb`) against the
BBD-in-FLUX's ≈6.18 at the gate's operating point. It is in this design because
two different delays are better than the same one twice, not because it clears
the gate.

## 3. Scope — three movements, and how they are coupled

| # | movement | what it delivers | needs |
|---|---|---|---|
| 1 | audio-rate cross-deck bus | a deck can hear its neighbour; the sampler gains cross-deck recording | — |
| 2 | `ENGINE_BBD = 5` | the BBD becomes a playable engine | 1 |
| 3 | FLUX reverts to a tape echo | two different delays in series | — |

**They are not independent, and the earlier draft's claim that they are was
wrong.** Movements 2 and 3 both grow `FxMem` and therefore both change
`Part::init`'s signature and every one of its callers (§7.1). They also both
touch the hand-written ITCM hotset (§8.4). Whichever lands second takes a
mechanical conflict.

**Therefore: 1 → 2 → 3, in that order, one branch each.** Movement 3 is last
because it is the only one that can be deferred indefinitely without stranding
anything, and because by then `FxMem`'s new shape already exists.

Each movement gets its own plan, its own bench evidence, and its own definition
of done (§4.6, §6.9, §7.6). This stays one spec because the three share §2's
arithmetic and splitting it would triplicate that section.

---

## 4. Movement 1 — the cross-deck audio bus

### 4.1 Why the existing path does not serve

`_dry_tap` (BODY's excitation bus, `instrument.cpp`) is written once per block
and read once per block, in mono. As a drive envelope for a resonator that is
correct. As an audio source it is a signal decimated to 96 samples — aliased
noise, not audio. The new bus runs at audio rate.

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

### 4.3 The contract

`_deck_tap[2]`, stereo, **a fixed one-sample latency in both directions,
independent of CHOKE**: read at the top of the sample, written at the bottom.

The tap point is the source deck's output **after** its FX chain (`pl[]` /
`prr[]` in `instrument.cpp`) — what the player hears, and already available at
exactly that point. A pre-FX tap would need new plumbing inside `Part` for a
sound that is rarely wanted.

Cost: four stores per sample. An ISA hand-count says ≈400 cycles/block ≈ 0.04
points. **That count is not evidence** — round 4 made the same kind of count
about the same loop and was falsified by 2–4× in the unfavourable direction. It
is measured in §4.6, not waived.

### 4.4 Source selection

`Part::process` selects what reaches `process_in()` from the **existing**
per-deck `set_excitation_sources(tape, other_deck, audio_in)`. That control
today drives BODY's control-rate excitation bus; it now drives the audio-rate
engine input as well. No new panel control, which the hardware constraint
requires.

**Only the `other_deck` flag is shared.** The excitation bus defaults
`audio_in` to **false**, deliberately, so an unmodified BODY deck behaves
exactly as Task 9 left it (`part.h`) — but the sampler receives audio-in
**unconditionally** today. Gating the audio-rate path on the same flag would
silence sampler recording by default. Each path keeps its own audio-in
behaviour unchanged; `other_deck` is the single quantity both consumers read.

### 4.5 Mutual routing is allowed; one bound replaces the exclusivity rule

Both decks may select `other_deck`, exactly as they can today. Idempotence is
untouched and `bench/workloads_body.cpp` keeps measuring what it measures.

**Sampler ↔ sampler is the one unbounded case.** Both engines monitor their
input through, neither bounds it, and the circulation reaches `inf`/`NaN`
rather than merely getting loud — which poisons the buffers. The master limiter
cannot prevent it: `Limiter` is an `Instrument` member applied to the summed
output after MORPH and reverb, **outside** the loop, and there is no per-deck
limiter (per-deck COMP has a bit-exact bypass).

**Fix: bound the cross-deck sum in `Part`, not in the sampler.**
`SamplerEngine`'s monitor is `if (_monitor) { l += _in_l; r += _in_r; }`,
deliberately *"dry input at unity"*; a `fast_tanh` there would move the monitor
level for every existing user (tanh(1) ≈ 0.76) and break the neutrality proof.
The bound belongs where the new path is created:

```c
if (_src_deck) { el = fast_tanh(el + _deck_in_l); er = fast_tanh(er + _deck_in_r); }
```

With the source off — the default, and today's behaviour — the path is
**bit-exact unchanged**. With it on, the engine's input is bounded at ±1 for
*every* engine, so no loop of any shape reaches `inf`/`NaN`; it saturates into a
thick distorted tone, which is what this instrument does everywhere else. Sum
first, bound the sum: the same idiom as `fast_tanh(_bus_dc.Process(bus))`.

**Note on the BBD's own bound.** `bbd.h` states that `Process()` is bounded by
construction at `sat_out_ = kSatCeil` = 0.9. That is the bound *inside* the
saturator; `BbdEcho::Process` is `comp_.Expand(line_.Process(comp_.Compress(x)))`
and the expander runs after it, so **0.9 is not the output bound**. An earlier
draft used it as one. The `fast_tanh` above does not depend on this either way.

### 4.6 Definition of done — movement 1

- `_deck_tap[2]` exists, stereo, one-sample latency, verified equal in both
  directions across a CHOKE sweep that crosses zero.
- With `other_deck` off on both decks, every render-hash ctest is unchanged
  (`ctrl_identity`, `wave_formant_sweep`) and the four existing engines are
  bit-identical. This is the neutrality proof, and it matters more here than for
  WAVE or BODY: those added engines, this changes `Part::process`, which every
  engine runs.
- Sampler ↔ sampler mutual routing at full monitor produces a finite, bounded
  output over 10 s — no `inf`, no `NaN`.
- A paired same-source A/B bench row prices the bus (§4.3's 0.04 is replaced by
  a measurement, reported as `pct_max`).

---

## 5. Movement 2 — `ENGINE_BBD = 5`

`BbdEcho` moves out of `Flux` into an `IPartEngine` implementation. The class is
already self-contained: injected memory, a clean `Process(in, clock_hz)`
signature, and no knowledge of music. What has to be written is the musical
layer that `Flux` used to provide.

`EngineId` gains `ENGINE_BBD = 5`, appended — ids go in milestone order and are
never renumbered, because patches persist them.

`consumes_input()` returns `true`, overridden **together with** `process_in()`
as `engine_iface.h` requires. The pairing is a convention nothing enforces; an
engine that implements one and forgets the other goes silently deaf.

### 5.1 The physics, restated so the rest of this section is checkable

From `bbd.h`:

```
    ticks/sample = 2 * f_clk / fs
    cells        = stages / 2
    delay        = stages / (2 * f_clk)
```

Constants: `kClockMaxHz` = 32 000, `kMinStages` = 512, `kMaxStages` = 16 384,
`kFilterHz` = 3600, `kSatCeil` = 0.9, `kLossCoef` = √3 − 1.

Two behaviours of the line that the design depends on, both quoted from source:

- **`SetClock(hz)` with `hz ≤ 0` or non-finite means hold**: no ticks, no
  division by the clock, the output filter coasts. The charge stops moving —
  and so does the output.
- **`SetStages` deliberately does not clear the buffer**, "so a stage change
  drifts in time and pitch instead of clicking."

### 5.2 The clock is derived from the deck's cycle

**Decided: the BBD engine syncs to tempo through `set_cycle`.**

`IPartEngine::set_cycle(seconds)` already exists and `Part` already pushes the
master-lane cycle length to every engine. The BBD engine derives its clock from
it:

```
    T     = cycle_seconds * div        // the delay time, on the grid
    f_clk = stages / (2 * T)           // clamped to kClockMaxHz
```

The alternative — a free-running clock with no tempo relation, like a physical
pedal — was rejected: the engine would be the only thing in the instrument
running against the groove, and rhythmic gestures through it would be chance.

**`kClockMaxHz` must be clamped inside the engine.** Today that clamp lives at
`flux.cpp:368`, which movement 3 deletes. Losing it lets `BbdLine` see a clock
it cannot count ticks from.

### 5.3 Lane mapping

| lane | function class | → BBD |
|---|---|---|
| `LANE_SOURCE` | POSITION / **TIMBRE** | **DRIVE** — the dirt inside the loop |
| `LANE_PITCH` | **PITCH** (master lane) | **STAGES** — transposition + brightness |
| `LANE_SIZE` | **SIZE** / FILTER | **`div`** — the delay length against the grid |
| `LANE_MOTION` | SHAPE / **MOTION** | **FEEDBACK** — proximity to bloom |
| `LANE_LEVEL` | LEVEL | output level |

**PITCH takes STAGES, and this follows from the sync decision rather than being
chosen freely.** Once `T` is locked to the grid, `f_clk = stages / (2T)` makes
the clock proportional to the stage count — and the clock *is* the pitch of the
stored charge. Doubling STAGES at constant `T` doubles the clock: the content in
flight is read out an octave higher, and the line's own sample rate doubles with
it, so brightness rises by the same gesture. **512…16 384 is 32×, exactly five
octaves.**

That is a melodic instrument built out of a delay line: in STEP the master lane
quantizes, so the stored charge transposes in semitones **while the repeat time
stays nailed to the beat**. `LANE_SIZE` moves the rhythm, `LANE_PITCH` moves the
pitch, and neither disturbs the other. Both lanes do what their names say.

**The consequence, stated rather than hidden: short delays truncate the top of
the pitch range.** `f_clk ≤ kClockMaxHz` means `stages ≤ 64 000 · T`.

| delay time `T` | reachable stages | pitch range |
|---:|---:|---|
| ≥ 256 ms | 512…16 384 | full 5 octaves |
| 125 ms | 512…8 000 | ~4.0 octaves |
| 100 ms | 512…6 400 | ~3.6 octaves |
| 50 ms | 512…3 200 | ~2.6 octaves |

At 120 BPM a quarter note is 500 ms, so the full range is available at and below
1/4. This is a property of the instrument to play with, not a bug to design
around — but the top of the PITCH lane goes flat at short divisions, and the
implementation must clamp rather than alias.

**The retired claim:** the roadmap says *"RATE bends stored pitch, STAGES is a
brightness axis."* Under sync that is no longer true — STAGES is the pitch axis
and brightness rides along with it. `docs/roadmap.md` needs amending in the same
round.

### 5.4 The grid, the gate, and the fires

**The deck's own mode switch chooses the pitch grid.**

```c
_pitch_q = (_engine_id == ENGINE_SAMPLER ||
            (_engine_id == ENGINE_BBD && !_step_on)) ? pitch_raw : pitch_quantized;
```

- **STEP → quantized.** Semitone transposition of the stored charge, in the key.
- **FLOW → free.** Continuous bends, which is the gesture FLOW exists for.

Without this decision a BBD deck would ride the global SCALE/ROOT grid glided by
`Quantizer`'s own slew on top of the engine's, offset by the Center's DRIFT tune
tap. Nothing new to learn: the control that already chooses the deck's mode
chooses the grid with it.

**Side effect to hear:** switching STEP↔FLOW moves the lane's target, and the
value rides the slew. That is a swoop on every mode change, not a click. On an
instrument whose premise is that time and pitch are one quantity this is
arguably the feature; confirm by ear.

**A step fire latches the clock and holds it until the next one** — the pattern
`SynthEngine` already uses for pitch, applied to the quantity that stands in for
it here.

**The latch is gated on `set_flow`.** An earlier draft claimed "in FLOW there
are no discrete fires". That is false: `engine/mod/lane.cpp:447-452` reads
`bool gated = _step_mode ? _effective_gate(slot) : true;` — *"FLOW has no
per-step gate so it always fires."* A FLOW deck fires once per master-lane
cycle. Un-gated, the latch would freeze the clock at the top of every cycle and
the continuous bend FLOW was chosen for would not happen. **In FLOW the engine
ignores fires and follows the plane continuously.**

`trigger_chord` keeps its default fan-out to `trigger()` per note. It is left
alone: COLOR takes its content from the stereo pair instead (§5.7).

### 5.5 Freeze

**Decided: the freeze is a circulating freeze, not a stop.** Input muted, loop
gain held at the value that keeps the level constant; the content keeps going
round and stays audible.

`SetClock(0)` would be free — it is already implemented and documented as
"hold" — but it stops the READ ticks too, so the output goes silent. That is a
pause in the signal, not a freeze of it.

**The loop gain is not 1 and must be measured.** Inside the loop sit the
charge-transfer pole (`kLossCoef`), the input and output filter chains at
`kFilterHz`, and the compander, which partially compensates for all of them. The
knob law cannot be reused either: `apply_feedback` computes
`fb = _fb_norm * _fb_scale` with `_fb_scale = 1.2 / bbd_drive_gain(drive)`, so
the knob's own maximum is 1.2× the unity point *as scaled by DRIVE* — a bloom
setting, not a hold.

**Specified as:** the engine calls `SetFeedback(k)` directly, bypassing the knob
law, with `k` a constant found by measurement: the value at which a 1 kHz burst
holds its peak within ±0.5 dB over 10 circulations at the boot stage count.
`k` is a tuning constant in the engine, not a parameter.

**Risk, accepted:** `k` is correct at one operating point. STAGES and DRIVE both
move the loop's losses, so at the extremes the freeze will drift down or bloom
up. Bounding that is a listening question; the measurement above is what makes
it a knob-shaped problem rather than an undefined one.

**What drives the freeze:**

- **`set_gate()` in STEP** — gate high freezes. Composes with §5.4 into a
  sequenced freeze.
- **FLOW ignores the gate.** A FLOW deck's gate is effectively always on;
  without this rule a FLOW BBD would be permanently frozen.
- **`set_hold()` (CHOKE) closes the input** and lets the tail run out — the same
  gesture by which a FLOW engine releases its drone.

### 5.6 The engine is stereo

**Two `BbdEcho` lines per BBD deck, one per channel.**

This does not reopen the FLUX mono collapse by inheritance — it is a different
object, in a different slot, with a different input. The reason the collapse was
acceptable is stated in the roadmap itself: *"What remains is carried by the dry
path's existing per-voice pan, not by the echo."* An FX-layer echo may be mono
because the deck's dry signal carries the image. **A part engine has no dry path
— it *is* the signal path**, and its input is stereo at every boundary:
`process_in(float inL, float inR)`, `process(float& outL, float& outR)`, and
§4.3's bus.

(The roadmap's standing instruction against restoring stereo FLUX was withdrawn
by the owner on 2026-07-30 and the roadmap amended — commit `886119b`. The
owner's framing: mono came out of necessity.)

**Cost.** The mono collapse measured the second line directly:
`instrument_worst_bbd` fell 125.24 → 112.88, i.e. **12.36 points for two lines
≈ 6.18 per line** — independent corroboration of §2's corrected 5.9–6.3. A
stereo BBD deck is ≈11.8–12.6 against a SYNTH deck's 17.60–18.21. All of those
are `pct_avg`; §8.3 measures `pct_max`.

**A mono source through a stereo engine is mono, and that is correct.** With
both lines fed the same signal at the same clock they are bit-identical, so a
BBD deck fed from a mono source images centred until COLOR opens (§5.7). This
was raised in review as a defect; it is not one — with a genuinely stereo source
the two lines differ from the first sample.

Placement stays MORPH's job, as it is for the voices; the engine now delivers a
real image for MORPH to place rather than the same sample twice.

### 5.7 COLOR — the clock offset between the two lines

COLOR sets chord size and hence voice count, and a BBD deck has neither. It is
the only free control on the deck, and its absence is what creates whatever
saving exists.

**Decided: COLOR offsets the clocks of the two stereo lines, symmetrically and
geometrically.** At 0 both run together and the image is centred. Opened, the
clocks diverge — first into chorus width, then into two distinguishable
pitches.

- **Symmetric** is not invented here: `SynthEngine::set_detune` is already an
  *"independent symmetric spread"*, and mirroring keeps the centre pitch fixed
  as COLOR opens instead of dragging the deck sharp.
- **Geometric** because pitch tracks the clock ratio directly, so equal knob
  steps give equal intervals.
- **Zero additional cost** — it reuses lines the stereo decision already paid
  for. This is why the read-point mechanism (Appendix B) is not proposed: it
  would cost roughly one line again, and COLOR 2 already lands at a SYNTH deck's
  level.

**"Two tones" only exists in the self-oscillating regime.** Below bloom the
offset is a chorus width; the two lines are colouring one signal, not sounding
two pitches. The two-tone description holds at high FEEDBACK and nowhere else,
and the endpoint should be tuned in whichever regime is judged the more useful.

**The endpoint is left for the ear.** SYNTH's DETUNE spread is ±17.5 cents
(±35 across the pair) — far too narrow to reach anything like a second tone —
and how far past it to go cannot be settled on paper.

**Known risk, to check by ear rather than design out:** at high FEEDBACK both
lines self-oscillate and offsetting their clocks sets those two oscillations
beating. That may be the best thing the control does or the reason to bound it.

### 5.8 The VOICE row — five knobs and one menu parameter

`Part` forwards six VOICE setters (`part.h:138-148`): ATTACK, DECAY, RESONANCE,
SUB, DETUNE, FILT. **Only five are panel knobs.** `DETUNE_A/B` is not in
`kParamCtls` — `part.h` calls it "the independent, **widgetless** Detune
parameter", exposed only as a context-menu slider.

**One parameter has no lane and must live here: MIX.** The old FLUX was
send-style — `Flux::process` ends in `l += wet`, adding onto a signal already
travelling through the chain. An engine has no such signal: it *is* the path,
and the dry input arrived through `process_in()` where `outL/outR` cannot see
it. `Instrument::process` composes its output as `l = al * ga + bl * gb` — the
audio input reaches the output **nowhere**. Without MIX, external audio fed to a
BBD deck is heard wet only.

| knob | → BBD engine |
|---|---|
| ATTACK | freeze ramp — engage/release time of the freeze |
| DECAY | freeze feedback trim — how long a held snippet survives |
| RESONANCE | **MIX** — dry against delayed |
| SUB | **input level** — how much neighbour / audio-in arrives |
| DETUNE *(menu only)* | **slew time** — how far the BBD bends when modulated |
| FILT | **corner of the fixed filter chain** (`kFilterHz`, 3600 Hz) |

**ATTACK and DECAY keep their function class.** The pair is "the shape of the
event in time" on every engine — note envelope on SYNTH, exciter length and
damping on BODY, window attack and decay on the sampler, the freeze in and out
here.

**DETUNE → slew time is a real control, but it is on a menu parameter.**
`flux.h` records the slew as a deliberate musical value: *"Slewing it is not
what a physical part does, but it produces exactly the class of artefact this
device already makes — a drift in time and pitch — which turns STAGES into a
playable gesture rather than a setup control."* Since `LANE_PITCH` and
`LANE_SIZE` are both driven by the plane, this decides *how* the BBD answers
modulation. **Open: whether it deserves a panel slot** — §7.4 frees one, and
this is the strongest candidate for it.

**FILT → filter corner** resolves a contradiction already in the source:
`kFilterHz` is annotated "Ear-tunable" and, in the same breath, "the filters are
the device's character, not a setting". Putting it on FILT settles that in one
direction; BODY's FILT-as-brightness is the precedent.

**RESONANCE → MIX is arbitrary and is recorded as such.** The two have no
semantic relation; on hardware the silkscreen is fixed and the player memorises
it. There is **no precedent for a dead knob** in this instrument, which is why
an arbitrary live assignment beats leaving it inert.

A meaningful alternative was declined: `bbd_analog_spec(bool output_kind, ...)`
keeps its direction flag deliberately, *"because giving the two chains different
corners later must not need a signature change"* — so RESONANCE could take the
**input** corner against FILT's **output** corner, putting both in the filter
family. It leaves MIX homeless, and MIX is load-bearing.

Everything else the BBD contains stays fixed and must **not** be promoted to a
knob to fill space: the compander (*"NOT a parameter… the device's character"*),
`kFiltOrder` (refused on CPU grounds), `kSatCeil`, and `kLossCoef` (derived, and
it tracks the clock for free).

**Implementation trap.** `Part::set_voice_*` forwards to each engine by name,
one hand-written line per knob. A new engine not added to all six lines has
silently dead knobs — no crash, no assert, the same failure class as the
`process_in` / `consumes_input` pairing. **All six forwards must be extended
together.**

### 5.9 The rest of the panel

**The modulation-plane controls are all functional** — RATE, SHAPE, DENSITY,
SMOOTH, RANGE, MELODY, MOD, STEPS, and the STEP / FORM / NEWPHRASE / SONG
buttons. These produce the lane values that play the BBD; they are the reason
the engine move is musically worth anything.

**TUNE becomes the most important control on the deck.** `inst.set_tune(p, …)`
offsets pitch, and on a BBD under §5.3 pitch is the stage count — so TUNE sets
the base transposition with `LANE_PITCH` modulating around it. Note that
`pitch_pre_quant()` runs TUNE through the same quantizer, so it follows the
STEP/FLOW grid decision with everything else.

**SOURCE needs no change.** It is already a lane *base* —
`set_target_base(p, LANE_SOURCE, SOURCE_A)` — and its caption already changes
with the engine. On a BBD deck it is the DRIVE base, for free.

**STAGES_A/B is orphaned by movement 3** (a tape echo has no stage count) and
that is convenient: the orphaned knob becomes the `LANE_PITCH` base on a BBD
deck. Re-pointing a knob per engine at host level is not a new mechanism — the
sampler already moves `SUB_A` from `set_voice_sub` to
`set_target_base(LANE_SIZE)` as GENE SIZE.

`DRIVE_A/B` is **not** a panel knob — it is menu-only, because *"DRIVE lost its
panel slot to DRAG"* (`Spotymod.cpp`). §7.4 gives that slot back.

GRIT, GRITMODE, COMP, REV_MIX, FLUX, FLUXRATE, FLUXFB and LINK all keep their
meaning behind a tape echo. `REC_A/B` is inert on a BBD deck, but the host
already gates it on `ENGINE_SAMPLER`, so it is equally inert on a SYNTH deck
today.

### 5.10 The FX chain behind the engine

`Part::process` calls `_fx.process()` after `_engine->process()` for every
engine — the chain belongs to the deck, not to the engine. A BBD deck therefore
has GRIT → FLUX (tape) → FX MIX → COMP behind it, plus the reverb send tap:
**two delays in series, the BBD first.**

- **No runaway path.** Series, not a loop — BBD out into tape in, with no route
  back. The only feedback is each unit's own, and both are bounded.
- **No collision between the target rows.** The deck carries two: the five
  engine lanes (`LaneId`) and the five FX targets (`FxTargetId`). The engine
  takes the lanes, FLUX takes the FX targets, so the two delays have fully
  independent time and feedback control. **This does not extend to the panel
  knobs**, which do collide — §5.9 and §7.4.
- **Switching the tape off is not free.** GRIT and FLUX sit behind SoftSwitches,
  so a BBD deck that does not want the tape echo pays nothing *for the echo* —
  but `PartFx::process` still runs the five `FXT` smoothers, `_comp.process()`
  and the reverb send tap unconditionally. The bench prices that residue exactly:
  `fx_none` = 24 509 cycles = **2.55 points per deck**.
- **The comparison does not count the chain twice**, because it runs identically
  on a SYNTH deck. It does **not** follow that coupling is zero — see §8.2.

### 5.11 Integration with code that assumes notes

**CHOKE stage 1 is not a BBD bug.** `Instrument::process` opens the inhibit
window with `bool window = _parts[pri].gate() || _parts[pri].flow();`, and
`flow()` is `!_step_on` — a deck state. **Every** engine in FLOW as the priority
side already holds that window open permanently, and `instrument.cpp` documents
it as intended (*"FLOW: a drone is always 'on'"*). `set_inhibit` suppresses
triggers and gate pulses, **not audio**. What remains is a design question, not
a defect: "the priority side is holding a note" is vacuous for a voiceless
engine. Changing it would affect every engine and is left open on those terms.

**CHOKE stage 2 degrades to stage 1 on a BBD deck, and that stands.**
`Part::max_voice_env()` loops over `SynthEngine::kVoices` and returns 0 on a
voiceless engine, so the `> 1e-4` decay window never opens. This is arguably
correct — a delay tail is not an audible note, and a frozen one would be
infinite, so a BBD deck would otherwise choke its neighbour forever. Recorded
because it happens by accident of the implementation.

**The silence trap.** A BBD deck with no source selected outputs nothing; the
player selects the engine, hears silence, concludes it is broken. **The default
source for a deck switched to `ENGINE_BBD` is audio in.** On VCV with no cable
in IN_L/IN_R the host passes `nullptr`, which is the state most players will
first meet the engine in — so the default must also make the *neighbouring
deck* audible, or the engine must show its state (§9).

**Engine switching clears the line.** `set_engine` already crossfades
click-free. Not clearing means switching back replays old charge; surprising
persistence across an engine change has no defence, and a player who wants the
old content back has no way to know it is still there. The hook is
`BbdLine::Reset()`, which `Init` already performs and which is also what stops a
VCV sample-rate reinit from replaying stale charge.

**The VCV surface is longer than "defaults and scenarios":**

- `ENGINE_A/B` is `configSwitch(c.id, 0.f, 3.f, …, {"Synth","Sampler","Wave","Body"})`
  and needs its range, label list and colour-shade table extended.
- The engine dispatch is a ternary whose own comment says *"anything that isn't
  0/2/3 still falls through to Sampler"* — **a bare enum append routes BBD to
  the sampler.**
- `sourceCaption()` covers states 1/2/3 only, so §5.9's "the caption already
  changes with the engine" does not extend to a fifth one.
- `bench/audition/init_patch.cpp` has the same fall-through and never learned
  BODY either.
- Defaults go in `kInitParamDefaults[]` (`host/vcv/src/init_patch.hpp`, read via
  `initParamDefault(id)`) and in `configControls`. **`defaultFor()` was deleted
  and `test_panel.py` asserts it stays deleted** — writing one back fails the
  guard. The snapshot source is `drone.vcvm`; there is no `init.vcvm`.
- `host/vcv/res/test_panel.py` asserts the **exact text** of both the ENG config
  block and the dispatch ternary — and **is already red, with 53 failures**, on
  BODY-era drift. Movement 2 cannot use it as an acceptance gate until it is
  green again. **Fixing it is part of movement 2's first task**, not a
  precondition to be discovered halfway through.

### 5.12 Definition of done — movement 2

- A deck set to `ENGINE_BBD` passes audio from audio-in and from the neighbour,
  with MIX at 0 (dry only) and 1 (wet only) both correct.
- `LANE_PITCH` transposes the stored charge across the reachable stage range
  (§5.3) with `LANE_SIZE` held: the repeat interval measured from the render CSV
  does not move.
- `LANE_SIZE` moves the repeat interval with `LANE_PITCH` held: the pitch of a
  1 kHz burst's first repeat does not move.
- At `T` short enough to bind `kClockMaxHz`, the top of the PITCH lane clamps —
  it does not alias or wrap.
- The freeze holds a 1 kHz burst within ±0.5 dB over 10 circulations at the boot
  stage count (this is the measurement that fixes `k`, §5.5).
- In FLOW, a lane cycle boundary does not latch the clock (§5.4).
- COLOR 0 with a mono source is bit-identical between L and R; COLOR opened is
  not.
- Switching away from and back to `ENGINE_BBD` produces silence, not old charge.
- `test_panel.py` is green, including the extended ENG block and dispatch.
- The other four engines are bit-identical (render hashes unchanged).
- `inst_bbd_engine_worst` exists as a bench row (§8.3).

---

## 6. Movement 3 — FLUX reverts to a tape echo

### 6.1 There is nothing to revert to — it has to be rebuilt

`21087f2` did not modify the tape echo; it **deleted** it. `DeLine`, `TapeBpf`
and `EchoDelay` are all gone, and nothing in the tree implements an
interpolating delay line any more. This is not `git revert` and it is not a
one-line `FxMem` change.

`git show e004a3d^:engine/fx/flux.h` and the matching `flux.cpp` are the
reference, but **restoring them verbatim is not sound-neutral**: `fast_tanh`
changed underneath, so the saturation on the read path is not what it was. The
round must either accept the new curve or pin the old one deliberately.

**Confirmed non-conflict:** `BbdEcho` lives entirely in `engine/fx/bbd.{h,cpp}`
and has no `Flux` dependency, so movement 3 removes a *user* of the class, not
the class. Movement 2 keeps it.

### 6.2 The tape echo is stereo

As it was: `EchoDelay<kMaxSamples> _echo_l, _echo_r`. The owner withdrew the
"FLUX stays mono" instruction on 2026-07-30 — mono came out of necessity, under
budget pressure, in step (3) of a CPU programme (*"Erst den Mantel, messen, dann
auf mono"*), and the listening pass that followed accepted a fait accompli.

Two further reasons it does not bind: the listening was done on a
**bucket-brigade** in the FX slot, which movement 3 replaces; and the
observation behind it — *"what remains is carried by the dry path's existing
per-voice pan"* — survives untouched and may well produce the same verdict a
second time. It is the verdict being re-taken, not the argument.

What is **not** reopened: no panning or widening layer is bolted on top, and no
DRIVE or compander re-tuning is owed.

### 6.3 Memory — the number that was missing

`21087f2`'s own message records it: **`kMaxSamples` dropped from 262 144 to
`kMaxStages / 2` = 8192.** The tape buffer was 32× the BBD's. Today
`FxMem::echo` is `float* echo[PART_COUNT]` and the Rack `Module` holds
`float echo[2][8192]` = 64 KB **by value**.

**Specified: the tape line keeps its old length, 262 144 floats per channel.**
That is 5.46 s at 48 kHz, 2.73 s at 96 kHz and 1.37 s at 192 kHz — the buffer is
in samples, so the reachable delay time shrinks at higher rates, exactly as it
did before.

**`FxMem` gains a second buffer family, not a second dimension on the first**,
because the two lengths differ by 32×:

```c
struct FxMem {
    float* echo[PART_COUNT][2] = {};   // tape, kTapeSamples = 262144 each
    float* bbd [PART_COUNT][2] = {};   // BBD engine lines, kMaxStages/2 each
    AmbientReverb* reverb = nullptr;
    SampleBuffer::Frame* sampler_buf[PART_COUNT] = {};
    size_t sampler_frames = 0;
};
```

| what | per line | per deck | instrument |
|---|---:|---:|---:|
| tape echo (stereo) | 1 MB | 2 MB | **4 MB** |
| BBD engine (stereo) | 32 KB | 64 KB | **128 KB** |

On a Daisy with 64 MB of SDRAM that is affordable. **On VCV it is not free:**
the Rack `Module` holds the tape buffer by value, so it is **~4 MB per module
instance**, plus the render host's static. The alternative — heap-allocating it,
as the sampler buffers already are — is the cheaper change and should be taken.

**The null contract must be specified, and the earlier draft picked neither
model.** `Instrument::init(float)` hands an empty `FxMem`, and that overload is
what most tests use, so `set_engine(p, ENGINE_BBD)` there must not fault.
**Specified: `nullptr` → that line runs silent**, matching `SamplerEngine`'s
documented behaviour rather than `Flux`'s `_buf_ok` guard, because the engine
has no bypass path to fall back to.

### 6.4 `Part::init` and its callers

`FxMem` is unpacked by `Instrument::init` into
`Part::init(sr, seed, echo, sampler_mem, sampler_frames)`. That signature grows
in **both** movements 2 and 3, which is the coupling §3 records. Every direct
caller changes with it:

- `bench/workloads_instr.cpp`, `bench/workloads_body.cpp`, the bare-`Part` tests
- `host/render/main.cpp`, `host/vcv/src/Spotymod.cpp`, `bench/mem.cpp`,
  `bench/audition/memory.cpp`

There are **five source lists** to keep in step, and one is already broken:
`bench/audition/Makefile` lists `flux.cpp` without `drag.cpp` while
`flux.cpp:310` calls `derive_intervals` unconditionally — an undefined reference
waiting for whoever builds that target next. Fix it in passing or trip over it.

### 6.5 LINK: THIN takes the whole knob, DRAG is dropped

**THIN comes across and gets the whole knob.** It is a pure gate on the
sibling's rhythm — engine-agnostic, mechanism-free, and it works on a tape echo
exactly as on a BBD. `LINK_A/B` stops being bipolar with two mutually exclusive
halves and becomes a plain unipolar THIN over its full travel.

**DRAG is dropped.** Three reasons that agree:

1. **It was designed on the BBD's clock** — *"the neighbour's rhythm pulls the
   delay time. The clock moves, so the stored charge bends in pitch."* On a tape
   echo the same gesture is a read-pointer sweep: a different mechanism, so this
   would be a re-implementation, not a port.
2. **It has nowhere to live if it follows the BBD instead.** Every lane and knob
   on a BBD deck is spoken for.
3. **It was not judged worth the room**, and the panel constraint says controls
   should leave rather than arrive.

**Removing it is not a clean excision — DRAG and THIN share machinery.**
`flux.cpp` runs one accumulator for both, with the comment *"One accumulator,
two consumers. They are mutually exclusive by construction"*, and
`_drag_step_len` is armed **inside `apply_drag()`** (`flux.cpp:280`:
`_drag_step_len = thinning ? _delay_time * _sr : 0.f;`). Deleting `_drag_phase`,
`_drag_step_len` and `apply_drag()` therefore **breaks THIN**. The shared
timebase has to be extracted first and renamed, in its own commit, with THIN's
tests green before anything is deleted.

What does go: `_drag`, `_drag_iv`, `_drag_i`, `_drag_active`, one branch in the
per-sample path, and the uniformity guard — which exists only because *"an even
pattern is a failure for DRAG and a RESULT here"*. `engine/fx/drag.{h,cpp}`
stays: it houses `link_tuning`, which THIN still needs.

**Consequence to accept:** a BBD-engine deck has no rhythmic coupling to its
neighbour of its own. The sibling reaches it through CHOKE, through the
cross-deck audio bus, and through THIN on the tape echo behind it — one stage
later in the signal path than DRAG used to act.

### 6.6 Patch migration

`LINK` is `configParam<LinkQuantity>(c.id, -1.f, 1.f, …)` today, with THIN on the
**negative** half. Making it unipolar 0..1 clamps every saved THIN value to 0 —
the setting vanishes — and reinterprets every saved DRAG value as THIN at that
amount.

**The earlier draft pointed at the wrong mechanism.**
`host/vcv/src/form_song_migration.hpp` is 36 lines about a version integer and
FORM/SONG JSON; Rack restores params itself, so it does not touch this at all.
The remap needs **new `dataFromJson` code**: read the stored LINK value, map
`v < 0 → -v` and `v > 0 → 0`, and bump the patch version so it runs once.
`LinkQuantity`'s display string and the `init_patch.hpp` LINK note change with
it.

### 6.7 Leftover BBD-shaped state to dispose of

- `_fx_base` slot `FXT_FLUX_TIME` boots at 0.5 *because* the BBD reads it as a
  geometric clock multiplier (`part.h`). A tape echo reopens that default.
- `Instrument::drag_time_for_test`, `Flux::drag_time_s()`,
  `tests/test_drag.cpp` — each needs a disposition.
- `bench/anchor.cpp` anchors `instrument_worst_bbd` / `_dtcm`, whose retirement
  touches the bench contract tests, `bench/families.cpp` and `bench/profiles.py`.
- `bench/itcm_hot.lds` lists `flux.o` and `bbd.o` — see §8.4.

### 6.8 What the tape echo's cost is expected to look like

An interpolating delay line's cost **does not vary with delay time**; the BBD's
does (`ticks/sample = 2·f_clk/fs`), which is why `instrument_worst_bbd` exists as
its own row. A reverted tape FLUX is therefore expected to have a flat cost
curve, with `instrument_worst` alone as its worst case — so the `_bbd` rows can
retire rather than being re-pointed.

`instrument_worst_taps` is **not** a forecast for it. Commit `5d53901` says what
that row measured: *"instrument_worst_taps measures DUST/ROT under the full
instrument"* — the tap bank, deleted in `e004a3d`.

### 6.9 Definition of done — movement 3

- An interpolating stereo tape echo exists behind FLUX's unchanged public form
  (`SoftSwitch`, `engaged()`, the bit-exact off path, `set_rate`/`set_mix`/
  `set_feedback`/`set_bpm`, the shared delay-time slew).
- With FLUX disengaged, every render hash is unchanged.
- THIN works over the full LINK travel, with its tests green **before** any DRAG
  symbol is deleted (§6.5).
- A patch saved before the change loads with its THIN setting intact (§6.6).
- The tape buffer is heap-allocated on VCV, not a 4 MB by-value `Module` member.
- `bench/audition/Makefile` builds.
- `instrument_worst` is re-measured in the same build and reported as `pct_max`;
  the `_bbd` rows are retired from the anchor set.

---

## 7. The panel after all three movements

| control | before | after |
|---|---|---|
| STAGES_A/B | FLUX stage count | `LANE_PITCH` base on a BBD deck; unused otherwise |
| DRIVE_A/B | menu-only (lost its slot to DRAG) | **§7.4** |
| LINK_A/B | bipolar DRAG ↔ THIN | unipolar THIN, full travel |
| COLOR_A/B | chord size / voice count | clock offset between the stereo lines on a BBD deck |
| RESONANCE_A/B | filter resonance | MIX on a BBD deck |
| ENGINE_A/B | 4 states | 5 states |

**§7.4 — DRAG's freed panel slot is an open question this spec does not
answer.** The strongest candidate is DETUNE (§5.8), which is a real performance
control currently reachable only through a context menu. Giving it a knob would
also settle the "six knobs, five widgets" awkwardness in the VOICE row. Against
it: the hardware constraint says controls should leave rather than arrive, and
leaving the slot empty is a legitimate outcome.

---

## 8. Budget — what must be measured

**This section makes no claim that the design saves CPU.**

### 8.1 Every figure that is not traceable to a measured row

| figure | § | status |
|---|---|---|
| ≈400 cycles/block for the bus | §4.3 | ISA hand-count; the same class of count was falsified by 2–4× in round 4 |
| the tape echo's in-context cost | §6.8 | unmeasured, and cannot be measured before it exists |
| the freeze constant `k` | §5.5 | **defined as a measurement**, not estimated |
| COLOR's endpoint | §5.7 | left for the ear, deliberately |

### 8.2 Two structural cautions

- **Component rows do not sum.** The roadmap names this with a figure:
  *"Component rows summed to ~120 % of budget while `instrument_worst` measured
  ~159 % — a ~375k-cycle (39-point) gap with no named owner."*
- **Coupling is not zero, and its direction is unfavourable.** Swapping a SYNTH
  deck (small per-sample working set) for a BBD deck (a 32 KB line walked at
  1.33 cells/sample) changes the instrument's *memory* behaviour, and this repo
  measures that class large — `sweep_grit_no_bbd_mem` vs `sweep_grit_bare` names
  *"cache pressure from the BBD buffers merely being resident"* as the live
  hypothesis for a 3-point move **at an identical checksum**.

### 8.3 Rows that must exist

1. **`inst_bbd_engine_worst`, as a real worst case** — `LANE_PITCH` at the
   stage ceiling, `LANE_SIZE` at the shortest division that does not bind
   `kClockMaxHz`, **freeze engaged**, COLOR at maximum, both decks. One row at a
   comfortable operating point would reproduce §2's error under a new name.
2. **A sampler-engine instrument row in the gate set.** `inst_sampler_worst` at
   106.82 is the standing counterexample and is currently outside the gate:
   `setup_inst_worst` never calls `set_engine`, so both rows run `ENGINE_SYNTH`
   only. WAVE, BODY and the sampler have never been in the worst case at all.
3. **The stereo tape echo's flat cost in context**, inside movement 3.
4. **The bus, measured rather than waived**, as a paired same-source A/B.
5. **A second BBD line, same-build.** The ≈6.18 comes from the mono collapse's
   own before/after, which is a different build and the opposite direction.

All of these report `pct_max`. Bench rows are hand-registered in
`bench/run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY`, `bench/profiles.py`,
`bench/families.cpp` and `bench/anchor.cpp` — four places, all hand-maintained.

**Verify new rows against `bench.map`, not the memory table.** The bench build
can silently relink a stale object.

### 8.4 ITCM

The 6-point saving that produced the 102.64 gate is a **code-residency** result,
not a DSP one. `bench/itcm_hot.lds` holds a hand-written hotset —
`instrument.o, part.o, part_fx.o, flux.o, bbd.o, grit.o, reverb.o, comp.o,
synth_engine.o, voice.o` — using 42 240 of 65 536 bytes, **23 296 free**, behind
a hard `ASSERT`.

This design adds a sixth engine whose kernel must be resident to cost anything
like §2's figures, and movement 3 replaces `flux.o`'s text entirely.

**The failure is loud, not quiet, and that is good news.**
`bench/test_itcm_link.py` hand-asserts that `spky::BbdLine::Process` links
*inside* ITCM. Moving its only user into a new translation unit relocates the
symbol and fails that test — so the hotset cannot be silently forgotten. An
earlier draft called this a quiet failure and had it backwards.

---

## 9. Open questions

Real ones only. Everything resolved is in the body; everything superseded is in
Appendix A.

- **The cost claim (§2, §8.3)** — unproven; needs peak measurement.
- **DRAG's freed panel slot (§7.4)** — DETUNE is the candidate; empty is
  legitimate.
- **COLOR's endpoint, and whether beating self-oscillations are a feature**
  (§5.7) — the ear.
- **The freeze constant `k` across operating points** (§5.5) — one value is
  specified; whether it needs to track STAGES or DRIVE is a listening question.
- **CHOKE stage 1 for voiceless engines** (§5.11) — changing it touches every
  engine, so it is a shipped-behaviour decision, not a fix.
- **No engine-contract analogue.** WAVE and BODY both entered through
  `tests/synth_engine_contract.h` (`contract_round_robin_and_steal`,
  `contract_flow_drone_and_surface`, `contract_chord_surface_and_hold`,
  `contract_deterministic_seed`). A voiceless non-`SynthEngineT` engine cannot
  satisfy it. **A replacement contract, its test file, its `CMakeLists.txt`
  registration and an enum-stability pin all need writing** — §5.12's list is
  the raw material for it.
- **No observer.** Every prior engine added something the render CSV prints —
  BODY's `a_exc`/`a_matl`, the sampler's fill/grains/slices, FLUX's
  `stages_for_test`/`drive_norm_for_test`. A BBD deck would write 0 into
  `a_voices` and `a_v0..3` and expose no clock, stage count, freeze state or
  MIX, **so a demo scenario would pass vacuously.** At minimum: clock, stages,
  and freeze state.
- **Should MIX be modulatable?** §5.8 puts it on a knob, so it is set rather
  than played. Moving it to `LANE_LEVEL` would let the plane open and close the
  echo rhythmically — musically stronger — at the cost of the engine's output
  level and of LEVEL's status as the one function class all five engines share.
  Decided against for now; revisit once the engine can be heard.
- **Does a BBD deck still want a tape echo after it?** §5.10 settles the
  mechanics. Whether dark-and-compressed into longer-and-cleaner is worth
  playing is a listening question.

---

## 10. Explicitly not proposed

- **A forced trade** (BBD on → fewer voices, or GRIT locked to Drive). The
  arithmetic worked — COLOR halved plus GRIT locked reached ≈91 % — but it
  relocates the cost rather than changing the instrument.
- **BBD exclusivity** (only one deck may run it).
- **A second, cheap delay alongside the BBD in the FX chain.** Does nothing for
  the gate, which measures the worst case, and the worst case contains the BBD.
- **A new FEED control in the centre section.** §4.4's existing source selection
  carries it.
- **DRAG, in any layer** (§6.5). Lineage worth keeping while it is fresh:
  DUST/ROT → LINK(DRAG|THIN) → THIN. Each step dropped a mechanism and kept the
  gesture; this is the last one.
- **Restoring the DUST/ROT tap bank onto the reverted tape echo.** Its function
  already returned as LINK; its knobs were renamed to `set_drive`/`set_stages`
  and re-pointed; and `instrument_worst_taps` measured it at **4.19 points**
  over `instrument_worst` (run 2 of two — run 1 gives 5.16, so the honest
  bracket is 4.19–5.16), which is the whole margin this design might win.
- **Raising `kClockMaxHz` or lowering `kMinStages`.** `kClockMaxHz` carries a
  physical argument — the clock must not overtake the fixed 3.6 kHz filter
  chain. `kMinStages` does **not**: its comment is a modelling one (*"the rest
  of the scale is a chip no pedal exposes"*), and it is a floor, not a ceiling.
  An earlier draft called both "physically argued". Neither is moved here, but
  only one of the two would be hard to move — relevant if §5.3's short-delay
  pitch truncation turns out to matter.

---

## Appendix A — superseded claims

Kept in one place so the body reads as a design rather than as an argument with
itself. Each of these appeared in `2026-07-30-bbd-part-engine-design.md`.

| claim | why it is wrong |
|---|---|
| "The BBD costs 8–9 points, the size of the overrun" | Baseline was two merged rounds stale. The overrun is 2.64, measured at `d570e47`. |
| "Two independent measurements agree" | One calculation and one coincidence — a gross component cost and a net saving are not measurements of the same quantity. |
| "120.9 % → 108.69 %" | Mixed two rows: 120.9 is `instrument_worst` anchored, 108.69 is `instrument_worst_bbd` offline. |
| "A BBD deck is cheaper than SYNTH, so no BBD configuration is the worst case" | Measured counterexample in this repo (§2, the sampler). |
| "One `BbdEcho` line costs 4.55 points" | Measured at boot defaults, 3.9× below the gate's tick rate. ≈5.9–6.3. |
| "`Process()` is bounded at 0.9" | That is the bound inside the saturator; the compander's expander runs after it. |
| "STAGES does not transpose / is a pure brightness axis" | Contradicted by `SetStages`'s own comment, and inverted outright by the sync decision (§5.3). |
| "In FLOW there are no discrete fires" | `lane.cpp:447-452`: FLOW always fires, once per cycle. |
| "The freeze is feedback at 1" | `apply_feedback` scales the knob by `1.2 / bbd_drive_gain(drive)`; the unity point is a measurement (§5.5). |
| "Movement 3 is the move that clears the gate" | A stereo tape echo costs 5.91/deck against the BBD's ≈6.18. It saves essentially nothing. |
| "`FxMem` gains a field" | Two buffer families differing 32× in length, plus `Part::init`'s signature and ten callers across five source lists (§6.3, §6.4). |
| "Not a `git revert`" (as the whole account of movement 3) | Three classes were deleted, ~230 lines. Nothing in the tree implements an interpolating delay line (§6.1). |
| "DRAG's removal is a clean excision" | DRAG and THIN share `_drag_phase`, `_drag_step_len` and `apply_drag()`. Deleting them breaks THIN (§6.5). |
| "`form_song_migration.hpp` handles the LINK migration" | It is 36 lines about a version integer and FORM/SONG JSON. LINK needs new `dataFromJson` code (§6.6). |
| "The ITCM failure mode is quiet" | `test_itcm_link.py` hand-asserts `BbdLine::Process` inside ITCM; the failure is loud (§8.4). |
| "DRIVE is a panel knob" / "DETUNE is a panel knob" | Both are menu-only. |
| "The sampler leaves SUB and DETUNE dead" | SUB is re-pointed to `set_target_base(LANE_SIZE)`; DETUNE is widgetless. There is no precedent for a dead knob. |
| "A BBD deck pays nothing for a disengaged tape echo" | `fx_none` = 2.55 points/deck for the FX shell (§5.10). |
| "DETUNE is ±35 cents" | ±17.5 per voice, ±35 across the pair. |
| "The clock is called at ~10 sites" | That is `bench.elf`; the firmware has 2. |
| "`defaultFor()` / `init.vcvm`" | `defaultFor()` is deleted and a test forbids its return; the snapshot is `drone.vcvm`. |
| "The three movements are independent" | 2 and 3 both grow `FxMem` and both touch the ITCM hotset (§3). |

## Appendix B — the read-point mechanism, recorded but not proposed

Retained because the reasoning is sound and would apply again if the budget
changed. Under §5.6's stereo decision it is **not proposed**: it costs roughly
one line again, and COLOR 2 already lands at a SYNTH deck's level.

`BbdLine` has no read pointer — *"all charge packets are clocked forward
together, and the delay is a consequence of the clock, not of an index."* So
"taps" is the wrong word and a tap-delay reading of COLOR does not fit the
model.

A different reading does. Reading the same line at cells N, N/2, N/3, N/4 gives
delays in the ratio 1 : ½ : ⅓ : ¼ — the first four partials of the harmonic
series, since delay time is pitch on a BBD. Below ~8 ms of delay they are
simultaneous pitches, not echoes. That is COLOR's gesture exactly: how many
notes sound at once, with the notes coming from the geometry of the line rather
than from a scale table.

Two caveats that would have to be settled first:

- **Cost.** Each extra read point needs its own `ybbd_old_` and its own
  `Xout_mem_[3]` chain, because the reconstruction filter is driven by the step
  between consecutive readings. Read off the kernel, ≈40 % of a line — **read,
  not measured**. The one measured bracket is a warning: `instrument_worst_taps`
  priced the deleted tap bank at ~1.05 points per tap, and a BBD read point does
  strictly more work (`interpolate_g`, three complex multiplies, its own filter
  chain). A factor of 1.7 is plausible; a factor of 3 would not be surprising.
- **The meaning changes across the time control.** Above ~8 ms the read points
  are rhythmic repeats, not chord tones.
