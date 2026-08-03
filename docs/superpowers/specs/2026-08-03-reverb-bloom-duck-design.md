# Reverb bloom duck — the mix makes room for the room

**Date:** 2026-08-03
**Status:** approved for planning
**Supersedes nothing** — builds on 10961a0 (return ceiling + DECAY remap), which stays.

## Problem

With DRIVE at 0.3–0.6 the dry bus already sits near the master shaper's knee
(measured 0.98 at DRIVE 0.60 on Bastian's cliping_test.vcvm). The reverb
return joins the master **at unity on top of** that bus, so a bloom
(DECAY > 100%) adds ~0.5–0.6 of wet and pushes the sum from "just at the
knee" to deep inside the tanh — the shaper's saturation escalates exactly
while the room swells. There are **no digital overs anywhere** (0.000% of
samples at full scale, measured in 10961a0); the "clipping" is the master
DRIVE shaper working on a sum nobody set.

Three return-side fixes were tried and failed for recorded reasons
(DECAY-tied trim, send-relative ratio, hard ceiling — see reverb.cpp and
10961a0). They all tried to bound the wet. The wet is already bounded by
its soft ceiling; what escalates is the **sum**.

## Decision

Ambient-classic ducking: as the return blooms, the **dry bus makes room**.
The saturation amount at the master then stays roughly what the player
dialed with DRIVE before the bloom, instead of escalating with it. The
bloom swallows the mix; the mix does not fight it.

Chosen over two alternatives:
- a sum-referenced gain computer (control signal would contain the fast
  dry material — the measured pumping failure mode of the two dead rides),
- an equal-power dry/wet law (would touch quiet-reverb mixes that are
  tuned by ear and must stay untouched).

## Architecture

One new read-out on the reverb, one new gain stage in the instrument.
No new panel control (hardware constraint).

### AmbientReverb: expose the return envelope

```
float return_level() const { return _wet_peak * _lim_gain; }
```

`_wet_peak` (seconds-slow peak follower, already there for the return
ceiling) times the ceiling's own ride = the level actually handed to the
master. No new state, no new cost.

### Arming: the duck exists only in the bloom regime

A level threshold alone cannot tell a bloom from a loud ordinary room —
10961a0's own measurement: an ordinary long room legitimately returns
+6.0 dB over its send, a runaway bloom +7.6 dB. The levels overlap, so
a hot send into a long sub-unity room would cross any usable threshold
and the duck would touch a by-ear mix.

What separates the regimes is not level but **who controls the room**:
below unity loop gain the player does (the room is input-driven, its
level is a choice — never duck it); above unity the loop does (the room
self-drives toward a level nobody set — that is what the duck is for).
The knob encodes exactly this bit. So the duck is **armed only while
`decay_loop_gain(decay_norm) > 1.0`**, computed in
`Instrument::set_reverb_decay` from the same public curve the tooltip
uses (one source, no drift). Disarmed → target 1.0, always.

This is not 02134e3's failed DECAY-tied trim: that read the knob as a
*level*, which it is not. Here the knob answers only the yes/no question
it genuinely encodes. No hysteresis at the unity point: the seconds-slow
slew makes arming flicker inaudible.

Disarm mid-bloom (DECAY pulled below unity while the tail is loud) is
accepted as-is: the duck releases over `kDuckUpS` while the tail decays,
the sum is briefly hot but bounded by the return ceiling and the master,
and musically the mix comes back as the bloom dies. No special case.

### Instrument: duck the dry, never the send

In `Instrument::process`, on the existing control raster
(`_ctrl_ctr == 0`), compute a duck target from `_reverb->return_level()`:

- disarmed, or `env <= kDuckThresh` → target is **exactly 1.0**; the
  dry path is bit-identical to today. Guarded by a test, the same way
  `limiter_gain()` guards the return ceiling.
- armed and above the threshold, the target falls linearly in level,
  reaching the floor at `kDuckFull`:
  `target = 1 - (1 - kDuckFloor) * min(1, (env - kDuckThresh) / (kDuckFull - kDuckThresh))`
- per-sample one-pole slew toward the target, seconds not milliseconds:
  down over ~1.5 s (the bloom itself swells over 2–3 s), up over ~4 s
  (so a wash fluctuating near the threshold breathes below audibility —
  the measured lesson of the hard-ceiling dead end). Implemented as a
  residual (`_duck_gain − _duck_target`) held in its own persisted float
  and decayed by a pure multiply each sample, re-based onto the target
  only when the target itself moves — not `_duck_gain` rebuilt from
  target every sample, which is algebraically the same update but stalls
  short of the target (measured ~0.994 after a bloom at 48 kHz) because
  the addition back into target's scale rounds away the remaining gap
  before the next sample can act on it.

The duck gain multiplies **only the dry terms** (`al*ga*dga`,
`ar*ga*dga`, `bl*gb*dgb`, `br*gb*dgb`). The sends into the room are
untouched: the feed, MORPH and MIX curves keep their M4 semantics, and
the wet output for a given performance is identical with and without the
duck.

Two paths that must stay pre-duck — multiply inside the `l`/`r` mix
expressions, never scale `al/ar/bl/br` in place:

- `_dry_tap` (the excitation bus BODY listens to) reads `al/ar/bl/br`
  directly; a ducked excitation bus would starve the other deck's
  resonator exactly when the bloom peaks.
- `_deck_tap` (SPKY_DECK_BUS) likewise.

With no reverb injected (`_reverb == nullptr`) or the room asleep,
`duck_gain()` reads exactly 1.0.

### Starting constants (all ear-tunable, one place)

| constant | value | meaning |
|---|---|---|
| `kDuckThresh` | 0.30 | below: exactly 1.0 even when armed. Shapes onset within the bloom regime; the *regime* gate is the arming, not this number |
| `kDuckFull` | 0.60 | env at which the floor is reached (the settled bloom plateau) |
| `kDuckFloor` | 0.316 (−10 dB) | how far the mix steps back; "makes room", not mute |
| `kDuckDownS` | 1.5 s | ride into the duck |
| `kDuckUpS` | 4.0 s | ride back out |

### Reset / sleep interplay

`clear()` already zeroes `_wet_peak` → `return_level()` reads 0 → the
duck target snaps to 1.0 and the gain slews back over `kDuckUpS`. On
`_rev_primed` (first block) the duck gain resets to exactly 1.0, like
the mix smoothers. A cleared room therefore never leaves a ducked dry
behind for longer than one up-ride.

## What this deliberately does not fix

At DRIVE ≥ ~0.4 the **bloom alone** still crosses the shaper knee
(wet 0.6 × pre-gain 2+ > knee). The duck keeps the saturation amount
constant at the player's DRIVE setting; it does not make the bloom
cleaner than the mix was before it. That would require the return to
join after the shaper — considered and rejected in this design round
(the duck was chosen as the musical behavior).

## Tests (each proven red first)

The instrument exposes `duck_gain()` for tests, the way the reverb
exposes `limiter_gain()`: from outside, a duck is indistinguishable
from quieter playing, so the read-out is the only honest probe.

1. **Transparency guard:** boot-default patch (DECAY 0.55) but with a hot
   send (REV_SEND = 1 both decks) and MIX 0.9, so the room is genuinely
   loud, not merely quiet → `duck_gain()` reads exactly 1.0 (`==`, not
   `≈`) over the whole render. A hot send matters: a quiet room passing
   this guard proves nothing.
2. **Loud-but-sub-unity guard (the arming test, the steady-state form):**
   DECAY 0.75 (loop gain ~0.94) held for the room's *whole* life, hot
   send, 15 s → `duck_gain()` reads exactly 1.0 on every single block,
   with a `REQUIRE` that `return_level()` actually crossed `kDuckThresh`
   during the render (measured on this rig: env_max ≈ 0.447). Red with a
   pure level threshold; this is the test that forces the arming to
   exist, and it holds the regime fixed for the render's entire span so
   nothing borrows proof from the env's own drain (see test 4).
3. **The duck ducks (render-minimum form):** DECAY 1.0, hot send, 15 s →
   the *minimum* `duck_gain()` seen over the render falls below 0.5. The
   rig's return envelope breathes (~0.39–0.58, ~10 s period — generative
   material), so a single checkpoint would straddle the line; the render
   minimum pins the spec's claim exactly. (Red before implementation: it
   reads 1.0.)
4. **Regime-release guard (the disarm test):** duck the room first (DECAY
   1.0, 15 s), then drop DECAY to 0.75 (sub-unity — the player takes the
   room back) while the tail is still loud, and render 4 s. This does
   *not* re-prove the regime bit — over the full 4 s window the env also
   drains through `kDuckThresh` on its own (measured: `return_level` 0.301
   at 3.0 s, 0.261 at 4.0 s), so a plain level duck would eventually rise
   here too; that proof is test 2's job. What this guards: the gain rises
   monotonically, clears a bound derived from the 4 s up-slew
   (`(1−g0)·(1−1/e) > 0.1`), and the env is honestly still over threshold
   at the 1 s checkpoint (so the rise isn't free). The same test then
   kills the sends and drops to DECAY 0.5, renders 90 s more, and checks
   `duck_gain() == 1.0` exactly — the residual-form slew's eventual exact
   recovery (see the Architecture slew note above).
5. **No stepping:** jump DECAY 0.5 → 1.0 mid-render → the dry gain's
   per-sample delta stays below a slew-derived bound.
6. **Send purity:** wet-solo bloom (MIX = 1, DECAY 1.0) settles at the
   known return plateau (~0.5–0.6), not at plateau × duck floor — if
   the duck ever multiplied the send or the return, this level drops
   and the test goes red.
7. **Tap purity:** during a full duck, the excitation bus (read via the
   existing `excitation_bus()` test observer) carries the unducked deck
   level — red if an implementation scales `al/ar/bl/br` in place.
8. **Re-init forgets the duck:** re-initialising an instrument that was
   visibly ducked reads `duck_gain() == 1.0` before any `process()` call.

## Cleanup audit (the "zerbastelt" question)

Checked before this design: 02134e3's DECAY-tied bloom trim was already
removed by 10961a0 ("supersedes"); reverb.cpp carries no leftover trim.
What remains and stays: the DECAY curve remap (playability, orthogonal)
and the return soft ceiling (complementary — it bounds the wet, the duck
makes room in the dry; without it the wet alone reaches ~1.25 and
saturates the master regardless of ducking). Nothing to tear out.
