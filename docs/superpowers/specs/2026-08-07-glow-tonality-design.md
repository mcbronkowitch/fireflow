# Glow tonality: the BBD bend and the scale draw

**Date:** 2026-08-07
**Scope:** two changes aimed at one complaint — Glow sounds dissonant. Both
live in the flow layer; neither touches the Fireflow module's behaviour.

## 1. What was measured first

The complaint came with a hypothesis: the scale mechanic is broken, or the two
decks get different scales. Both are false, and the measurement is worth
keeping so nobody re-opens the question.

**Structurally**, scale and root are one draw per terrain and global to both
decks: `terrain.cpp:268-278` draws them once, `instrument.h:93-96` pushes the
scale to *every* part, and `flow_params.h:121` pushes the same root index to
PART_A and PART_B. No story curve targets `P_SCALE` or `P_ROOT`, so they sit on
the terrain base and move only on NEW.

**Measured**, 24 terrains rendered at static macros with no NEW press, reading
`a_pcv`/`b_pcv` from `mods.csv` (`Part::pitch_cv()` is `target_value(LANE_PITCH)`,
i.e. the quantized pitch, `0..1 = 36` semitones) and testing the two decks'
combined pitch-class set against all 13 masks × 12 roots:

- **20 of 24 terrains: both decks' notes fit exactly one common scale and root.**
- The 4 exceptions all have the same cause — one deck that does not quantize
  at all.

Note for anyone repeating this: the CSV prints `%.4f`, so a grid semitone reads
as 33.9984, not 34.0. A tolerance tighter than ~0.01 semitones makes every deck
look off-grid and every conclusion drawn from it is an artifact.

**The real off-grid path** is `part.cpp:230-232`: the SAMPLER never quantizes,
and the BBD does not quantize in FLOW. Across the same 48 decks, **9 (19 %)
played mostly off-grid** — 7 sampler decks (silent in the render host, `fill=0`,
no input material) and 2 BBD decks, which are audible. The BBD case is the
common one by construction: `kModeW[ARCH_DRONE] = 0.15` (`taste.h:643`) and
drone carries 50 % of the archetype weight, so most terrains run FLOW, which is
exactly the mode in which the BBD's pitch is continuous.

Two further contributors are named here but not addressed by this spec: the
scale list is drawn uniformly across all 13 entries including whole tone and
hijaz (fixed below), and BODY's `ModeBank` stretch produces inharmonic
partials, which reads as "out of tune" independently of any scale.

## 2. Bounding the BBD bend under Glow

The BBD's pitch in FLOW is not a note, it is the delay clock, spread
geometrically across the whole reachable window (`bbd_music.h:74-78`,
"a bend, not a keyboard"). Quantizing it would mean routing it through the STEP
path and losing the bend. Instead the flow layer bounds how far the bend may
travel.

The flow layer has no PITCH parameter. The lever is `P_RANGE_A/B` →
`SuperModulator::set_range`, which touches **only** `LANE_PITCH`
(`super_modulator.cpp:82`).

**Where.** A runtime guard in `Flow::recompute_and_push`, beside the existing
`kBodyFiltFloor` block (`flow.cpp:489-493`), not in `apply_constraints`. Same
reason as that one: the blend interpolates between two terrains whose engine
assignments differ, so a deck can run as BBD for nearly the whole ramp on a
RANGE value drawn by a terrain that never put a BBD there.

```cpp
else if (p == P_RANGE_A || p == P_RANGE_B) {
    const int ep = (p == P_RANGE_A) ? P_ENGINE_A : P_ENGINE_B;
    if (int(_pushed[ep] + 0.5f) == ENGINE_BBD && !_mode_now
        && v > kBbdFlowRangeMax) v = kBbdFlowRangeMax;
}
```

Reading `_pushed[ep]` for the engine works because `ENGINE_A/B` lead the
parameter table (the `static_assert` at `flow.cpp:53` already pins that for the
FILT floor); the guard needs the same ordering and should be covered by it.

The mode is read as `_mode_now`, which at this point in the loop is equal to
`_pushed[P_MODE]`: `P_MODE` stays **last** in the table (stream seeding,
`flow_params.h:86-88`), so neither has been touched by this tick's push yet, and
`_mode_now` was set from exactly that field at the end of the *previous* tick
(`push_mode_and_steps`). `_mode_now` is read rather than the field because it
names the question the guard actually asks: "the mode the instrument is
currently running".

What actually matters is the property this equality carries: both readings lag
this tick's candidate mode by one control tick. A mode change happens only on
NEW or wake, and one tick at 100–500 Hz is inaudible. On the first forced tick
after wake both are `false` (FLOW, zero-initialised), so the clamp applies and
is released a tick later if the terrain is STEP — harmless in the conservative
direction.

**The constant** lives in `taste.h` as a semitone budget, not a raw RANGE
value, so it stays tunable by ear:

```cpp
inline constexpr float kBbdFlowSemis     = 1.f;                 // total travel
inline constexpr float kBbdFlowRangeMax  = kBbdFlowSemis / (2.f * 60.f);
```

The 60 is the window width in semitones: `kMaxStages / kMinStages = 32`
(`bbd.h:93-94`) is 5 octaves. The 2 is `apply_range`: at `r <= 0.5` the lane
output is **unipolar** `0..2r` (`mod/range.h:16`), so the travel is one-sided.

**Accepted consequence.** At `kBbdFlowSemis = 1`, RANGE is capped at 0.0083 and
the BBD deck's PITCH lane is effectively static: the clock stands still and the
tape-wobble that lane contributed is gone. The deck keeps its bandwidth and
timbre, and movement has to come from DRIFT/MOTION/FLUX instead. This is close
in effect to switching the lane off, and it is the owner's decision (2026-08-07)
after the trade was stated. Raising `kBbdFlowSemis` buys the motion back at a
proportional cost in off-key travel.

## 3. Weighted scale draw

`terrain.cpp:276` draws the scale uniformly over all 13 entries, so whole tone,
hijaz, phrygian and harmonic minor together take 31 % of terrains. Replace with
a weighted draw, tempered by adventure like every other weight table:

```cpp
scale = pick_weighted(r, w, SCALE_LIST_COUNT);   // w[i] = temper(kScaleW[i], adv)
```

Two properties make this surgical:

- `pick_index` and `pick_weighted` each consume **exactly one**
  `next_unipolar()` (`terrain.cpp:56-73`), so the stream position after the
  scale draw is unchanged.
- Stage 2 must move **after** the adventure draw (currently `terrain.cpp:293-296`)
  so `t.adventure_base` exists. The two use different streams
  (`kStreamTonality` / `kStreamAdventure`), so the reordering consumes nothing.

Together: **the ROOT draw stays bit-identical; only the scale changes.**

`kScaleW[SCALE_LIST_COUNT]` in `taste.h`, written in `ScaleId` order so it
indexes with the mask table. The rows below are grouped by friction instead —
how much a scale can rub when two sustained voices land on it at once,
computed from `SCALE_MASKS` (`quantizer.h:36-50`), not by feel:

| Scales | contains m2 | contains tritone | weight each | group |
|---|---|---|---|---|
| Minor pent, Major pent | no | no | 0.175 | 0.35 |
| Aeolian, Dorian, Mixolydian, Lydian | yes | yes | 0.1125 | 0.45 |
| Hirajoshi, Pygmy, Kumoi | yes | no | 0.0667 | 0.20 |
| Phrygian, Hijaz, Harmonic minor, Whole tone | see below | see below | 0.025 | 0.10 |

Any 7-note mode contains both a minor second and a tritone — that is a property
of 7 notes in 12, not of the choice of mode, so the modes cannot be made
clash-free by picking differently among them. Only minor and major pentatonic
are free of both. Whole tone has no minor second but three tritones.

Tempering means the weights are the table as written at adventure 0 and flatten
toward uniform at adventure 1, so an adventurous terrain can still reach whole
tone. Same law as `kModeW`, `kCarrierW`, `kRateRungW`.

## 4. Tests

- **Scale distribution** (`test_flow_terrain.cpp`): a rate over many masters —
  the min/maj-pentatonic share ≈ 0.35 and the exotic share ≈ 0.10, each with a
  tolerance derived from the sample size; plus the adventure coupling, that the
  distribution flattens at high adventure. A rate, not a set of pinned seeds:
  the population is what the change is about.
- **BBD range clamp** (`test_flow_runtime.cpp`): a terrain with a BBD deck in
  FLOW, asserting `param_now(P_RANGE_*) <= kBbdFlowRangeMax`; and, in the same
  run, that a non-BBD deck is *allowed* above the cap. Without the second half
  the test passes against an implementation that clamps every deck
  unconditionally.
- Both prove RED once before the fix lands.

## 5. Fallout

Every existing terrain code draws a different scale afterwards (same root).
Acceptable: saved patches carry no compatibility promise in this dev alpha.

No render-hash gate is affected — `ctrl_identity` and `wave_formant_sweep` are
not flow scenarios.

`test_flow_new.cpp:217-222` runs on a fixed seed and aggregates over six
parameters, so it should be unaffected; that is to be verified by running it,
not assumed.

## 6. After

Same population, same method as §1, measured after both changes landed:

- decks mostly off-grid: **9/48** (was 9/48)
- terrains whose two decks share no scale: **2/24** (was 4/24)
- scale-group shares over 10 000 masters: clean pentatonic **0.2965**, modes
  **0.4088**, mild pentatonic **0.1891**, exotic **0.1056** (was 0.154 / 0.308 /
  0.231 / 0.308 uniform)

The no-shared-scale count halved, and the scale-group shares land close to
`kScaleW`'s post-temper mixture (`test_flow_terrain.cpp`'s own comment: clean
0.301, exotic 0.106) — the weighted scale draw (§3) is doing what it was built
to do.

The off-grid deck count did not move, and the reason is that **off-grid was
the wrong yardstick for this change** — it measures grid alignment, not motion.
An unquantized deck sits between the semitones by definition, however still it
stands, so the count could not have fallen. The composition is the same as §1:
7 silent SAMPLER decks (`fill=0`, no input material, `part.cpp:211-225` — TUNE
there transposes a recording as a whole, and snapping that to the instrument's
scale is meaningless) and the same 2 audible BBD decks.

The quantity the guard actually bounds is **pitch travel while the deck is a
BBD**, and that is where the change shows up. Measured on the two BBD decks
(`4540215F` deck a, `D5336898` deck a), 20 s each, macros parked at 0.5,
splitting the samples by `a_fclk > 0` so only ticks where the deck really is a
BBD are counted:

| `kBbdFlowSemis` | `4540215F` | `D5336898` |
|---|---|---|
| effectively off (3600) | 10.634 semitones | 10.498 semitones |
| **1 (shipped)** | **0.342** | **0.385** |
| 0 (lane fully off) | 0.000 | 0.000 |

So the guard takes a BBD texture deck from nearly an octave of continuous glide
against a scale-locked carrier down to about a third of a semitone — inside the
one-semitone budget, with room to spare. Setting `kBbdFlowSemis` to 0 buys the
remaining 0.35 semitones and costs the lane entirely; it is not worth it, and
the shipped value stays at 1.

**A trap this measurement walked into first, recorded so the next one does
not.** Taken as a plain min/max over the whole render, the same two decks
measure 3.97 and 2.35 semitones of travel even with the lane fully off — which
looks like proof of a second, unbounded path into `LANE_PITCH`. It is not.
Those figures come from **3 samples out of 15 000**, in the wake transient
before the deck's engine has switched to BBD; `Instrument::bbd_clock_hz`
returns 0 whenever the deck is not currently a BBD, which is what makes them
separable. A min/max over a whole file is maximally sensitive to a transient,
exactly as a tolerance tighter than 0.01 semitones is maximally sensitive to
the CSV's `%.4f` rounding (§1). Split the samples first, then take the range.
