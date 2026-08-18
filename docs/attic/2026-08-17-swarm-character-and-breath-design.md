# SWARM: character and breath

Design, 2026-08-17. Supersedes the voicing half of
`2026-08-17-swarm-additive-drone-engine-design.md`; everything that spec says
about the bank, the retarget slicing and N stands unchanged.

## 1. The complaint, and what measurement found under it

Bastian, by ear: SWARM always sounds the same, HARM sounds detuned at almost
every setting, and modulation does not make the engine breathe.

Four probes were run before any of this was designed. Setup for all of them:
seed `0xC0FFEE`, `set_seed()` before `init()`, 48 kHz, root 220 Hz
(`LANE_PITCH` = 1/3, since `pitch_to_hz(p) = 110 * 8^p`). Probe sources are
scratch files, not repo files; the numbers are reproduced here because they are
what the design rests on.

### 1.1 HARM has the wrong law, not the wrong setting

Deviation of each overtone from its pure position, DRIFT off, in cents:

| HARM | 2nd | 3rd | 6th | 12th |
|---|---|---|---|---|
| 0.0 | +0 | +0 | +0 | +0 |
| 0.2 | +140 | +222 | +362 | +502 |
| 0.4 | +280 | +444 | +724 | +1004 |
| 0.6 | +420 | +666 | +1086 | +1506 |
| 0.8 | +206 | +742 | +1496 | +1718 |
| 1.0 | −39 | +815 | +1423 | +2052 |

Two findings, both decisive.

**There is no gently-inharmonic zone.** At HARM 0.2 — one fifth of the travel —
the octave partial is a whole tone sharp. A real piano's 12th partial sits about
+50 cents; HARM 0.2 delivers ten times that. The cause is
`f = f0 * pow(n, 1 + beta)` with `kStretchMax = 0.35`, which is
`1200 * beta * log2(n)` cents: the HARM 0.6 row matches that formula to within
1 cent. The exponent form also bends the *low* partials hardest in absolute
terms, which is backwards — physical inharmonicity leaves the fundamental and
the octave nearly untouched and grows upward.

**Above 0.6 the knob stops being a scale.** The 2nd overtone reads +420 at
HARM 0.6, +206 at 0.8 and −39 at 1.0. Turning HARM up past the cluster
threshold does not produce *more* inharmonicity, it produces *differently
random* inharmonicity. The top third of the travel is not ordered.

Together these explain "few usable settings" exactly: the knob crosses from
pure to ten-times-a-piano inside 20 % of its travel and then wanders.

### 1.2 The engine cannot breathe, because nothing in it is slow

Steady-state RMS in FLOW, FLOOR 1, DRIFT off, across the **entire** travel of
each lane:

| Lane swept 0.0 → 1.0 | Level movement |
|---|---|
| TILT (`LANE_SOURCE`) | **0.57 dB** |
| FOCUS (`LANE_SIZE`) | 2.68 dB, V-shaped (quietest in the middle) |

A full-depth LFO on SOURCE moves the level by half a decibel. `_normalize_power()`
pins total power at every control tick, which was deliberate — "TILT, BAL and
FOCUS move colour and not level" — and the measured cost of that decision is
that spectral modulation produces no swell at all.

Direction reversals of the actual oscillator frequency, one partial, 60 s:

| MOTION | Excursion | Time between reversals |
|---|---|---|
| 0.25 | 5.5 cents | 0.027 s |
| 0.50 | 12.3 cents | 0.021 s |
| 1.00 | 29.4 cents | 0.018 s |

39–56 reversals per second across this range. DRIFT is not drift; it is a rough
random FM in the low audio range. MOTION changes only the depth, never the rate,
exactly as `_advance_drift`'s comment says. Apart from the bloom envelope,
**SWARM contains no process slower than 26 ms anywhere in MOTION 0.25–1.0.**
It cannot breathe because nothing in it is slow.

> **Audit note (fix round 4).** The table above was re-measured against the
> pre-task engine (45d2eaa) rebuilt from git, same lane settings, 5 s settle +
> 60 s, bank median over all `kPartials` slots: **5.500 c / 0.0254 s**,
> **12.209 / 0.0199**, **29.263 / 0.0180**. The excursions reproduce; the
> intervals run about 5 % faster than the 0.027 / 0.021 / 0.018 recorded here,
> and all three figures reproduce as *bank medians* rather than as the "one
> partial" the heading claims — slot 7 alone gives 5.348 / 11.904 / 28.633
> cents, up to 10 % away. Two claims in the prose were adjusted to match: the
> reversal rate is 39–56 per second, and the "no process slower than 27 ms"
> bound is the floor of **this table**, not of the engine. Below the table's
> lowest row the pre-task interval does lengthen — 0.0336 s at MOTION 0.15 —
> but 33.6 % of its control ticks are frozen there, so that is the rounding
> gap fix round 1 mistook for slowness, not a slower process. The diagnosis is
> untouched.

> Measurement note. The first version of the drift probe sampled
> `target_hz_for_test` every control tick and reported a reversal every 0.002 s.
> That was an artifact: `_advance_drift` runs only on a partial's own retarget
> slice, while `_rebuild_targets` rewrites the whole target array every tick, so
> sampling the target alternates drifted and undrifted values and manufactures
> one reversal per tick. The table above reads `partial_hz_for_test` — the
> bank's own frequency, which is what is audible.

### 1.3 FLOOR is not dead, but it is the only control that is not colour

RMS of seconds 7–8, after the bloom has finished:

| | FLOOR 0 | FLOOR 1 | Difference |
|---|---|---|---|
| STEP, FALL 0.1 | silence | −16.1 dB | full authority |
| FLOW, FALL 0.1 | −34.5 dB | −16.1 dB | 18.4 dB |
| STEP, FALL 0.5 (default) | −59.8 dB | −16.1 dB | 43.6 dB |

FLOOR has real authority. But in FLOW — where the factory patches boot — it is
a pure level control across 18 dB, touching no colour. Of SWARM's six VOICE-row
slots it is the only one that shapes nothing, which is why it is the one that
gives up its panel place.

## 2. What changes

Three things, in the order they matter:

1. **CHARACTER** takes the RES slot: four spectral laws instead of one, so the
   engine has more than a single family of sounds.
2. **HARM is rescaled and re-pointed**: it keeps one meaning — *how far from
   pure* — and each character decides where "away" points. Its full travel
   becomes usable in all four.
3. **Three breathing mechanisms**: a slewed normalizer, a spectral stagger on
   the modulation lanes, and a moving stereo image. Plus MOTION gains a time
   base.

**FLOOR loses its knob** and folds into the top of FALL.

Nothing in `SwarmBank` changes. The hot loop, N = 14, the retarget slicing and
the measured 95.67 % / 102.03 % CPU rows are untouched by design — every change
here lives in `_rebuild_targets`, `_control_tick` or the setters.

## 3. Control map

| Panel slot | Today on SWARM | After |
|---|---|---|
| ATTACK | RISE | RISE (unchanged) |
| DECAY | FALL | FALL, with a drone zone at the top (§6) |
| **RES** | **FLOOR** | **CHARACTER** (4 zones, §4) |
| SUB | SUB | SUB (unchanged) |
| DETUNE | HARM | HARM, rescaled and character-dependent (§5) |
| FILT | BAL (even/odd) | BAL (unchanged; LADDER and VOWEL only, §4.5) |
| `LANE_SOURCE` | TILT | TILT (unchanged) |
| `LANE_SIZE` | FOCUS | FOCUS (unchanged; VOWEL reinterprets it, §4.2) |
| `LANE_MOTION` | DRIFT depth | DRIFT: breath → shimmer (§7.4) |
| `LANE_PITCH` | root | root (unchanged) |
| `LANE_LEVEL` | level | level (unchanged) |

CHARACTER is stepped, and that costs nothing: the modulation lanes reach only
SOURCE / SIZE / PITCH / MOTION / LEVEL, never the VOICE row, so a VOICE-row
parameter is static per patch whatever its shape.

### 3.1 The RES slot's range disagrees with itself, and CHARACTER sits on it

`param_table.h:89` gives `P_RES_A` / `P_RES_B` the engine range **0 .. 0.75**.
Neither shipping surface honours it:

- VCV configures RES through the generic `configParam(c.id, 0.f, 1.f, init, lbl)`
  branch (`Fireflow.cpp:436`) and `pp()` hands the raw knob value to
  `set_voice_resonance`, so the knob reaches 1.0.
- The render host's scenario reader calls `inst.set_voice_resonance` directly.
  `swarm_drone.json:13` already sets 0.8, and `sampler_extremes.json` sets 1.0
  deliberately.

`apply_param` — and therefore the 0.75 clamp — is reached only from `tests/` and
`bench/`. So 0..1 is the real reachable range on both surfaces, the §1.3
measurement at FLOOR 1.0 is a reachable state, and CHARACTER's four 0.25 zones
fit the knob as it actually behaves.

The table range is still raised to 0..1 as the first task of the plan, because
otherwise CHOIR is unreachable from `test_param_impact` and `test_param_table`
and one whole character would go untested on the path those two guard. This
0.75 table ceiling was never where a by-ear resonance decision lived: there is
no single by-ear cap on this signal. Downstream of `Part::set_voice_resonance`
(`part.h:184`), `VoiceT::set_resonance` clamps the synth/wave voices to 0.95
(`voice.cpp:91`) and `SamplerEngine::set_resonance` clamps to 0.90
(`sampler_engine.cpp:1011`), both chosen by ear; BBD, Body and Swarm carry no
by-ear ceiling at all and already ran the full 0..1 this setter passes
through. Raising the table's own range touches none of those five clamps.

## 4. CHARACTER: four laws

Four zones of 0.25 on the RES knob, **with hysteresis**, following
`ChordBuilder::set_color`'s existing zone idiom (`part.h:344, 708`). The
hysteresis is not decoration: `fireflow-choke-silences-a-deck` records a
five-zone control drawn continuously that muted a whole deck, and a boundary
that chatters under a slewed control value is the same failure.

Zone boundaries at 0.25 / 0.50 / 0.75, hysteresis width `kCharHyst` (by ear,
start at 0.02).

### 4.1 LADDER (0.00–0.25) — the repaired today

Partials sit on the pure overtone series, `f_n = n * f0`, which is exactly
today's HARM 0 behaviour and stays the reference sound.

HARM applies **physical inharmonicity** instead of the exponent:

```
f_n = n * f0 * sqrt(1 + B * n^2)
B   = kInharmMax * harm^2
```

`kInharmMax = 0.0013`. The square on `harm` is what puts the piano range in the
middle of the travel instead of the first hair of it. Printed from the law:

| HARM | B | 2nd | 3rd | 6th | 12th |
|---|---|---|---|---|---|
| 0.00 | 0 | +0.0 | +0.0 | +0.0 | +0.0 |
| 0.25 | 0.000081 | +0.3 | +0.6 | +2.5 | +10.1 |
| 0.50 | 0.000325 | +1.1 | +2.5 | +10.1 | +39.6 |
| 0.75 | 0.000731 | +2.5 | +5.7 | +22.5 | +86.7 |
| 1.00 | 0.001300 | +4.5 | +10.1 | +39.6 | +148.5 |

A concert piano sits near +50 cents at the 12th partial, which lands at HARM
0.55 — mid-knob, with usable resolution on both sides of it. The top of the
travel is three times a piano, which is the bell-like end, and one tenth of what
the same knob position produces today.

What this fixes: at HARM 1.0 the 2nd partial moves +4.5 cents. Today it moves
+420 cents at HARM 0.6. The low partials — the ones that carry pitch — stop
moving, which is the whole complaint.

### 4.2 VOWEL (0.25–0.50) — formant pair

Partial *positions* stay harmonic. What changes is the amplitude law: instead of
`_focus_weight`'s single raised-cosine window, the amplitude is the maximum of
**two** raised-cosine peaks at F1 and F2.

HARM sweeps a vowel path through the classic pairs:

| HARM | vowel | F1 | F2 |
|---|---|---|---|
| 0.00 | /u/ | 300 | 870 |
| 0.25 | /o/ | 500 | 1000 |
| 0.50 | /a/ | 730 | 1090 |
| 0.75 | /e/ | 530 | 1840 |
| 1.00 | /i/ | 270 | 2290 |

Linear interpolation in log-frequency between the five anchors. Peak width
`kVowelWidthOct` (by ear, start 0.45 octaves) — wider than a real formant
because 12 partials at a 220 Hz spacing cannot resolve a 100 Hz bandwidth, and a
peak narrower than the partial spacing turns the vowel into an amplitude
lottery.

FOCUS keeps both halves of the meaning `_focus_weight` already gives it, applied
to the pair instead of to one window. Its **side** (which way off 0.5) shifts
both formants together in octaves — the singer's size. Its **distance** from 0.5
scales the gap between F1 and F2: at 0.5 the pair sits at the tabled spacing,
and toward either end the two peaks close on each other, which darkens the vowel
toward a single resonance.

This is the character that gives SWARM a vocal register, which nothing else on
the panel can reach.

### 4.3 BELL (0.50–0.75) — modal ratios

Partials leave the overtone series for the free-bar / tubular mode set, whose
ratios are `((2k+1)/3)^2`: 1, 2.78, 5.44, 9.00, 13.44. These are the ratios that
make a struck bar sound struck.

HARM blends from harmonic to modal:

```
f_k = f0 * lerp(k, mode_ratio(k), harm)      // in log-frequency
```

so HARM 0 in BELL is identical to LADDER at HARM 0 and the zone boundary is
audible only when HARM is up.

**The mode set is truncated, and the truncation point is measured.** The
quadratic law reaches 51× f0 at the 12th partial — at the top of the played
range that is far past Nyquist, and `kMaxHzFrac` would mute it. Measured at
f0 = 880 Hz (`LANE_PITCH` = 1.0) against the 21 600 Hz mute threshold:

| Continuation above mode … | Partials muted at the top note |
|---|---|
| no truncation (pure quadratic) | 5 of 12 |
| mode 6, local-spacing continuation | 1 of 12 |
| **mode 5, unit-ratio continuation** | **0 of 12** |

So `kBellModes = 5`, and slots above it continue at **unit ratio spacing**
(`ratio += 1` per slot), which is harmonic spacing at that height. The 12th
partial then lands at 20.44 × f0: 2249 Hz at the bottom of the played range,
17 991 Hz at the top, audible at both ends.

This is the physical shape as well as the affordable one — the inharmonic strike
character of a bell lives in its low modes, and real bells carry dense,
near-harmonic upper partials above them.

This is where large detuning is the *purpose* rather than a defect — which is
what makes today's HARM top end usable instead of discarded.

### 4.4 CHOIR (0.75–1.00) — the literal swarm

Partials collapse onto the chord tones. Slot k takes tone `k % nch`, and the
members of each tone's group alternate between the tone's 1st and 2nd harmonic
so the group has body rather than being a pure unison. Each member is detuned by
its own seeded offset:

```
f = tone_hz * octave_member * (1 + _spread[slot] * kChoirCentsMax * harm / 1200)
```

`kChoirCentsMax = 50` cents. At 220 Hz a 25-cent offset beats at about 3.2 Hz —
chorus, not vibrato.

HARM here is the spread in cents: 0 is a hard unison (a single loud sine per
tone), 1.0 is a wide, slowly beating choir. This is the character the engine's
name promises and the one no other FireFlow engine can produce — SYNTH and WAVE
spread oscillators, but not fourteen of them across a chord.

### 4.5 What BAL does in each character

BAL (the FILT slot) multiplies even overtones, which is meaningful only where
there *are* even overtones. It stays live in LADDER and VOWEL, and is inert in
BELL and CHOIR, where the partials are not an overtone series at all. Inert, not
repurposed: a control that changes meaning four times is a control nobody
learns.

## 5. HARM, unified

One meaning across all four characters — *how far from pure* — with the
character deciding the destination:

| Character | HARM 0 | HARM 1 |
|---|---|---|
| LADDER | pure series | +150 cents at the 12th (bell-like piano) |
| VOWEL | /u/ | /i/ |
| BELL | pure series | full modal set |
| CHOIR | hard unison | 50-cent choir spread |

HARM 0 is the pure harmonic series in three of the four, so the knob always
starts from the same known place.

The `kHarmClusterStart` / `kStretchMax` / `kClusterSpan` arc is deleted. The
seeded `_spread[]` array survives — CHOIR uses it for the detune offsets, and it
keeps its "drawn once at init and reseed" property, so a chord change is still a
glissando and NEW still redraws the individual.

The fundamental exemption (`if (over > 1)`, `swarm_engine.cpp:162`) becomes
unnecessary: none of the four laws displaces the fundamental. It is deleted with
its comment, and the finding that produced it stays recorded in
`docs/engine-map.md` §9.

## 6. FLOOR folds into FALL

FLOOR's knob is gone. FALL carries both:

```
fall_n  < kDroneStart : fall = lerp(kFallMinS, kFallMaxS, fall_n / kDroneStart)
                        floor = 0
fall_n >= kDroneStart : fall = kFallMaxS
                        floor = (fall_n - kDroneStart) / (1 - kDroneStart)
```

`kDroneStart = 0.75` (by ear). The existing FALL range survives intact in the
lower three quarters; the top quarter turns a very long decay into a standing
drone, which is the gesture a drone player reaches for anyway.

FLOW's `kFlowFloorMin` floor still applies on top, unchanged, so the drone
promise holds wherever FALL sits.

Measured cost of the fold, from §1.3: a **short** bloom with a **loud** standing
drone behind it is no longer buildable. Drone with long decay is. This was
Bastian's call with the measurement in hand.

## 7. Breathing

### 7.1 A slewed normalizer

`_normalize_power()` stays, but its gain `g` passes through a `OnePole` with a
long time constant `kNormSlewS` (by ear, start 0.7 s) before it multiplies the
targets.

This separates two things that are conflated today. Turning a knob slowly stays
level-matched, because the slew catches up. An LFO at 0.3 Hz produces a real
swell, because it does not. The normalizer keeps doing its job — no setting is a
volume control — while modulation regains the dynamic that §1.2 measured away.

The risk is real and named: the deck can now get genuinely louder under fast
modulation. Gate G-F bounds the steady-state span; the part limiter bounds the
rest.

### 7.2 Spectral stagger on the modulation lanes

SWARM already owns this mechanism, for the envelope only: `_env_ring` plus
`_apply_stagger` (`swarm_engine.cpp:349-363`) keep a ring of past control-tick
values and let every partial read it at its own lag, so the bloom reaches the
low partials first.

Do the same for `LANE_SOURCE` and `LANE_SIZE`. Each partial then sees the lane
value as it was some ticks ago, with the lag rising with partial index. A
coherent LFO stops being a block shift and becomes a **wave travelling up the
spectrum**. This is the largest single breathing win, and it is an existing
shipping mechanism applied at a second site rather than a new one.

Sizing: the bloom ring spans 12 ms; this wants 0.2–2 s. Storing every 4th
control tick gives 250 floats per lane for 2 s — 2 KB for both lanes, at an 8 ms
lag resolution that is far finer than the effect needs.

Maximum lag `kModStaggerS` (by ear, start 0.9 s), scaled by MOTION so a static
patch is unaffected.

### 7.3 A moving stereo image

`_t_pan[slot]` is a static ramp over the slot index today and never moves. Add a
third bounded random walk per partial, `_pan_walk[]`, beside the existing
`_det_walk[]` and `_amp_walk[]`, advanced on the partial's own retarget slice
and scaled by WIDTH. `SwarmBank` already glides pan for free — it is folded into
`al`/`ar` — so this costs 14 floats and one RNG draw per slice.

> **Correction (fix round 4).** "Scaled by WIDTH" was not enough, and taking it
> literally is what left the image moving at MOTION 0 for three rounds. The
> walk is scaled by **WIDTH × `excursion_depth_of(MOTION)`**: WIDTH sets how
> wide the image is and still gates a mono patch to mono, MOTION sets whether
> it moves at all and stops it exactly at 0. The walk also shares its step
> with the pitch and amplitude walks, so `kDriftStepDepthBoost` widens it at
> the top of MOTION by 1.43×. §7.4's fix round 4 correction has the
> measurements and the reason the wide top is kept.

### 7.4 MOTION gains a time base

MOTION becomes a **breath → shimmer** axis instead of pure depth:

- Bottom of the travel: slow — reversal interval well short of literal
  "1–3 s" (see the corrections below for the measured number, the tradeoff
  that stops it there, and what was therefore not bought).
- Top of the travel: fast — today's measured behaviour, which is a usable
  shimmer once it is a *choice*. Delivered: 28.0–30.5 cents at MOTION 1.0
  against 27.2–29.8 measured on the pre-task engine at the same settings
  (fix round 3's own eight-seed bank-median measurement, re-measured in fix
  round 4; §1.2's own figure is 29.4 cents for one partial).
- **Excursion is no longer a separate "wide"/"narrow" axis riding on the
  same knob as rate.** The original two bullets here promised "wide" at the
  bottom and "narrow" at the top; the shipped code did the reverse (depth
  multiplies the excursion at the very end, so a low MOTION setting produced
  the SMALLEST swing, not the widest — re-measured on the pre-task engine in
  fix round 4: **3.145 cents** peak-to-peak at MOTION 0.15 against 29.263 at
  MOTION 1.0, i.e. **9.3× narrower** at the bottom than at the top. This line
  said "0.94 cents, ~20× narrower" for two rounds; 0.94 is fix round 1's
  frozen code, which measures 0.885 at the same setting, not the pre-task
  engine's 3.145). Fix round 2 corrects the CODE to match the
  original intent instead of the text: `kDriftDepthFloor` floors the
  excursion at full strength above `kDriftFloorRampEnd` (0.01), fading in
  from zero below it, so excursion is close to constant across the travel and
  MOTION is a rate axis nearly alone. See the correction below for the
  measured excursion this produces.

Mechanically — **corrected in fix round 4**, because the sentence that stood
here described a design fix round 2 replaced — MOTION's rate axis is `H`, the
number of retargets a random walk target is held for (`kDriftHoldMax`).
`kDriftWalkStep` and `kDriftPull` are unconditional constants at their pre-task
values and are *not* functions of MOTION; the only depth scaling left on the
step is `kDriftStepDepthBoost`, which widens the excursion at the top of the
travel and does not move the rate. What MOTION scales at the output is the
excursion, through `excursion_depth_of()` — one shape shared by the pitch, the
amplitude and (since fix round 4) the pan walk, exactly 0 at MOTION 0, which is
what keeps MOTION 0 static (today's G27). MOTION is a lane, so the breath rate
is itself modulatable. The fix round 2 and 3 corrections below say the same
thing; this paragraph disagreed with its own corrections for two rounds.

> **Correction (fix round 1, post-implementation) — SUPERSEDED, kept for the
> record.** Fix round 1 corrected the "1–3 s" claim above to a measured
> "0.357–0.382 s" and attributed a per-slot spread to floating-point rounding
> near a smoothed signal's turning points. Both numbers were real
> measurements of a real phenomenon, but fix round 2's review showed the
> phenomenon itself was a bug: fix round 1 had shrunk `kDriftStepSlow` far
> enough that `SwarmBank::set_target`'s `inc_slope` fell below the float32
> ULP of the phase accumulator it feeds, so the accumulator FROZE (measured:
> 93.9–100% of control ticks bit-identical to the previous tick across
> MOTION 0.02–0.15) rather than moved slowly. The "0.357–0.382 s" fix round 1
> reported was the gap between rounding events on a frozen signal, not
> motion — proved decisively by `kDriftStepSlow = 0` (no random input at all)
> passing fix round 1's own gate with MORE margin than the shipped value. See
> the fix round 2 correction immediately below for what replaced this.
>
> **Correction (fix round 2, post-implementation).** MOTION's time base is no
> longer `kDriftWalkStep`/`kDriftPull` scaled by depth (deleted); it is `H`,
> the number of retargets a randomly-drawn walk target is held for before the
> walk draws a fresh one and interpolates toward it (`_advance_drift`,
> `swarm_engine.cpp`; `kDriftHoldMax`, `swarm_config.h`). `H = 1` at MOTION 1
> is a fresh draw every retarget, the pre-task cadence; `H` grows toward
> `kDriftHoldMax` as MOTION approaches 0. (This paragraph claimed `H = 1`
> "reproduces the pre-task shimmer exactly" for one round. It did not — see
> the fix round 3 correction below.)
>
> `kDriftHoldMax` is NOT chosen to hit 1–3 s. A large `H` reaches that range
> (measured: `H = 256` gives a 2.31 s bank-median reversal interval) but does
> so mostly by re-creating fix round 1's own freeze: dividing an already-small
> excursion into 256 per-retarget slices makes most of those slices sub-ULP
> again (measured: 93% of control ticks frozen at `H = 256`, barely better
> than fix round 1's 93.9–100%). `kDriftHoldMax = 32` is chosen instead on the
> frozen-tick fraction: measured across the actual MOTION range this axis
> covers (0.02/0.05/0.10/0.15), it holds at 46.72/44.72/42.45/39.04 %, i.e.
> **39–47 %** frozen (this line said "42–48 %" for two rounds; re-measured in
> fix round 4 on the code as it stands, and `swarm_config.h` has always said
> 39–47) — MOST ticks show
> real motion, a plain and checkable contrast with fix round 1's near-total
> freeze. `swarm_config.h`'s comment above `kDriftHoldMax` has the full H
> sweep, both statistics (frozen fraction AND interval) at each point.
>
> **One partial is not a sample of the bank**, and the per-slot spread's
> mechanism is also corrected from fix round 1's account: it is the float32
> BINADE POSITION of a partial's own `inc = hz / 48000` in `SwarmBank`, not
> an octave-vs-non-octave rounding argument — fix round 1's octave experiment
> was real but was run on a mostly-frozen signal, so the mechanism it
> inferred could not have been the true one. `swarm_config.h`'s comment above
> `kDriftWalkStep` has the corrected explanation and the re-measured
> magnitude. G44 gates the bank's median (and, new in fix round 2, the bank's
> median excursion — see G44 itself, `tests/test_swarm_engine.cpp`, for why
> the interval alone was provably insufficient: it is exactly what let fix
> round 1's freeze pass as "slower").
>
> **Correction (fix round 3, post-implementation).** Three numbers in the
> paragraphs above were measured on an earlier code state and do not hold on
> the shipped engine; two mechanisms needed a code fix rather than a text
> fix. Every figure below was printed by a probe run against the code as it
> now stands **unless the sentence names another code state or another
> window** — the qualifier matters, and fix round 4 had to add it because two
> figures here were quietly neither (see items 2 and the low-end paragraph).
> Method: seed 99 unless a seed set is named, `set_seed` before
> `init(48000)`, CHARACTER 0, RISE 0.02, FALL 1, FLOW on, lanes
> `{ TILT 0.5, FOCUS 0.5, PITCH 1/3, MOTION m, LEVEL 1 }`, 5 s settle + 60 s
> window, bank median over all `kPartials` slots. Figures re-measured in fix
> round 4 on the code as it stands after the pan fix; the pan walk draws from
> the same RNG stream it always did, so every pitch figure in this block is
> unchanged by that fix and was re-printed, not carried.
>
> 1. **The shimmer end lost a third of its excursion, and is restored.**
>    Deleting the pre-task step boost (`kDriftRateDepthBoost = 0.5`, now
>    `kDriftStepDepthBoost`) took MOTION 1.0 from 27.212–29.757 cents to
>    18.708–20.341 across eight seeds — systematic, non-overlapping, and
>    invisible to a gate whose bar was 5 cents. Restored: **28.004–30.499
>    cents**, cadence 0.01752–0.01778 s against the pre-task 0.01748–0.01797 s.
>    §1.2's "29.4 cents at MOTION 1.00" is therefore delivered, not 69 % of
>    it. `H = 1` does **not** reproduce the pre-task shimmer *exactly* — the
>    walk also draws a pan step now, so the RNG stream differs — it lands
>    inside the pre-task band, which is what the claim should always have said.
>    G44's shimmer subcase now gates 25 cents and reddens on the deletion.
> 2. **MOTION is continuous at the origin.** `kDriftDepthFloor` applied whole
>    makes MOTION a step function: 0 cents at exactly 0 and **13.232 cents,
>    43 % of full travel**, at the first representable non-zero value —
>    measured in fix round 4 on the code as it now stands with only
>    `kDriftFloorRampEnd` mutated to 1e-9, i.e. the ramp deleted and nothing
>    else, at this block's stated 5 s + 60 s window, against that same code's
>    30.499-cent travel at MOTION 1.0. (This line read "9.475 cents, 56 %"
>    for one round, under this block's own promise that every figure was
>    measured on the current code at this window. Rebuilt and re-measured,
>    that pair is fix round 2's code at a **20 s** window — 9.4752 against
>    that state's own 16.7855 — and at 60 s the same state gives 13.2323 of
>    20.3404, 65 %. The defect is real either way and was understated, not
>    overstated; the method sentence was still false.) The floor is now
>    faded in linearly over 0..`kDriftFloorRampEnd` (0.01). Measured cents at
>    MOTION 1e-7 / 1e-4 / 0.001 / 0.005 / 0.01 / 0.02 / 0.05: **0.000 / 0.032
>    / 1.219 / 6.576 / 13.285 / 13.343 / 13.833**, i.e. ~1.33 cents per 0.001
>    of the lane through the ramp (4.4 % of full travel per step, uniform) and
>    a plateau from 0.01 upward that is *identical* to fix round 2's — with
>    the boost disabled, the ramped code gives 13.2324 / 13.4745 / 14.0652 /
>    20.3404 cents at MOTION 0.02 / 0.05 / 0.15 / 1.0, matching the round-2
>    review's independently measured 13.232 / 13.474 / 14.065 / 20.340 to
>    every printed digit. The ramp changes nothing at or above 0.02.
>    MOTION exactly 0 stays exactly static: 0 of 420 000 tick × slot samples
>    move, on pitch and — since fix round 4 — on the pan as well. **Two**
>    accepted limits, both documented at the constant. First, below ~0.001 the
>    fade is mostly frozen (97.37 % of ticks at 0.001, 99.99 % at 1e-4, 100 %
>    at 1e-7), because a floor scaled that far down is back under
>    `SwarmBank`'s ULP cliff. Second (fix round 4): the fade is only resolved
>    as finely as the lane is sampled, once per control tick, so how much it
>    buys depends on how fast MOTION is swept. Measured, MOTION driven as a
>    triangle 0.2 → 0 → 0.2 and the worst per-tick pitch jump any slot shows
>    compared against the same code with the ramp deleted: **20–24×** better
>    at a 40 s triangle, 15–17× at 20 s, 5–9× at 8 s, 4–5× at 4 s, 2–3× at
>    2 s, 1.3–1.6× at 1 s and 1.1–1.3× at 0.5 s. The fade therefore reads as a
>    fade for lane movement slower than roughly one traverse per second and
>    not for faster movement.
> 3. **The binade correlation is real but small.** "Spearman 0.956" was
>    measured on fix round 1's frozen code. Re-measured at MOTION 0.15, the
>    four base seeds give **+0.513 / +0.539 / +0.423 / +0.204** with a
>    per-slot spread of 1.195× / 1.128× / 1.184× / 1.293× rather than 2.7–3×,
>    and the per-slot ranking barely reproduces across seeds. **Those are four
>    samples, not bounds** (fix round 4): over sixteen seeds rho runs
>    **−0.055 … +0.785**, median +0.256, positive at **15 of 16** seeds — not
>    at every seed, as this line claimed for one round — and the spread runs
>    1.113× … 1.293×. The mechanism is derivable in closed form
>    (`ulp(x)/x = 2⁻²³/m`) and the measurement corroborates it; the phenomenon
>    is no longer load-bearing for the gate's design.
>
> **Measured result, MOTION's practical low end, on the shipped code:** at
> MOTION 0.15 — G44's own setting — the bank-median reversal interval is
> **0.2419 s** (0.2386–0.2419 s across four seeds at this block's 5 s + 60 s
> window; 0.2381–0.2429 s under G44's own no-settle method, which is where
> that band came from and which this block wrongly presented as its own —
> corrected in fix round 4) and the bank-median peak-to-peak excursion
> **15.141 cents** (14.451–15.141 across the same four seeds with the settle,
> 15.171–15.586 without it); at MOTION 0.02 the interval is
> **0.2804 s**. **This falls short of the 1–3 s promised above, and the
> promise is not being paid.** Against the pre-task engine at the *same*
> setting (0.0336 s at MOTION 0.15) it is 7.2× slower; against the pre-task
> engine's fastest cadence (0.0180 s at MOTION 1.0) it is 13.4×.
>
> The tradeoff that stops it there is measured, not assumed. `H` is the only
> knob that reaches 1–3 s, and it buys the interval with the freeze this
> whole round removed (MOTION 0.02, shipped code, seed 99):
>
> | H | frozen ticks | bank-median interval | bank-median p2p |
> |---|---|---|---|
> | 16 | 26.57 % | 0.1469 s | 15.00 cents |
> | 32 (shipped) | 46.72 % | 0.2804 s | 13.34 cents |
> | 64 | 72.40 % | 0.5715 s | 10.99 cents |
> | 256 | 92.91 % | **2.3077 s** | 6.19 cents |
>
> `H = 256` does reach the spec's range, at 92.91 % frozen control ticks —
> indistinguishable from the 93.94 % that made fix round 1's "slow breath" a
> bug, and at 40 % of the excursion. So 1–3 s is reachable and was not bought:
> what the mechanism delivers at a fidelity worth shipping is ~0.24–0.28 s.
> That is a taste decision left open for retuning by ear, not a defect.
>
> (Frozen-tick fractions in this section and in `swarm_config.h` are **pooled**
> over every slot and control tick, not medians of per-slot rates. The median
> runs about 1.7 pp higher — 40.70 % where the pooled figure is 39.04 % — and
> nothing said which was meant until fix round 4.)
>
> **Correction (fix round 4, post-implementation).** MOTION did not own the
> stereo image. `_pan_walk` was scaled by WIDTH alone, so at MOTION 0 the
> image kept moving **0.297–0.347 pan units peak-to-peak** at WIDTH 1 (probe,
> eight seeds, 5 s settle + 60 s, bank median of `target_pan_for_test`) —
> 39–45 % of the same seed's own MOTION-1.0 travel — while pitch and amplitude were
> bitwise frozen. §9's gate G-L was therefore false for half of what a
> listener hears, and no gate could catch it: G27 and G44's MOTION-0 subcase
> read frequency, and both of G43's subcases ran at MOTION 1.
>
> The pan walk is now scaled by the **same** `excursion_depth_of()` the pitch
> and amplitude walks use — the floor, its origin fade and its exact zero at
> MOTION 0, one shape in one place (`swarm_engine.cpp`). WIDTH keeps its job:
> it sets how wide the image is. MOTION owns whether it moves. Measured on the
> code as it now stands (seed 99, WIDTH 1, 5 s settle + 60 s, bank-median pan
> peak-to-peak; the MOTION-1.0 travel is 0.779 under the same setup):
>
> | MOTION | 0 | 1e-7 | 1e-4 | 0.001 | 0.005 | 0.01 | 0.02 |
> |---|---|---|---|---|---|---|---|
> | pan p2p | 0.000 | 0.000 | 0.003 | 0.031 | 0.154 | 0.303 | 0.304 |
> | % of travel | 0 | 0 | 0.4 | 4.0 | 19.8 | 38.9 | 39.0 |
>
> The image fades in over the same 0..`kDriftFloorRampEnd` ramp as the pitch
> and is untouched from 0.01 upward, where it was already at its plateau. At
> MOTION exactly 0 it is **bitwise** static: 0 of 420 000 tick × slot samples
> move across five seeds, the same window that proves the pitch static, and
> the mutation that removes the scaling makes 325 807 of those 420 000 move.
> G43 gains a third subcase that gates both halves.
>
> **MOTION 1.0's travel is deliberately left wide.** All three walks step one
> shared `step`, so restoring `kDriftStepDepthBoost` in fix round 3 widened
> the pan walk as well: measured at WIDTH 1, MOTION 1.0, eight seeds, the
> bank-median pan p2p is **0.743–0.836** as it stands against **0.537–0.574**
> with the pan step alone taken off the boost (1.43× on the means). The wide
> one is kept — the pre-task engine had no pan walk at all, so there is no
> band to restore to, and the shimmer end should move the image further than
> the breath end, which is the same statement the pitch excursion makes.
> §7.3's "scaled by WIDTH" is amended there.

## 8. Cost

Everything above lives in `_rebuild_targets`, `_control_tick` and the setters.
`SwarmBank::process` — the loop that the bench priced at 7405 cycles per partial
per block — is not touched, so N stays 14 and the kernel measurement stands.

That is **not** a claim that the engine's CPU is unchanged. `_rebuild_targets`
is already the control tick's expensive half (two `std::pow` per partial per
tick plus the sort), and VOWEL adds a two-peak evaluation per partial while the
stagger adds two ring reads per partial. The operative gate is the relative one
the N decision already uses: `inst_swarm_engine_worst` must not exceed the same
image's `instrument_worst`. It must be re-run; §10 open point 1 owns it. If it
fails, N drops to 12, which was already measured as the fallback.

Memory: +2 KB for the two stagger rings, +56 bytes for `_pan_walk[]`.

## 9. Gates

Every gate below must be shown RED once against today's engine before it counts
(`fireflow-tests-must-be-able-to-fail`). The §1 numbers are what most of them
are red against.

| Gate | Claim | Today |
|---|---|---|
| G-A | LADDER, HARM 1.0: the 12th partial is within 100–200 cents of pure | +1506 at HARM 0.6 → RED |
| G-B | In every character, each partial's deviation grows monotonically with HARM | 0.8 / 1.0 rows are non-monotone → RED |
| G-C | Adjacent characters are audibly distinct at identical settings (spectral distance above a threshold) | only one law exists → RED |
| G-D | Sweeping CHARACTER up and down across a boundary does not chatter (hysteresis holds) | no zones → RED |
| G-E | A 0.3 Hz full-depth LFO on `LANE_SOURCE` moves RMS by at least 3 dB | 0.57 dB → RED |
| G-F | Steady-state level span across the whole TILT travel stays under 1.5 dB (the normalizer still normalizes once the slew has settled) | 0.57 dB today. Its RED is against a *wrong* 7.1 — delete the normalizer instead of slewing it and this goes to 20+ dB. Prove that RED by stubbing `_normalize_power` out once |
| G-G | A step on `LANE_SIZE` reaches the top partial measurably later than the bottom one | coherent, zero lag → RED |
| G-H | At MOTION 0.15 the drift reverses direction no more than twice per second | 0.027 s → RED. **Not met as written** (fix round 3): the shipped bank median is 0.2419 s, i.e. 4.1 reversals per second, against 0.0336 s at the same setting pre-task. G44 gates 0.2 s instead. §7.4's fix round 3 correction has the measured H tradeoff that stops it there |
| G-I | FALL 1.0 stands (RMS at 8 s within 3 dB of the bloom peak); FALL 0.5 decays | FLOOR carries this today → RED after the fold |
| G-J | Bench: `inst_swarm_engine_worst` ≤ `instrument_worst` in the same image | must be re-measured |
| G-K | No character at any HARM / FOCUS / BAL combination produces silence | the CHOKE lesson |
| G-L | MOTION 0 is still exactly static — pitch, amplitude **and the stereo image** | today's G27, must survive. **Met on all three since fix round 4** (G27 + G44's and G43's MOTION-0 subcases): 0 of 420 000 tick × slot samples move on pitch and on pan, five seeds, 1 s settle + 60 s. It was **false for the image** for three rounds — 0.297–0.347 pan units peak-to-peak at MOTION 0 and WIDTH 1, 39–45 % of its own travel — because `_pan_walk` was scaled by WIDTH alone and both G43 subcases ran at MOTION 1. §7.4's fix round 4 correction has the fix and the numbers |

No bit-exactness or byte-identity gates: renders here are sanity checks
(`fireflow-bit-exactness-not-required`).

## 10. Open points

1. **The CPU re-measurement (G-J).** Owed on the board, and the o3 caveat from
   the N decision still stands — `docs/bench/2026-08-17-swarm-n-decision.md`.
   This is the only open point that can change the design: if the relative gate
   fails, N drops to 12.
2. **The by-ear constants.** `kCharHyst`, `kVowelWidthOct`, `kChoirCentsMax`,
   `kDroneStart`, `kNormSlewS`, `kModStaggerS` and the MOTION rate curve are all
   first-try values. They belong to Bastian's listening session. `kInharmMax`
   and `kBellModes` are *not* on this list — both were measured (§4.1, §4.3).

   Two of the by-ear ones ARE gate-hostage, corrected by the review
   (2026-08-17 fix wave, findings I8/G16): `kDroneStart` is read directly by
   G20 (`0.7f * kDroneStart`) and, since the fix, by G16 as well — both derive
   their FALL knob positions from it rather than a raw literal, so the gates
   track a listening-session move instead of reddening on one. `kVowelWidthOct`
   is not derived by any gate but IS load-bearing for one: G35's
   `peak_count(e) >= 2` requires it to stay narrower than half the closest
   `kVowelF1`/`kVowelF2` gap (`/a/`, 0.578 octaves) — widening it past that
   would merge VOWEL's two peaks and redden G35. Left as a listening decision
   rather than derived (the boundary depends on the discrete partial grid, not
   only the formant gap, per the comment on the constant), but no longer
   undocumented. `kCharHyst`, `kChoirCentsMax`, `kNormSlewS`, `kModStaggerS`
   and the MOTION rate curve remain un-gated.
3. **The VCV panel caption.** RES must print CHARACTER on a swarm deck through
   `DYNAMIC_CAPTIONS` in `gen_panel.py`, the way DETUNE already prints HARM.
   Both panels are generated and guarded — never hand-edited.
4. **`SwarmEngine::set_detune` stays hostless.** Unchanged by this design; the
   reasoning in `part.h`'s block comment still holds.
5. **Plan shape.** This spec is large enough to split into two implementation
   plans — CHARACTER + HARM + the FALL fold (§4–6), then breath (§7) — which are
   independent apart from MOTION. Decided at the writing-plans step, not here.
6. **VOWEL, at BAL exactly −1.0, has a narrow silence residual by-ear
   constants would have to fix.** Found during the 2026-08-17 fix wave's own
   verification, not part of the original design. At BAL == −1.0 exactly,
   VOWEL's even-overtone amplitude hits EXACTLY zero (§4.5) — the same
   contract G12 pins for LADDER — and VOWEL's formant peaks can then land in
   the log-frequency GAP between two adjacent ODD-only harmonics, wide enough
   at the bottom of the ladder (`/a/` to the next odd partial) that
   `kVowelWidthOct`'s half-width does not bridge it. Measured (probe, current
   code): **46 of 2541** HARM × FOCUS × pitch combinations silent at BAL ==
   −1.0 exactly (11 HARM steps × 21 FOCUS steps × 11 pitch steps, SUB 0),
   against **0** at BAL == −0.95. Fixing it needs a by-ear widening of
   `kVowelWidthOct` or `kVowelGapRange` (§10 point 2) — outside a bug-fix
   wave's authority over first-try constants, and not attempted here. G-K
   (§9) sweeps BAL to ±0.95 rather than the exact ±1.0 extreme specifically
   to avoid this residual; that exclusion is a live gap in G-K's coverage,
   not a rounding choice, and it stays open until this point is closed.
7. **VOWEL's FOCUS clamp is correct but not fully alive at the top of the
   pitch axis.** The 2026-08-17 fix wave's F1/F2 clamp (§4.2, keeping the
   pair inside the spectrum VOWEL actually builds) closed CRITICAL 2's
   silence but did not restore FOCUS's full sweep everywhere: at pitch 1.0
   (880 Hz root) both formants clamp to the fundamental across roughly the
   bottom half of FOCUS's travel, and only start moving after that.

   The re-review could not reproduce this point's first table and named the
   real defect: it named neither the SUB value nor the weighting, so the
   setup did not travel with the number (`docs/engine-map.md` §6). Re-measured
   with the setup stated in full: seed 99 (`fresh_swarm`'s default), single
   note (`trigger`, not `trigger_chord`), `LANE_PITCH` 1.0 (880 Hz root),
   `SUB 0.0`, `HARM 0.5`, `BAL 0.0`, `TILT`/`LANE_SOURCE` at its 0.5 default,
   40 `kCtrlInterval` settle ticks, centroid = amplitude-weighted mean of
   `target_hz_for_test` over `target_amp_for_test` (the TARGETS, not the
   glided bank), in octaves relative to `root_hz_for_test`:

   | FOCUS | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
   |---|---|---|---|---|---|
   | centroid (oct) | +0.000 | +0.000 | +0.000 | +0.567 | +1.000 |

   The qualitative claim holds under this setup and every other SUB value
   probed (0.0, 0.3, 1.0): FOCUS 0.00/0.25/0.50 always read identically
   (clamped flat), and only 0.75 and 1.00 move — SUB and HARM shift the
   exact centroid values (the table above is HARM-sensitive: HARM 0.75
   reads 0.000/0.000/0.439/0.394/1.343 under the same setup, non-monotone
   between 0.50 and 0.75), but never the shape. The previous table's values
   (−0.30/−0.30/−0.30/−0.18/+0.26) came from the engine's default `SUB 0.3`,
   left unstated — reproducible once that default is named, but the wrong
   number to publish without it.

   Checked explicitly against the two-pass restructuring (Blocking 1,
   §4/§9): it does NOT remove this limit for a single note — VOWEL's clamp
   bounds reduce to the same `[f0, f0 * n_sw]` the one-tone case always had,
   whether derived from the built spectrum or the old fixed formula, so the
   two are numerically identical there. (The restructuring DOES fix the
   equivalent chord case, where the bounds are no longer fixed.) Better than
   the silence it replaced — the deck is always audible now — but the lower
   part of FOCUS's lane is inert at the top of the pitch axis, which is a
   real, if lesser, design limit rather than a remaining bug.
8. **The modulation stagger (§7.2) fragments the FOCUS window into a comb
   under fast or deep modulation — a spectrum shape no single SIZE value can
   produce.** Found during the task 3 fix wave (fix round 1, IMPORTANT 1),
   and deliberately left UNBOUNDED here — a listening decision, not a bug —
   because each partial reads SIZE at its OWN lag (that IS the wave the
   stagger was asked to produce, spec section 7.2's own opening sentence), a
   moving SIZE means different partials evaluate `_focus_weight_at` against
   DIFFERENT centres at the SAME instant: a partial reading mid-travel is
   wide open while a neighbour reading an extreme is windowed shut, or the
   reverse. The audible set can therefore fragment into several separate
   contiguous "islands" instead of the single contiguous run a static SIZE
   always produces.

   Measured (probe): LADDER, TILT 0.5, single note (pitch 1/3), SIZE driven
   as a sine 0..1 at `kCtrlInterval` granularity, "audible" = amplitude > 5%
   of that tick's loudest partial, island count per control tick over a 5 s
   window —

   | LFO | MOTION 0.5 | MOTION 1.0 |
   |---|---|---|
   | 0.3 Hz | 2 islands, 1.8% of ticks | 2 islands, 13.2% |
   | 1.0 Hz | 2 islands, 40.8% | 2 islands, 81.1%; 3 islands, 6.5% |
   | 2.0 Hz | 2 islands, 85.3%; 3 islands, 7.2% | 3 islands, 49.0%; 4 islands, 23.6% |
   | 5.0 Hz | 3 islands, 46.1%; 4 islands, 41.0%; 5 islands, 7.4% | 4 islands, 77.1%; 5 islands, 4.0% |

   (single-island ticks fill the remainder of each row; 0-island ticks never
   occurred in this probe.) More islands at higher LFO rate and at higher
   MOTION, monotone in both — `SuperModulator`'s FREE range reaches 30 Hz, so
   0.3–5 Hz sits well inside panel reach. The comment on
   `SwarmEngine::_focus_weight_at` (`engine/swarm/swarm_engine.cpp`) carries
   the same table beside the code. Whether this comb reads as the intended
   character of a "wave travelling up the spectrum" or as something to tame
   is Bastian's call in the listening session, not a decision this fix wave
   makes.
