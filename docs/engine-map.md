# Engine map

What you cannot learn by reading one file. Everything here was **measured**, not
inferred — each table names the probe that produced it, and a probe against the
real engine costs about half a second (see [Probes](#probes) at the bottom).

This file exists because spec drafts kept asserting runtime behaviour that the
code does not have. Mean review rounds per spec grew 1.44 → 4.33 between 10 July
and 13 August 2026 as `engine/` grew to 86 files / ~20 800 lines, and the
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

**Sibling authority:** [`flow-fireflow-param-map.md`](flow-fireflow-param-map.md)
owns the *other* question — which of the 64 `ParamId`s a hand-authored `Fireflow`
patch can carry into a `flow` overlay (47 base rules, 45 mapped, 2 unreachable,
pinned by `tests/test_flow_overlay.cpp:42`). It maps **parameters across hosts**;
this file maps **behaviour inside a lane**. Neither answers the other's question,
and a design touching both needs both.

## How to read a citation

Files are named without their directory. The key:

| Named | Lives at |
|---|---|
| `lane.cpp`, `lane.h`, `song_form.h`, `super_modulator.cpp/.h`, `waveforms.h` | `engine/mod/` |
| `part.cpp`, `part.h` | `engine/parts/` |
| `center.cpp` | `engine/center/` |
| `flow.cpp`, `taste.h`, `terrain.cpp` | `engine/flow/` |
| `Fireflow.cpp`, `Glow.cpp`, `init_patch.hpp` | `host/vcv/src/` |

Note the repo also carries a full second tree under `.worktrees/`; a bare grep
will hit both.

**Line numbers rot — the quoted expression beside each is what identifies the
site.** This is not theoretical: the sibling param map was written on 2026-08-12
and by 2026-08-14 every one of its 39 `Fireflow.cpp` citations had drifted by
exactly 12 lines, plus six in `flow.cpp` and one in `flow_params.h`. All 69 were
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

**That identity is this setup line's, not the lane's — it holds at SMOOTH 0 and
does not survive the top of the SMOOTH axis.** `_update_slew()`'s note-interval
slew clamp (`lane.cpp:361`, `if (_flow_melody_on())`) is now the only place on
the melody path that still splits on `_step_mode`, so a glide is clamped in FLOW
and not in STEP. Measured on a note deck, seed 999, 8 steps, SHAPE 0, RANGE 1,
VARY 0, 20 s, max |STEP − FLOW|: bit-identical at SMOOTH 0.00 / 0.25 / 0.50 at
both 0.5 Hz and 2 Hz and at SMOOTH 0.75 at 0.5 Hz, but **0.0787 at SMOOTH 0.75 /
2 Hz, 0.2069 at SMOOTH 1.00 / 0.5 Hz and 0.2995 at SMOOTH 1.00 / 2 Hz**. Do not
carry the identity past SMOOTH 0; that clamp is the SHAPE/SMOOTH rework's to
decide, and nothing on the `melody-reachable` branch touched it.

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
999/12345/7/4242), and §7 below shows how rarely the terrain draws that quarter —
never at all on a drone. A note deck now emits the phrase at every SHAPE
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
just after a SPOT; a terrain base rule below 1.0 widens the band further.

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
| `_flow_melody` | `part.cpp:43,441` from the engine id: **off for SAMPLER and BBD** | any host directly; Glow cannot reach it except by changing the engine |
| `_melodic` | `super_modulator.cpp:14`, unconditionally, once | anything else, ever |
| `_active[slot]` | boots **all true** (`part.h:639`); only writer today is `Fireflow.cpp:881` (LANE_PITCH, `!samplerPart`), pushed every block | **the flow layer — `flow.cpp` never calls `set_target_active`** |
| `_shape_offset` | `center.cpp:143-144` every control tick | — (it is re-pushed continuously; it cannot be "left" at a value) |

### Settled: does a Sampler deck's PITCH lane modulate under Glow?

**Yes.** Two reviews disagreed on this; the code settles it. `_active` boots true,
the only writer is the `Fireflow` module, and the flow layer never calls it. So on
a Glow terrain a Sampler deck keeps `_active[LANE_PITCH] == true`, while
`_flow_melody` is false (engine id). Under the `Fireflow` module the same deck is
deactivated. **Host-dependent, and only one of the two hosts protects it.**

⚠️ **This is a known defect, not a contract.** It is recorded here so the question
stops being re-argued, not so the behaviour is preserved — do not build a design
that relies on it. `part.h:634-638` already flags the neighbouring half of the
same problem: the `Fireflow` module re-pushes `set_target_active` every block, so
the day an M6 pad can toggle `LANE_PITCH` that push will silently overwrite it,
*"harmless today only because the pad doesn't exist yet."*

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

**To probe the flow layer** (`generate()`, terrain draws) the line is longer —
`engine/flow/` needs C++17 and the vendored DaisySP headers, because `terrain.h`
pulls in `instrument.h`:

```bash
clang++ -std=c++17 -O2 -Iengine -Ithird_party -Ilib/DaisySP/Source \
        -o probe.exe probe.cpp engine/flow/terrain.cpp
```

---

## 7. Reachability: what a terrain actually draws

§2 and §3 describe what a knob position *does*. This section is about which
positions a Glow terrain ever *reaches* — a different question, and the one that
decides whether a feature exists in play. Measured with the real `generate()`,
not a re-implementation of `draw_span`, over **masters 1…20 000, every reroll
counter at 0, no `BaseOverlay`** — that population is what every percentage below
means:

| | |
|---|---|
| mean drawn SHAPE, both decks | **0.314** |
| highest SHAPE seen in the 20 000 terrains | 0.955 |
| either deck in the §3 fade zone (SHAPE > 0.75) | **4.84 %** |
| either deck above SHAPE 0.95 | **0.01 %** (2 terrains) |
| deck A drawn with RANGE < 0.25 | **25.29 %** (deck B 24.98 %) |
| terrains whose archetype is *drone* | **49.5 %** |

Per archetype, share with a deck in the fade zone: **drone 0.00 %** — 0 of the
9905 drone terrains — pulse 10.1 %, arp 8.9 %, fragment 9.7 %. The drone can
never get there: `taste.h:998-999` caps its SHAPE span at `{0, 0.25}`, and it is
half of all terrains.

Re-measured on 2026-08-14. The figures this table carried before (4.65 %, mean
0.316, max 0.956, deck A RANGE 24.97 %, drone 49.2 %) do not reproduce over the
population above; shifting the master range by one (0…19 999) moves nothing past
the third digit. **They look like a smaller draw of this same population, not a
different generator** — measured over masters 1…N for N ∈ {1, 2, 5, 10, 20, 50,
100} × 1000, the mean is 0.3160 at N = 5 000 (the old figure exactly) and deck A's
RANGE < 0.25 is 24.96 % at N = 10 000, while max SHAPE climbs 0.885 → 0.962 and
the drone share wanders 48.9 – 50.2 % with N. The one figure that never printed
is the fade-zone share: **4.60 – 5.02 % across all seven sizes**, never 4.65 %.

So quote the fade-zone share as *what a population of this size prints*, not as a
constant: it is **4.84 %** at masters 1…20 000, and about 5 % anywhere in that
range. `ModLane::_compute_raw()`'s comment quotes the same 4.84 % and points
here — the two were corrected together, on purpose, so they cannot disagree
again. **What does not move with N is the zero.** Over masters 1…100 000 — a
superset of every size above — **0 of 50 238 drone terrains** reach the fade
zone, and the highest SHAPE ever drawn on a drone is **0.2449**, under the 0.25
cap with room to spare. That is `taste.h:998-999` showing through, not a sampling
result, and it is the only figure in this section safe to quote without its
population.

**Neither SHAPE nor RANGE is reachable by any macro.** `P_SHAPE_A/B` and
`P_RANGE_A/B` appear in `kBaseRules` (`taste.h:991-999`) and in **no** story
curve, so nothing in the macro layer can move them; only the in-lane offsets of
§2 can, and on the melodic lane those total ±0.40. A drone's 0.25 base plus 0.40
is 0.65, still under the 0.75 where the bank starts crossfading toward the
phrase. **On a SAMPLER or BBD deck, therefore, the melodic phrase is unreachable
on half of all terrains by any means available to the player** — those two engine
classes still route PITCH through the waveform bank in both modes (§1), and the
drone's cap is not something a knob or a macro can lift. A note deck no longer
depends on any of this: since `07d5b9d` it emits its phrase at every SHAPE.

### The melody's second gate: RANGE, through the quantizer

The pitch axis is 36 semitones over `0..1` (`part.cpp:228-229`; the
`_detune_cents * 1/3600` at `:244` corroborates), and `LANE_PITCH` is handed
depth 1.0 unconditionally (`part.cpp:98`), so a lane value lands on that axis
directly. What the phrase then moves, after quantizing (Aeolian, STEP, 8 steps,
VARY 0, mean of seeds 12345/777/4242):

| RANGE | span (semitones) | distinct scale degrees |
|---|---|---|
| 1.0 | 8.42 | 4 |
| 0.4 — top of the drone band | 4.11 | 3 |
| 0.25 | 2.57 | 2 |
| **0.1 — bottom of the drone band** | **1.03** | **1** |

At the bottom of its RANGE band a deck plays the entire phrase — every FORM,
every SONG, every seed — on **one** scale degree.

### And the inversion, which has since inverted

This section used to close on FORM being unreachable rather than inert: at SHAPE
1.0 three of the four other `Principle`s emitted a different value set from
`TwoMotif`, at SHAPE 0.0 none of them did, and the terrain draws the quarter of
the knob where that begins to change in 4.84 % of cases, its far end in 0.01 %.
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
4.84 % / 0.01 % shares above are what govern them. Pinned by
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
