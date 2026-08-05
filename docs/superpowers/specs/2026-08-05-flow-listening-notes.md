# Flow listening notes

**Date opened:** 2026-08-05
**Scope:** the running logbook for the flow layer's listening phase (Plan A of
`docs/superpowers/specs/2026-08-05-flow-machine-design.md`, §2's
"Plan A first, Plan B does not start until Plan A's calm corner and house
seed have survived a listening pass"). Plan A itself
(`engine/flow/` — terrain generator, six-macro story layer, weather, NEW
gestures, render-host scenario wiring, audio sanity gates) is built and green
(946 test cases, `ctest` 4/4). Nothing below changes that; every item here is
an ear decision, not a bug.

## Open questions on arrival

Seeded from what Tasks 9 and 10 measured while building the audio gates —
found because they can't be settled without ears.

1. **Calm corner may be too quiet.** Renders and an independent
   re-measurement both put it at RMS ≈ 0.00092, about −61 dBFS — roughly
   1.5 % of the `kCalmCornerRmsMax` ceiling. The spec (§3) wants a quiet
   background *presence*, not silence. `kCalmCornerRmsMin` in `taste.h` is
   deliberately only a **silence detector**, not a musical target — the
   musical question is open.

2. **Terrain loudness spread ≈ 15.9 dB** at identical macro settings, no
   blend involved: min RMS 0.0158 (master `0x707`), max 0.0983 (master
   `0x101`). A NEW press can therefore land the player substantially louder
   or quieter. Owner's direction: **pull the very quiet patches up** —
   asymmetric, *not* full per-terrain normalization to a common target,
   which would flatten the loudness contrast that helps terrains feel
   different.

3. **Engine-switch blends dip to near-silence.** Worst measured case,
   master `0x101`: 65.16 dB below a no-press control run, with 4.00 s of the
   6 s blend spent more than 20 dB below it. **Known and accepted for now**
   — do not write this up as a defect. Mechanism: the outgoing engine is
   switched away, the incoming BBD delay line starts empty, and what decays
   is the reverb tail with nothing feeding it. Owner's idea for later: hand
   the transition to the reverb — a crossfade into a long reverb tail across
   the switch, so it covers the gap while the incoming engine primes, which
   costs no second engine instance. Note the constraint that motivated it:
   running two engines in parallel to cross-fade is expensive on the Daisy
   target.

4. **The texture deck's duck opens as a step.** It switches at blend phase
   0, so there is no time before the press for the duck to ramp in; the
   rising half is clipped and the send jumps to the duck peak. Audible.
   One-line fix if wanted: delay the texture switch by ~0.25 s in
   `switch_phase_for`.

5. **`kDuckWetTarget = 0.95` collides with the SPACE story's send ceiling**,
   so at SPACE = 1 the duck does nothing at all — the wettest setting is the
   one where the engine switch is least covered.

6. **A pending discrete is frozen against its own macro for up to 1.5 s** of
   a blend (the hold that keeps a re-press from dragging the carrier deck
   through a terrain it never played). If that reads as a dead knob, the
   answer is to shorten `kCarrierStaggerFrac`, **not** to reintroduce the
   jump.

7. **Discrete churn from quantizer hysteresis.** Under randomized macro
   sweeps `P_SCALE` was measured changing 258 times inside one 6 s blend —
   and 256 times with no button presses at all, so this is pre-existing
   hysteresis chatter, not a NEW-op effect. The committed gate bounds churn
   with macros held *static*; the hysteresis width itself is a listening
   number.

The audio gates in `tests/test_flow_audio.cpp` are sanity bounds, not
musical judgements — no NaN, no clipping, RMS inside plausible ranges, a
silence floor. They do not say any of the above sounds right. The numbers
above are what a listening session should try to move.

## Log

| Date | Seed / terrain code | Verdict | `taste.h` change |
|---|---|---|---|
| | | | |
