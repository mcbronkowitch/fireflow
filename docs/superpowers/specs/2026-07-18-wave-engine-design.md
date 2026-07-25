# WAVE — PPG-style wavetable part engine

**Date:** 2026-07-18
**Status:** implemented and completed after the hardware bench (2026-07-25).
**Scope:** a third selectable part engine (`ENGINE_WAVE`) — a 4-voice wavetable
scanner behind the existing `IPartEngine`/`SynthEngine` semantics. One baked-in
curated bank, no new panel control, no new parameter id. No change to the mod
plane, FX, reverb, or the master chain.

## Problem

The SYNTH engine covers the analog-ish corner: polyblep morph sine→tri→saw→pulse
through an SVF. What's missing is the digital-glassy corner — PPG/Microwave
territory: bells, vocal formants, hollow resonant spectra, spectral junk — where
TIMBRE scans *through a bank of waveforms* instead of morphing between four
analog shapes. The SuperModulator lanes are the position modulator this class of
synthesis always wanted; the engine only has to hold still and be cheap.

CPU is the hard constraint: every published Daisy CPU figure proved unreliable
(bench-firmware spec, 2026-07-18), so this engine must be *structurally* cheaper
than SYNTH, and implementation waits for the on-hardware bench.

## Decisions (user)

- Character: **digital-glassy (PPG/Microwave)** — frame scanner, not additive,
  not phase distortion.
- Tables: **one curated bank, baked into the firmware** by a Python tool
  (`gen_panel.py` precedent). No SD, no runtime loading.
- Polyphony: **4 voices, full parity** with SYNTH (round-robin, steal, FLOW
  drone, chord layer, CHOKE hold).
- FILTER lane: **per-voice `spky::SvfLp`**, retained bit-identically from SYNTH
  (RESO/FILT knobs keep their meaning).
- Voice interior: **2 detuned table reads + sub sine** — DETUNE knob works as
  today; only the oscillator core is swapped.
- DSP core: **hybrid scanner** — continuous frame lerp (audible, must be
  smooth), nearest mip with a control-rate crossfade (inaudible, may be cheap),
  int16 tables.

## Existing infrastructure this reuses

- `engine/synth/synth_engine.*`, `engine/synth/voice.*` — the entire allocation
  machine: round-robin + oldest-steal, FLOW sustain/demote, drone promise,
  chord slots + stab humanization, CHOKE hold, velocity slew, control-rate
  cadence (`kCtrlInterval = 96`).
- `engine/synth/morph_osc.h` — the oscillator interface WAVE's core mimics:
  `init / set_freq / set_detune_cents / set_morph / reset_phase / process`.
- `engine/synth/env.h`, `spky::SvfLp`, `engine/util/fast_sin.h` (sub sine),
  `engine/mod/rng.h` (drift rates) — unchanged, one level above the osc.
- `host/render/scenario.cpp` engine parsing; VCV per-part engine button
  (`Spotymod.cpp` ENGINE_A/B); `docs` bench-firmware workload list.

## Design

### 1. Integration — swap the oscillator, keep the engine

`WtOsc` implements the exact `MorphOsc` interface; `set_morph(0..1)` means
*bank position* instead of shape morph — same range, same control-rate feed,
same TIMBRE mapping (position + t² · DETUNE_MAX detune spread).

- `Voice` becomes `VoiceT<OscT>` and `SynthEngine` becomes
  `SynthEngineT<OscT>` — pure type parameterization, no behavior change.
  `using Voice = VoiceT<MorphOsc>` / `using SynthEngine =
  SynthEngineT<MorphOsc>` keep every existing name and test working; the SYNTH
  reference renders must stay **byte-identical** after the refactor.
- The new engine is `SynthEngineT<WtOsc>` (`using WaveEngine = ...`). All
  allocation/FLOW/chord/CHOKE semantics are inherited, not copied.
- `EngineId` grows `ENGINE_WAVE = 3` because `ENGINE_SAMPLER = 2`. `Part` holds
  the third engine instance
  (tables are `static const`, shared; the instance itself is small — voices +
  bookkeeping). Scenario parser learns `"wave"`. VCV ENG is
  **Synth → Sampler → Wave**; saved values `0` and `1` remain compatible, and
  value `2` selects WAVE. No other surface.
- VOICE edit layer (ATTACK/DECAY/RESO/SUB/DETUNE/FILT) applies literally —
  the setters live in `SynthEngineT` and are engine-agnostic already.

New files: `engine/synth/wt_osc.h` (header-only core),
`engine/synth/wt_bank.h/.cpp` (generated int16 data + metadata),
`tools/bake_wavetables.py` (generator; output committed, build needs no
Python).

### 2. Bank layout

- **16 frames.** Base mip 1024 samples per frame; halved-length mip chain
  1024 → 512 → 256 → 128 → 64 → 32 → 16 (7 levels, one per octave of
  fundamental) ≈ 2032 samples/frame ≈ 4 KB int16 → **65,024 bytes**
  (**32,512 int16 samples**), `static const`.
- **Band-limiting happens at bake time.** Each mip level is spectrally
  truncated in the tool: a level of length N keeps partials through N/11,
  yielding caps of 93, 46, 23, 11, 5, 2, and 1 partials. This is the smallest
  uniform guard that keeps the images of 2-tap linear interpolation below the
  `-36 dB` acceptance threshold across every frame and the full admitted pitch
  range. The top mip is therefore fundamental-only, making WtOsc's existing
  `0.45 * sample_rate` cap honestly alias-safe without narrowing its API.
- int16 → float scaling is folded into the output gain (free).

### 3. WtOsc audio path

Per `process()` call, steady state:

1. Phase increment + wrap (float phase, as in `MorphOsc`).
2. Position ramp: one add per sample toward the control-rate target position
   (see §4).
3. Read: 2-tap linear interp in frame ⌊pos⌋ and frame ⌊pos⌋+1 at the current
   mip level; lerp by frac(pos) → **4 taps + 3 lerps**.

Mip selection runs at control rate: `level = clamp(floor(log2(f / f_base)))`.
On a level change the read crossfades old→new level over one 96-sample control
block (equal gain — the tables are near-identical below the truncation point);
only during that block does step 3 double. Pitch is latched at trigger in STEP,
so crossfades are rare; FLOW glides crossfade as they cross octaves.

No polyblep, no branches beyond the wrap and the crossfade flag, no libm in
the audio path. Structurally cheaper than `MorphOsc` (fast_sin polynomial +
branchy tri/saw/pulse + polyblep residuals).

### 4. Two custom details

- **Position ramp.** `set_morph` arrives once per 96 samples. Shape-morphing
  hid that; a frame scan can zipper. The position therefore glides linearly
  per sample to the new target across the control block (cost: 1 add). This is
  the only per-sample smoothing in the core.
- **No position wrap.** The bank is dramaturgically a *line* (dark → bright →
  digital); position clamps at both ends. TIMBRE extremes never jump across
  the bank seam.

Phase free-runs (no reset at trigger) — parity with `MorphOsc`. Sub sine,
detune split (± max/2 across reads A/B), drift LFOs, pan fan: all unchanged in
`VoiceT`.

### 5. Bank content — sweep dramaturgy

All frames are baked from harmonic recipes (per-partial amplitude + phase in
the Python tool — synthetic, deterministic, re-bakeable in seconds). Order is a
musical journey, because TIMBRE lanes will traverse it constantly:

| position | frames | character |
|---|---|---|
| 0.00–0.20 | 0–3 | sine → growing bell partials (odd/even mixtures) |
| 0.20–0.45 | 4–7 | vocal formants (ah → oh → eh) — the PPG sweet spot |
| 0.45–0.70 | 8–11 | hollow/resonant spectra (comb, fifth-heavy) |
| 0.70–1.00 | 12–15 | bright & digital: saw-adjacent → spectral junk, bit-organ |

The concrete recipes are **tuning material** — finalized in the deferred
listening test; the boundaries above are the intent, not a contract (same
philosophy as the DUST zone breakpoints).

### 6. Memory & data path

- The production bank is linked in `.qspiflash_data` at `0x90040000`.
  Keeping it in AXI SRAM overflowed that region, while mapped QSPI passed the
  hardware CPU gate; no SDRAM boot copy was needed.
- Desktop/VCV hosts compile the same generated `wt_bank.cpp` — identical bits
  on every platform.

### 7. CPU budget & the hardware gate

The direct-engine hardware gate passed. The bench firmware measures matched
`synth_2x4` and `wave_2x4` rows (two four-voice engines):

| run | `synth_2x4` average / maximum | `wave_2x4` average / maximum |
|---|---:|---:|
| 1 | 340347 / 346106 | 308497 / 312180 |
| 2 | 340342 / 346105 | 308503 / 311962 |

WAVE average and maximum are no greater than SYNTH in both runs, and WAVE
maximum is below the 960,000-cycle block budget. Both accepted captures have
identical unique 68-row sets and checksums and report the byte-verified QSPI
payload digest
`ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`.
Evidence: `docs/bench/2026-07-25-8c5f2e1.md` and
`docs/bench/2026-07-25-8c5f2e1.csv`.

### 8. Testing

- **Parity:** the existing SynthEngine semantic tests (FLOW drone, chords,
  steal order, CHOKE) run as a second template instantiation against
  `SynthEngineT<WtOsc>`. SYNTH reference renders stay byte-identical after the
  templatization (regression gate for the refactor itself).
- **Aliasing:** all 16 frames are covered immediately below and above every
  mip handoff, at every coherent DFT pitch through `0.45 * sample_rate`, and
  at that exact admitted ceiling; every case keeps alias energy
  `<= -36.0 dB`.
- **Continuity:** adjacent-frame control-boundary residual is `< 0.01` and
  active-mip retarget residual is `< 0.01`; position and mip ramps last exactly
  96 samples.
- **Determinism:** same seed → bit-identical render on desktop and VCV (int16
  tables are exact; no float bake drift).
- **Reference scenario:** a new `wave_formant_sweep.json` (TIMBRE lane
  traversing the formant zone in FLOW) as listening + regression anchor.
  Its final SHA-256 is
  `a2a2fdb22044a0554e08b4ce6145033a81f105d980bc3f125e01d7e5271dc651`.
  The preserved SYNTH reference SHA-256 is
  `659af928e1f273d9ba9619f9ad235844fec1c2277557ed81a0c2dc065c6eb336`.

## Out of scope

- SD/user wavetables, multiple banks, a bank-select control.
- Position wrap modes, internal position envelopes (the mod plane *is* the
  position modulator).
- Unison beyond the existing 2-read detune; mip-tilt filter coupling
  (considered, rejected in favor of plain SVF parity).
- Any panel/param change beyond the engine cycle that already exists.
