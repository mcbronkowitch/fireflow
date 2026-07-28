# FLUX rhythm taps — design

**Date:** 2026-07-28
**Status:** **SUPERSEDED, never implemented.** Replaced entirely by
`docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md` (same day). No
tap lines are built.

The idea survives; the implementation does not. Rather than adding delay heads
beside the echo, DRAG hands the existing echo's clock to the neighbour's
rhythm — zero new lines, zero CPU per sample, and none of the three fights with
the BBD model that this document works so hard to win (bandwidth falling toward
burial, the clock ceiling colliding with short gaps, cost scaling with line
count).

**Kept for the record**, because it is where the numbers live: §8's hardware
measurements of the BBD, including the correction that the shipping echo costs
2.2× its design estimate, and §10's account of what two review passes found.
Read it for those; do not build from it.
**Depends on:** `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md` (the BBD rewrite, branch `bbd-delay`)

## What this is

Two extra delay heads per part, placed by the *other* deck's rhythm, audible in
FLUX's output and — when they are sounding — the sole excitation for BODY.

This restores an idea, not an implementation. The tape-era `TapBank` did
exactly this and was deleted with the tape (`e004a3d`), because a BBD has no
read pointer to move. What went with it was the only thing in the instrument
that made one deck's *timing* articulate the other deck's *material*. The
excitation bus still has an "other deck" source (`Part::_other_deck_tap`), but
it carries the sibling's raw audio — continuous, not articulated. That is the
gap.

## Why it is cheap now, and was not before

The obvious way to get the idea back is to restore the tape delay alongside the
BBD behind a mode switch. Costed: **+3.88 MiB** (the tape needs 262,144 floats
per channel against the BBD's 8,192; sharing one buffer between two engines
still means allocating the larger), a mode switch on a panel that must stay
reducible to real hardware, two knobs that change meaning per mode, and the
restored tap bank on top. It also drags back `taps.{h,cpp}`'s tape-specific
machinery.

Own tap lines instead cost **32 KiB**, for the reason the BBD rewrite exists:
**delay length is clock rate, not buffer size.** A two-second tap does not need
a bigger buffer, only a slower clock. What it costs instead is bandwidth, since
bandwidth follows the clock — and that is a two-edged gift (§3).

## 1. Architecture

New `engine/fx/taps.{h,cpp}`. The name is reclaimed; one piece of the content
is not new.

### 1.1 `derive_offsets` returns with its rules intact and its bounds opened up

```cpp
void derive_offsets(const RhythmView& rv, int32_t min_offset,
                    int32_t max_offset, int32_t out[2]);
```

Restored from `main` with one change of shape. The two rules that were
expensive to arrive at come back untouched:

- **The uniformity guard.** Evenly spaced taps *are* a delay; that diagnosis
  killed zone S twice. When both gaps fall within `kUniformTol` of their mean,
  the second is spread to `kUniformSpread × g0` (the MOTION lane's ×3/4
  polyrhythm) so the result limps instead of gridding.
- **Mute, never clamp.** An out-of-range offset silences that tap. Clamping
  would place two taps at the same position, turning a missing echo into a
  doubled one.

What changes: the old signature took a single `tape_len` and derived its upper
bound from it, because on tape the only bound that existed was the buffer. A
BBD has no buffer bound and two *physical* ones instead, so both arrive as
arguments and the function stays pure:

- **`min_offset` is new, and it is not cosmetic.** `bbd_clock_hz` clamps at
  `kClockMaxHz = 32000` (`bbd.h:88,205`). At `kTapStages = 4096` that ceiling
  binds at `4096 / (2 × 32000)` = **64 ms**: anything shorter is silently
  pulled up to 64 ms. Two taps at 20 ms and 45 ms would both land there — the
  doubled echo that "mute, never clamp" exists to prevent, arriving through the
  clock instead of through a clamp. 64 ms is reachable, not theoretical: 1/32
  at 120 BPM is 62.5 ms. The bound depends on the sample rate, so it cannot be
  a header constant; `Flux` computes it and passes it in.

  **64 ms, not higher.** A larger floor is a real CPU lever — it caps the
  clock, and half the clock is 26 % off a tap line (§8) — but it is charged
  musically at a bad rate. At 128 ms a 1/16 PITCH lane at 120 BPM (125 ms)
  falls under it, so the most ordinary rhythm in the instrument would mute its
  near tap and show a single echo. The physical floor costs nothing and mutes
  only what the clock genuinely cannot render. A raised floor stays available
  as a pre-authorised lever if the budget demands it (§8).
- **`max_offset` is a musical bound, and it needs a number.** There is no
  buffer to run out of. **1.5 s** (≈341 Hz of bandwidth, §3) is the point below
  which a tap stops reading as an echo and starts reading as a thud. It is an
  ear value and is marked as one.

`tap_tuning::kMinGap` (32 samples) survives alongside `min_offset` and is not
redundant with it: it rejects gaps that are a buzz rather than a rhythm, and it
guards the spread the uniformity guard applies. Different concern, kept.

One deletion: the `static_assert` on `Flux::kMaxSamples` being a power of two.
It guarded `TapeTap`'s AND-masked read, and there is no masked read any more.

### 1.2 `TapBank` owns two **mono** `BbdLine` per part

One line per tap, not one per tap per channel. Each line is an independent
head, fed from the summed send and placed in the image by gains:

```
part send ─┬─→ BbdEcho   (main line, stereo: FEEDBACK, DRIVE, compander) ──→ echo
   (L+R)/2 ├─→ BbdLine   clock set so its delay == r·gap[0]         ─┐
           └─→ BbdLine   clock set so its delay == r·(gap[0]+gap[1]) ─┴→ pan → taps
```

Parallel heads off the **same send**, not in series behind the echo. That makes
a tap what it was before: this deck's own delayed material, at a position the
neighbour chose. No feedback on the tap lines.

**Mono is a measurement, not a preference.** A bare `BbdLine` in the tap
configuration costs 3.59 % of the block budget at its ceiling clock
(`docs/bench/2026-07-28-6338f63-bbd.md`). Stereo lines would be eight of those,
28.7 %, on top of the echo's four at 22.4 % — over half the block budget for
FLUX alone, which no other lever recovers (§8). Four mono lines are 14.4 %.

What it costs musically is close to nothing, because §4.1 pans the taps anyway:
a mono line at two gains produces the same image as two lines plus a pan. What
is genuinely lost is stereo information *within* one tap, which a sparse
rhythmic interjection barely carries.

**Two taps is the floor of the idea, not a budget compromise.** One tap placed
at `gap[0]` is just another echo — the "evenly spaced taps *are* a delay"
diagnosis that killed zone S twice applies directly, and the uniformity guard
would have nothing to guard. §4.1's knob would also lose its first half. Two
taps make a figure; one makes a repeat.

`r` is the TAPS ratio (§4.1). The clock for each line is

```
f = clamp( kTapStages / (2 · r · offset_seconds) · lane_mult , 0, kClockMaxHz )
```

built from the existing `bbd_clock_hz(offset_seconds, kTapStages)` with the
ratio folded into the time and the lane multiplier applied after, exactly as
`Flux::process` already does for the main line (`flux.cpp:167-171`).

**The ratio is applied to the bounds, not to the offsets.** `Flux` passes
`min_offset / r` and `max_offset / r` to `derive_offsets` and multiplies the
surviving offsets by `r` afterwards. Same result, and it keeps `derive_offsets`
free of the ratio. It also buys a property worth having: turning TAPS up
*recovers* taps that a fast neighbour rhythm had pushed under the floor.

**The tap lines do not follow STAGES.** They are fixed at `kTapStages = 4096`.
Following the knob looks free and is not: more stages need a *faster* clock for
the same delay, so at 16384 stages the 32 kHz ceiling binds below 256 ms and
most rhythm gaps would fall under the floor. Turning STAGES up would mute the
taps in rows. The echo sweeps its brightness; the taps hold theirs.

### 1.2.1 Where it lives, and who feeds it

`TapBank` is a member of `Flux`, not of `PartFx`. It has to be: §4.1 puts the
taps into `Flux::process`'s output sum, and that sum is computed inside
`Flux::process`. `Flux` gains `set_taps(float norm)` and
`set_rhythm(const RhythmView&)`, matching the shape of its existing setters.

**Buffers come from the host, exactly as the echo's do.** `Flux::kMaxSamples`
drives every allocation site today — `Spotymod.cpp:162`, `render/main.cpp:13`,
`bench/mem.cpp:36`, and `FxMem::echo` in `instrument.h`. A parallel
`Flux::kTapSamples = kTapStages / 2` constant and a parallel host-provided array
follow the same pattern. `Flux::init` takes the tap storage alongside the echo
storage; a null tap pointer disables the bank, the way a null echo pointer
already disables FLUX (`_buf_ok`).

**Offsets update at control rate.** `Instrument` already publishes
`rhythm(int p)` and already pushes cross-deck state once per control block
(`_other_deck_tap`, `instrument.cpp`). The rhythm push joins that existing
cadence — `derive_offsets` and the two `SetClock` calls it produces run there,
never per sample.

`BbdLine::SetClock` is a single assignment (`ticks_ = 2·hz/sr`) that touches no
line state, so re-pushing an unchanged rhythm is harmless. That it does not
reset the line is also precisely what §6 depends on: the stored charge stays put
while the clock moves, which is the pitch bend.

A parallel `FxMem` field (`float* taps[PART_COUNT][2]` — part, tap) carries the
storage, alongside the existing `echo[PART_COUNT][2]`. The two indices are not
the same two: `echo`'s second index is the channel, `taps`' is the tap, because
the tap lines are mono (§1.2). Same shape, different meaning — worth a comment
at the declaration.

### 1.3 Rhythm source

`Instrument::rhythm(1 - p)` — the *other* deck's PITCH lane, symmetric in both
directions. `RhythmView` publishes `gap[2]` plus `valid`; `valid` is false until
three onsets have been seen, which is what makes the taps self-gating (§4).

`rhythm_view.h:13-18` anticipated this consumer explicitly ("should a future fx
consumer of RhythmView appear"). The layering rule it protects — the mod layer
must not include fx headers — is preserved: `taps.h` includes
`mod/rhythm_view.h`, never the reverse.

## 2. Memory

`kTapStages = 4096` → 2048 cells → 8 KiB per line.

| | lines | total |
|---|---|---|
| main BBD lines (today) | 4 (stereo × 2 parts) | 128 KiB |
| tap lines (new) | 4 (mono × 2 taps × 2 parts) | 32 KiB |
| restoring the tape instead | — | 3.88 MiB |

On Daisy this lives in SDRAM (`bench/mem.cpp:36`, `DSY_SDRAM_BSS`) where it is
not a constraint. In VCV it is a per-instance member array; 32 KiB against the
~38.7 MB an instance already takes is noise.

**Memory is not what decides anything here, and the bench says so twice.**
`bbd_walk_sdram` costs 0.09 % of the block budget, and a tap line at 4096
stages (3.59 %) against one at 16384 (3.68 %) differs by 2.4 % at identical
clock. The working set is nearly free; the arithmetic is what costs. Both of
the decisions that look like memory decisions — mono lines (§1.2), not
following STAGES (§1.2) — were made on CPU and musical grounds respectively,
and the memory table merely follows them.

## 3. Bandwidth is the price, and it cuts both ways

Bandwidth follows the clock (`f_-3dB ≈ f_clk / 4`, `bbd.h:84`), so longer taps
are darker:

| tap offset | clock | bandwidth |
|---|---|---|
| 0.064 s (floor) | 32000 Hz | 8.0 kHz |
| 0.25 s | 8192 Hz | 2.0 kHz |
| 0.5 s | 4096 Hz | 1.0 kHz |
| 1 s | 2048 Hz | 512 Hz |
| 1.5 s (ceiling) | 1365 Hz | 341 Hz |

The tape-era tap bank had a ROT knob whose whole job was to filter the taps
apart from the echo (`kLpOpenHz`/`kLpSplitHz`, `kHpOpenHz`/`kHpSplitHz`). The
per-tap clock now does part of that by physics — but only part, and only in one
direction. It separates the taps tonally by moving them **down**; ROT could
also move their band **up** (`kHpSplitHz = 1500`), into where a mix has room.
Down is the same direction as being buried.

**ROT's filter spread is still dropped**, not ported — one less control, and
closer to the part being modelled. But the audibility work it was doing does
not disappear with it; §4.1 does that work with level and position instead.

## 4. Controls

### 4.1 TAPS takes DRIVE's panel slot, and is one knob doing one thing

`TAPS` occupies `{44.250, 89.400}` and `{169.110, 89.400}` — the positions DRIVE
vacates — same `WK_SMKNOB`, label `TAPS`, generated by `res/gen_panel.py`.
`PART_STRIDE` stays 23. STAGES is unaffected. Default **0**: the taps change
BODY's excitation source (§5), which is too much behavioural change to ship on.

| knob | taps | ratio `r` |
|---|---|---|
| 0 | silent | — |
| 0 → 0.15 | tap 0 fades in | ×1 |
| 0.15 → 0.3 | tap 1 fades in | ×1 |
| 0.3 → 1.0 | both at full level | ×1 → ×2 |

Two quantities, one knob, and they do not fight because on a BBD they are not
independent: farther is darker by construction, and darker is quieter to the
ear. The knob reads as a single physical axis — distance. Turning it up tells
one story: one echo, then two, then they drift apart and lose an octave of
brightness doing it. No point on the travel changes the knob's meaning, and
0.3 is a usable rest position — full presence, unstretched.

The staggered fade-in is not invented here. It is the tape-era `set_dust`
mapping, whose own comment calls it *"an accent hierarchy (strong/weak), which
is the groove dimension a stepped tap count could not give"*. Restored
deliberately; a flat two-taps-together fade would be a level control, and a
level control is not worth a panel slot.

**Accepted limitation:** quiet *and* far is no longer reachable from TAPS
alone. Physically that is consistent — distant things are dull and recessed —
but a wide, very reticent field of taps now needs FLUX MIX to get there.

**The taps get their own send level and do not pass through `_mix_lin`.** This
reverses the tape era's "MIX is the single wet control" rule, deliberately and
for cause. `flux.cpp:174` is a send structure — `l += echo * _mix_lin` — and
`_mix_lin` spans −40…0 dB (`flux.cpp:108`). Taps placed before it arrive at
MIX 0.5 at roughly −23 dB, band-limited to a few hundred Hz. That is precisely
the failure the tape taps died of ("man hört sie nur wenn Delay weit
aufgedreht ist"), and the BBD makes it worse, not better, because these taps
are narrow where the tape's were full-band. So:

```
l += echo_l * _mix_lin + taps_l * _taps_lin
```

The bit-exact off path is untouched: the tap lines are fed from the same
`send`, inside the same `_sw.is_idle()` early-out, so FLUX off is still FLUX
off. MIX simply stops being their valve.

The gain this buys is not only audibility. **MIX becomes a crossfader between
two worlds:** MIX low with TAPS high is dry material with rhythmic
interjections and no echo wash — a sound the old arrangement could not make at
all, because the wash had to be bought to hear the taps.

**`kTapGain = 0.7` is retired as a starting point.** It was set by ear for
parity with a *full-band* tape read. Against a signal limited to 0.5–2 kHz,
equal amplitude is not equal audibility, and the value is expected to land
above 1. It is re-derived on the ear pass against a band-limited reference.

**Panning widens, and now carries the whole stereo job.** `kPanNear`/`kPanFar`
= 0.92388/0.38268 is 22.5° off hard — timid. Starting point 0.98/0.195
(11.25°). For a signal that already sits tonally under the echo, stereo
position is the last free axis of separation, and since §1.2 made the lines
mono these two gains *are* the tap's stereo image rather than a treatment
applied to one.

### 4.2 The ratio's other direction belongs to the modulation lane

`FXT_FLUX_TIME`'s multiplicative clock pull reaches the tap clocks too. Without
it nothing moves a tap at performance rate except the neighbour changing its
pattern — and §6, the best property this design has, would have no trigger.

**Bounded to ×1/2 … ×2 on the taps**, against the main line's ×1/4 … ×4. The
floor check in §1.1 runs on the un-modulated offset; a ×4 shove could push both
taps into the ceiling at once and collide them there. ±1 octave on the whole
constellation is plenty, and it halves that window. Clean division of labour:
the knob stretches outward, the lane bends both ways.

### 4.3 DRIVE moves to the right-click menu

Same shape as `Detune A/B` (`Spotymod.cpp:1215-1229`): a real param with a
`ParamQuantity`, surfaced as a `ParamMenuSlider` in a submenu, with no panel
widget. Automation and patch storage are unaffected. New default **0.20**
(today: 0.15, `init_patch.hpp:81`).

The criterion is already written into the codebase, above the excitation-source
menu (`Spotymod.cpp:1230`): *"patch state, not a performance control — there is
no panel knob for it on any engine, so it lives here."* DRIVE qualifies. You set
it once; you do not ride it.

That criterion is not the whole argument, because DRIVE was made audible only
two days ago at the cost of two measurement rounds and two fixes, and the BBD
spec's errata closes by freeing `kDriveHiDb` to be chosen for distortion
character alone — an invitation to make DRIVE *more* rewarding, not less
present. Against a plain tap level the trade would have been close. Against
§4.1's knob — presence, count and distance in one gesture — it is not.

On hardware DRIVE becomes a setup value with no knob. Nothing depends on that
today — `set_drive` is called from bench, render and VCV, from no firmware host.

**No migration.** The project is in development and old patches are explicitly
not a constraint (owner's decision, 2026-07-28). The `host/vcv/README.md`
upgrade warning about DUST landing on DRIVE is removed rather than reworded.

## 5. Excitation routing

`PartFx::_tape_tap` carries:

- **the taps alone** while they are active — `TAPS > 0`, `rv.valid`, and at
  least one non-muted offset;
- **the echo**, as today, otherwise.

A resonator lives on transients. The point of the tap bank as an exciter was its
sparse articulation, and mixing the continuous echo back in would bury it. The
fallback exists so BODY does not go silent when the neighbouring deck rests.

This path never had §4.1's audibility problem — as BODY's exciter the taps are
the *sole* source. That the side path was already sound is a plausible reason
the hole in the main path went unnoticed.

`fast_tanh(_tap_dc.Process(...))` in `part_fx.cpp:72` is unchanged. It now bounds
two unbounded sources instead of one — the tap lines are no more bounded to unity
than `BbdEcho` is (`BbdLine` carries a DC feed-through term, `fout_->H == 1`).

### 5.1 Tap lines run without a compander — deliberately

`BbdEcho` wraps its line in the NE570 model. The tap lines do not. In a real
machine the companding wraps the delay path once, not each head, and a compander
per tap would cost more and give the taps different dynamics from the echo.

"Would cost more" now has a number: the compander and drive path together are
the difference between `bbd_ceiling` (5.61 %) and `bbd_line_only` (3.68 %) —
**1.93 % of the block budget per line**, better than a third of an echo line.
Four tap lines with companders would be 7.7 % on top of §8's 14.4 %.

This remains a voicing decision, not a cost one; the cost merely agrees. It
belongs on the ear pass.

## 6. What a moving tap sounds like — a simplification the rewrite paid for

The tape-era `TapBank` needed dip-and-relatch: a 2 ms fade either side of an
offset jump (`kDipSeconds`, `kRelatchMin`), because moving a read pointer on a
tape clicks.

A BBD does not click. Changing its clock **bends pitch** — design claim 2 of the
BBD rewrite, confirmed by ear. A drifting tap offset therefore glides, exactly
as a RATE or STAGES change does on the main line. The entire dip machinery is
dropped.

With §4.1 and §4.2 this stops being a property the design merely tolerates and
becomes the reason to touch the knob: the TAPS ratio sweeps the whole
constellation outward, in pitch and in time and in brightness at once, and the
lane bends it back.

## 7. Testing

### 7.1 Restored, with one bound added

`tests/test_taps.cpp` returns from `main` for `derive_offsets`: the uniformity
guard, mute-not-clamp, `kMinGap`, invalid rhythm. Those four are unchanged. The
signature change adds two: an offset below `min_offset` mutes rather than
clocking into the ceiling, and the two-taps-collide-at-the-ceiling case is
demonstrably prevented.

### 7.2 New

- **A tap arrives where the neighbour's rhythm puts it.** Arrival time measured
  the way `test_flux.cpp`'s `first_echo_index` does, against `gap[0]` and
  `gap[0]+gap[1]`.
- **Ratio.** At TAPS 1.0 arrival is at `2 × gap`, and a tap that was muted
  under the floor at ratio 1 sounds at ratio 2.
- **Symmetry.** A follows B and B follows A, in one case.
- **Self-gating.** No valid neighbour rhythm → measurably no taps. This is the
  property that makes the absence of a mute switch affordable.
- **Taps survive MIX.** At MIX 0 with TAPS up, the tap signal is present in the
  output — the assertion that pins §4.1's reversal and would have caught the
  original arrangement.
- **Excitation switching.** Taps active → the bus carries them; TAPS at 0 → the
  bus carries the echo. Asserted on the signal's sparseness over a window, not
  on an instantaneous sample.
- **Boundedness.** Echo plus taps stays finite and within today's bounds.

### 7.3 Must stay green, and must stay load-bearing

The two witnesses in `test_part_fx.cpp` (soft clip, DC block). Their premise
already shifted once this week when a tuning change moved the loop's reachable
peak. Both must be re-proven RED with their stage removed, not merely observed
green.

### 7.4 Not replaceable by a test

- The whole-instrument CPU gate. The component figures are in (§8); what is
  still missing is `instrument_worst_bbd` from the `system` profile, and no
  unit test can stand in for it.
- The ear pass owns `kTapGain`, the pan width, and the missing compander — and
  must audition the **top** of the TAPS travel specifically, not the middle.
  Full stretch is the darkest and therefore the most burial-prone setting the
  knob can reach. If it collapses there, the honest remedy is a modest level
  rise along the ratio: it dents the distance metaphor, but a knob must not
  fade itself out.

## 8. CPU — measured, and the one gate still open

Measured on the Seed, `bbd` profile, two runs, identical checksums
(`docs/bench/2026-07-28-3a6820c-bbd.md`, `docs/bench/2026-07-28-6338f63-bbd.md`).
All figures are percent of the block budget, per line:

| row | configuration | avg |
|---|---|---:|
| `bbd_ceiling` | `BbdEcho`, 16384 stages, clock at the ceiling | 5.61 % |
| `bbd_line_only` | bare `BbdLine`, 16384 stages, ceiling | 3.68 % |
| `bbd_line_tap` | bare `BbdLine`, 4096 stages, ceiling | **3.59 %** |
| `bbd_line_tap_half` | bare `BbdLine`, 4096 stages, half clock | **2.67 %** |
| `bbd_walk_sdram` | the memory shape alone | 0.09 % |

**The BBD is 2.2× more expensive than its own design predicted.** 561 cycles
per sample per line against the estimated ~260, so the four shipping echo lines
are ~22.4 % of the block budget at their worst case, not the ~10 % the BBD spec
claimed. That correction belongs to that spec as much as to this one.

**Half of a tap line's cost cannot be clocked away.** Solving
`cost = fixed + k·f` over the last two rows: ~176 cycles per sample are fixed
filter work and ~183 are event work at the ceiling. Halving the clock therefore
saves 26 %, not 50 %, and 1.76 % per line survives however slowly a tap runs.

This is what settles the shape of the design. Ranked by effect:

| lever | effect |
|---|---|
| mono tap lines (8 → 4) | **−50 %**, exact — taken, §1.2 |
| one tap instead of two | −50 % again — **rejected**, §1.2: the idea does not survive it |
| raising the tap floor to 128 ms | −26 % — **not taken**, §1.1: it mutes 1/16 rhythms |
| stage count | nothing (3.59 % vs 3.68 %) |

**Where that leaves the budget:** four mono tap lines at the ceiling are
**14.4 %**, on top of the echo's 22.4 %, so FLUX at its worst case is roughly
**37 %** of the block budget. At typical rhythm gaps (125–500 ms, i.e. an
eighth to a half of the ceiling clock) the tap lines fall toward their 1.76 %
floor and the figure is nearer 10 %.

At `TAPS == 0`, or with no valid neighbour rhythm, the lines do not run at all.

**The gate that is still open.** Whether 37 % fits is not answerable from this
profile: it needs `instrument_worst_bbd`, which lives in the `system` family.
The last whole-instrument figure, `instrument_worst` at 97.5 %, was measured
with the *tape* delay and a much cheaper FLUX; it no longer describes anything.
**The `system` run is a precondition of the plan, not a follow-up to it.**

Two levers stay pre-authorised if it does not fit, in this order: raise the tap
floor to 128 ms (−26 %, at §1.1's musical price), and hoist two per-sample
divisions out of `BbdLine` (~8 `VDIV.F32` per sample across the current four
lines) — the second helps the echo as much as the taps and costs nothing
musically, so it is worth doing regardless of the verdict.

**Second risk, unchanged: the ear values.** `kTapGain`, the pan width,
`max_offset` and the missing compander are all inherited from, or reasoned
about against, a different signal path. They are starting points (§7.4).

## 9. Out of scope

- Restoring the tape delay, in any form.
- A mode switch between delay models.
- Per-tap controls beyond the single TAPS knob.
- **A latch/freeze for the derived offsets.** Worth wanting while the taps were
  passive; with a playable ratio the knob already gives a reason to hold on to
  a moment, and a hold control has nowhere to live.
- **Feeding the taps back into the main line's input.** One addition, and it
  would let the neighbour's rhythm seed the echo — a real idea, and its own
  decision. Not this spec's.
- Any migration path for patches saved before this change.

## 10. Review pass, 2026-07-28

Recorded rather than silently folded in, because these are places the first
draft was *wrong*, not merely thinner:

1. **The bandwidth table was off by 2×.** It applied `f_clk / 2` where the
   model uses `f_clk / 4` (`bbd.h:84`). A 1 s tap is 512 Hz, not 1 kHz.
   Corrected in §3, and it is what turned §4.1's audibility question from a
   nicety into the reason for the send change.
2. **The 32 kHz clock ceiling collides with short gaps** and produces exactly
   the doubled tap that `derive_offsets`' own "mute, never clamp" rule forbids.
   Fixed by `min_offset` (§1.1). The first draft's claim that the function
   returns verbatim did not survive.
3. **Taps placed before `_mix_lin` reproduce the failure that killed the tape
   taps.** The rule they were obeying — one wet control — is the rule that
   caused it. Reversed in §4.1.
4. **Making the taps follow STAGES was proposed and rejected during the same
   pass.** More stages need a faster clock at fixed time, so following the knob
   would mute the taps as STAGES rises. Recorded because it looks free from a
   memory table and is not.
5. **`max_offset` had no value at all.** It was inherited implicitly from the
   tape's buffer length, which no longer exists.

### Measurement pass, same day

The bench ran before planning rather than after, on a new `bbd`-only profile
(`bench/profiles.py`, commit `3a6820c`) that links at 56.2 % SRAM where the
`full` profile does not link at all. Three things changed as a result, and one
did not:

6. **Eight stereo tap lines were never affordable.** At 3.59 % per line they
   are 28.7 %, against the echo's 22.4 % — over half the block budget for
   FLUX. §1.2 is now mono lines: four, 14.4 %, with an image that §4.1's pan
   already produced anyway. The draft treated stereo tap lines as the obvious
   shape and never priced them.
7. **The clock was assumed to be the lever, and it is not.** The draft's §8
   reasoned entirely about clock rate. Measurement split a tap line's cost
   evenly between fixed per-sample filter work and clock-driven event work, so
   the floor lever saves 26 % where the draft's reasoning implied 50 %. Line
   count turned out to be the only strong lever, which is why §1.2's mono
   decision is architecture rather than an optimisation.
8. **A 128 ms tap floor was proposed in review and rejected on the numbers'
   own terms.** It is the second-best CPU lever, but 1/16 at 120 BPM is 125 ms:
   it would mute the near tap of the most ordinary rhythm the instrument plays.
   Kept as a pre-authorised lever (§8) rather than a default.
9. **What did not change: the BBD's own cost is 2.2× its design estimate.**
   561 cycles per sample per line against ~260. That is a correction owed to
   `2026-07-27-flux-bbd-delay-design.md`, not to this spec, and it is recorded
   here only because this measurement is what found it.
