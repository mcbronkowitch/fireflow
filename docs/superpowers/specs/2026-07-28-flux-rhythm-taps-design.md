# FLUX rhythm taps — design

**Date:** 2026-07-28
**Status:** design approved, not yet planned
**Depends on:** `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md` (the BBD rewrite, branch `bbd-delay`)

## What this is

Two extra delay heads per part, placed by the *other* deck's rhythm, audible in
FLUX's output and — when they are sounding — the sole excitation for BODY.

This restores an idea, not an implementation. The tape-era `TapBank` did
exactly this and was deleted with the tape (`e004a3d`), because a BBD has no
read pointer to move. What went with it was the only thing in the instrument
that made one deck's *timing* articulate the other deck's *material*. The
excitation bus still has an "other deck" source (`Part::_other_deck_tap`), but
it carries the sibling's raw audio — continuous, not articulated. That is the
gap.

## Why it is cheap now, and was not before

The obvious way to get the idea back is to restore the tape delay alongside the
BBD behind a mode switch. Costed: **+3.88 MiB** (the tape needs 262,144 floats
per channel against the BBD's 8,192; sharing one buffer between two engines
still means allocating the larger), a mode switch on a panel that must stay
reducible to real hardware, two knobs that change meaning per mode, and the
restored tap bank on top. It also drags back `taps.{h,cpp}`'s tape-specific
machinery.

Own tap lines instead cost **64 KiB**, for the reason the BBD rewrite exists:
**delay length is clock rate, not buffer size.** A two-second tap does not need
a bigger buffer, only a slower clock. What it costs instead is bandwidth, since
bandwidth follows the clock — and that turns out to be a feature (§3).

## 1. Architecture

New `engine/fx/taps.{h,cpp}`. The name is reclaimed; one piece of the content
is not new.

### 1.1 `derive_offsets` returns verbatim

```cpp
void derive_offsets(const RhythmView& rv, int32_t max_offset, int32_t out[2]);
```

Restored from `main` unchanged. It is pure — a `RhythmView` and a length in
samples, nothing tape-specific — and it carries the two rules that were
expensive to arrive at:

- **The uniformity guard.** Evenly spaced taps *are* a delay; that diagnosis
  killed zone S twice. When both gaps fall within `kUniformTol` of their mean,
  the second is spread to `kUniformSpread × g0` (the MOTION lane's ×3/4
  polyrhythm) so the result limps instead of gridding.
- **Mute, never clamp.** An out-of-range offset silences that tap. Clamping
  would place two taps at the same position, turning a missing echo into a
  doubled one.

One deletion: the `static_assert` on `Flux::kMaxSamples` being a power of two.
It guarded `TapeTap`'s AND-masked read, and there is no masked read any more.

### 1.2 `TapBank` owns four `BbdLine` per part

Two taps × two channels. Each line is an independent head:

```
part send ─┬─→ BbdEcho   (main line: FEEDBACK, DRIVE, compander)  ──→ echo
           ├─→ BbdLine   clock set so its delay == gap[0]         ─┐
           └─→ BbdLine   clock set so its delay == gap[0]+gap[1]  ─┴→ taps
```

Parallel heads off the **same send**, not in series behind the echo. That makes
a tap what it was before: this deck's own delayed material, at a position the
neighbour chose. No feedback on the tap lines.

Each line's clock comes from the existing `bbd_clock_hz(offset_seconds,
kTapStages)`. Offsets arrive in samples from `derive_offsets`; seconds is
`offset / sample_rate`.

### 1.2.1 Where it lives, and who feeds it

`TapBank` is a member of `Flux`, not of `PartFx`. It has to be: §4.1 puts the
taps into the sum before `_mix_lin`, and that sum is computed inside
`Flux::process`. `Flux` gains `set_taps(float norm)` and
`set_rhythm(const RhythmView&)`, matching the shape of its existing setters.

**Buffers come from the host, exactly as the echo's do.** `Flux::kMaxSamples`
drives every allocation site today — `Spotymod.cpp:162`, `render/main.cpp:13`,
`bench/mem.cpp:36`, and `FxMem::echo` in `instrument.h`. A parallel
`Flux::kTapSamples = kTapStages / 2` constant and a parallel host-provided array
follow the same pattern. `Flux::init` takes the tap storage alongside the echo
storage; a null tap pointer disables the bank, the way a null echo pointer
already disables FLUX (`_buf_ok`).

**Offsets update at control rate.** `Instrument` already publishes
`rhythm(int p)` and already pushes cross-deck state once per control block
(`_other_deck_tap`, `instrument.cpp`). The rhythm push joins that existing
cadence — `derive_offsets` and the two `SetClock` calls it produces run there,
never per sample.

`BbdLine::SetClock` is a single assignment (`ticks_ = 2·hz/sr`) that touches no
line state, so re-pushing an unchanged rhythm is harmless. That it does not
reset the line is also precisely what §6 depends on: the stored charge stays put
while the clock moves, which is the pitch bend.

A parallel `FxMem` field (`float* taps[PART_COUNT][2][2]` — part, channel, tap)
carries the storage, alongside the existing `echo[PART_COUNT][2]`.

### 1.3 Rhythm source

`Instrument::rhythm(1 - p)` — the *other* deck's PITCH lane, symmetric in both
directions. `RhythmView` publishes `gap[2]` plus `valid`; `valid` is false until
three onsets have been seen, which is what makes the taps self-gating (§4).

`rhythm_view.h:13-18` anticipated this consumer explicitly ("should a future fx
consumer of RhythmView appear"). The layering rule it protects — the mod layer
must not include fx headers — is preserved: `taps.h` includes
`mod/rhythm_view.h`, never the reverse.

## 2. Memory

`kTapStages = 4096` → 2048 cells → 8 KiB per line.

| | lines | total |
|---|---|---|
| main BBD lines (today) | 4 | 128 KiB |
| tap lines (new) | 8 | 64 KiB |
| restoring the tape instead | — | 3.88 MiB |

On Daisy this lives in SDRAM (`bench/mem.cpp:36`, `DSY_SDRAM_BSS`) where it is
not a constraint. In VCV it is a per-instance member array; 64 KiB against the
~38.7 MB an instance already takes is noise.

## 3. Bandwidth is the price, and it replaces a control

Bandwidth follows the clock, so longer taps are darker:

| tap offset | clock | bandwidth |
|---|---|---|
| 0.25 s | 8192 Hz | 4.1 kHz |
| 0.5 s | 4096 Hz | 2.0 kHz |
| 1 s | 2048 Hz | 1.0 kHz |
| 2 s | 1024 Hz | 512 Hz |

The tape-era tap bank had a ROT knob whose whole job was to filter the taps
apart from the echo (`kLpOpenHz`/`kLpSplitHz`, `kHpOpenHz`/`kHpSplitHz`). The
per-tap clock now does that by physics. **ROT's filter spread is dropped**, not
ported — one less control, and closer to the part being modelled.

## 4. Controls

### 4.1 TAPS takes DRIVE's panel slot

`TAPS` occupies `{44.250, 89.400}` and `{169.110, 89.400}` — the positions DRIVE
vacates — same `WK_SMKNOB`, label `TAPS`, generated by `res/gen_panel.py`.
`PART_STRIDE` stays 23. STAGES is unaffected.

`0..1` maps to a gain up to `kTapGain = 0.7`, the value at which full tap level
sits at parity with a direct read (set by ear in the tape era; kept with its
rationale). Taps join **before `_mix_lin`**, so FLUX MIX remains the single wet
control — the same rule the tape-era `flux.cpp` stated. Equal-power spread
±22.5° (`kPanNear`/`kPanFar`) puts the taps beside the echo rather than inside
it.

### 4.2 DRIVE moves to the right-click menu

Same shape as `Detune A/B` (`Spotymod.cpp:1215-1229`): a real param with a
`ParamQuantity`, surfaced as a `ParamMenuSlider` in a submenu, with no panel
widget. Automation and patch storage are unaffected. New default **0.20**
(today: 0.15, `init_patch.hpp:81`).

The criterion is already written into the codebase, above the excitation-source
menu (`Spotymod.cpp:1230`): *"patch state, not a performance control — there is
no panel knob for it on any engine, so it lives here."* DRIVE qualifies. You set
it once; you do not ride it.

On hardware DRIVE becomes a setup value with no knob. Nothing depends on that
today — `set_drive` is called from bench, render and VCV, from no firmware host.

**No migration.** The project is in development and old patches are explicitly
not a constraint (owner's decision, 2026-07-28). The `host/vcv/README.md`
upgrade warning about DUST landing on DRIVE is removed rather than reworded.

## 5. Excitation routing

`PartFx::_tape_tap` carries:

- **the taps alone** while they are active — `TAPS > 0`, `rv.valid`, and at
  least one non-muted offset;
- **the echo**, as today, otherwise.

A resonator lives on transients. The point of the tap bank as an exciter was its
sparse articulation, and mixing the continuous echo back in would bury it. The
fallback exists so BODY does not go silent when the neighbouring deck rests.

`fast_tanh(_tap_dc.Process(...))` in `part_fx.cpp:72` is unchanged. It now bounds
two unbounded sources instead of one — the tap lines are no more bounded to unity
than `BbdEcho` is (`BbdLine` carries a DC feed-through term, `fout_->H == 1`).

### 5.1 Tap lines run without a compander — deliberately

`BbdEcho` wraps its line in the NE570 model. The tap lines do not. In a real
machine the companding wraps the delay path once, not each head, and a compander
per tap would cost more and give the taps different dynamics from the echo.

This is a voicing decision, not an implementation detail. It belongs on the ear
pass.

## 6. What a moving tap sounds like — a simplification the rewrite paid for

The tape-era `TapBank` needed dip-and-relatch: a 2 ms fade either side of an
offset jump (`kDipSeconds`, `kRelatchMin`), because moving a read pointer on a
tape clicks.

A BBD does not click. Changing its clock **bends pitch** — design claim 2 of the
BBD rewrite, confirmed by ear. A drifting tap offset therefore glides, exactly
as a RATE or STAGES change does on the main line. The entire dip machinery is
dropped.

## 7. Testing

### 7.1 Restored

`tests/test_taps.cpp` returns from `main` for `derive_offsets`: the uniformity
guard, mute-not-clamp, `kMinGap`, invalid rhythm. The function is unchanged, so
its tests are too.

### 7.2 New

- **A tap arrives where the neighbour's rhythm puts it.** Arrival time measured
  the way `test_flux.cpp`'s `first_echo_index` does, against `gap[0]` and
  `gap[0]+gap[1]`.
- **Symmetry.** A follows B and B follows A, in one case.
- **Self-gating.** No valid neighbour rhythm → measurably no taps. This is the
  property that makes the absence of a mute switch affordable.
- **Excitation switching.** Taps active → the bus carries them; TAPS at 0 → the
  bus carries the echo. Asserted on the signal's sparseness over a window, not
  on an instantaneous sample.
- **Boundedness.** Echo plus taps stays finite and within today's bounds.

### 7.3 Must stay green, and must stay load-bearing

The two witnesses in `test_part_fx.cpp` (soft clip, DC block). Their premise
already shifted once this week when a tuning change moved the loop's reachable
peak. Both must be re-proven RED with their stage removed, not merely observed
green.

### 7.4 Not replaceable by a test

A bench workload for the tap lines, then the CPU gate on hardware. See §8.

## 8. Risks

**CPU is the one that can kill this.** Eight tap lines on top of four main
lines. The gate is unmeasured and `instrument_worst` last sat at 97.5 % of the
block budget at maximum.

Two things ease it, neither of them a guarantee:

- A `BbdLine`'s tick loop runs `2·f_clk/fs` times per sample, so long taps clock
  slowly and cost little. The per-sample filter-branch work does not shrink with
  the clock, and that is the floor.
- At `TAPS == 0`, or with no valid neighbour rhythm, the lines do not run at
  all.

**The measurement comes before shipping, not after.** A cheap lever is already
identified if it is close: hoisting two per-sample divisions out of `BbdLine`
(~8 `VDIV.F32` per sample across the current four lines).

**Second risk: `kTapGain = 0.7` and the missing compander are both ear
decisions inherited from a different signal path.** They are starting points, not
settled values.

## 9. Out of scope

- Restoring the tape delay, in any form.
- A mode switch between delay models.
- Per-tap controls beyond the single TAPS level.
- Any migration path for patches saved before this change.
