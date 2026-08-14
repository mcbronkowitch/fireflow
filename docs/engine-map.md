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
| `lane.cpp`, `lane.h`, `super_modulator.cpp/.h`, `waveforms.h` | `engine/mod/` |
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

Eight flag combinations collapse to **five behaviours**. Measured at SMOOTH 0
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
peak, measured. Pinned by `tests/test_engine_map.cpp` (§1 case). What is still
true is the shape of the flag: `_flow_melody` is an engine-class flag, not a mode,
so a design that treats "FLOW melody" as a mode orthogonal to STEP is describing a
state that does not exist.

At the 8 steps this table is measured at, the STEP note deck's stream is the FLOW
note deck's stream **sample for sample** (max deviation 0.0 over ten seeds) — the
step clock's `8/steps` scaling is 1 there. That is a property of the step count,
not of the lane: at 4 steps the two diverge by 0.298 and at 16 by 0.526, measured
at seed 12345. Both halves are pinned by the §1 case.

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

**The phrase ambitus is a distribution, not a number.** Ten seeds, correct
construction order, rate 0.5 Hz, 20 s, VARY 0:

```
0.155  0.246  0.295  0.351  0.354  0.388  0.590  0.653  0.698  0.840
```

Since 2026-08-14 this is the STEP note deck's distribution as well, not only
FLOW's: over ten seeds (999, 12345, 7, 4242, 31337, 1, 2, 3, 77, 888) the STEP
p2p equals the FLOW p2p to the last bit at 8 steps — 0.155 … 0.822 on that set.

So the ratio against the 2.000 sine staircase runs **2.4× to 12.9×** over the seed
set above, and a wider set will widen it. Any design that routes phrase values
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
| **SHAPE** | `_shape` + `_ev_shape` + `_shape_offset` + `_kick_shape`, then clamped (`lane.cpp:554`) | — |
| **SMOOTH** | `_smooth` **only** (`lane.cpp:349` → `:360`) | — |
| **RATE** | — | `_phase_inc * (1 + _ev_rate)`, plus DRIFT via `set_rate_scale` |
| **PHASE** | `_phase + _ev_phase` (`lane.cpp:114, 552`) | — |

**SHAPE has four writers. SMOOTH has exactly one.** `ModLane::_smooth` has a
setter, a reader, and nothing else — grep for it and filter out the unrelated
`_morph_smooth` / `_lvl_smooth` / `_drift_smooth` / `PartFx::_smooth`. That
asymmetry — not the waveform bank — is why one knob feels unpredictable and the
other feels inert.

The three hidden SHAPE contributors and their ranges:

| Source | Set by | Range | Character |
|---|---|---|---|
| `_shape_offset` | DRIFT, per control tick (`center.cpp:143-144`) | `±0.15 · tap · weather · drift`, taps `{+0.8, −1.0}` — the smoothed DRIFT knob is a **fourth factor**, which is what makes the term exactly 0 while DRIFT is 0 | continuous, τ ≈ 45 s |
| `_ev_shape` | EVOLVE random walk (`lane.cpp:693`) | clamped `±0.25` | drifts, decays on settle |
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
**explicit early return** at `lane.cpp:564`, `if (!_step_mode && !_flow_melody_on()) return 0;`,
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
| `_cur_step` | STEP path and the FLOW-melody path. `tick()` also writes it in a non-STEP branch (`lane.cpp:1013`), unreachable for the FLOW LFO only because `next_edge` is always 1.0 there | — but for the FLOW LFO this is moot: `_sh_slot()` **early-returns 0** at `lane.cpp:564` before reading `_cur_step` at all. Do not reason about its value on that path |
| `_flow_melody` | `part.cpp:43,441` from the engine id: **off for SAMPLER and BBD** | any host directly; Glow cannot reach it except by changing the engine |
| `_melodic` | `super_modulator.cpp:14`, unconditionally, once | anything else, ever |
| `_active[slot]` | boots **all true** (`part.h:640`); only writer today is `Fireflow.cpp:881` (LANE_PITCH, `!samplerPart`), pushed every block | **the flow layer — `flow.cpp` never calls `set_target_active`** |
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
decides whether a feature exists in play. Measured with the real `generate()`
over 20 000 terrains (not a re-implementation of `draw_span`):

| | |
|---|---|
| mean drawn SHAPE | **0.316** |
| highest SHAPE seen in 20 000 terrains | 0.956 |
| either deck in the §3 fade zone (SHAPE > 0.75) | **4.65 %** |
| either deck above SHAPE 0.95 | **0.01 %** |
| deck A drawn with RANGE < 0.25 | **24.97 %** |
| terrains whose archetype is *drone* | **49.2 %** |

Per archetype, share with a deck in the fade zone: **drone 0.00 %**, the other
three 8.5–10.1 %. The drone can never get there — `taste.h:998-999` caps its
SHAPE span at `{0, 0.25}` — and it is half of all terrains.

**Neither SHAPE nor RANGE is reachable by any macro.** `P_SHAPE_A/B` and
`P_RANGE_A/B` appear in `kBaseRules` and in **no** story curve, so nothing in the
macro layer can move them; only the in-lane offsets of §2 can, and on the melodic
lane those total ±0.40. A drone's 0.25 base plus 0.40 is 0.65, still under the
0.75 where the bank starts crossfading toward the phrase. **On half of all
terrains the melodic phrase is unreachable by any means available to the player.**

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

### And the inversion worth remembering

FORM is not inert; it is unreachable. At SHAPE 1.0, three of the four other
`Principle`s emit a different value set from `TwoMotif`. At SHAPE 0.0, **none of
them do** — all five collapse onto the same 5-value sine staircase, which spans
the *whole* pitch axis and clears 3–4 scale degrees.

So the instrument moves more pitch with the thing that carries no melodic
information than with the phrase, and draws the phrase's corner of the knob in
0.01 % of terrains. Any SHAPE/SMOOTH design has to answer where on the axis the
melody should live and where its RANGE headroom comes from — not whether the
melodic lane emits the phrase, which it already does.
