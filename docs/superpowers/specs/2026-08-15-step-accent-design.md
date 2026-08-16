# STEP accent: per-note velocity and decay from the groove rank

**Date:** 2026-08-15
**Status:** implemented; merged to `main` 2026-08-16, released in 2.21.4

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

`_note_accent` initialises to 0 and is cleared on every mode change alongside
`_cur_step` and `_frozen` (`lane.cpp:210`), and the accessor is additionally
`_step_mode`-guarded:

```
float note_accent() const { return _step_mode ? _note_accent : 0.f; }
```

The reset, not the guard, is what covers every reachable path: `_note_accent`
is written only by `_start_note`, which runs under `_step_mode` alone
(`lane.cpp:671`); `set_step()` is the only thing that changes mode; and it
zeroes `_note_accent` on every mode change (`lane.cpp:211`), before the
accessor's guard is ever consulted. So no reachable sequence can leak a stale
STEP accent into FLOW, guard or not — and no gate can be written that
distinguishes the two, which is why none does. The guard stays anyway, as
deliberate redundancy against a future second writer of `_note_accent` outside
`_start_note`: one mode test, at the one place that cannot be bypassed, costs
one comparison and is cheap insurance the day that writer appears. **FLOW is
then constant 0 with no mode test in any consumer** — the STEP-only scope is a
property of the source, not a condition sprinkled across the engines.

## 4. Delivery

`Part::_fire_trigger()` pushes `_engine->set_accent(_mod.note_accent())`
immediately before `trigger_chord()`.

`IPartEngine::set_accent(float)` is a new virtual with a no-op default — the
`set_gate` / `set_cycle` / `set_excitation` idiom (`engine_iface.h`). Engines
that do not implement it cost nothing and need no per-engine branch at the call
site, and an engine swap needs no re-sync reasoning because whichever engine is
active is exactly the one that gets pushed.

**`Part::trigger_manual()` is a second call site, and it does not inherit the
sequencer's accent.** `trigger_manual()` (the PLAY tap / TRIG press) calls
`_engine->trigger_chord()` directly, on the same footing CHOKE already gives
it — `part.h`'s comment on the method: "a user gesture and is deliberately
NOT inhibited" by the sequencer's policy. A manual strike is an anchor by
definition: there is nothing for it to be subordinate to, exactly the §1
argument for why a `k == 1` STEP note is loud. So `trigger_manual()` pushes
`_engine->set_accent(0.f)` immediately before `trigger_chord()`, unconditionally
-- a manual strike always lands at full strength, whatever accent the last
STEP fire left sitting in the engine. Measured, before this push existed: a
press landing while the engine held `_accent = 0.857143` produced audio
differing in 9806/24000 rendered samples (max `|d|` 0.0369) from the same
press with the accent cleared -- roughly 40% velocity instead of full
strength, silently. Gated by
`tests/test_step_accent.cpp`'s "a manual trigger strikes at full strength
even while the engine holds a high accent from the sequencer" case.

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
chord compensation `1/sqrt(n)` (`synth_engine.cpp:164`, `:273`). The two are
independent quantities — how many notes are sounding at once, and how strong
this note is meant to be — and composing them is correct. At `a == 0` the
product is the value that ships today, which is what makes the "chord
compensation unchanged" gate in §8 possible.

**The latch survives.** The per-control-tick refresh `_voices[v].set_vel(_vel_now)`
(`synth_engine.cpp:370`) is gated on `_sustaining[v]`, and in STEP no voice is
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

This "30 % of the set time" figure is exact for `VoiceT` (SYNTH/WAVE), whose
`set_decay_scale` multiplies `decay_s` linearly. It is not exact for
`BodyVoice` (BODY): `_apply_env()`'s `_damping = d_s / (d_s + 1)` is not
linear in `d_s`, so the same scale factor produces a different proportional
ring time. Measured (seed 99, cycle 0.25 s, DEC knob 1.0): SYNTH 42929 →
13231 samples (ratio 0.3082, matches `kAccentDecFloor`); BODY 212802 → 32893
samples (ratio 0.1546, about twice as strong). Full write-up:
`docs/engine-map.md` §8. This is a documentation fact, not a defect — BODY's
damping curve is unchanged, long-standing, by-ear design.

Two things this needs that do not exist today:

- **The engine must keep the knob position.** `set_decay()` stores only
  `_decay_ratio = 0.1 · 80^n` (`synth_engine.cpp:415`) and the mapping is not
  worth inverting. Store `_decay_n` beside it.
- **The voice must latch the scale.** `set_env_times(attack_s, decay_s)` is
  pushed to *every* voice on *every* control tick (`:314`), so a decay written
  at trigger time is overwritten within a control block. Add
  `V::set_decay_scale(float)`, set in `_do_trigger` next to `set_vel`, applied
  where the voice consumes `decay_s`. Unlike VEL, the `_sustaining` closure of
  §5 does not protect this one — the refresh is unconditional.

Note the useful side effect of `decay_s` already being cycle-relative
(`decay_s = _decay_ratio · _cycle_s`, `:302`): the accent inherits that and
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
  `a == 0`. Swept over STEPS 4/8/16 and the four seeds. This does **not**
  discriminate the `L`-vs-`_groove_k()` normalization choice (G2,
  intermediate DENSE, below) — measured: at `k == 1` the sole firing slot is
  always rank 0, so its accent is `0 / anything == 0` under either
  denominator.
- **G2 (DENSE 1.0) — the contour spans.** At DENSE 1.0 the set of accents
  emitted over one cycle equals `{ r/(L-1) : r in 0..L-1 }` and therefore
  reaches both 0 and 1. Red if the rank lookup reads a stale groove — exactly
  the failure the settle-wrap in every helper in `tests/test_step_accent.cpp`
  exists to guard against. The `% L` in `rank_of_slot[slot % L]` is **not**
  exercised by this gate, or by any gate in this file: in every settled state
  `pattern_groove.len == _effective_length()`, so `slot % L == slot` there and
  dropping the modulo is a no-op post-wrap. It is load-bearing only in the
  pre-wrap window immediately after `set_step()`, where the groove table is
  still the previous cell's (and the modulo also prevents an out-of-range
  read there) — a window every test helper here deliberately runs past before
  it trusts anything it reads. Like G1, this cannot discriminate the `L`-vs-`_groove_k()`
  normalization either — at DENSE 1.0, `_groove_k()` computes `k == L`
  exactly, so the two denominators are numerically identical there.
- **G2 (intermediate DENSE) — the normalization is `L`, not `k`.** At an
  intermediate DENSE where `k < L` (e.g. 8 steps, DENSE 0.5, `k = 4`), the set
  of accents emitted over one cycle equals exactly `{ r/(L-1) : r in 0..k-1 }`.
  Red if `_start_note` normalizes against `_groove_k() - 1` instead of
  `groove_length - 1`: measured, the correct code's maximum accent there is
  `3/7 ≈ 0.4286`; the mutation gives `1.0` instead. Neither G1 nor G2
  (DENSE 1.0) can catch this, for the reasons stated above — this is the only
  case that does.
- **G3 — FLOW is silent about it.** A note deck in FLOW reports `a == 0` for
  the whole run, and a deck driven STEP → FLOW → STEP reports 0 throughout the
  FLOW leg. The property is redundantly protected, not unprovable:
  `_note_accent` is written only by `_start_note` (STEP-only, §3), and
  `set_step()` zeroes it on every mode change before `note_accent()`'s guard
  is ever consulted — each of the two mechanisms independently holds the
  line, so no *single* mutation of guard-or-reset can redden this gate; only
  removing both at once does — measured: dropping `note_accent()`'s guard
  (`lane.h:126`) and `set_step()`'s `_note_accent = 0.f` reset
  (`lane.cpp:211`) together produces `CHECK_FALSE( leaked ) is NOT correct!
  values: CHECK_FALSE( true )`, a stale STEP accent leaking into the FLOW
  leg; reverted, the case passes again. That is why Step 2's single-mutation
  round did not redden it — a different claim from "cannot be proven red."
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

- **The render hashes are expected NOT to move.** An earlier draft of this
  section assumed the opposite. Both hashed scenarios run in FLOW, where the
  accent is 0 by §3: `wave_formant_sweep.json:10` sets `set_step` with
  `"flag": false` explicitly, `ctrl_identity.json` carries no `set_step` action
  at all, and `ModLane::_step_mode` boots false (`lane.h:308`). So
  `ctrl_identity` and `wave_formant_sweep` should pass untouched — and if
  either moves, that is a **finding**, not a baseline to bump: it would mean
  the accent is reaching a FLOW deck. The re-baseline procedure in
  `tests/check_render_hash.cmake` is not expected to be needed here.
- **No RNG draw is added or moved.** The accent reads a table the groove
  generator already filled. Determinism at a given seed is untouched, and the
  COLOR-0 short-circuit that byte-identity depends on (`fireflow-gotchas`) is
  not on this path.
- **Two by-ear constants.** `kAccentVelFloor` and `kAccentDecFloor`, both 0.3
  on the first try, deliberately equal so the first listening session says
  which of the two halves wants to be different. They are tuning values, not
  invariants — no gate above depends on either number.
