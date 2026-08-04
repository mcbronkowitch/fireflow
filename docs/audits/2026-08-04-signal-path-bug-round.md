# Signal-path bug round — 2026-08-04

**Date:** 2026-08-04
**Status:** findings only, nothing fixed, nothing decided
**Scope:** the audio path from `Part::process` through `PartFx`
(GRIT → FLUX → FX MIX → COMP → send tap) into `Instrument::process`
(MORPH → bloom duck → reverb send/return → master limiter).
**Baseline:** 59e6dcd (`chore(release): bump plugin version to 2.18.1`)

Everything below was **measured**, not inferred from reading. Each finding
names the probe that produced its numbers; the probe sources are in the
appendix so the numbers can be reproduced or refuted. No source file was
changed to obtain them — the probes link against the engine as it stands.

Four defects and a handful of observations. Findings 2 and 3 share one root
cause. Finding 1 has the widest reach and is the only undefined behaviour in
the audio path.

---

## 1. `SoftSwitch` is hardwired to 48 kHz — OOB read at 44.1 kHz, click at 96 kHz

**Where:** `engine/fx/fx_util.h:82-106` (`SoftSwitch::process`),
`engine/fx/fx_util.h:27-34` (`hann_value_at`)

`init()` scales the step size with the sample rate — `_kof = 1/(0.004·sr)` —
but the iterator bound in `process()` is the constant `191`. The two only
agree at 48 kHz, where 4 ms *is* 192 samples. Away from that rate the ramp
either overruns the Hann table or never reaches its end.

`hann_value_at` clamps `npos` but **not `ipos`**, so an overrun is a read past
the end of the 192-entry static table, not a saturation.

Measured rise ramp (probe `sw2.cpp`):

| sample rate | largest single-sample step | highest table index touched |
|---|---|---|
| 44100 | **0.917** (ramp ends at 0.726, then jumps to 1.0) | **205** — out of bounds on a 192-entry array |
| 48000 | 0.008 | 189 ✅ |
| 96000 | **0.508** (ramp only reaches 0.492) | 94 |

At 44.1 kHz the last ~14 ramp samples are whatever floats follow the table in
memory, clamped into [0,1] by `process()`'s return. At 96 kHz the ramp is
mathematically fine but stops halfway and steps the rest.

**Reach.** Every switched block in the instrument:

- engine change — `Part::_engine_fade`, `engine/parts/part.h:444`
- GRIT on/off — `Grit::_sw`, `engine/fx/grit.h:73`
- FLUX on/off — `Flux::_sw`, `engine/fx/flux.h:53`
- sampler cut/overdub — `SampleBuffer::_cut`, `engine/sampler/sample_buffer.h:65`

44.1 kHz is a common Rack setting, so on those hosts every FX toggle and every
engine switch both clicks and reads out of bounds.

**Prior contact.** This exact out-of-bounds read has already been hit once:
`engine/sampler/sample_buffer.cpp:24-32` records it ("reproduced as an
out-of-bounds assertion under the MSVC STL") and fixes the *call site* by
adding the missing `_cut.init(sample_rate)`. That repair is correct as far as
it goes, but it addressed an uninitialised `_kof` of 1.0. The bound bug
survives a perfectly correct `init()` at any rate below 48 kHz.

**Test gap.** `tests/test_fx_util.cpp:19` exercises 48 kHz only.

**Inherited, not introduced.** `src/core/softswitch.h:56` and
`src/core/hann.h:22-32` carry the identical construction. On the Daisy the
rate was fixed at 48 kHz, so it never bit; the VCV plugin is where it does.

---

## 2. FLUX fades the wrong side of the tape line

**Where:** `engine/fx/flux.cpp:155-182`

`const float send = _sw.process();` scales only what goes **into** the delay
line (`_echo_l.Process(l * send, samples)`). The return is added ungated:
`l += wet_l * _mix_lin`. When the ramp finishes, `if (_sw.is_idle()) return;`
removes the return in one step.

So the 4 ms anti-click ramp gates a signal that is already in the past, and
leaves the one that is actually audible untouched.

Measured at 48 kHz — where finding 1 does *not* apply, so this is the fade
working as designed — with MIX 1.0, FB 0.5, a 200 Hz tone for 1 s, then
silence (probes `flux_probe.cpp`, `flux_on.cpp`):

- **Switching FLUX off:** the echo keeps playing at full level for the whole
  ramp, then steps from **−0.476 to 0.000 in one sample** at i=191. That is a
  full-scale click the ramp does nothing to prevent.
- **Switching FLUX back on**, after the block sat idle through 10 s of
  silence: the first sample out is **−0.466**, peak 0.610 over the next 2000
  samples. While idle `_echo.Process` is never called, so the line is frozen
  rather than decaying — turning FLUX on replays the take from ten seconds ago
  at full level and the 0.5 feedback keeps it alive.

**Contrast.** GRIT gets this right — `engine/fx/grit.cpp:99` crossfades the
*output*: `l = gl*k + l*(1-k)`.

**Test gap.** Every FLUX test uses `set_on(true, true)` (immediate). The
ramped path has no coverage. `tests/test_grit.cpp:103` does cover GRIT's.

---

## 3. Reverb clear-on-sleep amputates the tail — same shape as finding 2

**Where:** `engine/instrument.cpp:279-292`

The per-deck wet gains glide to zero over `kMixSmoothS` (10 ms). The instant
`wga` and `wgb` are both exactly 0, the room is cleared and `process()` is
skipped from then on. As in finding 2, the fade rides the **send**; the
**return** is cut, not faded — and a reverb return at that moment is a tail
seconds long, not a residue.

Measured against the real `AmbientReverb` with `instrument.cpp`'s own gain
logic replicated exactly (probe `rev2.cpp`), input going silent at the same
moment the knob moves:

| DECAY | MIX | cut lands | return level at the cut |
|---|---|---|---|
| 0.55 | 0.25 | 66 ms after the knob | −19.7 dBFS |
| **0.75** | **0.50** | 72 ms after the knob | **−10.5 dBFS** |
| 0.85 | 1.00 | 76 ms after the knob | −30.2 dBFS |
| 0.95 | 1.00 | 76 ms after the knob | −29.5 dBFS |

The two loud rows are the ones to read; the quiet ones are phase accidents of
where the wash happened to be when the cut landed, not evidence that a long
DECAY is safer.

Turning REVERB MIX down therefore kills the tail with an audible step about
72 ms later.

---

## 4. The COMP ceiling fires at knob 0.001

**Where:** `engine/fx/comp.cpp:70-72`

```cpp
const float cap = kEnvCeiling / std::max(_env, 1e-6f);
if (_gain_target > cap) _gain_target = cap;
```

The cap is applied unconditionally. It is not scaled by `_curve_amount`, so at
knob positions where the compressor is meant to be doing nothing it still
enforces a −8 dBFS envelope ceiling.

Measured with a 220 Hz tone at peak 0.8 (probe `comp_probe.cpp`):

```
COMP 0.000 -> out peak 0.8000   ( +0.00 dB)    bypass, bit-exact
COMP 0.001 -> out peak 0.4505   ( -4.99 dB)    <-- the first 0.1 % of the travel
COMP 0.010 -> out peak 0.4504   ( -4.99 dB)
COMP 0.050 -> out peak 0.4492   ( -5.01 dB)
COMP 0.200 -> out peak 0.4417   ( -5.16 dB)
COMP 0.500 -> out peak 0.4243   ( -5.51 dB)
COMP 1.000 -> out peak 0.4090   ( -5.83 dB)    <-- the whole rest: 0.8 dB
```

And the way back out is a step, not a glide. `Comp::process`'s disengage
branch (`comp.cpp:76-83`) snaps `_gain` to 1.0 the sample `engaged()` turns
false. Riding COMP from 0.5 to 0 on that same tone:

```
largest one-sample gain jump 0.4546 (0.5454 -> 1.0000, +5.3 dB)
```

This is the curve failure the limiter's own comment describes for the old
linear DRIVE law (`engine/fx/limiter.h:30-38`, "spent the whole knob in its
first fifth") — here compressed into the first thousandth, plus a click at the
bottom stop.

**Reachable from the panel.** COMP is a real knob (`generated_panel.hpp:144`,
`:167`), init defaults 0.630 / 0.561 (`init_patch.hpp:36`, `:59`), pushed at
`Spotymod.cpp:488`.

---

## Observations — not defects, but worth knowing

**Master DRIVE is the one unsmoothed parameter in the path.**
`Spotymod.cpp:712` pushes it on the `ctrlDiv = 16` raster (`Spotymod.cpp:263`)
and `Limiter::set_drive` (`limiter.h:39`) writes `_pre` directly. `_pre` spans
1…4, so a knob sweep steps the master gain every 16 samples. Everything else
that reaches the audio path is smoothed — LEVEL (~10 ms in `SynthEngineT`), the
five FX targets (2 ms in `PartFx`), MORPH (30 ms in `Center`), the reverb dry/wet
legs (10 ms in `Instrument`). DRIVE is the exception.

**Three level riders in series on the same signal.** The reverb's return
ceiling (`reverb.cpp:182-196`), the bloom duck (`instrument.cpp:139-155`) and
the master limiter (`limiter.h:45-61`) all act on the same sum. The duck's
envelope is `AmbientReverb::return_level()`, which is `_wet_peak * _lim_gain`
— a level the reverb's own ceiling has *already* pulled down. The harder one
rides, the less the other has to do. Each is individually justified in its own
comment block; the interaction between them is not written down anywhere, and
it is the most convoluted stretch of the path.

**The master limiter has no lookahead, and at DRIVE 0 its knee is narrow.**
A 1.4-peak tone starting after a −26 dBFS wash (probe `lim_probe.cpp`):
**24 of the first 400 samples sit at |out| ≥ 0.999** — the first crest is
flat-topped for roughly 0.5 ms while the follower (attack coefficient 0.05,
τ ≈ 20 samples) catches up. At DRIVE 0 the knee is `kKneeHi = 0.891` with a
transition only 0.109 wide, so anything the follower has not yet seen is hard
clipping in all but name.

This is the documented design (`limiter.h:16-22` voices the knee morph as a
feature), and it is bounded — `shape()` can never exceed 1.0. Noted here
because it is the most likely source of a "it crackles on transients" report,
and because it interacts with the by-ear finding already recorded in memory
that master DRIVE stops controlling dirt once the limiter rides.

---

## Suggested order, if any of this gets fixed

1. **Finding 1** — the only UB in the audio path, and the widest reach.
   Clamping `ipos` in `hann_value_at` removes the UB but leaves the click:
   the iterator bound has to come from the sample rate for the ramp to be a
   ramp at any rate but 48 kHz.
2. **Findings 2 and 3 together** — one root cause (fade the return, not the
   send), two call sites. Fixing 2 without 1 leaves FLUX still clicking at
   44.1 kHz through a different mechanism.
3. **Finding 4** — scale the cap by `_curve_amount` so knob 0 is inert, and
   glide out of the disengage instead of snapping.

Nothing here is decided. Findings 2 and 3 in particular change how the blocks
sound when switched, which is an ear call, not a code call.

---

## Appendix: probes

Sources are in `2026-08-04-signal-path-bug-round/probes/`. Built against the
tree at 59e6dcd, no source changes. From the repo root, with `env.sh` sourced:

```sh
D=docs/audits/2026-08-04-signal-path-bug-round/probes
I="-I engine -I third_party -I lib/DaisySP/Source"

clang++ -std=c++17 -O0 $I -o sw2.exe        $D/sw2.cpp
clang++ -std=c++17 -O1 $I -o flux_probe.exe $D/flux_probe.cpp engine/fx/flux.cpp \
                                            lib/DaisySP/Source/Utility/dcblock.cpp
clang++ -std=c++17 -O1 $I -o flux_on.exe    $D/flux_on.cpp    engine/fx/flux.cpp \
                                            lib/DaisySP/Source/Utility/dcblock.cpp
clang++ -std=c++17 -O1 $I -o rev2.exe       $D/rev2.cpp       engine/fx/reverb.cpp
clang++ -std=c++17 -O1 $I -o comp_probe.exe $D/comp_probe.cpp engine/fx/comp.cpp
clang++ -std=c++17 -O1 $I -o lim_probe.exe  $D/lim_probe.cpp
```

- `sw2.cpp` — `fx_util.h` only. Runs `SoftSwitch` at 44100/48000/96000 and
  reports the largest single-sample step of the rise ramp.
- `flux_probe.cpp` — `engine/fx/flux.cpp` + `dcblock.cpp`. Fills the tape line
  with a 200 Hz tone, switches FLUX off into silence, reports the step at the
  moment `is_idle()` takes over.
- `flux_on.cpp` — same link set. Same fill, switches off, waits 10 s of
  silence, switches on, reports the first samples and the peak of the
  resurrected tail.
- `rev2.cpp` — `engine/fx/reverb.cpp`. Replicates `instrument.cpp`'s wet-gain
  `OnePole` and equal-power law around the real `AmbientReverb`, sweeps
  DECAY/MIX, reports where the clear lands and at what return level.
- `comp_probe.cpp` — `engine/fx/comp.cpp`. Steady 0.8-peak tone, sweeps the
  COMP knob for steady-state output peak, then rides 0.5 → 0 and reports the
  largest one-sample gain jump.
- `lim_probe.cpp` — `limiter.h` only. Quiet wash, then a 1.4-peak onset;
  counts samples pinned at |out| ≥ 0.999.
