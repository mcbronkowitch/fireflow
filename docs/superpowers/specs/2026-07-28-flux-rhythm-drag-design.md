# FLUX drag — the other deck's rhythm owns the clock

**Date:** 2026-07-28
**Status:** design approved (owner, 2026-07-28), not yet planned
**Supersedes:** `docs/superpowers/specs/2026-07-28-flux-rhythm-taps-design.md`
entirely. No tap lines are built.
**Depends on:** `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`
(the BBD rewrite, branch `bbd-delay`)

## What this is

One knob that hands FLUX's delay time over to the *other* deck's rhythm.

Not extra echoes beside the existing one — the existing one, warped. At zero the
instrument behaves exactly as it does today. Turned up, the echo's repeat
interval is pulled toward the neighbour's two most recent gaps, alternating
between them, so the tail limps in the neighbour's groove instead of running
on the grid.

## Why this replaces the tap bank

The same idea — one deck's *timing* articulating the other deck's *material* —
was specified this morning as two extra delay heads per part, and priced on
hardware this afternoon. The measurement is what killed it, and the reframe is
what makes the idea affordable:

| | tap bank | drag |
|---|---|---|
| new `BbdLine`s | 4 (after mono) | **0** |
| worst-case CPU | 14.4 % of the block budget | **0 per sample** |
| SDRAM | 32 KiB | 0 |
| whole-instrument gate | blocking (`system` profile) | none |
| audible at low MIX | only with its own send (§4.1 of that spec) | always — it *is* the echo |

The tap bank also had to fight the model at three points, and each fight is
simply absent here: bandwidth fell in the direction of burial, the 32 kHz clock
ceiling collided with short gaps and produced doubled taps, and cost scaled
with line count. None of those exist when the neighbour's rhythm moves a clock
that is already running.

**And it is what the device does.** In a BBD the clock *is* the instrument —
design claim 2 of the BBD rewrite, confirmed by ear. The real Deluxe Memory Man
modulates exactly this timing node with its LFO; that is what the chorus switch
is. Replacing that LFO with the neighbour's rhythm is the most native gesture
the model has. Parallel read heads were a tape idea wearing a bucket brigade's
clothes.

**One finding from the retired spec is kept**, because it is about the BBD and
not about taps: the shipping echo lines measure 561 cycles per sample per line,
~22.4 % of the block budget for the four of them, against the BBD design's
~260-cycle estimate (`docs/bench/2026-07-28-6338f63-bbd.md`). That correction is
owed to the BBD spec and is unaffected by this one.

## 1. The mechanism

### 1.1 Two intervals, from the neighbour

`Instrument::rhythm(1 - p)` — the other deck's PITCH lane, symmetric in both
directions, the same source the tap design used. `RhythmView` publishes `gap[2]`
plus `valid`, and `valid` is false until three onsets have been seen, which is
what makes the whole feature self-gating: no neighbour rhythm, no effect,
without a mute switch.

`engine/fx/drag.{h,cpp}` carries one pure function:

```cpp
void derive_intervals(const RhythmView& rv, int32_t out[2]);
```

It is `derive_offsets` from `main` (deleted with the tape at `e004a3d`) with its
two rules intact and its output re-read:

- **The uniformity guard stays, and earns its keep for a new reason.** When both
  gaps fall within `kUniformTol` of their mean, the second is spread to
  `kUniformSpread × g0` — the MOTION lane's ×3/4 polyrhythm. In the tape era the
  argument was "evenly spaced taps *are* a delay". Here it is sharper: RATE is
  already tempo-synced to divisions, so an echo locked to an even neighbour
  rhythm is a sound the instrument already makes. The limp is the thing that
  cannot be had any other way, and the guard is what guarantees one.
- **`kMinGap` stays.** Below 32 samples a gap is a buzz, not a rhythm, and it
  would make `kUniformSpread × g0` round into a second gap equal to the first.
- **Intervals, not cumulative offsets.** The old function returned `g0` and
  `g0 + g1`, because a tap is a *position* behind the write head. A repeat
  interval is a *duration*, so `out[1]` is `g1`. One line, and the only
  substantive change to the function.
- **Both bounds are gone.** The tap design needed a `min_offset` (against the
  clock ceiling silently collapsing two taps onto each other) and a
  `max_offset` (against a tap so dark it read as a thud). Neither applies to a
  single line: `bbd_clock_hz` already clamps at `kClockMaxHz`, and with one
  line there is nothing for a clamped value to collide *with*. A long target is
  just a slow dark echo, which RATE can already ask for. `derive_intervals`
  needs no sample rate and no bounds at all.

Layering is unchanged and still one-directional: `drag.h` includes
`mod/rhythm_view.h`, never the reverse — the rule `rhythm_view.h:13-18`
protects, and the "future fx consumer" it anticipated.

### 1.2 The target alternates on the echo's own repeats

Self-clocked, deliberately. `Flux` accumulates elapsed samples at control rate
and flips the active target index each time the *current* delay time has
elapsed:

```
_drag_phase += ctrl_block_samples
if (_drag_phase >= t_current · sr) { _drag_phase -= t_current · sr; _drag_i ^= 1; }
```

One add and one compare per control block. Since the echo's repeat interval
*is* `t_current`, "one target per elapsed delay time" is exactly one target per
repeat, without any cross-deck event.

The alternative — stepping on the neighbour's onsets — would tie the two decks
together audibly rather than only rhythmically, and is the more expressive
option. It is **out of scope here** (§7) because `RhythmView` carries gaps
latched at a cycle wrap and no onset event, so it needs plumbing this design
does not. If the self-clocked version proves musical, that is the next step, not
a replacement.

### 1.3 The knob interpolates geometrically, and the existing slew does the rest

```
T[i]    = out[i] / sample_rate                    the neighbour's interval
t_echo  = t_ladder^(1-d) · T[_drag_i]^d           d = DRAG, 0..1
```

Geometric, because pitch tracks the clock ratio directly — the same reasoning
that gave the modulation lane its ×1/4…×4 mapping. At `d = 0` the ladder is
untouched and the instrument is **bit-identical to today**. At `d = 1` the
neighbour owns the time outright and RATE goes inert; that is a clear and
teachable behaviour, not a defect.

`t_echo` is written into `_dt_target`, the existing 30 ms slew's input
(`flux.cpp:18,148`). **That slew is what turns a step into a bend** — the whole
pitch-glide behaviour comes for free from a smoother that is already there, and
this design adds no smoothing of its own. `_stage_current`, the ceiling clamp
and the lane's multiplicative pull downstream (`flux.cpp:167-171`) are all
untouched: DRAG moves the base time, exactly where the RATE ladder moves it.

### 1.4 Three things arrive together, and they are the point

Because one clock carries all three, a single knob produces:

- **a limp** — successive repeats land at `g0`, `g1`, `g0`, … instead of on a grid;
- **a pitch bend at every step** — whatever is in the line when the clock moves
  is bent, the tape-nudge the BBD spec documents as a feature;
- **a tonal breath** — bandwidth is `f_clk / 4`, so the long step is darker than
  the short one and the echo pulses in brightness with the neighbour's groove.

None of the three costs anything beyond the interpolation that produces the
first.

## 2. Controls

**DRAG takes DRIVE's panel slot** — `{44.250, 89.400}` and `{169.110, 89.400}`,
same `WK_SMKNOB`, generated by `res/gen_panel.py`. `PART_STRIDE` stays 23,
STAGES is unaffected. **Default 0**, which is also the bit-identical path.

**DRIVE moves to the right-click menu**, same shape as `Detune A/B`
(`Spotymod.cpp:1215-1229`): a real param with a `ParamQuantity`, surfaced as a
`ParamMenuSlider`, no panel widget. Automation and patch storage unaffected. New
default 0.20 (today 0.15, `init_patch.hpp:81`). The criterion is already written
above the excitation-source menu (`Spotymod.cpp:1230`): *"patch state, not a
performance control."* You set DRIVE once; you ride DRAG.

That trade deserves stating plainly, because DRIVE was made audible only two
days ago over two measurement rounds and two fixes. Against a plain tap level it
would have been close. Against a knob that re-times, bends and re-colours the
echo from the other deck's playing, it is not.

On hardware DRIVE becomes a setup value with no knob. Nothing depends on that
today — `set_drive` is called from bench, render and VCV, from no firmware host.

**No migration.** The project is in development and old patches are explicitly
not a constraint (owner's decision, 2026-07-28). The `host/vcv/README.md`
upgrade warning about DUST landing on DRIVE is removed rather than reworded.

**The name.** DRAG, for tape drag: one syllable, physical, and it sits beside
GRIT and FLUX. TAPS no longer describes anything.

## 3. Where it lives

`Flux` gains `set_drag(float)` and `set_rhythm(const RhythmView&)`, matching the
shape of its existing setters, plus two members (`_drag_phase`, `_drag_i`). One
DRAG state per part, since one `Flux` already carries both channels
(`Flux::process(float& l, float& r)`).

`Instrument` already publishes `rhythm(int p)` and already pushes cross-deck
state once per control block (`_other_deck_tap`, `instrument.cpp`). The rhythm
push joins that cadence. `derive_intervals` runs there, never per sample.

**No new memory, no new allocation site, no `FxMem` change.** The tap design's
whole §1.2.1 — host-provided buffers, a `kTapSamples` constant, a parallel
`FxMem` field across four call sites — has no counterpart here.

## 4. CPU

Per sample: **no new arithmetic of consequence.** At DRAG 0 it is one extra
compare (`_drag > 0.f`) against a path that otherwise runs unchanged. Engaged,
it is one add and one compare to advance the step counter; the interpolation
itself — two `powf`-class operations — runs only on a step change, not on
every sample.

Per control block, per part: one add, one compare, two `powf`-class operations
for the geometric interpolation. If those turn out to matter they become a
`daisysp::fmap`-style approximation, but a per-control-block cost against a
96-sample block is roughly 1 % of a per-sample cost by construction.

**This design needs no bench gate**, which is why it can be planned
immediately. The `system`-profile run that the tap design was blocked on
remains worth doing — `instrument_worst` at 97.5 % predates the BBD entirely
and describes nothing current — but it is no longer a precondition of this
work.

## 5. Tests

- **`derive_intervals`**, from the restored `test_taps.cpp` cases: the
  uniformity guard, `kMinGap`, an invalid rhythm. Adapted to intervals, minus
  the two bound cases that no longer exist.
- **DRAG at 0 is bit-identical to today.** The strongest assertion available
  here and the one that pins "no regression" for every existing patch. Same
  shape as `test_flux.cpp`'s existing bit-exact off-path test.
- **DRAG at 1 limps.** Successive echo arrivals alternate between `g0` and `g1`
  within tolerance, measured the way `test_flux.cpp`'s `first_echo_index` does.
- **A step bends pitch.** The clock ratio across a target change produces the
  expected frequency ratio — the property §1.4 sells, asserted rather than
  assumed.
- **Self-gating.** `rv.valid == false` → DRAG has no effect at any setting.
- **Symmetry.** A follows B and B follows A, in one case.
- **RATE still reaches the ladder** at intermediate DRAG, i.e. the
  interpolation is not accidentally saturating.

Must stay green *and* load-bearing: the two witnesses in `test_part_fx.cpp`
(soft clip, DC block). Their premise already shifted once this week; both are
re-proven RED with their stage removed rather than merely observed green.

## 6. Risks

**The ear pass owns this design more than any measurement does.** Three things
can only be settled by playing it:

1. **High FEEDBACK plus deep DRAG bends the entire tail on every step.** That is
   either the best thing here or seasickness. The lever if it is the latter is
   the 30 ms slew — lengthening it turns steps into drifts — not a change to the
   mechanism.
2. **Fast neighbour rhythms may never let the echo settle.** The targets change
   at the neighbour's cycle wraps; a busy PITCH lane means a clock that is
   always moving.
3. **`d = 1` makes RATE inert.** Defensible and teachable, but it is the kind of
   thing that reads as a bug the first time it happens.

**A design-level risk worth naming:** this is, strictly, a new modulation
*source* for a target that is already modulatable (`FXT_FLUX_TIME`). What earns
it a panel slot is that the source is not available as a lane and that it is the
only thing in the instrument that makes the two decks play *with* each other
rather than beside each other. That argument is sound but it is the weakest
joint in the design, and if the ear pass is lukewarm this is where to look
first.

## 7. Out of scope

- **Onset-locked stepping (variant B).** The more expressive option, needing a
  cross-deck onset event `RhythmView` does not carry. Deferred by owner's
  decision, 2026-07-28: try the simple one first.
- Tap lines, in any form. See the superseded spec for what was priced and why it
  did not survive.
- Restoring the tape delay, or a mode switch between delay models.
- Feeding anything new into BODY's excitation bus. The tap design's §5 — taps as
  BODY's sole exciter while active — has no counterpart here and is dropped
  along with the taps. BODY keeps the echo it gets today.
- Any migration path for patches saved before this change.
