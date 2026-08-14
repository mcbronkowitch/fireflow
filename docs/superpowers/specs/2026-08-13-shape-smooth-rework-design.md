# SMOOTH becomes interval-relative — and what SHAPE turned out to be

**Status:** design, sixth revision. **Scope reduced.** Drafts 1–5 tried to repair
SHAPE, SMOOTH and the melody's reachability in one document and were rejected
five times. Two independent reviews of draft 5 recommended the same split; this
file is now **one repair** — SMOOTH — plus §5, which hands the measured SHAPE
findings to the round that should own them.

- **The melody** moved to [`2026-08-14-melody-reachable-design.md`](2026-08-14-melody-reachable-design.md).
- **SHAPE** is deferred to the Marbles/VARY round, for the reason in §5.3.

Every number states its probe setup.

---

## 1. The problem: SMOOTH fails at both ends, oppositely

The slew law is **absolute seconds** — `t = 0.00002 · 25000^smooth`
(`lane.cpp:360`), i.e. 20 µs … 0.5 s — while lane rates span four decades.
Measured over 20 000 real terrains, composing
`free_hz(P_RATE) × pace_mult × tide_free × kLaneRatio[i]` for the four texture
lanes **of the 48 % of terrains that are in FLOW**:

```
texture-lane cycle (FLOW terrains): min 0.030 s   max 168 s
  < 4 s: 14.2 %      4..100 s: majority      > 100 s: 0.7 %
```

*(An earlier draft quoted 0.025–168 s with 43.9 % under 4 s. That pooled FLOW and
STEP terrains; in STEP, `super_modulator.cpp:28-29,41-42` uses `division_hz` and
hands texture lanes `_master_hz` with neither `kLaneRatio` nor TIDE, so the
distribution is a different one. The FLOW figures above are the ones this law
governs. `_mod_scale` — clamped to ×32 at `center.cpp:29` — is a further factor
neither figure includes.)*

Both ends are broken, in opposite directions:

- **Slow end.** Against a 100 s cycle even the top of the knob is 0.5 %
  smoothing — inaudible. This is the reported *"in FLOW I'm always at SMOOTH max;
  the middle doesn't exist."*
- **Fast end.** At a 0.03 s cycle the top of the knob is **~17× the cycle**. It
  does not do nothing; it annihilates the lane.

`_smooth` has exactly one writer and one reader (`lane.cpp:349` → `:360`) — no
hidden contributors, unlike SHAPE. The law itself is the whole fault, which is
why one change fixes both ends.

---

## 2. The design

```
τ = frac(smooth) · interval
frac(s) = s · TOP                    (linear; the taper comes from the one-pole)
interval:  FLOW LFO      -> the lane cycle
           FLOW melody   -> one SLOT, floored at _note_min_samples
           STEP          -> one step
```

`τ` is the one-pole time constant (`onepole.h:16`, `k = 1/(τ·sr)`; set at
`lane.cpp:386` and its tick twin `:395-397` — the file's own comment calls those
"a matched pair, not independent code"). For a cycle `T` the attenuation is
`1/√(1+(2πτ/T)²)`. **Measured on a patched build** at SHAPE 0.0, 0.5 Hz, seed 999,
against the analytic prediction:

| frac | 0.05 | 0.1 | 0.2 | **0.35** | 0.5 | 0.8 | 1.0 |
|---|---|---|---|---|---|---|---|
| predicted dB | −0.4 | −1.5 | −4.1 | **−7.6** | −10.4 | −14.2 | −16.1 |
| **measured dB** | **−0.40** | **−1.44** | **−4.10** | **−7.58** | **−10.12** | **−13.52** | **−15.13** |
| phase | 17° | 32° | 52° | 65° | 72° | 79° | 81° |

A usable, monotonic axis with a real middle — **for a texture LFO**. Not for a
melody: a note must arrive inside its own slot, which is exactly what
`kFlowSlewFrac = 0.35` encodes, set by arithmetic and **confirmed by ear**
(`lane.h:269,275`). So the law needs **two tops**:

- **`TOP_TEXTURE = 1.0`** for the four texture lanes.
- **`TOP_MELODY = 0.35`**, reusing the ear-confirmed constant rather than
  re-deriving it.

Draft 4 deferred this to "an ear question". It is not one — no single value serves
both cases. What *is* deferred honestly: whether `TOP_TEXTURE = 1.0` is too much
once heard.

**`interval` is pinned to one SLOT in FLOW melody mode, not the phrase.** Measured
at the rate `test_flow_melody.cpp:341-344`'s floor case runs (14 Hz):
`_note_min_samples = 2880`, cap `= 0.35 × 2880 = 1008`. Under the *phrase* reading
τ = `0.3474 × 3428.6 = 1191` and the `kFlowSlewFrac` clamp **binds**; under the
*slot* reading τ = 149 and it does not. Draft 4 claimed the clamp becomes
"unconditionally dead code" — false under its own definition of `interval`. With
the slot reading and `TOP_MELODY = 0.35` the clamp becomes redundant and may be
folded in; that is a simplification, not a proof of unreachability.

---

## 3. Gates

| # | Gate | Red when |
|---|---|---|
| G1 | SMOOTH 0.25 gives the same τ/cycle ratio at 0.02 / 0.5 / 5 / 30 Hz within 2 % | the law is still absolute. *Measured on a patched build: p2p 0.487 / 0.488 / 0.488 / 0.487 — rate-invariant.* |
| G2 | The melody note floor still holds at 14 Hz (`test_flow_melody.cpp`'s existing case) | `_note_min_samples` was dropped |
| G3 | **The calm-corner gate stays green** (`tests/test_flow_audio.cpp:273,418`) | the drone conversion below went wrong in the loud direction |
| G4 | **A drone terrain's texture lanes still move**: median p2p over a seed set stays within 3 dB of today's | the drone conversion went wrong in the **quiet** direction — which G3 cannot see, because an RMS gate fires only when things get louder |

---

## 4. Blast radius

- `engine/mod/lane.cpp:360` (the law), `:381-383` (the `kFlowSlewFrac` clamp),
  `:386` and `:395-397` (the matched pair — change both or they diverge).
- `engine/mod/lane.h:275` — `kFlowSlewFrac`.
- **`engine/flow/taste.h:1000-1001` — `P_SMOOTH_A/B` drone `{.5, .9}`, "drone =
  glassy". This is the single largest behavioural consequence in the spec.** Today
  that is τ = 3–181 ms against a 4–100 s cycle: cosmetic. Under the new law it is
  0.5–0.9 **× the cycle**, and the drone's texture lanes very nearly stop. Drones
  are 49.2 % of terrains. **Decide the conversion before any listening pass**, and
  gate it with G4, not G3.
- **Three** mirrors of the stored SMOOTH defaults, all of which must move together
  (`fireflow-control-merge-init-trap`): `host/vcv/src/init_patch.hpp:10,30`
  (`SMOOTH_A = 0.836144507f`, `SMOOTH_B = 1.0f`), `host/vcv/res/gen_panel.py:596,641`,
  and `host/vcv/res/test_panel.py:2263,2287` — the third is the panel guard itself
  (`CMakeLists.txt:300-304`), so missing it fails the build rather than the ear.
- `host/vcv/src/flow_patch_bridge.hpp:77,98,380` and
  `docs/flow-fireflow-param-map.md:176` — SMOOTH is a transferable base rule; its
  meaning changing is a converter concern.
- Tests: `test_flow_melody.cpp:539-561` and `:564` (the latter goes RED **by
  design** — STEP's slew changes from absolute 0.5 s to `frac × _steps`. That is a
  behaviour change to re-baseline, not a gate that cannot fail; draft 4 proposed
  retiring it for the wrong reason), `tests/test_lane.cpp:35-54`.
- Render hashes: `wave_formant_sweep` moves. `ctrl_identity.json` does **not** —
  verified by parsing: `set_smooth` twice with **0.0**, which is passthrough under
  both laws. (It *does* move under the melody spec, for an unrelated reason.)
- `shell/` and `bench/` recompile; `bench/workloads_mod.cpp:25,27` and
  `bench/audition/init_patch.cpp:55,57` set SMOOTH.

---

## 5. Handover: what SHAPE turned out to be

Five drafts of SHAPE repairs were measured and none survived review. The findings
are solid and should not be re-measured; the repairs were not. **SHAPE belongs to
the Marbles/VARY round** (`roadmap.md:2534`, marked ⬜ unscheduled, "Ordering is
open"), for the reason in §5.3.

### 5.1 The top quarter is an amplitude fade onto a fixed offset

Texture lane, FLOW, rate 0.5 Hz, 30 s, seed 999, VARY 0, SMOOTH 0:

| SHAPE | 0.70 | 0.80 | 0.90 | 0.95 | 1.00 |
|---|---|---|---|---|---|
| p2p | 1.600 | 1.600 | 0.800 | 0.400 | **0.000** |
| distinct | 797 | 2 | 2 | 2 | **1** |

Law `p2p = 2·(1 − 4·(sh − 0.75))`, analytic from `waveforms.h:32`. Cause:
`_sh_slot()`'s early return at `lane.cpp:564`. It parks on the held value,
**−0.528 … +0.274** over twelve seeds — a permanent per-seed offset, not silence.
*(Pinned by `tests/test_engine_map.cpp`.)*

### 5.2 The obvious repair does not survive the real code path

Making the S&H slot advance 32 steps per cycle works on `process()` — p2p 1.085 at
every rate from 0.02 Hz to 120 Hz. **But texture lanes in FLOW run `tick()`**
(`super_modulator.cpp:167-168`), which advances `kTickInterval = 96` samples per
call, i.e. samples the staircase at **500 Hz**. A 32-step staircase at lane rate
*f* steps at 32*f*, so one step per tick lands at **15.6 Hz** — inside
`kRateFreeMax = 30`. Measured on `tick()`, seed 999, 40 cycles:

| rate Hz | 0.5 | 15 | **20** | **25** | 30 | **500** |
|---|---|---|---|---|---|---|
| distinct | 32 | 32 | **25** | **22** | 32 | **1** |

Non-monotonic, with rational-ratio locks, and a null where the repair is
bit-identical to the frozen constant it replaces. **Any future SHAPE design must
be measured on `tick()`, not `process()`.** Three drafts published a number
measured on the wrong path; this is the trap.

### 5.3 The knob's real problem is `_ev_shape`, and it is VARY's

`lane.cpp:554` sums four sources into the played SHAPE. Their true sizes,
measured — not their nominal bounds:

- **DRIFT's tap is ±0.03, not ±0.15.** `_weather = tanh(_ou)` with `kOuTau = 45`,
  `kOuSigma = 0.10` (`center.cpp:8-9,321`). Simulated at DRIFT 1 over 10 minutes:
  **sd 0.024–0.030, |max| 0.076** against the nominal 0.120. On a bank whose
  quarters are 0.25 wide, inaudible.
- **`_ev_shape` has no mean reversion.** `lane.cpp:693` is a clamped random walk;
  all eight sites checked, decay happens only in the RENEW arm (`:702`) and under
  SETTLE (`:741,:825`). Under GROW it walks one way, saturates at ±0.25 and
  **stays there** — a permanent, silent relabelling of the knob. Its speed is
  per-wrap, so it is rate-dependent too.

So the reported *"turning SHAPE is completely unpredictable"* is `_ev_shape`, and
`_ev_shape` is VARY's only reach into this axis. **Draft 5 proposed deleting
DRIFT's tap and tightening `_ev_shape` to ±0.10** — removing the inaudible,
zero-mean, coherently-fanned term while shrinking the audible, one-way,
saturating one without repairing it. The sign was inverted.

**The repair to carry into the Marbles round:** give `_ev_shape` an
Ornstein–Uhlenbeck form — mean-reverting toward 0 — instead of a clamped walk. The
discretisation to copy is already at `center.cpp:321`. The knob then becomes the
**reference the modulation returns to**, which answers "unpredictable" by
construction rather than by subtraction, and the band can stay wide, which is what
"several modulations at once, non-linearly but coherently" needs. It is a few
lines at one site. That is why SHAPE and Marbles are one question, not two.
