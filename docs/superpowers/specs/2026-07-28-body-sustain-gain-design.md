# BODY — the sustain-gain contract (levelling continuous excitation)

An amendment to `2026-07-26-body-resonator-engine-design.md`. That spec stands;
this one adds one method to each resonator and one derivation to the voice.
Nothing in its §2 (the exciter and its zones) or §5 (control mapping) changes,
and no control moves.

Section numbers refer to this document unless the parent is named.

## Problem

BODY in FLOW is far louder than BODY in STEP, and by an amount the player
cannot predict, because it is set by MATL and DECAY rather than by any level
control. Measured at engine level — the same patch, the same note, the only
difference the mode switch, peak of the settled second against the peak of a
struck note:

```
                          SYNTH          BODY
MATL 0, cycle 1 s, DEC 0.2    -2.8          +4.8
MATL 0, cycle 1 s, DEC 0.9    -2.6         +14.9
MATL 0, cycle 12 s, DEC 0.9   +1.6         +16.7
MATL 1, cycle 1 s, DEC 0.5    -2.6         +37.6
MATL 1, cycle 1 s, DEC 0.9    -2.6         +57.6
MATL 1, cycle 12 s, DEC 0.9   +1.6         +59.8
```

SYNTH stays inside ±3 dB across the whole space. BODY spans 55 dB, and at the
top a single voice peaks at **88.2** — 39 dB above full scale, before the FX
chain and before the second deck. What reaches the ear is therefore whatever
the master limiter leaves, which is why the engine reads as flat and squashed
in FLOW however the controls are set.

**The cause is structural, not a defect in any one part.** A struck note
decays; a continuously driven resonator accumulates until dissipation balances
the input. The ratio between the two is the structure's own gain, and BODY's
spans three orders of magnitude across MATL and DECAY. `VoiceT` does not have
this problem because FLOW there sustains an envelope over an oscillator, which
is bounded by the envelope.

**It is not specific to the bow.** Measured per RESO zone at the voice level,
FLOW steady state against the struck peak:

```
zone 0  click / bow       +10 .. +63 dB
zone 1  noise burst       +18 .. +32
zone 2  sputter           +22 .. +41
zone 2  pure ping         +18 .. +57
```

The noise zone is incoherent — it cannot pump a mode in phase — and is still
+18 to +32 dB. Coherence changes how steep the effect is, not whether it
exists. Any fix that targets the bow alone would leave three quarters of the
control untreated.

**Relationship to the bow branch.** `fix/body-flow-bow` (commit `7e7fc99`,
unmerged) repairs a separate bug: the click zone's bow re-fired on a fixed 5 ms
timer, exactly 200 Hz at 48 kHz, so it droned at 200 Hz whatever was played.
That fix is confirmed by ear and is *why* zone 0 now appears in the table above
at +63 rather than near unity — the old bow was quiet only because it was
mistuned and coupled badly. The two changes are independent, but shipping the
bow fix without this one would make zone 0 the loudest zone instead of the
tamest.

## Decisions (user)

- **Compress, do not flatten.** The 55 dB collapses to roughly **8–12 dB**. A
  bowed bell stays louder than a bowed damped string — that difference is
  expression and the user chose to keep it — but inside a musical range. Full
  levelling (the SYNTH-like ±3 dB) and a fixed offset were both offered and
  rejected.
- **Feed-forward from the resonator's own gain**, not a fitted table and not an
  output-referred regulator. See §5 for why the other two lost.
- **STEP is untouched.** Everything tuned by ear on struck BODY stays exactly
  as it is, provably rather than approximately (§3).
- **The excitation bus is not regulated.** SUB is a level the player sets, and
  a body driven into self-oscillation through it is the behaviour the parent
  spec's §6 intends and bounds. Only the exciter is regulated.
- **Panel unchanged**, keeping the parent spec's strict-reinterpretation rule:
  zero new params, zero panel changes.

## Naming

**Sustain gain** — `sustain_gain()` in code, "sustain gain" in prose. It is the
gain a structure applies to *sustained* excitation, which is exactly what
distinguishes it from the strike response everything else in this engine is
tuned against. Not "Q": the mode bank has many Qs and a string has none in the
textbook sense, so the word would promise a precision neither structure has.

## Existing infrastructure this reuses

| piece | how |
|---|---|
| `KsString::_iir_damping` | already holds the coefficient the loop gain is derived from; the derivation is closed-form and adds no state |
| `ModeBank` | already computes its mode coefficients on the control tick; the gain falls out of the same numbers |
| `BodyVoice::_apply_params` | already the control-rate block, and already holds `_mix_string` / `_mix_modal`; the combination joins it |
| `BodyVoice::_sustaining` | already tracks FLOW vs STEP (`set_sustaining`), so no new mode plumbing |
| `Exciter` | unchanged — the gain is applied to its output in the voice, not inside it |

## Design

### 1. The contract

Each resonator answers one question:

```cpp
// Amplitude gain from sustained excitation to the steady state this structure
// settles at, at the frequency it is currently tuned to. 1 means nothing
// accumulates; large means a long ring. Control rate.
float sustain_gain() const;
```

**`KsString`.** The loop is delay line → damping filter → dispersion allpass →
back. The allpass has unit magnitude by construction and the DC blocker sits at
1.6 Hz, so the loop's magnitude at the fundamental is the damping filter's:

```
steady = 1 / (1 - |H(f0)|)
```

`_iir_damping` is a `daisysp::OnePole` in TPT form with `g = tan(pi * fc)`,
`lp = (g*in + state) * gi`, so

```
              g * |1 + e^-jw|                     w = 2*pi*f0/sr
|H(f0)| = -------------------------------
          |(1 + g) + (g - 1) * e^-jw|
```

which evaluates in closed form as

```
num = 2 * g * |cos(w/2)|
den = sqrt( ((1+g) + (g-1)*cos w)^2 + ((g-1)*sin w)^2 )
```

Both `g` and `w` are already known at the point `set_params` computes them. The
identity checks: at DC the expression gives exactly 1, at Nyquist exactly 0.

**`ModeBank`.** No single closed form: the bank is several resonators and the
excitation is broadband. The declared gain is the **largest** individual mode
gain. This is an approximation and is written down as one — it bounds the worst
case, which is what the compensation needs, and it errs toward compensating too
much rather than too little.

### 2. Combining and applying

In `BodyVoice::_apply_params()`, weighted the way the two structures actually
sum — the MATL blend is equal-power, so the gains combine by power:

```
g        = _mix_string^2 * _str_a.sustain_gain()
         + _mix_modal^2  * _bank.sustain_gain()
_exc_gain = pow(g, -k)
```

`_str_a` alone stands for the string leg: A and B differ only by DETUNE's
spread, which moves f0 by at most 70 cents and the gain by far less than the
compression tolerance. Reading one string rather than averaging two keeps one
`pow` out of the control tick and states the assumption where it can be argued
with.

**k is the whole design in one number.** k = 0 leaves the level as it is, k = 1
flattens it completely, and in between the range compresses proportionally in
dB. For 55 dB down to 10, k = 0.82. It is tuning material and it is the only
constant a listening pass has to turn.

The gain reaches the signal in `BodyVoice::process()`:

```cpp
const float drive = _exciter.process() * _exc_gain
                  + (_sub > 0.f ? _excitation * _sub * _sub * 0.5f : 0.f);
```

### 3. Why STEP is provably untouched

`_exc_gain` is set to **exactly `1.0f`** whenever `_sustaining` is false — not
to a value that rounds to one. Multiplication by `1.0f` is exact in IEEE 754
for every finite operand, so a struck note is bit-identical to today's, and
`process()` needs no branch to guarantee it. §6 pins this as a test rather than
leaving it as an argument.

### 4. The edges

- **Ceiling on the declared gain.** `KsString` crossfades deliberately toward a
  lossless loop at damping ≥ 0.95 (`ks_string.cpp`, "crossfade to infinite
  decay"), where `1/(1-|H|)` diverges and the compensation would drive the
  exciter to silence. Both resonators clamp their declared gain to **1000
  (60 dB)**, which at k = 0.82 leaves the excitation at -49 dB rather than at
  zero. The number is tuning material like k, and it is a floor on the
  excitation, not a limit on the ring: the resonator still rings as long as it
  did, it is simply driven more gently.
- **Floor at 1.** The compensation may only attenuate. A structure reporting
  less than 1 is clamped up, so no parameter setting can make the exciter
  louder than it is today.
- **Pitch falls out.** The gain is evaluated at f0, so the ~5 dB-per-octave
  tilt measured across the pitch contract disappears without a rule of its own.

### 5. The two approaches that lost

Recorded so they are not re-proposed.

**A fitted compensation table** over damping x MATL x brightness x pitch. The
range to be fitted is 55 dB and strongly non-separable, and every change to the
resonators over the past weeks would have silently invalidated it. Rejected
before measurement was complete and the measurement did not rescue it.

**An output-referred regulator (AGC).** Self-calibrating, and it needs nothing
from the resonators. But its plant is the resonator, whose time constants reach
seconds at long DECAY; a stable regulator must be slower still, which leaves
the onset — the part that is played — unregulated, and invites pumping. It is
the documented fallback if `ModeBank`'s gain cannot be expressed usefully
enough to hold the §6 band.

**Jitter on the bow's re-arm** was measured too, as a way to break the coherent
pumping. It buys about 40 dB of worst case, saturates by 20 % jitter, leaves 24
dB of spread, is not monotonic (2 % jitter makes short-DECAY strings *louder*),
and smears the pitch the bow fix had just repaired. Not a lever.

### 6. Testing

No checksum or byte-identity gates against stored files — this project's rule.
Bit-identity between two builds of the same code is a different thing and is
used here.

| kind | assertion |
|---|---|
| property | FLOW-to-STEP peak ratio stays inside a stated band (target 4–14 dB) across MATL x DEC x RESO x pitch — the finding this whole document exists for, and the test that would have caught +59.8 dB |
| property | a struck note is bit-identical between a build at k = 0.82 and one at k = 0 — the §3 guarantee |
| property | `sustain_gain()` is finite and >= 1 for every damping, brightness and pitch in the contract, including the infinite-decay crossfade region |
| property | `sustain_gain()` decreases monotonically as damping rises |
| property | `KsString::sustain_gain()` matches a measured steady state within 1.5 dB at three pitches x three dampings — the closed form is only worth having if it is right, and the tolerance is wide enough for the DC blocker and the allpass, which the derivation treats as unity |
| property | `_apply_params()` still runs once per control tick, not per sample — the parent spec's §4 claim, which the new `pow` must not break |
| render | a FLOW scenario at MATL 1 and long DECAY stays below full scale |

### 7. Hosts

No panel change; the regulation has no control surface. The VCV build is how
the listening pass judges k.

## Out of scope

- **The excitation bus.** A user-set level, and self-oscillation through it is
  intended (parent spec §6).
- **STEP.** Untouched by construction, §3.
- **The FILTER loudness tilt** merged as `fcbd9a2`. It changes brightness, which
  changes damping, which changes the declared gain — so this design consumes
  its result, but the tilt itself is settled and confirmed by ear.
- **The FILT dead zone below -0.6** at a centred lane. Shared by every engine,
  rooted in the fade window's geometry and the documented invariant
  `kFiltLeftScale >= 1 + kFiltFadeRange`. Its own decision.
- **BODY's absolute level against SYNTH.** The struck peaks in the table above
  are 0.069 against 0.156, so BODY is quieter in STEP even while it is far
  louder in FLOW. Closing that is a makeup-gain question the bass-string spec
  already parked, and compressing FLOW does not settle it.
