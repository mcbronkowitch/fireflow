# Engine map

What you cannot learn by reading one file. Everything here was **measured**, not
inferred — each table names the probe that produced it, and a probe against the
real engine costs about half a second (see [Probes](#probes) at the bottom).

This file exists because spec drafts kept asserting runtime behaviour that the
code does not have. Mean review rounds per spec grew 1.44 → 4.33 between 10 July
and 13 August 2026 as `engine/` grew to 86 files / ~20 800 lines (**it is 77
files / 17 791 lines today, measured 2026-08-16** — the drop is the flow layer's
removal on 2026-08-14, not a simplification of what is mapped here), and the
rejections were consistently facts no amount of reading finds: a variable nobody
writes, a knob position nothing reaches, an identity that floating point does not
honour. The fix is not to restructure the engine. It is to write down the parts
that are invisible, and to measure before claiming.

That opening statistic is derived, not remembered — reproduce it with:

```bash
for f in docs/superpowers/specs/*.md; do
  echo "$(basename $f | cut -c1-10) $(git log --oneline -- "$f" | wc -l)"
done | sort
```

**Scope:** the modulation layer, because that is where the tangle is — how a
lane behaves and what actually reaches its axes. Extend it when another area
burns a review round.

This file maps **behaviour inside a lane**. The sibling authority it used to
name — the parameter map from `Fireflow` into the terrain layer's overlay — went
to [`docs/attic/flow-fireflow-param-map.md`](attic/flow-fireflow-param-map.md)
with that layer on 2026-08-14. Nothing in this file depends on it.

## How to read a citation

Files are named without their directory. The key:

| Named | Lives at |
|---|---|
| `lane.cpp`, `lane.h`, `song_form.h`, `super_modulator.cpp/.h`, `waveforms.h` | `engine/mod/` |
| `part.cpp`, `part.h` | `engine/parts/` |
| `center.cpp` | `engine/center/` |
| `Fireflow.cpp`, `init_patch.hpp` | `host/vcv/src/` |

Note the repo also carries a full second tree under `.worktrees/`; a bare grep
will hit both. It is a checkout of `codex/glow-hardware-panel-design`, kept only
because `git worktree remove` refuses a tree containing submodules — it is not a
second copy of anything live.

**Line numbers rot — the quoted expression beside each is what identifies the
site.** This is not theoretical: the param map now in `docs/attic/` was written
on 2026-08-12 and by 2026-08-14 every one of its 39 `Fireflow.cpp` citations had
drifted by exactly 12 lines. All 69 of its citations were
re-derived from their quoted expressions on 2026-08-14. **When a line number here
does not show what the text says it shows, the text is still the claim — find the
expression and fix the number.** Where a fact is load-bearing enough that silent
rot would be expensive, pin it with a test instead (§1, §3 and §4 are pinned by
`tests/test_engine_map.cpp`).

---

## 1. The lane state space

A `ModLane`'s behaviour is a product of three flags, not one mode. Three derived
predicates do the actual gating:

```
_flow_melody_on()   = _melodic && !_step_mode && _flow_melody
_melody_engine_on() = _melodic && (_step_mode || _flow_melody)
_note_lane()        = _melodic && _flow_melody
```

Eight flag combinations collapse to **six behaviours** — count the last column,
not the measurement columns: rows 1 and 3 read alike there, as do rows 2 and 5,
and each pair is still two behaviours (a texture lane and a Sampler/BBD PITCH
lane). It said five until 2026-08-14, when the last row stopped being "identical
to the row above" and the count was left behind. Measured at SMOOTH 0
(passthrough, so the raw target is visible), RANGE 1, VARY 0, rate 0.5 Hz, 20 s,
seed 12345, **`set_melodic()` before `init()`** (see §6 — the order matters).

| `_melodic` | `_step_mode` | `_flow_melody` | p2p @ SHAPE 0 | distinct | behaviour |
|---|---|---|---|---|---|
| false | false | – | 2.000 | 13152 † | texture LFO, continuous |
| false | true | – | 2.000 | 5 | texture, STEP follower — staircase |
| true | false | false | 2.000 | 13152 † | PITCH **as an LFO** (Sampler, BBD) |
| true | false | true | **0.246** | 7 | FLOW melody phrase — *seed-dependent, see below* |
| true | true | false | 2.000 | 5 | STEP melody through the bank (Sampler, BBD) |
| true | true | **true** | **0.246** | 7 | **STEP melody phrase — note deck** |

**† These two cells do not reproduce.** Every other cell above was re-measured on
2026-08-14 under the setup stated above and matched to the digit; the `distinct`
count of the two *continuous* rows came out **17903**, not 13152, on two
independent re-measurements (and unchanged between `-O2` and `-O3`). The old
figure is left standing because nothing establishes *why* it differs — replacing
one unverified count with another buys nothing. Note what the quantity is: the
number of distinct `float` values a continuous LFO visits in 960 000 samples, i.e.
a count of rounding outcomes, which is exactly what §5 says not to trust. **The
p2p of those rows, which is the load-bearing figure, reproduces exactly.** No test
pins either count; the §1 case pins the melodic rows only.

**`_flow_melody_on()` is false whenever `_step_mode` is true — but the pitch
output no longer is.** The predicate still reads as written above and still
excludes STEP; what changed on 2026-08-14 (spec `melody-reachable`) is that
`_compute_raw()` gates on `_note_lane()` instead, which has no `_step_mode` term.
So a note deck emits its composed phrase in STEP as in FLOW, and a SAMPLER or BBD
deck keeps running the waveform bank in both modes. The last two rows are two
behaviours, not one measurement: at seed 12345 their streams differ by **1.086**
peak, measured (1.0862, re-measured 2026-08-14). What the §1 case in
`tests/test_engine_map.cpp` pins is that they diverge at all — `max_diff > 0.5f`
— not the 1.086; the figure is a measurement, the gate is a floor. What is still
true is the shape of the flag: `_flow_melody` is an engine-class flag, not a mode,
so a design that treats "FLOW melody" as a mode orthogonal to STEP is describing a
state that does not exist.

At the 8 steps this table is measured at, the STEP note deck's stream is the FLOW
note deck's stream **sample for sample** (max deviation 0.0 over ten seeds) — the
step clock's `8/steps` scaling is 1 there. That is a property of the step count,
not of the lane: at 4 steps the two diverge by 0.298 and at 16 by 0.526, measured
at seed 12345 (re-measured 2026-08-14: 0.0000 / 0.2979 / 0.5264). The identity is
pinned by the §1 case (`d < 1e-7f`); of the two divergences only the 16-step one
is pinned, and as a floor (`d16 > 0.1f`) — the 4-step figure is measured only.

**Superseded 2026-08-15 (spec `2026-08-13-shape-smooth-rework-design.md`,
branch `2026-08-14-smooth-interval-relative`): the note-interval slew clamp this
paragraph used to describe is gone.** Until then, `_update_slew()` carried a
clamp (`lane.cpp:361`, `if (_flow_melody_on())`) that was "the only split that
survives an equal step count" on the melody path, and the SMOOTH axis broke the
STEP/FLOW identity above SMOOTH 0 because of it: measured on a note deck, seed
999, 8 steps, SHAPE 0, RANGE 1, VARY 0, 20 s, max |STEP − FLOW| — bit-identical
at SMOOTH 0.00 / 0.25 / 0.50 at both 0.5 Hz and 2 Hz and at SMOOTH 0.75 at
0.5 Hz, but **0.0787 at SMOOTH 0.75 / 2 Hz, 0.2069 at SMOOTH 1.00 / 0.5 Hz and
0.2995 at SMOOTH 1.00 / 2 Hz**. That old law made SMOOTH an absolute wall-clock
time (`τ = 0.00002 · 25000^smooth`); the clamp existed to stop it over-gliding
a note deck in FLOW, and STEP never had the clamp applied to it, hence the gap.

**The new law makes the clamp unreachable, and it was deleted rather than kept
dead.** SMOOTH is now `τ = smooth · TOP · interval`, where `interval` is one
slot in FLOW-melody and one step in STEP and `TOP` is `kFlowSlewFrac` (0.35) on
the melodic lane — both branches of `_update_slew()` (`lane.cpp:379` STEP,
`:384` FLOW-melody) now compute the *same* interval whenever `_effective_length()`
agrees with `_steps` (`lane.cpp:268` — `kFlowPhraseSlots` in FLOW against the
STEPS count in STEP), which it does at 8 steps — **and additionally only below
≈2.083 Hz**, because the FLOW-melody branch floors its slot at
`_note_min_samples` (2880 samples) and STEP does not. Measured at 8 slots, seed
12345, SMOOTH 0.714: τ is identical at 0.5 / 1.0 / 2.0 / 2.05 / 2.083 Hz and
diverges from 2.1 Hz up (714.00 vs 719.71 samples), where the floor starts
binding and STEP keeps shrinking. Both cells measured below sit under that
threshold, so the identity result stands; the sentence around it would not have,
unqualified. Re-measured under the setup
above, same seed, same rate pair, all five SMOOTH values, both 0.5 Hz and
2 Hz: **max |STEP − FLOW| = 0.00000000, bit-identical, at every cell** —
verified by exact float comparison (`step_buf[i] != flow_buf[i]`), not only by
p2p. The identity now survives the whole SMOOTH axis in this setup, not only
SMOOTH 0.

That does not make it a property of the lane rather than the setup:
`_effective_length()` still splits FLOW from STEP outright whenever the step
count is not 8 (§1's own 4-step/16-step divergence above, 0.298 and 0.526,
comes from that split and is untouched by this branch — it is about phrase
*content*, not slew, and nothing here re-measures it), and `_on_boundary()`'s
FLOW-only note-rate floor (`lane.cpp:609`) still gates `_compute_raw()`
outright, inert at this paragraph's matched 8-step setup for the same reason
it always was. So "a glide is clamped in FLOW and not in STEP" is no longer
true — there is no clamp — but "STEP and FLOW behave identically" is still a
claim about this setup's step count, not about the lane in general. The
`_flow_melody_on()` guard itself is unchanged and still gates the FLOW-only
branch of `_update_slew()`; `docs/engine-map.md` §7 owns whether removing the
guard rather than the clamp is worth doing.

**In STEP the slot count cancels out of τ — the slow lanes are not glided
longer.** This corrects a mechanism that was stated in the spec
(`2026-08-13-shape-smooth-rework-design.md` §2.3, "one knob position is a
different τ per lane") and had propagated into `lane.cpp`'s STEP branch as a
comment. It does not hold: `clock_scale()` is `8/_steps` in STEP, so
`cycle/_steps` is `sr/(8·rate·(1+_ev_rate))` **whatever `_steps` is**. Measured
at master 0.5 Hz, deck steps 8, SMOOTH 0.714, seed 12345 — SOURCE/SIZE/MOTION/
LEVEL carry slot counts 4/16/12/6 and all four land on **τ = 4284.00 samples
exactly**; LANE_PITCH lands on 2998.80 only because its top is `kFlowSlewFrac`
(0.35) rather than `kSmoothTopTexture` (0.5). What differs per lane is
**τ/cycle**, since `cycle = step · slots` does differ — which is what §2.3's
attenuation table actually shows (monotone in slots). The spec's numbers
survive; its explanation of them does not.

**The slow end of the panel quantises SMOOTH, and used to freeze the lane
outright.** τ is proportional to the cycle now, so at RATE 0 (0.02 Hz) × PACE 0
(×1/32) × TIDE 0 (×1/4) the per-tick coefficient `k = 1/(τ·sr)` reaches ~1e-8.
The tick twin's coefficient is `1 − (1−k)^96`, and computed in **float** `1.f - k`
rounds to exactly `1.0f` below half an ulp, so the coefficient collapses. Driving
`ModLane` directly at 0.0003125 Hz (what RATE 0 + PACE 0 hand LANE_SIZE): p2p
**0.000000000 over 60 s** at SMOOTH 0.50/0.70/1.00, and one single coefficient
shared by every knob position from 0.15 to 0.40. Through the whole instrument the
outright freeze is **not** reachable — other per-tick motion keeps p2p off zero —
but the quantisation is: deck A's SOURCE at TIDE 0 read 0.00352925 / 0.00240338 /
0.00240338 at SMOOTH 0.20 / 0.60 / 1.00, the last two identical to the digit.
Fixed 2026-08-15 by deriving that coefficient in double (0.00398457 / 0.00293958
/ 0.00272623, strictly decreasing); gated by `tests/test_smooth_law.cpp`'s G6.
The per-sample slew is deliberately still float — `k` itself is representable and
was measured tracking the analytic settling curve at the same τ values.

**`_melodic` is not a choice.** `super_modulator.cpp:14` sets it unconditionally
to `i == LANE_PITCH`. Exactly one lane of five is melodic, on every deck, always.
There is no host call that changes this.

### The consequence that keeps getting missed

Read the p2p column across a *row*, not down it. At SHAPE 0 a melodic STEP lane
on a **SAMPLER or BBD** deck emits **2.000** — the sine sampled at 8 step
boundaries, which is 5 distinct values (`0, ±0.7078`, `±1` — `wave_sine` is
`fast_sin`, error < 1.2e−3), seed-independent, identical on every FORM and every
`SongMode`. It carries no melodic information at all, and on those two engines the
phrase still only appears at the S&H end of the bank.

Until 2026-08-14 that was the measurement for **every** STEP deck, note engines
included, and it is the reason the melody system was unreachable where the
instrument plays: FORM moved nothing below SHAPE 0.75 (measured, 0 of 4
`Principle`s differing from `TwoMotif` at SHAPE 0.00 and 0.50, on seeds
999/12345/7/4242) — a quarter of one knob was the whole of where it lived. A
note deck now emits the phrase at every SHAPE
instead — measured 3 of 4 `Principle`s differing at SHAPE 0.00, 0.50 and 1.00 on
all four seeds, pinned by `tests/test_melody_reachable.cpp`. Its ambitus is
**much smaller than the staircase's, and not a constant**.

**The phrase ambitus is a distribution, not a number.** Ten seeds (999, 12345,
7, 4242, 31337, 1, 2, 3, 77, 888), correct construction order, **default FORM**
(`Principle::Hierarchical`, `song_form.h:52` — FORM now moves this lane at every
SHAPE, so a probe that sets another `Principle` measures another distribution),
rate 0.5 Hz, SHAPE 0, SMOOTH 0, RANGE 1, VARY 0, 20 s:

```
0.155  0.246  0.315  0.318  0.351  0.549  0.594  0.607  0.698  0.822
```

Since 2026-08-14 that is the STEP note deck's distribution as well, not only
FLOW's: at 8 steps the STEP p2p equals the FLOW p2p to the last bit on every
seed in the set, so this is **one** distribution, not two. Re-measured on
2026-08-14 by the map-catches-up task, because it was two: the list this file
carried differed from the measured one in six of its ten entries and reproduced
under no variation tried (10 / 20 / 30 / 60 s, 4 / 8 / 16 steps, SHAPE 0 and 1,
rate 0.2 and 0.5 Hz, with and without a `set_step()` call), while the
`0.155 … 0.822` endpoints quoted beside it for the STEP row reproduced exactly.

So the ratio against the 2.000 sine staircase runs **2.4× to 12.9×** over the seed
set above, and a wider set will widen it. That is a ratio of **lane p2p**; it is
**not** the ratio on the pitch axis, where the clamp of `part.cpp:117` cuts it to
1.8×–10.7× — see §7's closing paragraph before quoting either at the other's
question. Any design that routes phrase values
where a waveform used to be pays a factor in that band — **quote the range, never
a single figure.** An earlier version of this file canonicalised "4.8×" from one
seed measured in the wrong construction order; that is exactly the mistake this
file exists to stop.

---

## 2. Axis fan-in — the asymmetry

The played value of a control is not the knob for every control, and **the shape
of the fan-in differs per axis**. This is the single most misleading thing about
the modulation layer: the axes look symmetric on the panel and are not.

| Axis | Sources summed into the played value | Additional multiplicative paths |
|---|---|---|
| **SHAPE** | `_shape` + `_ev_shape` + `_shape_offset` + `_kick_shape`, then clamped (`lane.cpp:559`) | — |
| **SMOOTH** | `_smooth` **only** (`lane.cpp:349` → `:360`) | — |
| **RATE** | — | `_phase_inc * (1 + _ev_rate)`, plus DRIFT via `set_rate_scale` |
| **PHASE** | `_phase + _ev_phase` (`lane.cpp:114, 557`) | — |

**SHAPE has four writers. SMOOTH has exactly one.** `ModLane::_smooth` has a
setter, a reader, and nothing else — grep for it and filter out the unrelated
`_morph_smooth` / `_lvl_smooth` / `_drift_smooth` / `PartFx::_smooth`. That
asymmetry — not the waveform bank — is why one knob feels unpredictable and the
other feels inert.

The three hidden SHAPE contributors and their ranges:

| Source | Set by | Range | Character |
|---|---|---|---|
| `_shape_offset` | DRIFT, per control tick (`center.cpp:143-144`) | `±0.15 · tap · weather · drift`, taps `{+0.8, −1.0}` — the smoothed DRIFT knob is a **fourth factor**, which is what makes the term exactly 0 while DRIFT is 0 | continuous, τ ≈ 45 s |
| `_ev_shape` | EVOLVE random walk (`lane.cpp:698`) | clamped `±0.25` | drifts, decays on settle |
| `_kick_shape` | SPOT (`lane.cpp:403,418`) | `±0.35` per draw, **accumulates** | decays to 0, τ ≈ 1.5 s |

**The fan-in also differs per lane.** `SuperModulator::spot()` skips `LANE_PITCH`
deliberately (`super_modulator.cpp:182` — "the melody is the anchor everything
else stumbles around"), while DRIFT and EVOLVE reach all five. So:

- **PITCH lane:** ±0.40 sustained (DRIFT + EVOLVE).
- **Texture lanes:** ±0.75 peak, of which ±0.35 is a decaying gesture.

Either way, **more than a third of the axis is not under the knob.** A knob at 1.0
can be pulled down to `0.60` on the melodic lane and `0.25` on a texture lane
just after a SPOT; any caller that sets the knob below 1.0 widens the band
further.

⚠️ **Those two bands are computed worst-case envelopes, not measurements** — the
only inferred numbers in this file, flagged as such. Reaching an edge needs
`|weather| = 1` **and** DRIFT at 1 **and** `_ev_shape` pinned at its clamp **and**
a fresh SPOT draw at its extreme, all at once. Nobody has measured the actual
distribution. The safe conclusion is directional and holds regardless: a spec
that says "at SHAPE 1 the behaviour is X" is describing an axis value the player
may never reach — and §3 below shows the one place where reaching it matters most.

---

## 3. The top of the SHAPE knob is a fade-out, not a waveform

Measured on a texture lane, FLOW, 30 s, seed 999. Pinned by
`tests/test_engine_map.cpp` (§3 case: the fade law at 0.90, p2p ≈ 0 at 1.00, the
non-zero park point across seeds, and VARY reviving the corner):

| SHAPE | VARY | p2p | distinct values |
|---|---|---|---|
| 0.70 | 0 | 1.600 | 796 |
| 0.80 | 0 | 1.600 | **2** |
| 0.90 | 0 | 0.800 | 2 |
| 0.95 | 0 | 0.400 | 2 |
| **1.00** | **0** | **0.000** | **1** |
| 1.00 | +0.5 | 0.906 | 5 |
| 1.00 | −0.5 | 0.450 | 4 |

Above SHAPE 0.75 the bank crossfades pulse → S&H (`waveforms.h`), so this is the
**top quarter** of the knob. On a **non-melodic lane in FLOW** `_sh_slot()`
returns 0 permanently — not because `_cur_step` is stale, but because of the
**explicit early return** at `lane.cpp:569`, `if (!_step_mode && !_flow_melody_on()) return 0;`,
taken before `_cur_step` is read at all — its observable face (distinct = 1 over
30 s) is pinned by `tests/test_engine_map.cpp` (§4 case). So "S&H" is a frozen
constant, and the
crossfade toward it is an **amplitude fade to a fixed DC offset**. Depth falls
linearly: p2p = 2·(1 − 4·(sh − 0.75)).

**It does not fade to silence, and this distinction matters.** The lane settles on
the held value, which is nowhere near zero. Measured at SHAPE 1.00, VARY 0, ten
seeds:

```
+0.090  −0.157  −0.227  −0.355  −0.382  −0.390  −0.520  −0.527  −0.527  +0.274
```

So the target parks at base + depth·(up to ±0.53), **permanently and per seed**.
A designer reading "the modulation turns off" expects the base value; what
actually happens is a silent static offset. Different bug, different fix.

VARY is what makes that corner move at all — mutation is the only thing that
changes the held value (VARY +0.5 restores p2p 0.906). This is the mechanism
behind the "SHAPE is unpredictable" report: for a quarter of its travel the knob
does not change the shape, it fades the modulation out onto an arbitrary offset,
and whether it does depends on a *different* knob.

The finding survives attack: unchanged across seeds 999 / 12345 / 7, across rates
0.05 / 0.5 / 2.3 Hz, under SMOOTH 0.5 and 1.0 and `_fixed_slew` (which add slew
ringing, not depth), and it scales proportionally with RANGE. The `distinct = 2`
is not a rounding artefact — the exact-float count is also 2.

---

## 4. Write-side index

Behaviour that depends on a variable is only as knowable as that variable's
**writers**. Reading the reader tells you nothing. The non-obvious ones:

| Variable | Written by | Never written by |
|---|---|---|
| `_cur_step` | STEP path and the FLOW-melody path. `tick()` also writes it in a non-STEP branch (`lane.cpp:1018`), unreachable for the FLOW LFO only because `next_edge` is always 1.0 there | — but for the FLOW LFO this is moot: `_sh_slot()` **early-returns 0** at `lane.cpp:569` before reading `_cur_step` at all. Do not reason about its value on that path |
| `_flow_melody` | `part.cpp:43,441` from the engine id: **off for SAMPLER and BBD** | any host directly — the only way to move it is to change the deck's engine |
| `_melodic` | `super_modulator.cpp:14`, unconditionally, once | anything else, ever |
| `_active[slot]` | boots **all true** (`part.h:639`); the only writer that runs by itself is `Fireflow.cpp:880` (LANE_PITCH, `!samplerPart`), pushed every block | **`engine/` itself — no engine code ever writes it. Away from that one host line it moves only when a caller sets it explicitly (the render host's `set_target_active` scenario action, or a test)** |
| `_shape_offset` | `center.cpp:143-144` every control tick | — (it is re-pushed continuously; it cannot be "left" at a value) |

### Settled: a Sampler deck's PITCH lane is deactivated by the host, not by the engine

`_active` boots **true**, and no engine code ever clears it. The single writer
that runs on its own is the `Fireflow` module, which pushes
`set_target_active(LANE_PITCH, !samplerPart)` every block — so under `Fireflow` a
Sampler deck's PITCH lane is deactivated, and under **any caller that does not
make that call** the same deck keeps `_active[LANE_PITCH] == true` while
`_flow_melody` is false (engine id). Two reviews disagreed on this; the code
settles it. `tests/test_param_impact.cpp:152-159` (`apply_patch`'s FORM/SONG
branch) depends on exactly this — it
forces `_active` on for the FORM/SONG cases precisely because nothing else in
the engine would.

⚠️ **The host-side deactivation is a known defect, not a contract.** It is
recorded here so the question stops being re-argued, not so the behaviour is
preserved — do not build a design that relies on it. `part.h:634-638` already
flags the neighbouring half of the same problem: the `Fireflow` module re-pushes
`set_target_active` every block, so the day an M6 pad can toggle `LANE_PITCH`
that push will silently overwrite it, *"harmless today only because the pad
doesn't exist yet."*

---

## 5. Floating point: what is not an identity

- `lerpf(a, b, t)` is `a + (b − a)·t`. At `t == 1` this is **not** `b`.
  Worst **absolute** error over operands in `[−1, 1]`: **5.96e−08 = 2⁻²⁴**, measured.
- The mismatch *rate* is not a property at all — it depends entirely on how you
  sample. Measured on different sweeps of the same function: 10.4 %, 9.4 %, and
  0 % (uniform random floats, coarse enough to be exact). A rate quoted without
  its sweep is meaningless.
- **The error is not 1 ULP.** Worst measured ULP *distance* on a 4001-point grid:
  **1024**. The absolute error is bounded because the operands are; the relative
  error is not, and it blows up as the held value approaches zero.
  → **Write such a gate as an absolute epsilon of ~6e−08. A ULP-relative or
  exact-equality gate fails near zero and cannot be trusted to go RED for the
  right reason.**
- Consequence: `shape_value(ph, 1.0f, hold)` does **not** return `hold` exactly,
  despite the claim in `2026-08-13-flow-melody-engine-design.md:328-330`, which is
  wrong. `ModLane::_compute_raw()` carried the same claim in a comment until
  2026-08-14, when spec `melody-reachable` rewrote that comment and dropped it —
  do not reintroduce it there.
  `test_waveforms.cpp:14-15` passes only because it already uses `.epsilon(0.01)`.
  Any gate written as bit-identity across this call cannot be relied on.

---

## 6. Probes

The rule this file exists to serve:

> **No runtime claim enters a spec, plan, or review reply until a probe has
> printed it.** A number quoted from a review is a claim, not a measurement.

It costs 0.4 s to compile and 0.1 s to run:

```bash
source env.sh                       # clang on PATH; never in a shell used for shell/ or bench/
clang++ -O2 -Iengine -o probe.exe probe.cpp engine/mod/lane.cpp
./probe.exe
```

Note `-Iengine`, not `-I.` — engine sources include as `mod/lane.h`. Types live in
`namespace spky`. Probes are scratch files; they belong in the scratchpad, not the
repo. Promote one to `tests/` only when it asserts something worth defending.

Skeleton:

### Construction order is part of the measurement

**`set_melodic()` must come BEFORE `init()`.** `init()` reads `_melodic`
(`lane.cpp:70`) and branches: melodic → `_generate_pattern_a()`, non-melodic →
`_fill_walk()`. `SuperModulator::init` therefore orders them that way
(`super_modulator.cpp:14-15`). A probe that calls `init()` first measures a
melodic lane whose RNG stream was consumed by a contour walk the engine never
runs on it — a different object, with different numbers.

This is not hypothetical: the first version of this file shipped a skeleton with
the order reversed, and §1 row 4 carried its wrong figure (0.420 / 10 distinct
instead of 0.246 / 7) into the map until an independent review re-measured it.
**A probe is only as good as its setup, so the setup belongs in the report.**
State seed, rate, duration, and construction order beside every number.

```cpp
#include "mod/lane.h"
#include <cstdio>
int main(){
    spky::ModLane l;
    l.set_melodic(false);              // BEFORE init() -- see above
    l.init(48000.f, /*seed*/ 12345);
    l.set_step(false, 8);
    l.set_rate_hz(0.5f); l.set_shape(1.0f); l.set_smooth(0.f);
    l.set_range(1.f); l.set_variation(0.f);
    float mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < 48000 * 20; i++) { float v = l.process(); if (v < mn) mn = v; if (v > mx) mx = v; }
    printf("p2p %.4f\n", mx - mn);
}
```

Set `set_smooth(0.f)` when you want to see the raw target; the slew otherwise
hides everything the target does. Count `distinct` values alongside p2p — that is
what separates "a waveform" from "a two-value square", and p2p alone will not tell
you.

---

## 7. Reachability: where the melody lives on the axes

§2 and §3 describe what a knob position *does*. This section is about which
positions the melody is actually *reachable* from — a different question, and
the one that decides whether a feature exists in play.

Until 2026-08-14 this section opened on a population study of the terrain
generator: how often a drawn patch put a deck in §3's fade zone (about 5 %),
and the fact that it never did so on a drone, half of all draws, because the
drone's SHAPE span was capped at `{0, 0.25}`. That generator was deleted with
the terrain layer; the study cannot be reproduced and is not restated here. Its
by-ear content — the drone SHAPE cap and the coupling finding around it — is in
[`docs/attic/taste-by-ear-notes.md`](attic/taste-by-ear-notes.md) §1.3.

**What survives it, and is engine truth rather than population truth:** the
played SHAPE is the knob plus the three in-lane offsets of §2 and nothing else,
and on the melodic lane those offsets total ±0.40. So a SHAPE knob parked below
0.35 cannot reach the 0.75 where the bank starts crossfading toward the phrase,
whatever else is turned. **On a SAMPLER or BBD deck the melodic phrase
therefore lives in the top quarter of the *played* SHAPE and nowhere else**
(knob 0.35 and up, not knob 0.75 and up) — those two
engine classes still route PITCH through the waveform bank in both modes (§1).
A note deck no longer depends on any of this: since `07d5b9d` it emits its
phrase at every SHAPE.

### The melody's second gate: RANGE, through the quantizer

The pitch axis is 36 semitones over `0..1` (`part.cpp:228-229`; the
`_detune_cents * 1/3600` at `:244` corroborates), and `LANE_PITCH` is handed
depth 1.0 unconditionally (`part.cpp:98`), so a lane value lands on that axis
directly. What the phrase then moves, after quantizing (Aeolian, STEP, 8 steps,
VARY 0, mean of seeds 12345/777/4242):

| RANGE | span (semitones) | distinct scale degrees |
|---|---|---|
| 1.0 | 8.42 | 4 |
| 0.4 | 4.11 | 3 |
| 0.25 | 2.57 | 2 |
| **0.1** | **1.03** | **1** |

At the bottom of the RANGE knob a deck plays the entire phrase — every FORM,
every SONG, every seed — on **one** scale degree. (The two boundary values were
chosen because they were the ends of the band the deleted terrain generator drew
for a drone; the measurement is the lane's, not the generator's, and stands
without it.)

### And the inversion, which has since inverted

This section used to close on FORM being unreachable rather than inert: at SHAPE
1.0 three of the four other `Principle`s emitted a different value set from
`TwoMotif`, at SHAPE 0.0 none of them did, and a patch had to be parked in the
top quarter of the knob before any of it began to change.
Commit `07d5b9d` (spec `melody-reachable`) took the
SHAPE dependence off the note engines, and the measurement moved with it.
Re-measured 2026-08-14 — STEP, 8 steps, rate 0.5 Hz, SMOOTH 0, RANGE 1, VARY 0,
20 s, `set_melodic()` before `init()`, seeds 999 / 12345 / 7 / 4242, every cell
the same on all four:

| deck | SHAPE 0.00 | SHAPE 0.50 | SHAPE 1.00 |
|---|---|---|---|
| note engine (`_flow_melody` true) | **3 of 4** | **3 of 4** | 3 of 4 |
| SAMPLER / BBD | 0 of 4 | 0 of 4 | 3 of 4 |

"*n* of 4" is how many of the four other `Principle`s emit a value set that
differs from `TwoMotif`'s. On a note deck FORM now reaches the output at every
SHAPE; on the other two engine classes the old picture stands unchanged, and the
top-quarter gate above is what governs them. Pinned by
`tests/test_melody_reachable.cpp`.

**What did not change is the size of the gesture, and that is the real remaining
limitation.** The staircase a SAMPLER/BBD deck emits at SHAPE 0 is still p2p
2.000, which at RANGE 1 saturates the *whole* 36-semitone pitch axis — measured
36.000 semitones after the base-plus-clamp of `part.cpp:117`, on every seed,
because ±1.0 around a 0.5 base clips at both ends. The note deck's phrase is
0.155 … 0.822 p2p over the ten seeds of §1, which through the same chain is
**3.4 to 19.8 semitones**, i.e. **1.8× to 10.7× narrower**.

⚠️ **That is not the 2.4×–12.9× of §1, and substituting one for the other is a
mistake this file has now made once.** §1's factor is the ratio of *p2p*, before
`clampf(0.5f + v, 0.f, 1.f)`; the clamp is exactly what stops it carrying to the
pitch axis, because it truncates the staircase (which reaches the rails on every
seed) far harder than the phrase (which mostly does not). It also **reorders the
seeds**: the widest phrase in p2p is seed 3 at 0.822, but the widest in semitones
is seed 888 at 19.777, whose p2p is only 0.549. Measured per seed, ten seeds,
setup as in §1: p2p ratio 2.43× … 12.89×, **semitone ratio 1.82× … 10.69×**.
Quote the semitone figure whenever the subject is audible pitch — a RANGE design
sized against 2.4×–12.9× is sized against a limitation overstated by about a
fifth.

So the instrument still moves less
pitch with the phrase than it moved with the decoy that carried no melodic
information; what changed is that the phrase is now what it moves. How much of
those semitones survives quantization is the RANGE gate above, which is
**unsolved** — it is what §2.2 of the `melody-reachable` spec is about, and
nothing on this branch touched it. Settled, and no longer a question for a
SHAPE/SMOOTH design: where on the SHAPE axis the melody lives. Everywhere, on a
note deck.

---

## 8. The groove cell: length, rank, and when it exists

Measured 2026-08-15 on a note deck (`_melodic` and `_flow_melody` both true),
STEP, rate 0.5 Hz, SHAPE 0, SMOOTH 0, RANGE 1, VARY 0, `set_melodic()` before
`init()`, seeds 999 / 12345 / 7 / 4242, STEPS 4 / 8 / 16, DENSE 0.0 / 0.05 /
0.125 / 0.25 / 0.5 / 0.75 / 1.0.

- **`pattern_groove.len` equals the STEPS count**, not `pg_target_len()`'s
  constant 8 — that function sizes the pitch motif, not the groove cell.
- **`set_step()` does not regenerate the groove. The next cycle wrap does.**
  Read the table immediately after the call and you get the previous cell: at
  4 steps an 8-slot groove whose slots 4..7 the phrase never reaches. The
  first probe written against this measured exactly that and reported a false
  mismatch on two of three STEPS counts.
- **`rank_of_slot[]` is a permutation of `0..L-1` with slot 0 pinned to rank
  0**, in every cell measured, both patterns of the song pair. Enforced, not
  emergent: `phrase_gen.h`'s groove build fixes `score[0] = 2.0` above every
  other slot's jittered score before the stable sort that produces `order[]`
  (`phrase_gen.h:297`), so slot 0 always sorts first regardless of seed.
- **The firing set is exactly `{ slot : rank_of_slot[slot % L] < k }`** with
  `k = clamp(round(DENSE·L), 1, L)` (`lane.cpp:643`, `:655`) — every cell of
  the sweep matched after the settle wrap.

Pinned by `tests/test_step_accent.cpp`, as exactly as the gates carry it: G1
pins the firing-set formula's `k == 1` case — exactly one note fires and its
accent is 0. The DENSE-1 G2 case checks the *accent set* — `uniq.size() ==
steps` (L distinct values) and `*uniq.rbegin() == Approx(1.0)` (the top one
reaches 1) — which is consistent with `rank_of_slot[]` being a permutation
but is not itself what separates `len == STEPS` from a stale or wrong-length
groove: fires per cycle are bounded by STEPS whatever `L` is (a `k > STEPS`
groove would still only ever fire `STEPS` times), so it is specifically the
`*uniq.rbegin() == Approx(1.0)` assertion — reachable only when the last
distinct accent is `(L-1)/(L-1) == 1`, i.e. `L` actually equals the fired
count — that holds `len == STEPS`. Slot 0 being rank 0 specifically is not
pinned by any gate at all; it rests on `phrase_gen.h:297`'s `score[0] = 2.0f`
(cited above), read directly. The intermediate-DENSE G2 case is what holds
the `groove_length`-vs-`_groove_k()` normalization choice.

### Red-proofing found one real gate gap and one false one; the gap is closed, the other is a documented property

Red-proofing `tests/test_step_accent.cpp` on 2026-08-15 (one-line mutations,
one at a time, `-DCMAKE_BUILD_TYPE=Release`) found that G2 and G3, as they
stood, could not catch the mutation each is named for.

**G2 was a real gap, now closed.** The original case ("at DENSE 1 the contour
is the whole rank scale") runs at `set_density(1.f)`, and `_groove_k()`
(`lane.cpp:643-646`) computes `k = clamp(round(density·L), 1, L)`; at
`density == 1` that is `L` exactly, so `_groove_k() - 1 == groove_length - 1`
and a mutation normalizing `_start_note`'s accent by `_groove_k()` instead of
`groove_length` is bit-identical to the original at the one density that case
exercises. G1 (DENSE 0) can't see it either — the sole firing slot is always
rank 0, so its accent is `0 / anything == 0` regardless of the denominator.
**A new case, "accent G2: at an intermediate DENSE, the fired accents are
exactly the k lowest ranks over L-1", closes it**: at DENSE 0.5, `k < L` for
every STEPS in the sweep (e.g. 4/8/16 steps → k 2/4/8), so
`_groove_k() - 1 ≠ groove_length - 1` there and the two normalizations
diverge. The case asserts the exact accent set `{ r/(L-1) : r in 0..k-1 }`,
not just its bound, which also re-pins that the firing slots are precisely
the `k` lowest ranks. Red-proved: under the `_groove_k()` mutation it fails on
every STEPS/seed pair (first: `CHECK( 1 == Approx( 0.333333 ) )`, steps 4,
seed 999); reverted, all three `accent G2*` cases pass (140/140 assertions).

**G3 was never a gap — it is a property of the source, confirmed rather than
patched.** `_note_accent` is written only by `_start_note`, which runs under
`_step_mode` alone (`lane.cpp:671`), and `set_step()` is the only thing that
changes mode; its `mode_changed` branch zeroes `_note_accent`
(`lane.cpp:211`) on every STEP↔FLOW transition, before `note_accent()`'s
`_step_mode` guard (`lane.h:126`) is ever consulted. So no reachable sequence
can leak a stale STEP accent into FLOW, guard or not, and no gate can be
written that tells a guarded accessor apart from a guardless one — which is
why G3 doesn't. The design spec's §3 used to argue the opposite (the guard as
the load-bearing mechanism, the reset as unaudited support); it now states
the reset as what covers every reachable path and the guard as deliberate
redundancy against a future second writer of `_note_accent`. **The guard
stays, uncovered by any gate, on purpose** — removing it or writing a test for
it were both considered and rejected: cheap insurance the day a second writer
appears is worth more than a gate that could only ever pass.

### The DEC accent is about twice as strong on BODY as on SYNTH

`SynthEngineT<V>::_do_trigger()` (`synth_engine.cpp`) applies one formula --
`set_decay_scale(1.f - (1.f - kAccentDecFloor) * _accent * _decay_n)` -- to
every `V` it is instantiated with, and a comment beside that call used to
claim "at DEC 1 the weakest note rings for `kAccentDecFloor` of the set time"
without qualification. That is exact for `VoiceT` (SYNTH/WAVE), where
`set_decay_scale` multiplies `decay_s` linearly, so the scale factor IS the
ring-time ratio. It is not exact for `BodyVoice` (BODY): `_apply_env()` maps
`_damping = d_s / (d_s + 1)`, which is not linear in `d_s`, so the same scale
factor produces a different proportional ring time.

Measured (seed 99, `set_cycle(0.25f)`, one struck note, DEC knob 1.0, note
length = index of the last sample above 5% of the note's own peak,
`tests/test_step_accent.cpp`'s `note_len_samples` helper): at accent 1 vs.
accent 0, SYNTH goes 42929 → 13231 samples, ratio 0.3082 (matches
`kAccentDecFloor` within G5's window); BODY goes 212802 → 32893 samples,
ratio 0.1546 — about twice as strong. At DEC knob 0 both are exactly 1.0
(inert), so the knob-gating half of the coupling is unaffected; only the
DEC-1 proportional strength differs by engine.

This is a documentation fact, not a defect: `BodyVoice::_apply_env()`'s
damping curve is long-standing, by-ear design, and no gate in
`tests/test_step_accent.cpp` asserts a BODY decay ratio at all (G5 is
`SynthEngine`-only) — BODY's compile-enforced obligation is only that
`set_decay_scale` exists and both callers (`set_env_times`, `set_decay_scale`
itself) route through `_apply_env()`, not that its ring time matches SYNTH's
proportionally.

### The fire-before-process ordering is what makes FLOW's auto-drone safe from a stale accent

`Part::process` (`part.h`) runs `_fire_trigger()` before `_engine->process()`
within the same sample, and the lane fires on the first FLOW sample after
`set_step(false)` — so when `SynthEngineT::process()`'s `_auto_pending` drone
promise fires that same sample, it is struck with the accent
`_fire_trigger()` just pushed (0, since FLOW's `note_accent()` is 0 by §3),
never with whatever `_accent` the engine held from the STEP leg an instant
before. Measured 2026-08-16: 0 of 360 switch points leaked a stale STEP
accent into the FLOW auto-drone (STEPS 4/8/16 × 4 seeds × 3 rates × 10 switch
phases), max `|d|` 0.000000. This is the invariant
`tests/test_step_accent.cpp`'s STEP→FLOW seam case measures from sample 0 of
the switch rather than after a settle window — the ordering is why there is
nothing to settle.

## 9. FEED: where the coupling tips

Spec: `docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md`.
Measured 2026-08-19 on `FeedBank` alone — `engine/feed/feed_pair.h`, driven
directly, with **no** `FeedEngine` around it: no chord layer, no pitch
attenuation, no envelope, no `tanh` ceiling and no SUB. Everything below is a
statement about the ring, not about a FEED deck. What the finished engine
measures is a separate section, written when there is a finished engine.

**Setup, in full.** 48 kHz. Played pitch 220 Hz. `feed_cfg::kPairs` was **4 and
a PLACEHOLDER** when this ran — `kPDecided` was false and the `feed_pairs`
bench had not happened, so every figure here is "at P = 4" and not "at P".
Pairs snapped (not glided) onto a zero-mean signature spanning ±S cents, equal
amplitude `1/kPairs`, pan = the signature. `set_damp_coef(1)` throughout, i.e.
the DAMP one-pole fully open, which is the worst case — a FEED deck at FILT
centre runs coefficient 0.342 and is quieter than this. 2 s settle before every
measurement.

### The pitch estimator is part of the measurement, and two of them lied

Every "cents off" figure here is YIN (de Cheveigné & Kawahara 2002) — the
cumulative mean normalized difference function, absolute threshold 0.15,
parabolic peak — over a **131072-sample (2.73 s)** window, reporting both a
frequency and an aperiodicity. Three estimators were tried; the first two
returned confident wrong answers, which is the only kind of measurement error
that survives into a document:

| Estimator | Failure | Measured |
|---|---|---|
| `argmax` of raw autocorrelation | locks to a SUB-multiple | −1200.97 ct (the octave below) and −1905.27 ct (~73 Hz), on rows including BOND 0 |
| normalized autocorrelation, shortest lag reaching 0.9·max | locks to a HARMONIC | +1904.44 ct (the third harmonic, 660 Hz) |
| YIN | — | ±0.02 ct on sine, 8-harmonic, missing-fundamental and harmonics-3..9-only test signals; 0.9726 aperiodicity on white noise |

Two consequences that any later gate inherits:

- **Aperiodicity is part of the answer.** A row whose aperiodicity reaches 0.30
  has no fundamental to be right or wrong about, and is counted as *unpitched*
  below rather than folded into a cents average. A ring driven past its
  stability point genuinely has no pitch; an estimator that returns a number
  anyway is how a gate ends up asserting something about noise.
- **The window has to outlast the beat.** A ±S cent cluster beats at
  `220·(2^(S/600) − 1)` Hz — 0.764 Hz at S = 3, i.e. a 1.31 s period. Measured
  at S = 3 with a 16384-sample (0.34 s) window: **+1203 ct at aperiodicity
  0.08**, a confident octave error, at five different BOND positions. The same
  row at 131072 samples reads +0.02 ct. Any gate that estimates FEED's pitch
  needs a window of at least two beat periods at the smallest spread it uses.

### RATIO 3.5 is excluded, and it is arithmetic rather than a defect

A half-integer modulator:carrier ratio doubles the signal's period, so the
fundamental of a 220 Hz carrier at ratio 3.5 really is 110 Hz. Measured
−1200 ct at every BOND including 0. The ratio grid here is therefore
{1, 2, 3, 7}. This is also, independently, the argument for RATIO's integer
magnet (spec §4): the knob's lower half must not stand on a half-integer.

### The feedback amount is the dominant variable, and 0.30 cycles was past the edge

Worst |cents| among *pitched* rows, and how many of the 20 depth × ratio rows
had no fundamental at all, at spread 7 ct:

| `fb` (cycles) | (rad) | BOND 0 | BOND 0.5 | BOND 1.0 |
|---|---|---|---|---|
| 0.04 | 0.251 | −0.40, 0 unpitched | −0.40, 0 | +0.56, 0 |
| 0.06 | 0.377 | −0.40, 0 | +0.47, 0 | +0.75, 0 |
| **0.08** | **0.503** | **−0.40, 0** | **+0.55, 0** | **+0.70, 3** |
| 0.10 | 0.628 | −0.40, 0 | +0.55, 0 | +0.75, 5 |
| 0.12 | 0.754 | +1903, 0 | −0.68, 0 | +0.87, 7 |
| 0.15 | 0.942 | +1907, 0 | −1.25, 1 | −1202, 9 |
| 0.20 | 1.257 | +1903, 0 | −0.97, 5 | +0.47, 12 |
| 0.30 | 1.885 | +23.7, 2 | −1.91, 8 | +0.61, 12 |
| 0.40 | 2.513 | −0.40, 15 | −1.98, 12 | +0.41, 14 |

Isolated further: **one pair, spread 0, BOND 0** — no coupling, no cluster,
nothing but self-feedback — shifts +49.6 ct at `fb` 0.30 and +0.04 ct at
`fb` 0.15, index 0.70. The mechanism is the classical one: a feedback-FM
operator stops being periodic at its carrier somewhere past β = 1 rad
(= 0.159 cycles), and 0.30 cycles is 1.885 rad. The two-sample average
(`0.5·(m[n−1] + m[n−2])`, the Plaits recipe) lifts that limit — the table shows
BOND 0 clean up to 0.10 cycles, not up to 0.159/2 — but it does not remove it.

`feed_cfg::kFbBaseCycles` was **0.30 by ear, first try, and is 0.08 measured**.
It is still a by-ear constant and Task 13 may move it; what this section fixes
is the ceiling it may not be moved above.

### The BOND threshold, and the cliff

At `fb` 0.08, spread ≤ `kSpreadKneeCt`, over 80 rows per BOND position
(4 spreads × 5 depths × 4 ratios), 131072-sample window:

| BOND | worst cents | unpitched |
|---|---|---|
| 0.0 | +0.253 | 0/80 |
| 0.1 | +0.240 | 0/80 |
| 0.2 | +0.294 | 0/80 |
| 0.3 | +0.392 | 0/80 |
| 0.4 | +0.465 | 0/80 |
| 0.5 | +0.508 | 0/80 |
| 0.6 | +0.551 | 0/80 |
| **0.7** | **+0.609** | **0/80** |
| 0.8 | **+22.522** | 1/80 |
| 0.9 | +0.692 | 1/80 |
| 1.0 | +0.718 | 3/80 |

**BOND 0.7 is the last position at which every row is pitched and inside a
cent.** The break at 0.8 is not gradual — the worst row jumps from +0.609 ct to
+22.522 ct and the first row loses its fundamental entirely — which is the
cliff §2.3 asks the knob to have, arriving at 70 % of its travel.

Two numbers follow, and they are the map's rather than the ear's:

- `feed_cfg::kBondPitchThreshold` = **0.7**.
- `feed_cfg::kPitchCentreTolCt` = **2.0 ct**. Sized as 3.3× the worst reading
  below the threshold (0.609 ct) and 11× below the first broken row (+22.5 ct),
  so it discriminates with a wide margin in both directions. Against §2.6's own
  calibration — SWARM's withdrawn +420 ct earned "HARM almost always sounds
  detuned", +4.5 ct was accepted — 2.0 ct is comfortably inside what was
  accepted by ear and two orders of magnitude below what was rejected.

**Do not retune either in a listening session — re-run the probe.**

### SPREAD: the lower half has a fundamental, the upper half stops having one

`fb` 0.08, BOND ∈ {0, 0.35, 0.7}, 27 rows per spread, same window:

| spread (ct) | worst cents | unpitched |
|---|---|---|
| 0 | +0.522 | 0/27 |
| 3 | +0.534 | 0/27 |
| 5 | +0.582 | 0/27 |
| **7** | **+0.609** | **0/27** |
| 10 | +0.590 | 1/27 |
| 15 | +0.939 | 3/27 |
| 20 | +1.221 | 4/27 |
| 30 | +1.708 | 7/27 |
| 45 | +2.433 | 10/27 |
| 60 | +4.509 | 11/27 |

The boundary between "audibly beating" and "audibly a cluster" lands on
**7 cents**, which is `kSpreadKneeCt`'s by-ear first-try value to the cent —
the last spread at which every row still has one fundamental. Above it the
cluster progressively stops having one, reaching 10 of 27 rows at
`kSpreadMaxCt` = 45.

That is **not** a reason to lower `kSpreadMaxCt`, and §3.4 is why: the upper
half of SPREAD is *meant* to reach dense roughness. What it does mean is that
§9.9's pitch-centre claim is a claim about SPREAD's lower half, and a gate that
asserted it across the whole travel would be asserting a pitch that the design
deliberately dissolves. The rows that remain pitched at 45 ct are still within
2.433 ct, so nothing above the knee is *out of tune* — there is simply less and
less of a single "tune" to be out of.

### Spectral flux: the shape of the G10 measurement, on the bank

Normalized magnitude flux between two Hann-windowed 32768-sample spectra taken
9 s apart, `fb` 0.12, ratio 1:

| BOND | spread 7 ct | spread 20 ct |
|---|---|---|
| 0.00 | 0.182 / 0.164 | 0.054 / 0.055 |
| 0.25 | 0.377 / 0.350 | 0.234 / 0.266 |
| 0.50 | 0.412 / 0.332 | 0.291 / 0.286 |
| 0.75 | 0.335 / 0.301 | 0.281 / 0.293 |

(two figures per cell: depth 0.5 / depth 0.8.)

The structure the two-sided inner-life gate needs is here — coupled BOND moves
the spectrum 2–5× as much as BOND 0 does — but **the BOND 0 floor is 0.05–0.18,
not the ~0.02 a gate might assume**, because a bank of detuned pairs beating
against each other is not perfectly stationary over 9 s either. A G10 written
against an absolute "still < 0.02" would fail on a correct engine. The
threshold belongs on the *ratio*, and the absolute floor has to be measured on
`FeedEngine` — with the envelope and the ceiling in the path — rather than
carried over from this table.

### What the finished engine measures

Measured 2026-08-19 on `FeedEngine` at 48 kHz — the whole engine, so the chord
layer, the pitch attenuation, the envelope, the `tanh` ceiling and SUB are all
in the path, unlike the bank-only figures above. `set_seed()` before `init()`
(the `SynthEngineT` order — `init()` consumes the seed to draw the SPREAD
signature and the per-pair feedback offsets), seeds 99 / 999 / 4242 / 7 /
12345. P is `feed_cfg::kPairs` and is **still a placeholder here**: the
`feed_pairs` bench has not run, `kPDecided` is false, and gate `feed G8` is red
until it does. Every figure below is at P = 4.

**BOND 0 is a line spectrum and coupled BOND is not.** Over a 262144-sample
(5.5 s) window, with every lane static and FLOOR 1, the spectral flatness of
the deck's output:

| seed | BOND 0 | BOND 0.5 | BOND 1.0 |
|---|---|---|---|
| 99 | 0.000547 | 0.005369 | 0.125733 |
| 4242 | 0.000826 | 0.004624 | 0.066854 |
| 999 | 0.000436 | 0.003013 | 0.036366 |
| 7 | 0.000933 | 0.004263 | 0.062244 |
| 12345 | 0.000416 | 0.005348 | 0.078832 |

Monotone in BOND at every seed, worst separation 4.6× at half BOND and 39× at
full. A fixed set of frequencies produces a line spectrum however hard its
members beat against each other, so this is the coupling and nothing else.

**Flatness, not window-to-window flux, is what separates them**, and that is a
measurement rather than a preference. The flux measure §2.4 suggests does not
discriminate on this engine: BOND 0 gives 0.031–0.210 against BOND 0.5's
0.124–0.231 — overlapping ranges, because the SPREAD signature is a random draw
and the two closest pairs can land unresolvably close, beating with a period of
tens of seconds. Which seed was drawn decided the answer.

**Flatness at BOND 0 also reads inharmonicity, so the gate runs at an integer
RATIO.** Measured at BOND 0 across the RATIO knob: 0.00064 at 1:1 and 0.024 at
4:1, against 0.084 at 2.5, 0.187 at 6.8 and 0.288 at 11. Only an integer
modulator:carrier ratio puts the sidebands on the carrier's harmonics.

**The pitch centre holds across the whole BOND travel below the threshold.**
At played pitch 0.35 (227.76 Hz), depth 0.7, FLOOR 1, over SPREAD's lower half
and five seeds, with a 131072-sample YIN window: the worst drift from the same
configuration's own BOND 0 reading is **1.204 ct**, and at SPREAD 0 — where
there is no cluster to confound it — the worst absolute offset from the played
pitch is **0.063 ct**. Aperiodicity stayed below 0.004 on every row.

Two consequences the gate is written around:

- The claim is about BOND, so the quantity is drift **with BOND**. An absolute
  bound at fixed SPREAD measures something else: a detuned cluster's perceived
  centre sits a fraction of its own half-spread from the geometric mean of its
  pairs — up to 3.795 ct at `kSpreadKneeCt` on seed 99 — because the pairs are
  at fixed frequencies and their exact positions are a random draw. G13 pins
  the arithmetic centre; G15 pins what the knob does to it.
- **The engine does not break beyond the threshold at this pitch.** Measured at
  BOND 0.8 / 0.9 / 1.0, SPREAD 0, seeds 99 and 4242: −0.07 to −0.12 ct at
  aperiodicity 0.0008. The bank breaks there and the engine does not, because
  the pitch attenuation is doing its job: at pitch 0.35 the effective feedback
  is ~0.059 cycles, below the 0.08 the bank's cliff was measured at. The cliff
  §2.3 asks BOND to have therefore lives at the BOTTOM of the pitch range,
  where the attenuation is weakest — which is a listening question for the
  by-ear pass, not something a gate should assert.

**The feedback attenuation spans 0.135 down to 0.034 cycles** from the bottom of
the pitch axis to the top — a factor of 4.0 — which is why BOND audibly weakens
toward the top of a chord. A decision, not a side effect (G12):

| pitch | Hz | fb range, drive 0.14 | fb range, drive 0.08 |
|---|---|---|---|
| 0.00 | 110.00 | 0.13532 – 0.14591 | 0.07732 – 0.08338 |
| 0.25 | 185.00 | 0.10994 – 0.11855 | 0.06283 – 0.06774 |
| 0.50 | 311.13 | 0.08457 – 0.09119 | 0.04833 – 0.05211 |
| 0.75 | 523.25 | 0.05920 – 0.06384 | 0.03383 – 0.03648 |
| 1.00 | 880.00 | 0.03383 – 0.03648 | 0.01933 – 0.02084 |

(the spread within each row is NEW's per-pair offset, ±`kFbOffsetRange`.)

**RATIO's lower half spends 34.3 % of its travel within 2 % of an integer**
against 4.8 % for a linear map over the same knob positions — **7.2×** — and the
map is monotone throughout (G26). The gate asserts the ratio against the linear
baseline rather than the 34.3 %, because that figure is a statement about
`kRatioMagnetExp`, which is a by-ear constant.

**SPREAD is exact at the centre and two-segment in its travel** (G13, G14). The
geometric mean of the pairs sits within **0.021 cents** of the tone at every
knob position, which is what the per-group zero-meaning buys:

| knob | `_spread_ct` | span across the pairs | geometric mean vs tone |
|---|---|---|---|
| 0.00 | 0.00 ct | 0.00 ct | −0.0155 ct |
| 0.25 | 3.50 ct | 6.29 ct | −0.0188 ct |
| 0.50 | 7.00 ct | 12.59 ct | −0.0185 ct |
| 0.75 | 26.00 ct | 46.77 ct | −0.0206 ct |
| 1.00 | 45.00 ct | 80.95 ct | −0.0185 ct |

**A FEED deck voices at most `kPairs / kPairsPerTone` chord tones**, so every
voiced tone keeps a group SPREAD can reach. At P = 4 that cap is 2, and at
COLOR's four-note chord the deck voices 2 tones (G25).

**FILT is a real low-pass on the output, and was not one until 2026-08-19.**
It drove a one-pole INSIDE the feedback path; Bastian reported the knob as
doing nothing, and the measurement says he was right. Three reasons, each
sufficient on its own: the in-loop pole never touches the carrier, so it
changes how the coupling wanders rather than where the spectrum ends; the
signal it filtered peaks at `kFbBaseCycles`, which is 0.08 cycles at the
bottom of the pitch range and 0.019 at the top; and its coefficient
`1 - exp(-2*pi*fc/sr)` already sat at 0.34 at the centre detent, 0.72 half a
travel up and 0.98 at the top, so the upper third of the knob was
indistinguishable from wide open. The in-loop pole is now FIXED at
`kDampFixedHz` — it is half of this engine's anti-aliasing, the other half
being FeedPair's two-sample average, and a knob that can switch that off is
not a knob. FILT is an `SvfLp` on the deck output, on the instrument's usual
60 Hz–14 kHz rails.

Measured on the finished path, seeds 99/999/4242/7/12345 over
BOND × DEPTH × RATIO × pitch, 1 s per cell (G33):

| `kFiltRes` | damping ratio | worst output | vs `kSatCeil` |
|---|---|---|---|
| 0.15 | 0.38 | 0.8714 | **1.584× (+4.00 dB)** |
| 0 | 0.91 | 0.5353 | 0.973× (−0.24 dB) |

**`SvfLp::SetRes` maps through `r^0.25`, so a value that reads as "a touch of
resonance" is not one.** 0.15 is already 62 % of the way to full resonance, and
`0.707` — where a two-pole stops peaking at all — is passed well below it.
`kFiltRes` is therefore 0, which is a measurement and not a preference: the
filter sits AFTER the tanh (upstream, the saturator would regenerate exactly
the partials the filter removed and the knob would read weak again), so a
resonant peak there breaks the one hard bound the deck offers. Any future
engine reaching for `SvfLp` with a "small" resonance should read this row
first — the trap is in the shared primitive, not in FEED.

**The right half of FILT is scaled by `kFiltRightScale`, and the left half is
the sampler's arithmetic verbatim.** On a sampler deck FILT is a TRIM added to
a lane that already carries the cutoff, so `n_raw = kFiltNeutral + amt`
saturating early costs nothing. FEED has no such lane — `LANE_SIZE` is SPREAD
here — so FILT is the whole control, and unscaled it clamped at `n_raw = 1` by
knob position 0.25: measured bit-identical 14 000.0 Hz from there to +1, i.e.
the top 70 % of the right half dead in the hand. Scaled, the travel is
monotone across its whole length: 60 Hz at −0.6, 3 582 Hz at the detent,
14 000 Hz at +1, with −1.0…−0.8 the fade to silence. High-band share above
4 kHz runs 0.720 wide open against 0.0100 nearly shut — a factor of 72.

### The drive went to 0.14 by ear, and what moved

`kFbBaseCycles` was raised from 0.08 to **0.14** on 2026-08-19 after a
six-render A/B (variants of `kDampFixedHz` × `kFbBaseCycles`, BOND stepped
0 → 1 over 26 s at pitch 0.35). Bastian picked the hard-drive, bright-path
variant: *"ruhig hart, filtern kann ich selbst dahinter"* — the escalation is
the point, and taming its top end is the output FILT's job now that FILT is a
real low-pass. Darkening the in-loop path instead (1200 Hz and 500 Hz were
both rendered) was rejected because it removes the brightness for everyone
rather than leaving it under a knob.

**0.14 knowingly sits above the 0.08 at which the bank alone tips.** That
ceiling is still the measurement §2 recorded; the engine is now driven past
it over the lower part of the pitch range on purpose. Do not "correct" one
number to the other.

**The BOND pitch threshold fell from 0.70 to 0.65** (`kBondPitchThreshold`).
It is a description the gate samples below, not a setting — nothing in the
engine reads it — so it has to be re-run whenever the drive moves. What the
re-run found matters more than the number: at pitch 0.35 the ring **does not
break anywhere on the knob**. Across the whole BOND travel and SPREAD's lower
half the aperiodicity peaks at **0.0171**, against the 0.30 that would mean
"no fundamental". The drift is a smooth monotone creep, and it crosses the
2 ct tolerance rather than falling off anything:

| BOND | drift at SPREAD 0 | at 0.25 | at 0.50 |
|---|---|---|---|
| 0.25 | 0.001 ct | 0.237 ct | 0.295 ct |
| 0.50 | 0.002 ct | 0.825 ct | 1.158 ct |
| 0.65 | 0.005 ct | 1.259 ct | 1.813 ct |
| 0.70 | 0.006 ct | 1.363 ct | **2.003 ct** |
| 1.00 | 0.015 ct | 1.861 ct | 3.007 ct |

So the honest statement is "the 2 ct bound holds to BOND 0.65, and a 3.1 ct
bound holds everywhere". `kPitchCentreTolCt` was deliberately NOT widened to
3.5 to keep the whole travel inside the gate: loosening a bound so a test
passes is how a gate stops being one. The cliff proper still lives at the
bottom of the pitch range, where the attenuation is weakest.

**BOND is not inert and never was.** The complaint that produced this round
was "klingt nicht gut", not "tut nichts", and the two need different fixes.
Spectral flatness across the knob, seed 99, FLOOR 1, DEPTH 1, at the old
drive:

| pitch | BOND 0 | 0.50 | 1.00 | span |
|---|---|---|---|---|
| 0.00 | 0.00002 | 0.00161 | 0.04803 | 2492× |
| 0.35 | 0.00004 | 0.00119 | 0.02040 | 543× |
| 0.70 | 0.00006 | 0.00057 | 0.00190 | 31× |
| 1.00 | 0.00027 | 0.00104 | 0.00205 | 7.6× |

Monotone everywhere, and heavily back-loaded: half the knob travels between
two line spectra. A future "BOND does nothing" report should be checked
against the *distribution* of its effect before its magnitude.

### Level parity, and the ceiling that was a compressor

Reported by ear ("die Engine scheint mir viel lauter zu sein als die anderen"),
then measured. One `Part`, identical lanes, identical LEVEL 1, no part FX and
no reverb, 6 s from one trigger:

| engine | FLOW peak | FLOW RMS | vs SYNTH | STEP RMS | vs SYNTH |
|---|---|---|---|---|---|
| SYNTH | 0.333 | −21.8 dB | ±0 | −32.9 dB | ±0 |
| WAVE | 0.444 | −19.0 dB | +2.8 dB | −29.5 dB | +3.4 dB |
| BODY | 0.053 | −31.8 dB | −10.0 dB | −62.0 dB | −29.1 dB |
| FEED (before) | 0.547 | −10.0 dB | **+11.8 dB** | −14.8 dB | **+18.1 dB** |
| FEED (after) | 0.343 | −18.7 dB | +3.1 dB | −25.3 dB | +7.6 dB |

**The 0.547 peak was `kSatCeil`, not the signal.** Sweeping LEVEL shows the
tanh was not a ceiling at all but a permanent compressor:

| LEVEL | peak | linear expectation | squash |
|---|---|---|---|
| 0.10 | 0.122 | 0.124 | −0.14 dB |
| 0.30 | 0.323 | 0.371 | −1.20 dB |
| 0.50 | 0.444 | 0.618 | −2.87 dB |
| 1.00 | 0.537 | 1.236 | **−7.24 dB** |

So the coupling was being judged through 7 dB of gain reduction before it
reached any compressor downstream — and the master limiter's drive-0 knee
(−1 dBFS, bit-transparent below) sat only 4.2 dB above one FEED deck, against
8.6 dB for SYNTH. Two FEED decks cleared it.

The cause is that `1/sqrt(kPairs)` normalizes for INCOHERENT summation, and FM
carriers on chord tones plus an uncoupled SUB are partially coherent. Fixed
with `kDeckGain` = 0.25 applied to the deck sum immediately before the ceiling;
the tanh now catches peaks (~0.8 dB at LEVEL 1) and the cliff it exists for is
untouched. Two decks now peak at 0.686 against SYNTH's 0.666.

**FEED still reads +7.6 dB RMS in STEP, and that is density rather than gain.**
Its peaks match (0.308 against SYNTH's 0.167 is the struck case; the drone case
is 0.343 against 0.333). A FEED note keeps ringing where a SYNTH note has
decayed — the ring is free-running by design, a trigger only injects into it.
Trimming further to equalise the struck RMS would leave the drone too quiet,
and the drone is the mode the engine exists for.

**BODY is 10 dB below SYNTH as a drone and 29 dB below struck**, on the same
settings. That is unexamined here: BODY's level depends heavily on MATL and
EXCIT, and one probe setting is not a verdict on it. Recorded because the
parity brief covers every engine, not just this one.

### What the hardware measures, and the state nobody was benching

Patch Submodule, 480 MHz, block 96, dcache+icache, `-O2`, AXI, over USB; two
runs, checksums and QSPI digest identical.
[`docs/bench/2026-08-19-ac26589-feed-axi-o2-patch_sm-usb.md`](bench/2026-08-19-ac26589-feed-axi-o2-patch_sm-usb.md).

| workload | avg % | max % |
|---|---:|---:|
| `inst_feed_engine_worst` (both decks FEED) | 75.99 | 81.41 |
| `inst_bbd_engine_worst`, same image | 93.56 | 98.39 |
| `instrument_worst`, same image | 99.30 | 104.18 |
| `feed_pairs` at P = 4 | 8.04 | 8.24 |

Task 11's gate — `inst_feed_engine_worst` must not exceed `instrument_worst`
**inside one image** — passes on both average and maximum, with room. P is
still a placeholder: one point is not a slope, so `kPDecided` stays false and
`feed G8` stays red.

**Two traps this capture set, both worth more than the numbers.**

**1. The FEED worst case was priced on a silent deck.** `inst_feed_engine_worst`
set FILT to −1, which under the old DAMP mapping was "darkest" and under the new
one is exactly where `_filt_gain` fades to zero. The ring, the `tanh` and the
SVF still ran, but Grit, Comp, the centre reverb and the master limiter were all
priced on zeros. Corrected to −0.4, the darkest cutoff that does not fade; the
SVF costs the same at every cutoff, so the value is chosen for the deck emitting.
Silence had understated the row by about 1 percentage point.

**2. A cross-capture subtraction is not a measurement, and here is the size of
it.** Between `3bdcb17` and `ac26589`, rows whose checksum did not change at all
moved from **−0.31 % to +2.54 %** on a relink — `mod_plane_2x_center` alone swung
−4.59 % on an intermediate capture. `inst_feed_engine_worst` grew 6.10 %, of
which the heavy-instrument drift band accounts for 0.43–2.54 %. So the filter and
drive work costs FEED somewhere in **+24 500 to +39 000 cycles** on two decks
(+2.6 to +4.1 points of block budget). That range is the honest answer; splitting
it finer needs an ablation image, not another capture.

### FEED spends 92 % of a silent tail in denormals

Task 11 step 5.1, measured 2026-08-19 on the desktop. `FeedEngine`, FALL 0 so
FLOOR is 0 too, one hit, then 20 s:

| quantity | subnormal | zero | normal |
|---|---:|---:|---:|
| deck output L/R | 1 759 436 | 96 780 | 63 784 |
| per-pair `amp` (every 64th sample) | 57 960 | 8 | 2 032 |

The envelope reaches exactly 0. `p.amp` does not, and **the reason is the fix
for the glide runaway**. `FeedBank::set_target` arms `_slope_left` and the
snap-to-target at the end of `process` only fires when that counter expires —
but `_rebuild_allocation` re-targets every pair every control tick (96 samples,
well inside `kSlopeSamples`), so the counter is re-armed before it ever expires
and the amplitude converges *geometrically* instead of arriving. Geometric
convergence to zero lands in the subnormal range and stays there. The comment at
`engine/feed/feed_pair.h:76` describes this behaviour precisely — as a feature.

Nothing else in the bank decays: `m1`, `m2`, `o1`, `o2` and the DAMP state `lp`
are all `fast_sin` outputs on a path that is never amplitude-scaled. A bare
`FeedBank` retargeted **once** counts **zero** subnormals across all of them and
lands `amp` at exact zero — the contrast is the retargeting, not the ring.

**What it costs, on the desktop, where the penalty is not the board's:**

| state | ns/sample | |
|---|---:|---|
| sounding, FTZ off (the state the bench measures) | 59.98 | |
| sounding, FTZ on | 56.51 | |
| **idle tail, FTZ off** | **99.05** | **1.65× a sounding deck** |
| idle tail, FTZ on | 56.36 | the penalty is entirely denormals |

**So FEED's worst case may be silence, not any knob setting**, and no bench row
measures it: `inst_feed_engine_worst` fires a trigger every ~half second
specifically to keep the voices busy. The 81.41 % above is the cheap state. This
also reaches the P decision — sizing `kPairs` against a sounding deck sizes it
against the wrong case.

### Fixed, and what the board says

The local fix, chosen over `FPSCR.FZ` because it is the smaller claim and
leaves the instrument-wide decision where spec §8 put it. It took two parts,
and the second only became visible once the first worked:

1. **`FeedBankT::kArriveEps`** (`1e-9`). `set_target` snaps a value to its
   target when the remaining travel falls under it, at the control rate, so
   the audio path is untouched. The increments went subnormal along with the
   values, which is why a SOUNDING deck at static pitch was paying a smaller
   version of the same tax.
2. **`SvfLp::FlushDenormals`**. With the glide arriving, the deck goes
   *exactly* silent -- and a two-pole fed exact zeros decays into the same
   place. Opt-in at the caller's control rate, so `Voice` and `SamplerEngine`
   are bit-for-bit unaffected and the `daisysp::Svf` equality still pins the
   algebra.

Desktop, `FeedEngine`, 20 s after one hit at FALL 0:

| | subnormal output samples | idle vs sounding | FTZ off vs on |
|---|---:|---:|---:|
| before | 1 759 436 of 1.92 M | 1.65× | 1.76× |
| after | **0** | **1.00×** | **1.00×** |

**FTZ making no difference at all is the proof**, not the ratio: nothing on
FEED's path reaches the subnormal range any more. The sounding case got 6 %
faster too (59.98 → 56.47 ns/sample).

On hardware, with the new `inst_feed_engine_idle` row
([`500775c`](bench/2026-08-19-500775c-feed-axi-o2-patch_sm-usb.md)):

| workload | avg % | max % |
|---|---:|---:|
| `inst_feed_engine_idle` (both decks SILENT) | 75.64 | **76.77** |
| `inst_feed_engine_worst` (both decks sounding) | 77.70 | 83.32 |
| `instrument_worst`, same image | 98.67 | 103.48 |
| `feed_pairs` at P = 4 | 8.03 | 8.21 |

Silence is now the CHEAPER state, which is what a working engine looks like.
The spread between average and maximum is the other tell: 1.5 % on the idle
row against 7.2 % on the sounding one, i.e. a genuinely steady state rather
than a tail doing intermittent work.

**The ARM factor itself is unmeasured.** 1.65× is a desktop number, and this
image does not contain the broken engine to compare against. Getting it would
cost a commit of a deliberately reverted engine plus a board round; the fix is
justified without it, so it has not been spent.

**What it took to reach silence at all**, because none of it is visible from
reading the workload: `set_step(p, true, 16)` from the worst-case config keeps
the sequencer firing, `DENS 0` still leaves a note on every phrase boundary
(2.58 s), and FEED's FALL at 0 needs **6.67 s** to cross `Env`'s 1e-4 idle
threshold at 120 BPM. The row now sets DENS, RATE and VARY to 0 and *waits*
for both parts to report idle rather than warming for a guessed number of
blocks -- a guess that was short by 333 blocks out of 3000 and halted the
board with no row and no message, twice. An `assert` in a bench setup has no
failure mode that reaches the operator.

### P = 6, and the two things it buys

Decided 2026-08-19 from three sweep points on the Patch Submodule;
[the confirming run carries the full derivation](bench/2026-08-19-3def5d5-feed-axi-o2-patch_sm-usb.md).

**18 655 cycles per pair**, plus ~2 100 fixed. Linear to within 3 % over a 4×
range in P, and the whole-engine row agrees independently at 18 708 per pair
per deck -- the check that caught SWARM's kernel row sizing its bank too
generously found nothing to catch here.

| P | `feed_pairs` avg | two-deck worst max | % budget | tones at COLOR max |
|---:|---:|---:|---:|---|
| 2 | 39 110 | 710 455 | 74.0 | 1 of 4 |
| 4 | 77 176 | 799 939 | 83.3 | 2 of 4 |
| **6** | **118 025** | **888 099** | **92.51** | **3 of 4** |
| 8 | 151 190 | 951 817 | 99.2 | 4 of 4 |

Two traps in reading that table. **The worst-case row runs with FLUX off**, as
the BBD row does, so no percentage in it includes stereo tape -- one switch
away, 10.3 % on its own. And **6 was confirmed rather than interpolated**: the
linear fit predicted 91.04 % and the run measured 92.51 %, so the fit is about
1.6 % optimistic between its points and the surviving reserve is 7.5 %.

P is not only a CPU number. The bank voices `kPairs / kPairsPerTone` tones
capped at `ChordBuilder::kMaxNotes` = 4, so six pairs sound a complete triad
and drop only the fourth note at the top of COLOR. Four pairs would have
sounded half the chord, silently -- which is why the decision did not simply
take the cheapest P that fits.

`kDeckGain` was re-measured at the new P, as its own comment demands, and left
at 0.25: the deck rose 0.6 dB (drone peak 0.343 → 0.369), the RMS distance to
SYNTH is unchanged at +3.05 dB, and two decks reach 0.738 against the limiter's
0.891 knee.

### The two open questions got a knob

Neither DEPTH nor the in-loop DAMP cutoff had a panel home, and both are
recorded above as unanswered. They spent 2026-08-19 as context-menu sliders and
are **panel knobs** since the same evening — `DPTH` and `EDGE`, in VOICE on both
panels, one per deck.

- **DEPTH**, the FM index, 0..1. It was never unreachable in principle — it is
  the `LANE_MOTION` base — but the host pinned it to `feed_cfg::kDepthBase` and
  the only way to move it was to route MOD at it. On an FM engine the index is
  arguably the most musical parameter there is, which makes spec §4's
  "DEPTH at 0.5 must be a good sound" a claim worth being able to disprove.
- **DAMP cutoff**, first as an absolute 200 Hz–16 kHz LOG control and, later
  the same day, as the bipolar **EDGE trim** it is now: `FeedEngine::set_edge`,
  centred exactly on `kDampFixedHz` and spanning `feed_cfg::kEdgeOctaves` (2.0)
  either side, i.e. 800 Hz to 12.8 kHz. 3200 Hz was confirmed only against
  DARKER alternatives — variants B and C at 1200 and 500 Hz were rejected by
  ear — and nobody has heard it against a brighter one, so the trim reaches up
  as far as it reaches down. It still reaches past where half this engine's
  anti-aliasing stops working (the other half is FeedPair's two-sample
  average): hearing where it starts to alias is the point.

Both boot neutral, so a patch that never touches them behaves exactly as
before. As ParamIds Rack persists them for free, so the module holds no state
for them at all — the two float arrays, their JSON keys and their reset all
left with the menu. `set_edge` carries an exact-argument guard on the KNOB
value: the host pushes it every block from a knob that almost never moves, and
without the compare that is one libm `pow` plus one `exp` per part per block
for nothing. The firmware calls neither; `init()` reaches the cutoff through
`set_edge(0)`, which is the only other writer.

**The gate on DPTH's default RECOMPUTES it** rather than comparing against a
literal (`test_feed_shipped_defaults_are_the_engine_constants`): it reads
`kDepthBase` out of `feed_config.h` and derives what `INIT_DEFAULTS` must hold,
because that knob IS the `LANE_MOTION` base. EDGE's half of that gate is gone
with the rails it derived from — a trim's boot value is its centre, on every
engine at once, and Task 8 of the DPTH/EDGE plan replaces it with the stronger
assertion the trim makes possible: that each engine's neutral is a NAMED
CONSTANT in its own header.

**EDGE broadcasts to all six engines since 2026-08-19** (`Part::set_voice_edge`,
spec voice-knobs-dpth-edge §4.4), but five of them still answer with a stub, so
it is audible on FEED alone until Tasks 4–7 land;
`tests/test_voice_edge_broadcast.cpp` is the ledger of which ones are real. DPTH
reaches all six for real. The plate word `EDGE` is still a placeholder —
`DAMP` is taken by BODY's `DECAY`.

### Three measurement traps this engine sets, and what they cost

Recorded because each one produced a confident wrong number first, and each is
the kind a later gate will walk into again:

1. **A pitch estimator that is not YIN.** Both simpler estimators returned
   octave errors with high confidence — see the table above. Any FEED pitch
   measurement needs YIN and needs to read its aperiodicity.
2. **A window shorter than the beat.** A ±S cent cluster beats at
   `f·(2^(S/600) − 1)`; at S = 3 ct that is a 1.31 s period, and a
   16384-sample window read +1203 ct at aperiodicity 0.08.
3. **The `tanh` ceiling standing in for the thing being measured.** Two gates
   were caught by this. G18 (does the index ride the envelope?) passed with the
   envelope removed from `set_index` entirely, because at LEVEL 1 the attack
   window clips and the tail does not, which moves the spectral centroid by
   itself. G21's DEC-0 inert half read a 7 % note-length gap that was the
   ceiling compressing the louder note's peak. **Any FEED gate whose subject is
   spectral shape or relative level must run below `kSatCeil`.**
