# BODY — resonator part engine (string ↔ metal ↔ bell)

**Date:** 2026-07-26
**Status:** design approved, implementation gated on the hardware bench (see §7)
**Supersedes:** `2026-07-18-string-engine-design.md` (STRING). That spec's core
decisions survive; its scope, its integration plan and its cost model do not.
**Scope:** a fourth selectable part engine (`ENGINE_BODY`) — a 4-voice
resonator instrument behind the existing `IPartEngine` semantics, morphing
continuously from Karplus-Strong string through dispersive metal into a modal
bell, with a playable exciter and a sympathetic excitation bus. One template
refactor. No new panel control, no new parameter id. No change to the mod
plane, FX, reverb, or the master chain.

## Problem

STRING was specified as a Karplus-Strong engine and explicitly ruled out
modal/bell territory: *"Modal/bell territory (`Resonator`, 24 modes with
per-sample coefficient math) is out: too expensive, wrong corner."* The
hardware bench appeared to confirm it — `resonator` cost 329,000 cycles
(34.27 % of the block) and `modal_voice` 360,844 (37.58 %) for **one** voice.

That reading was wrong about the cause. `daisysp::Resonator::Process`
recomputes every mode coefficient **per sample**: a `powf` (measured at
`micro_powf` = 19,013 cycles per block, i.e. 198 cycles per call), a
`NthHarmonicCompensation` loop, and a full pass over all 24 modes building
`mode_f` / `mode_q` / `mode_a` before any filtering happens. The 34 % is the
price of that implementation, not the price of modal synthesis.

This fork moves exactly that kind of work to the 96-sample control cadence and
has twice been paid for it — `engine/util/svf_lp.h` (CPU hunt round 3) and the
reverb-send `sinf` removal. `svf_lp.h` even names this case in its own header:
*"If a future feature needs the band-pass … go back to `daisysp::Svf`, which is
still vendored, and pay for what you use."*

With coefficients on the control tick, only the band-pass bank remains per
sample. That reopens the corner the old spec closed, and it reopens it without
the polyphony sacrifice the user was prepared to make.

The second problem is structural. The old spec's §1 planned
`SynthEngineT<V>` templated on the **voice** type. What was actually built is
`SynthEngineT<OscT>` holding `VoiceT<OscT>`; WAVE shipped as
`SynthEngineT<WtOsc>`. A resonator voice has no oscillator, no `SvfLp` and no
`Env` — it cannot be a `VoiceT<OscT>`. The refactor the old spec assumed still
has to happen, and it now has two regression gates instead of one.

## Decisions (user)

- Sound world: **struck metal / modal**, **stretched string → metal**, and
  **sympathetic resonance**. FM bells are out — digital character, and ZAP
  covers that corner.
- Structure: **one continuous morph**, not a model switch and not two
  engines. Both structures run; one control blends them, and the mod lanes can
  drive it.
- Sympathetic excitation sources: **own FLUX tape, the other deck, and the
  audio input** — all three.
- Bus surface: **the context menu selects sources, SUB is pure level.** The
  user accepted the hardware debt this creates; §6 resolves it by making the
  selection patch state rather than a performance control.
- Degradation ladder: if the bench refuses the workload, **voice count gives
  first, the mode count stays.** A rich bell at lower polyphony beats four poor
  ones. (Carried verbatim from the user's framing.)
- Surface: **strict reinterpretation** — zero new params, zero panel changes
  (unchanged from the STRING spec).

## Naming

The engine is no longer a string engine, so it is not called one. **BODY**
(resonating body); the contextual SOURCE label becomes `MATL`. Milestone M5j
is renamed STRING → BODY in `README.md` and `docs/roadmap.md`; the milestone's
position in the order (before M5k/ZAP) does not change.

## Existing infrastructure this reuses

- `engine/synth/synth_engine.*` — the entire allocation machine: round-robin +
  oldest-steal, FLOW sustain/demote, drone promise, chord slots + stab
  humanization, CHOKE hold, velocity slew, control-rate cadence
  (`kCtrlInterval = 96`), FILT silence-fade invariant (`_filt_gain`).
- `lib/DaisySP/Source/PhysicalModeling/KarplusString.{h,cpp}` — the string
  primitive: 1024-sample delay line + stretch line, one-pole damping filter,
  DC blocker, curved-bridge/dispersion nonlinearity. MIT (Electrosmith +
  Émilie Gillet), joins the `daisysp_min` target. The `StringVoice` wrapper is
  **not** used.
- `lib/DaisySP/Source/PhysicalModeling/resonator.{h,cpp}` — read as reference,
  not linked. `ResonatorSvf<4>`'s batched band-pass recurrence and the mode
  frequency/Q/amplitude formulas are the model for §4; the per-sample
  coefficient loop is what §4 exists to remove. Attribution follows the
  `svf_lp.h` precedent (THIRD_PARTY.md, MIT, Electrosmith + Gillet).
- `engine/util/fast_tanh.h` (soft clip — **not** libm `tanhf`, measured at 208
  cycles per call), `engine/util/fast_sin.h` (ping exciter),
  `engine/util/onepole.h`, `engine/mod/rng.h` (deterministic exciter).
- `engine/fx/flux.h` (tape tap), `IPartEngine::process_in` (audio input — the
  sampler already uses it), `engine/instrument.*` (cross-deck tap).
- `host/render/scenario.cpp` engine parsing; VCV per-part engine button
  (`Spotymod.cpp` ENGINE_A/B) and per-part context menu (Detune A/B
  precedent); bench-firmware workload list.

## Design

### 1. Integration — the template moves one level up

`SynthEngineT` is re-templated on the **voice** type. The allocation machine is
inherited by every engine, never copied.

- `using SynthEngine = SynthEngineT<VoiceT<MorphOsc>>` — SYNTH.
- `using WaveEngine  = SynthEngineT<VoiceT<WtOsc>>` — WAVE.
- `using BodyEngine  = SynthEngineT<BodyVoice>` — this spec.
- `EngineId` grows `ENGINE_BODY = 4` (appended; no persisted id changes
  meaning). Scenario parser learns `"body"`. The VCV engine button cycles five
  states (LED shade; panel unchanged — hardware-reducibility constraint).

**Regression gate: both SYNTH and WAVE reference renders stay byte-identical
across the refactor.** The old spec named only SYNTH because WAVE did not
exist yet.

**Voice contract.** `BodyVoice` implements exactly the methods
`SynthEngineT` calls on its voice today — `trigger`, `set_sustaining`,
`set_pitch_hz`, `set_vel`, `set_env_times`, `set_morph`, `set_detune_cents`,
`set_sub_level`, `set_cutoff_hz`, `set_resonance`, `set_pan`,
`set_drift_amount`, `update_control`, `process`, `active`, `env_value` — with
resonator-native semantics (§5). Compile-time substitution, no virtual
dispatch. The contract grows two methods: `set_hold(bool)` (palm mute) and
`set_excitation(float)` (per-sample bus feed, §6). `VoiceT<OscT>` implements
both as no-ops.

**`active()` without an envelope.** The body rings out on its own; nothing
reports "done". Replacement: a control-rate energy follower (block peak,
decaying). `active()` = follower above ~−72 dB **or** recently triggered (a
minimum hold so a quiet strike is not stolen instantly). `env_value()` returns
the follower — LED and introspection semantics stay meaningful. The follower
matters more here than it did for STRING: a 24-mode bell at low damping rings
far longer than a plucked string, and steal order depends on it.

New files: `engine/body/body_voice.h/.cpp`, `engine/body/mode_bank.h/.cpp`,
`engine/body/exciter.h`, `engine/util/svf_bp.h`.

### 2. Voice interior

```
Exciter (RESO: click/noise/sputter/ping) ─┐
                                          ├─→ ┌─ String A ─┐  (KS pair, ±spread)
Excitation bus (SUB, §6) ─────────────────┘   ├─ String B ─┤
                                              └─ ModeBank ─┘  (24 band-pass modes)
                                                    │
                                              MATL morph → pan (MOTION fan + drift) → vel
```

No Svf, no Env — the decay *is* the envelope, for both structures. The FILT
silence-fade invariant keeps running through the engine-side `_filt_gain`.

**The exciter** (`exciter.h`, own deterministic `Rng` per voice, fixed distinct
seeds). RESO morphs the strike character across four zones:

| zone | character |
|---|---|
| 0 | **click** — filtered impulse, bare pluck / mallet |
| 1 | **noise burst** — Dust-like filtered noise |
| 2 | **granular sputter** — rng-gated micro-bursts |
| 3 | **tonal ping** — short `fast_sin` blip at the fundamental (pitched hammer) |

Zone boundaries and crossfades are tuning material (DUST-zone precedent). In
STEP the exciter fires as a strike of ATTACK-controlled length; in FLOW the
same character becomes *continuous* excitation — the bowed drone, honoring the
drone promise. Excitation level follows velocity (chord gain comp).

**Strike position is not a control.** Modal synthesis wants a position
parameter; this design fixes it per exciter zone (tuning material) rather than
spending a knob the hardware does not have.

### 3. MATL — the material morph

The three sound worlds are one physical axis, not a crossfade of unrelated
models: harmonic partials (string) → stretched partials (dispersion) → freely
inharmonic partials (modal). MATL walks it:

| MATL | material |
|---|---|
| 0.0 | plain Karplus string — harmonic, plucked, bowed |
| ~0.5 | dispersive string — stretched partials, prepared piano, gamelan, cimbalom |
| 1.0 | modal bank — tubular bell, bowl, gong, glass |

The string's own dispersion/curved-bridge nonlinearity rides *up* the axis
rather than sitting on its own knob — which is what frees RESO for the exciter.
The string↔bank blend is equal-power; the exact taper, the point at which
dispersion peaks, and the bank's amplitude entry are tuning material for the
listening pass. **The table rows are the contract, the curves are not.**

Both structures run at every MATL position. That is what makes the morph
modulatable (SOURCE is a lane) and what makes §7's cost the worst case rather
than an average.

### 4. The mode bank — control-rate coefficients

The load-bearing decision of this spec.

`ModeBank` holds 24 band-pass modes in batches of 4 (`SvfBp`, a band-pass-only
sibling of `SvfLp`, built the same way: only the outputs actually read, no
drive term, no per-`SetFreq` `powf`).

- **Per control tick (96 samples):** mode frequencies, Q values and amplitudes
  are computed once from pitch, DETUNE (stretch), DECAY (damping → Q) and
  FILTER (brightness → q_loss), then cached. This is where the `powf` and the
  stretch-factor loop live — 24 modes' worth of coefficient math once per
  block instead of 96 times.
- **Per sample:** the batched band-pass recurrence and the amplitude-weighted
  sum. Nothing else.

Coefficients are linearly interpolated across the block toward their new
targets where a step would be audible (pitch, stretch); Q and amplitude step at
the tick like every other control-rate parameter in the engine.

A test counts coefficient recomputations per block and fails if the count
exceeds one per control tick — the same shape of guard the `svf_lp` work
earned.

### 5. Control mapping — strict reinterpretation

| control | SYNTH meaning | BODY meaning |
|---|---|---|
| SOURCE lane | osc morph + t²·DETUNE_MAX | **MATL** material morph (§3) + t²·spread (same formula) |
| FILTER lane + FILT | Svf cutoff 60 Hz–14 kHz | brightness 0..1 (log map of the same Hz value) — feeds the string damping filter *and* the bank's q_loss |
| ATTACK | env attack (% of cycle) | exciter length: 2 ms click ↔ bowed swell (tempo-coupled) |
| DECAY | env decay (× cycle) | damping — ring time follows the tempo; sets string damping and mode Q together |
| RESO | Svf resonance | **exciter character**, four zones (§2) |
| DETUNE | ±35 ct osc spread | inharmonicity: string spread × ~4 (up to ~140 ct) *and* mode-bank stretch — one "how broken is this material" axis |
| SUB | sub-sine level | **excitation bus level** (§6) — how open the body is to the room |
| CHOKE | drone release + retrigger pause | the same + **palm mute**: damping snaps high on both structures |
| PITCH / MOTION / LEVEL | (unchanged) | (unchanged: latch/track, pan fan + drift, master gain) |

Exact response curves (damping-vs-decay map, stretch taper, spread scale) are
tuning material for the listening pass.

**The chord layer survives.** With four voices intact, COLOR, chord slots and
stab humanization work on BODY — struck bell chords, strummed. This is
precisely what the §7 degradation ladder would spend, and the reason the ladder
is a fallback rather than the plan.

### 6. The excitation bus

Three sources, each tapped **one control block late** (96 samples ≈ 2 ms,
inaudible) so simultaneity is broken and deck order does not matter:

| source | tap |
|---|---|
| own FLUX tape | the part's echo playback signal, not the mixed output — `PartFx` hands the engine the previous block's tap |
| the other deck | the other part's dry output from the previous block — `Instrument` holds it, which makes the coupling symmetric and order-independent |
| audio input | `process_in`, already wired for the sampler |

Path: enabled sources summed → DC block → `fast_tanh` soft clip → SUB² gain
(max ≈ 0.5) → `set_excitation()` on every active voice, added to the
excitation input of both structures.

Self-oscillation (high SUB + echo feedback, or two BODY decks feeding each
other) is *intended* territory but bounded: soft clip, gain < 1, and the
body's own damping.

**SUB = 0 ⇒ the whole path is hard-gated, bit-exact off** — including the
cross-deck tap, so two BODY decks never couple unasked.

**Source selection is patch state, not a performance control.** Three
checkboxes in the per-deck context menu (Detune A/B precedent), stored in the
preset. On hardware this is an ALT gesture or simply preset data; the faceplate
gains nothing. This is the one place where the spec accepts a control that is
not on the panel, and it is confined to per-sound setup.

### 7. CPU budget & the hardware gate

Measured anchors (`docs/bench/2026-07-25-d294556.md`, run 1 — Daisy Seed,
480 MHz, block 96, dcache+icache, block budget 960,000 cycles):

| workload | cycles | % block |
|---|---:|---:|
| `synth_2x4` (8 voices, bare engine) | 338,360 | 35.24 |
| `wave_2x4` | 309,154 | 32.20 |
| `synth_4_voices` (one part) | 170,995 | 17.81 |
| `echo_walk_sram` (read + write + wrap) | 1,501 | 0.16 |
| `micro_powf` (one call site per sample) | 19,013 | 1.98 |
| `instrument_worst` (anchored, real callback) | — | 97.47 max |

Estimate, per voice per sample:

| component | cycles |
|---|---:|
| double Karplus string (2 ×) | 100–140 |
| 24-mode bank, coefficients on the control tick | ~240 |
| exciter + pan + mix + follower | ~25 |
| **total** | **365–405** |
| *for comparison: one measured SYNTH voice* | *445* |

Extrapolated over 8 voices × 96 samples: **`body_2x4` ≈ 280k–311k cycles ≈
29–32 % of the block** — at or below `wave_2x4` (32.2 %), below `synth_2x4`
(35.2 %). If it holds, BODY costs no polyphony at all.

That stays an *estimate* until measured. The gate:

- The bench firmware's workload list grows three entries: **`body_2x4`** (both
  parts, MATL at the modal end — the worst case, since both structures run),
  **`body_2x4_string`** (MATL at 0, the ablation that prices the bank), and
  **`inst_body_worst`** (BODY on both decks inside the full FX chain, bus hot).
- **Implementation starts only after the hardware bench has run** and confirms
  the workload fits alongside the full instrument. Same harness, same anchor
  calibration. (STRING precedent, unchanged.)

**Degradation ladder, if the bench refuses.** The mode count stays at 24; the
voice count gives: 4 → 2 → 1. Each halving of voices roughly halves the engine
cost. What is spent, in order: overlapping STEP notes first, then the chord
layer entirely (a 1-voice BODY makes COLOR meaningless on that deck). Mode
reduction is **not** the first lever — the user chose a rich bell at low
polyphony over four poor ones.

### 8. Memory

- Strings: `daisysp::String` holds 1024 + 256 floats ≈ 5 KB → 2 strings ×
  4 voices × 2 parts ≈ **80 KB static SRAM** — deliberately *not* SDRAM: the
  reads are random access, exactly the cache trap the research flagged. The
  delay-line floor (~47 Hz at 48 kHz) sits below the pitch contract (min
  110 Hz); the primitive's internal upsampler path stays unused.
- Mode bank: 24 modes × (2 state + 3 cached coefficient floats) ≈ 500 B per
  voice → ≈ **4 KB** across both parts. Negligible.
- Excitation bus: two one-block float pairs per part. Negligible.

Headroom check against the measured audition build (engine + audio, no UI
shell): SRAM 38,276 B of 261,408. The 84 KB fits with room, and SRAM data was
never the binding constraint — SRAM_EXEC (code) is, at 60.5 % in that build.

Desktop, VCV and firmware compile the same code — identical bits on every
platform.

### 9. Hosts & demo scenarios

- Scenario parser: `"body"`. VCV: engine button cycles test-tone → synth →
  sampler → wave → body (`EngineId` order); per-deck context menu gains the
  three excitation source checkboxes.
- `body_strum.json` — STEP + chord layer, MATL rising from string to bell
  across the render: plucked harp → prepared piano → struck bells (listening +
  regression anchor).
- `body_bow.json` — FLOW drone, bowed excitation, SUB sweep into tape
  self-oscillation, choked by CHOKE.
- `body_sympathetic.json` — deck A on the sampler, deck B on BODY with the
  cross-deck source enabled and no notes of its own: the bell answers what the
  other deck plays.

### 10. Testing

- **Templatization gate:** SYNTH **and** WAVE reference renders byte-identical
  after the `SynthEngineT<V>` lift.
- **Parity semantics:** the existing engine tests (FLOW drone, steal order,
  chords, CHOKE) run as a further instantiation against
  `SynthEngineT<BodyVoice>`.
- **Control-rate gate:** mode coefficients are recomputed at most once per
  `kCtrlInterval`; a counting test fails on regression.
- **Determinism:** same seed → bit-identical render on desktop and VCV.
- **Stability torture:** max inharmonicity + max SUB + max brightness + min
  damping + all three bus sources hot + BODY on both decks over a long render
  → bounded output, no NaN/Inf.
- **SUB-0 gate:** byte-identical to the bus compiled out, including the
  cross-deck tap.
- **Follower:** a voice frees after ringing out; a quiet strike is not stolen
  instantly; a palm-mute render shows the fast energy drop.
- **Tuning:** the fundamental (mode 1, and the string) tracks pitch within a
  few cents across the register at both ends of MATL.

## Out of scope

- FM bells (user decision — ZAP's corner).
- Sympathetic coupling beyond the two decks; per-string or per-mode outputs
  beyond the existing pan.
- A Rings/Torus port, Plaits' engine layer, bowed-exciter modeling beyond
  continuous excitation.
- New panel controls or parameter ids; exciter samples.
- Any change to GRIT/FLUX/reverb/master — every bus tap is read-only.
- Mode count as a runtime control (§7: the count is fixed, chosen by bench and
  ear; only the voice count degrades).
