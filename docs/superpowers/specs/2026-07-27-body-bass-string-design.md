# BODY — the bass string (low-end fundament for the resonator)

An amendment to `2026-07-26-body-resonator-engine-design.md`. That spec stands;
this one adds a third structure to the voice and changes one exciter mapping.
Nothing in its §5 (control mapping) or §6 (the excitation bus) is reversed.
Section numbers below refer to this document unless the parent is named.

## Problem

BODY has no energy below the note it is playing, and at the bottom of its
range it does not even have much energy *at* the note. Both halves of that are
measured, not impressions.

Two renders, identical patch (MATL at minimum, SUB at 12 o'clock, every
modulatable target pinned), same note — the bottom of the pitch contract,
110 Hz. One with BODY on deck A, one with SYNTH:

```
                      BODY          SYNTH
RMS                -57.9 dBFS    -28.0 dBFS
peak               -30.9 dBFS    -16.4 dBFS

band energy, dB relative to the file's total
  20– 60 Hz          -37.1         -4.2      <- SYNTH's sub sine, 55 Hz
  60– 90 Hz          -38.4        -19.6
  90–130 Hz          -10.4         -2.2      <- the fundamental, 110 Hz
 200–400 Hz           -3.4        -40.8      <- where BODY's energy actually is
 800–1600 Hz          -7.4        -66.5
```

Roughly 38 % of SYNTH's energy at this note sits at 55 Hz, an octave below what
is being played, and all of it comes from `VoiceT`'s sub sine
(`engine/synth/voice.cpp:56,117-119`). BODY has nothing there and structurally
cannot: a resonator's lowest partial *is* the note, the pitch contract floors
at 110 Hz (`pitch_to_hz`, `engine/synth/synth_engine.cpp:15`), and BODY owns no
oscillator. Listening confirms the arithmetic — the engine reads as if a
high-pass sat across it. There is no high-pass. The only `DcBlock` in the FX
chain is on the tape tap (`engine/fx/part_fx.h:93`), and the one inside the
Karplus–Strong loop is at 1.6 Hz, which costs about 0.4 dB across a full
two-second ring at 110 Hz. What is heard is absence, not filtering.

The second half is a register tilt. The same voice, one octave apart:

```
harmonic     110 Hz     220 Hz
  h1         -6.4 dB     0.0 dB    <- fundamental is 6 dB under h3 at the bottom
  h3          0.0 dB    -7.4 dB
  h8        -10.3 dB   -26.0 dB    <- 16 dB more upper-harmonic content at the bottom
  h10       -11.0 dB   -29.9 dB
```

The low note is not a transposed copy of the high one; it is relatively
*brighter*, and its fundamental is not the strongest partial. The cause is a
mismatch of units. `Exciter::_recompute_filter` sets its click/noise low-pass
in absolute Hz (2–8 kHz, 1–10 kHz — `engine/body/exciter.h:150-159`), while the
string's damping filter is a fixed multiple of the fundamental
(`damping_f = frequency * 2^(damping_cutoff/12)`,
`engine/body/ks_string.cpp:112-113`). A 110 Hz string therefore gets roughly
twice as many harmonics driven into it as a 220 Hz string, at the same relative
damping. Adding an octave underneath a fundamental this weak would produce a
sub *plus* a thin note, so both are fixed here.

One further measurement, because it removes a candidate: SUB at 0 and SUB at
0.5 render identical spectra to two decimal places. `PartFx` forces
`_tape_tap` to exactly 0 whenever FLUX is not engaged
(`engine/fx/part_fx.h:89-92`), so on BODY the excitation bus — and with it the
SUB knob — is silent in any patch without FLUX. That is not a defect, but it
did mean the panel's one plausible low-end control was doing nothing here.

## Decisions (user)

- **A real octave below the note**, not merely a stronger fundamental. Weight
  comparable to SYNTH's means energy where SYNTH has it.
- **The octave is a third Karplus–Strong string, not an oscillator.** BODY's
  identity is "the engine with no oscillator"; a sine at f0/2 would end that
  claim for a feature that a string does better anyway.
- **Its level comes from the sounding pitch, not from a knob** — full at the
  bottom of the range, zero at the top, linear in pitch. This was the user's
  proposal and it dissolves a conflict rather than managing one: with no knob
  required, SUB never has to be re-assigned.
- **SUB stays the excitation bus level.** The parent spec's §6 is unchanged. An earlier draft of
  this spec moved the bus into the context menu to free SUB; the user rejected
  it, correctly — burying the level of the engine's most distinctive feature in
  a menu makes it unplayable, and the ALT layer that §6 names as the hardware
  route does not exist yet.
- **Panel unchanged**, keeping the strict-reinterpretation rule of the parent
  spec: zero new params, zero panel changes.
- **The third string is the default; the bench decides.** If the measured cost
  fails the gate in §4, the fallback is documented and not re-litigated.
- **In scope alongside it:** the exciter's bandwidth follows pitch.
  **Out of scope:** makeup gain and the FILTER response curve.

## Naming

**The bass string** — `_str_bass` in code, "bass string" in prose. Not "sub",
not "sub-oscillator". The name is the physical object it models: the wound
bass string of a piano, which is thicker, duller and rings under the note
struck above it. "Sub" would import an oscillator's connotations into an
engine that deliberately has none, and would collide with the SUB knob, which
means something else entirely on this engine.

## Existing infrastructure this reuses

| piece | how |
|---|---|
| `KsString` | the bass string is a third instance, unmodified — the port already runs its parameter block at control rate |
| `Exciter` | one instance still, feeding all structures; only its filter mapping changes |
| `BodyVoice::_apply_params` | already the control-rate parameter block; the pitch-derived gain and the bass string's parameters join it |
| `SynthEngineT<BodyVoice>` | untouched — no new voice-contract method, no engine-level state |
| `bench/workloads_body.cpp` | the CPU gate in §4 runs on the existing BODY workload rows |
| `host/render` | the A/B pair in §6 uses the existing scenario schema |

## Design

### 1. Signal path

```
              ┌──→ String A ──┐
Exciter ───┬──┤               ├── 0.5·(A+B) ──┐
  + bus    │  └──→ String B ──┘               ├── MATL ──┐
           ├──→ ModeBank ─────────────────────┘          ├──→ pan → vel
           └──→ Bass string  f0/2 ─── g(p) ──────────────┘
```

The bass string sits **after** the MATL morph, not inside the string leg. This
is the one placement decision that matters: inside the leg, the octave would
vanish at MATL = 1, exactly where the bell is and exactly where a physical
instrument has its hum tone. After the morph it carries across the whole
material axis, the way SYNTH's sub carries across every timbre.

It is driven by the same `drive` signal as everything else, which means the
excitation bus rings it too. Opening the body to the room now also sets the
bass string going — the behaviour of a piano with the dampers lifted. This
falls out of the structure; it is not extra machinery.

That a string and not a sine produces the octave is the better half of the
idea. A sine at f0/2 would only rumble. The string emits a whole series above
its own fundamental, and its second harmonic lands on f0 — filling precisely
the 110–300 Hz gap the Problem section measured.

**Parameters.** Frequency f0/2. Damping follows DECAY like the others.
Brightness runs below A and B's, because thick strings are duller and because
an undamped harmonic series from f0/2 would muddy the mids. Nonlinearity stays
at 0: the bass string is the plain curved-bridge string, it does not follow
MATL. The brightness offset is tuning material for the listening pass; that
there *is* an offset, and its direction, are not.

**No hum mode in the mode bank.** An earlier draft added a mode at f0/2 to the
bank so the octave would survive at MATL = 1. Placing the bass string after the
morph already does that, and two mechanisms for one result is one too many.

### 2. The pitch-derived gain

```
g(p) = 1 − p        p = normalized pitch, 0 at 110 Hz … 1 at 880 Hz
```

Linear in *pitch*, not in Hz. The contract is `110 · 8^p`
(`synth_engine.cpp:15`), so a linear-in-Hz ramp would put the crossover far too
high; linear in `p` spreads the fade evenly across the three octaves and still
leaves about half the gain at the middle of the range, so the middle register
gains fundament too rather than only the lowest notes.

**Derived from the sounding frequency, not from the TUNE control.** TUNE is
only a transpose offset summed upstream; the note that actually sounds comes
from the PITCH lane, plus TUNE, plus the quantizer and root. Keying the gain to
TUNE would give no bass to low notes played through the lane, and would leave
the gain wrongly high when TUNE transposes up. `BodyVoice` already receives the
sounding frequency in `set_pitch_hz`, so `p = log8(f / 110)` recovered there is
correct in all four cases at once. The logarithm runs in `_apply_params()` —
control rate, the same place `std::pow` already runs — and never per sample.

**At p = 1 the gain is exactly `0.0f` and the bass string is not summed.** This
is a hard gate in the shape the parent spec's §6 already uses for SUB, and it
makes the change provably additive downward: at the top of the range this
engine does what it did before. §6 below pins it as a property.

At the bottom note the bass string stands at 55 Hz — the same frequency
SYNTH's sub occupies at that note. The comparison target is hit exactly rather
than approximately.

### 3. Exciter bandwidth follows f0

`Exciter::_recompute_filter` currently derives an absolute cutoff from the RESO
zone. It will derive a **ratio to the fundamental** instead, which the exciter
can already see (`set_freq` stores it for the ping zone).

**The anchor is the decision; the ratios are tuning material.** Anchor: the
geometric middle of the pitch range (≈ 311 Hz) keeps today's timbre exactly.
That makes this a redistribution between registers rather than a global
brightness shift — whatever was tuned by ear in the middle of the range stays
where it was, the bottom gets darker and a stronger fundamental, the top gets
brighter. Against that anchor today's click zone (2–8 kHz) is ≈ 6.4–26 × f0 and
the noise zone (1–10 kHz) ≈ 3.2–32 × f0. The result is clamped below Nyquist
and above a floor, as now.

The sputter/ping zone is unaffected: it already tracks the fundamental.

### 4. CPU budget and the gate

Anchors from `docs/bench/2026-07-27-c3c0cdb-body.md`, Daisy Seed, block 96,
block budget 960 000 cycles:

```
body_2x4     2 decks x 1 voice                      295 724   (30.8 %)
synth_2x4    2 decks x 4 voices                     338 694   (35.3 %)

estimate: KS string after the port     ~216 cycles/sample
          third string, both decks     216 x 96 x 2   = +41 472   (+4.3 %)
body_2x4 with the bass string                       ~337 200   (35.1 %)
```

The estimate says a three-string BODY deck pair stays cheaper than a SYNTH deck
pair, by roughly 1 500 cycles. It is an estimate: the 216 figure is derived
from the documented "4.2× less" ratio in `engine/body/ks_string.h`, not from a
fresh measurement. So it does not decide anything.

**Gate.** `bench/workloads_body.cpp` on the Seed: a BODY deck pair must measure
at or below `synth_2x4` in the same run. This is the yardstick the parent spec
already set for BODY — it may not cost more than what it replaces.

**Fallback if the gate fails.** String B is re-tasked as the bass string
instead of a third string being added: zero added CPU, because the loop already
runs and is bit-identical to String A below MATL 0.75 anyway (identical
parameters with DETUNE and DRIFT at 0, and `_noise_amount` is 0 below
nonlinearity 0.75 — `ks_string.cpp:150-152`). The cost is that DETUNE then spreads the octave
rather than unison, which is a real change to a §5 meaning and defensible as a
sound — a detuned octave string is the piano — but it is a fallback, not the
preference. This follows the degradation ladder of the parent spec's §7, one
level lower.

**The pitch-derived gain does not lower the worst case.** Worst case is low
notes, where the bass string runs at full gain. The gate must be measured
there, not at a convenient pitch.

### 5. Memory

`KsString` holds `DelayLine<float,1024>` plus `DelayLine<float,256>` — 5 120
bytes. A third string is +5 KB per voice, one voice per BODY deck, so **+10 KB
across a 2×4 patch**. No SDRAM, no allocation change.

**One coupling to record, because it is invisible and would fail quietly.** The
delay line bounds the bass string's lowest pitch. `delay` is clamped to
`[4, kDelayLineSize − 4]` = `[4, 1020]` (`ks_string.cpp:96`), which floors a
string at 48000 / 1020 ≈ 47 Hz. The bass string at the bottom note sits at
55 Hz, needing 873 samples — inside the line, with about 15 % headroom. It
fits, and it fits *first*: anyone who later extends the pitch contract below
110 Hz hits this clamp on the bass string an octave before the main strings
reach it, and the symptom is a silently mistuned octave, not a failure. Extend
`kDelayLineSize` to 2048 at that point.

### 6. Testing

No checksum or byte-identity gates — this project's rule; renders are sanity
checks (see the parent spec's §10).

| kind | assertion |
|---|---|
| property | `g(p)` is exactly `0.0f` at p = 1, and the bass string is not summed there — the additive-downward guarantee |
| property | `g(p)` is 1 at p = 0 and monotonically decreasing between |
| property | the bass string's frequency is f0/2 across the whole pitch range |
| property | the bass string's requested delay stays inside the delay line at the lowest note (the §5 coupling, pinned so an extended range fails loudly) |
| property | the exciter's cutoff-to-f0 ratio is constant across the pitch range — the §3 fix, stated as the invariant it creates |
| property | `_apply_params()` still runs once per control tick, not per sample — the parent spec's §4 claim, which the new logarithm must not break |
| render | measurable energy in the 40–70 Hz band at the bottom note, against `synth_low` as the reference |

### 7. Hosts and scenarios

The two diagnostic renders built while measuring the problem become the
regression pair in `host/render/scenarios/`: `body_low.json` and
`synth_low.json` — identical patches at the bottom of the range, one per
engine, every modulatable target pinned so the engine is the only variable.
They are what the §6 render row reads, and what the listening pass A/Bs.

VCV needs no panel change. The bass string has no control surface.

## Out of scope

- **Makeup gain.** BODY measures −57.9 dBFS RMS against SYNTH's −28.0 at an
  identical patch. The user deferred this deliberately. Recorded here because
  it affects the acceptance of *this* change: until the 30 dB is closed, every
  A/B against SYNTH is judged at mismatched loudness, which flatters the
  louder engine.
- **The FILTER response curve.** The measured sweep is non-monotonic — FILTER
  0.2–0.6 puts the fundamental on top, while both 0.0 and 0.9 read thin
  (tilt +5.5 dB and +7.0 dB respectively, against −4.0 dB at 0.6). Worth its
  own pass; it is not a low-end job.
- **A hum mode in the mode bank.** Dropped by YAGNI — §1 explains.
- **Extending the pitch contract below 110 Hz.** Shared by every engine and by
  `TestToneEngine`; a change there is not BODY's to make.
