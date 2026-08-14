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

**Scope:** the modulation layer, because that is where the tangle is — how a
lane behaves and what actually reaches its axes. Extend it when another area
burns a review round.

**Sibling authority:** [`flow-fireflow-param-map.md`](flow-fireflow-param-map.md)
owns the *other* question — which of the 64 `ParamId`s a hand-authored `Fireflow`
patch can carry into a `flow` overlay (47 base rules, 45 mapped, 2 unreachable,
pinned by `tests/test_flow_overlay.cpp:42`). It maps **parameters across hosts**;
this file maps **behaviour inside a lane**. Neither answers the other's question,
and a design touching both needs both.

---

## 1. The lane state space

A `ModLane`'s behaviour is a product of three flags, not one mode. Two derived
predicates (`lane.h:195,200`) do the actual gating:

```
_flow_melody_on()   = _melodic && !_step_mode && _flow_melody
_melody_engine_on() = _melodic && (_step_mode || _flow_melody)
```

Eight flag combinations collapse to **five behaviours**. Measured with
`statemap.cpp`: SMOOTH 0 (passthrough, so the raw target is visible), RANGE 1,
VARY 0, rate 0.5 Hz, 8 s, seed 12345.

| `_melodic` | `_step_mode` | `_flow_melody` | p2p @ SHAPE 0 | distinct | behaviour |
|---|---|---|---|---|---|
| false | false | – | 2.000 | 13152 | texture LFO, continuous |
| false | true | – | 2.000 | 5 | texture, STEP follower — staircase |
| true | false | false | 2.000 | 13152 | PITCH **as an LFO** (Sampler, BBD) |
| true | false | true | 0.420 | 10 | FLOW melody phrase |
| true | true | false | 2.000 | 5 | STEP melody |
| true | true | **true** | 2.000 | 5 | **identical to the row above** |

**`_flow_melody` is ignored whenever `_step_mode` is true.** The last two rows are
the same measurement. A design that treats "FLOW melody" as a mode orthogonal to
STEP is describing a state that does not exist.

**`_melodic` is not a choice.** `super_modulator.cpp:14` sets it unconditionally
to `i == LANE_PITCH`. Exactly one lane of five is melodic, on every deck, always.
There is no host call that changes this.

### The consequence that keeps getting missed

Read the p2p column across a *row*, not down it. At SHAPE 0 a melodic STEP lane
emits **2.000** — the sine sampled at 8 step boundaries, which is 5 distinct
values (`0, ±0.707, ±1`), seed-independent, identical on every FORM and every
terrain. It carries no melodic information at all. The real phrase (**0.420**
p2p, 10 distinct) only appears at the S&H end of the bank.

**Ambitus ratio: 2.000 / 0.420 = 4.8×.** Any design that routes phrase values
where a waveform used to be pays that factor. This number has now been rediscovered
three times in three drafts; it belongs here so it is found once.

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

**SHAPE has four writers. SMOOTH has exactly one.** Grep the whole engine for
`_smooth` and you get the setter, the reader, and nothing else. That asymmetry —
not the waveform bank — is why one knob feels unpredictable and the other feels
inert.

The three hidden SHAPE contributors and their ranges:

| Source | Set by | Range | Character |
|---|---|---|---|
| `_shape_offset` | DRIFT, per control tick (`center.cpp:143-144`) | `±0.15 · tap · weather`, taps `{+0.8, −1.0}` | continuous, τ ≈ 45 s |
| `_ev_shape` | EVOLVE random walk (`lane.cpp:693`) | clamped `±0.25` | drifts, decays on settle |
| `_kick_shape` | SPOT (`lane.cpp:403,418`) | `±0.35` per draw, **accumulates** | decays to 0, τ ≈ 1.5 s |

**The fan-in also differs per lane.** `SuperModulator::spot()` skips `LANE_PITCH`
deliberately (`super_modulator.cpp:182` — "the melody is the anchor everything
else stumbles around"), while DRIFT and EVOLVE reach all five. So:

- **PITCH lane:** ±0.40 sustained (DRIFT + EVOLVE).
- **Texture lanes:** ±0.75 peak, of which ±0.35 is a decaying gesture.

Either way, **more than a third of the axis is not under the knob.** With DRIFT
engaged a knob at 1.0 lands anywhere in `[0.60, 1.00]` on the melodic lane and
`[0.25, 1.00]` on a texture lane just after a SPOT; a terrain base rule below 1.0
widens the band further. A spec that says "at SHAPE 1 the behaviour is X" is
describing an axis value the player may never reach — and §3 below shows the one
place where reaching it matters most.

---

## 3. The top of the SHAPE knob is a fade-out, not a waveform

Measured with `deadzone.cpp` — texture lane, FLOW, 30 s, seed 999:

| SHAPE | VARY | p2p | distinct values |
|---|---|---|---|
| 0.70 | 0 | 1.600 | 796 |
| 0.80 | 0 | 1.600 | **2** |
| 0.90 | 0 | 0.800 | 2 |
| 0.95 | 0 | 0.400 | 2 |
| **1.00** | **0** | **0.000** | **1** |
| 1.00 | +0.5 | 0.906 | 5 |
| 1.00 | −0.5 | 0.450 | 4 |

Above SHAPE 0.75 the bank crossfades pulse → S&H (`waveforms.h`). But on a
**non-melodic lane in FLOW**, `_sh_slot()` returns 0 permanently: it reads
`_cur_step`, and nothing writes `_cur_step` outside STEP and the FLOW-melody path
(`lane.cpp:564`). So "S&H" is a frozen constant, and the crossfade toward it is an
**amplitude fade to DC**. At VARY 0 the top fifth of the knob is a two-value
square whose depth falls linearly to silence.

VARY is what makes that corner move at all — mutation is the only thing that
changes the held value. This is the mechanism behind the "SHAPE is unpredictable"
report: for a fifth of its travel the knob does not change the shape, it turns the
modulation off, and whether it does depends on a *different* knob.

---

## 4. Write-side index

Behaviour that depends on a variable is only as knowable as that variable's
**writers**. Reading the reader tells you nothing. The non-obvious ones:

| Variable | Written by | Never written by |
|---|---|---|
| `_cur_step` | STEP path and the FLOW-melody path only | the FLOW LFO path — it stays −1 → `_sh_slot()` = 0 |
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

---

## 5. Floating point: what is not an identity

- `lerpf(a, b, t)` is `a + (b − a)·t`. At `t == 1` this is **not** `b`. Measured:
  over 400 000 points, 41 669 mismatches (10.4 %), max error 5.96e−08 (1 ULP).
  The mismatch *rate* depends entirely on how you sample and is not a stable
  property — only "not exact, ≤ 1 ULP" is.
- Consequence: `shape_value(ph, 1.0f, hold)` does **not** return `hold` exactly,
  despite the comment at `lane.cpp:546-548` and the claim in
  `2026-08-13-flow-melody-engine-design.md:328-330`. Both are wrong.
  `test_waveforms.cpp:14-15` passes only because it uses `.epsilon(0.01)`.
- Any gate written as bit-identity across this call **cannot go RED reliably**.
  State it as equality within 1 ULP.

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

```cpp
#include "mod/lane.h"
#include <cstdio>
int main(){
    spky::ModLane l;
    l.init(48000.f, /*seed*/ 12345);
    l.set_melodic(false); l.set_step(false, 8);
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
