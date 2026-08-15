# STEP accent: per-note velocity and decay from the groove rank

**Date:** 2026-08-15
**Status:** design, approved in conversation, not implemented

## 1. What this is for

In STEP a deck plays every composed note at the same level and the same
envelope length. The notes differ in pitch and in placement and in nothing
else, so a dense pattern reads as a machine running rather than as a phrase
being played.

The animation has to be **density-dependent**, and in one specific direction:
when DENSE is low enough that a pattern fires a single note, that note should
be at full strength. It is the anchor; there is nothing for it to be
subordinate to. The differentiation should appear as DENSE opens the pattern
up and later, weaker notes join the anchor.

FLOW is explicitly out. A FLOW deck is a drone, and per-note dynamics there
would be animation where the design wants stillness.

**No new panel control.** The constraint is the hardware panel, and the answer
below needs none: the signal already exists in the groove, and its intensity is
already governed by DENSE.

## 2. Measured ground

Everything in this section was printed by a probe, not inferred. Setup for all
of it: note deck (`_melodic` and `_flow_melody` both true), STEP, rate 0.5 Hz,
SHAPE 0, SMOOTH 0, RANGE 1, VARY 0, `set_melodic()` before `init()`
(`docs/engine-map.md` §6), seeds 999 / 12345 / 7 / 4242, STEPS 4 / 8 / 16,
DENSE 0.0 / 0.05 / 0.125 / 0.25 / 0.5 / 0.75 / 1.0, three cycles per cell.

- **`pattern_groove.len` (`L`) equals the STEPS count** — 4 at 4 steps, 8 at 8,
  16 at 16. It is *not* the constant 8 that `pg_target_len()` returns; that
  function sizes the pitch motif, not the groove cell.
- **`set_step()` does not regenerate the groove. The next cycle wrap does.**
  Read the table immediately after the call and you get the pre-wrap one — at
  4 steps that is still an 8-slot cell whose slots 4..7 the phrase never
  reaches. The first version of this probe did exactly that and produced a
  false mismatch on two of three STEPS counts. Any test or probe that inspects
  a groove must run the lane to a wrap first.
- **`rank_of_slot[]` is a permutation of `0..L-1`, and slot 0 is always rank
  0** — every cell measured, both patterns of the song pair.
- **The firing rule is exact:** the slots that fire are precisely
  `{ slot : rank_of_slot[slot % L] < k }` with `k = clamp(round(DENSE·L), 1, L)`
  (`lane.cpp:643`, `:655`). Every cell of the sweep matched after the settle
  wrap.
- **The anchor rule holds.** At `k == 1` exactly one slot fires and it is slot
  0, whose rank is 0. At 4 steps that is every DENSE ≤ 0.25; at 16 steps, every
  DENSE ≤ 0.125.

The consequence that makes this design cheap: **the density dependence is
already in the rank.** Normalizing against `L` rather than against `k` means a
thin pattern draws only from the low end of the rank scale, so its notes are
loud by construction — no separate depth control has to enforce it.

## 3. The accent value

One scalar, computed in the lane, consumed in two places with independent
depths.

`ModLane::_start_note(slot)` — which runs in STEP only (`lane.cpp:671`) —
computes

```
_note_accent = (L > 1) ? rank_of_slot[slot % L] / (L - 1) : 0
```

and exposes it as `note_accent()`. `a == 0` is the anchor and the strongest
note; `a == 1` is the last note DENSE reveals.

`_note_accent` initialises to 0 and is reset alongside `_note_age` and
`_note_hold` on STEP entry (`lane.cpp:207`), so a deck that has never been in
STEP, and a deck that just left it, both report 0. **FLOW therefore reports a
constant 0 without a mode test anywhere downstream** — the STEP-only scope is a
property of the single writer, not a condition sprinkled across the consumers.

## 4. Delivery

`Part::_fire_trigger()` pushes `_engine->set_accent(_mod.note_accent())`
immediately before `trigger_chord()`.

`IPartEngine::set_accent(float)` is a new virtual with a no-op default — the
`set_gate` / `set_cycle` / `set_excitation` idiom (`engine_iface.h`). Engines
that do not implement it cost nothing and need no per-engine branch at the call
site, and an engine swap needs no re-sync reasoning because whichever engine is
active is exactly the one that gets pushed.

Deliberately **not** a new argument on `trigger_chord()`: that signature has a
default implementation fanning out to `trigger()`, and every engine overrides
one or the other. Widening it would touch all of them for a value most of them
ignore.

## 5. VEL — depth 0.3

In `SynthEngineT::_do_trigger()`:

```
_voices[pick].set_vel(vel * (1.f - (1.f - kAccentVelFloor) * _accent));
```

`kAccentVelFloor = 0.3f` (by ear, first try). The weakest note of a full
pattern strikes at 30 % of the anchor's amplitude.

It **multiplies onto** the existing `vel`, which today carries the equal-power
chord compensation `1/sqrt(n)` (`synth_engine.cpp:164`, `:247`). The two are
independent quantities — how many notes are sounding at once, and how strong
this note is meant to be — and composing them is correct. At `a == 0` the
product is the value that ships today, which is what makes the "chord
compensation unchanged" gate in §8 possible.

**The latch survives.** The per-control-tick refresh `_voices[v].set_vel(_vel_now)`
(`synth_engine.cpp:344`) is gated on `_sustaining[v]`, and in STEP no voice is
sustaining: `_do_trigger` sets `_sustaining[pick] = false` unconditionally in
its non-flow branch (`:211`), the sole writer of `true` is `:207` under
`if (_flow)` in that same function — the two auto-drone paths (`:120`, `:133`)
only arm `_auto_pending` and reach `_sustaining` through this one site — and
`set_flow(false)` clears any leftover through `_demote_all()` (`:117`). This is a closure over the writers, established by
reading them, not a measurement — §8's G6 is what turns it into something that
can go red.

## 6. DEC — depth 0.3, scaled by the DEC knob

```
decay_eff = decay_s * (1.f - (1.f - kAccentDecFloor) * _accent * _decay_n)
```

with `kAccentDecFloor = 0.3f` and `_decay_n` the DEC knob position `0..1`.

This is the coupling the feature is built around: **the room the accent has to
shorten a note is the room the DEC knob dialled in.** At DEC 0 the term
vanishes and the accent cannot touch the envelope at all; at DEC 1 the weakest
note decays in 30 % of the set time; between them the effect grows with the
setting. The knob stays the ceiling — the accent only ever subtracts.

Two things this needs that do not exist today:

- **The engine must keep the knob position.** `set_decay()` stores only
  `_decay_ratio = 0.1 · 80^n` (`synth_engine.cpp:390`) and the mapping is not
  worth inverting. Store `_decay_n` beside it.
- **The voice must latch the scale.** `set_env_times(attack_s, decay_s)` is
  pushed to *every* voice on *every* control tick (`:288`), so a decay written
  at trigger time is overwritten within a control block. Add
  `V::set_decay_scale(float)`, set in `_do_trigger` next to `set_vel`, applied
  where the voice consumes `decay_s`. Unlike VEL, the `_sustaining` closure of
  §5 does not protect this one — the refresh is unconditional.

Note the useful side effect of `decay_s` already being cycle-relative
(`decay_s = _decay_ratio · _cycle_s`, `:276`): the accent inherits that and
stays correct across tempo.

## 7. Engine scope

**SYNTH, WAVE and BODY.** All three are `SynthEngineT<V>` instantiations, so
both halves land in one place.

**SAMPLER and BBD take neither**, via the no-op default. The sampler has no
per-note envelope to scale — its DECAY is a grain window — and on the BBD
DECAY trims the frozen loop below its unity reference, which is not a note
property at all. This is exactly the set for which `_flow_melody` is true
(`part.cpp:441`), i.e. the same cut the deck already makes between "the PITCH
lane is a note" and "the PITCH lane is a read position".

Whether a sampler cloud should get a density-dependent accent is a separate
question with a separate answer, and it is out of scope here.

## 8. Gates

Each one is to be proven red once before it is trusted
(`fireflow-tests-must-be-able-to-fail`).

- **G1 — the anchor is loud.** At `k == 1`, every note that fires reports
  `a == 0`. Swept over STEPS 4/8/16 and the four seeds. Red if the
  normalization is ever taken against `k` instead of `L`.
- **G2 — the contour spans.** At DENSE 1.0 the set of accents emitted over one
  cycle equals `{ r/(L-1) : r in 0..L-1 }` and therefore reaches both 0 and 1.
  Red if the rank lookup loses the `% L` or reads a stale groove.
- **G3 — FLOW is silent about it.** A note deck in FLOW reports `a == 0` for
  the whole run, and a deck driven STEP → FLOW → STEP reports 0 throughout the
  FLOW leg.
- **G4 — DENSE is the intensity.** The spread of per-note peak levels at
  DENSE 1.0 is strictly greater than at the DENSE value that yields `k == 1`,
  measured on rendered audio rather than on the accent value. This is the gate
  that fails if the accent is computed correctly and then never reaches the
  sound.
- **G5 — the DEC coupling is real in both directions.** At DEC knob 0 the
  rendered note lengths at DENSE 1.0 are identical to the accent-free
  reference; at DEC knob 1 they are measurably not. A gate asserting only the
  second half would pass on an implementation that ignores `_decay_n`.
- **G6 — the chord compensation survives.** At `a == 0`, a COLOR > 0 chord
  produces the level it produces today. This is what pins §5's claim that the
  accent composes with `1/sqrt(n)` instead of replacing it, and it is the
  observable face of the `_sustaining` closure.

## 9. Consequences

- **The render hashes move.** Any scenario with a STEP note deck at DENSE > 0
  changes, so `tests/check_render_hash.cmake` is re-baselined in the same
  round. Per house rule these are sanity checks, not identity gates
  (`fireflow-bit-exactness-not-required`), but they are not self-updating.
- **No RNG draw is added or moved.** The accent reads a table the groove
  generator already filled. Determinism at a given seed is untouched, and the
  COLOR-0 short-circuit that byte-identity depends on (`fireflow-gotchas`) is
  not on this path.
- **Two by-ear constants.** `kAccentVelFloor` and `kAccentDecFloor`, both 0.3
  on the first try, deliberately equal so the first listening session says
  which of the two halves wants to be different. They are tuning values, not
  invariants — no gate above depends on either number.
