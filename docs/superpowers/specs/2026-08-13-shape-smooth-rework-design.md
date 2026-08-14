# SMOOTH becomes interval-relative — and what SHAPE turned out to be

**Status:** design, seventh revision. Drafts 1–5 tried to repair SHAPE, SMOOTH
and the melody's reachability in one document and were rejected five times. Two
independent reviews of draft 5 recommended the same split; revision 6 reduced
this file to **one repair — SMOOTH** — plus §5, which hands the measured SHAPE
findings to the round that should own them.

**Revision 7 (2026-08-14) is a repair of the spec, not of the design.** Revision
6 was written the day before the flow/Glow removal landed and cites four files
that no longer exist; two of its four gates ran in a test file that was deleted.
The law in §2 is unchanged. What changed: §1's evidence is re-measured on the
instrument instead of simulated over a deleted generator's terrains, §3's gates
are rebuilt out of what survives, §4's blast radius is corrected, and two
consequences revision 6 never named — STEP's per-lane spread and the init
patch's stored defaults — are now decisions with owners.

- **The melody** moved to [`2026-08-14-melody-reachable-design.md`](2026-08-14-melody-reachable-design.md)
  and shipped (`07d5b9d`).
- **SHAPE** is deferred to the Marbles/VARY round, for the reason in §5.3.

Every number states its probe setup.

---

## 1. The problem: the law is absolute seconds, so the knob's reach is whatever the rate makes it

The slew law is `t = 0.00002 · 25000^smooth` (`lane.cpp:360`), i.e. 20 µs … 0.5 s
of *wall clock*, while lane cycles span four decades. The knob therefore does not
have a behaviour; it has a different behaviour per patch.

### 1.1 At every setpoint that survives in the repo, it is inert

Revision 6 argued this from a distribution over 20 000 generated terrains. That
generator was deleted on 2026-08-14, so the claim is re-measured here against the
setpoints that actually remain: the four frozen operating points
(`tests/param_impact_points.h`) and the shipped VCV init patch
(`host/vcv/src/init_patch.hpp`).

*Probe: `Instrument` built through `apply_param()` exactly as
`test_param_impact.cpp::apply_patch` does, 48 kHz, 2 s of `process()` so Center's
smoothers settle, lane rates read from `Instrument::lane_rate_hz_for_test`.
Attenuation of the lane's own cycle from `1/√(1+(2πτ/T)²)`.*

| setpoint | mode | SMOOTH A / B | largest τ/T over all 10 lanes | attenuation |
|---|---|---|---|---|
| master 1 | FLOW | 0.321 / 0.375 | 0.0009 × T | −0.00 dB |
| master 2 | FLOW | 0.764 / 0.755 | 0.0035 × T | −0.00 dB |
| master 3 | STEP | 0.327 / 0.276 | 0.0016 × T | −0.00 dB |
| master 8 | STEP | 0.714 / 0.701 | 0.0107 × T | −0.02 dB |
| **VCV init patch** | FLOW | **0.836 / 1.000** | **0.0260 × T** | **−0.11 dB** |

**Across 50 readings the knob's largest effect anywhere is 0.11 dB.** The reported
*"in FLOW I'm always at SMOOTH max; the middle doesn't exist"* is not a taste
report — at these rates the knob is doing nothing at either end, and the stored
factory defaults sit at 0.836 and 1.000 **because** it is doing nothing.

### 1.2 At a mid-band rate it is not inert — it is all in the top quarter

The same law at a faster patch. *Probe: `host/render` at
`scratchpad/smooth_axis.json` — FLOW, both decks SYNTH, four texture lanes
active, master ≈ 0.5 Hz, SMOOTH stepped 0 → 0.25 → 0.5 → 0.75 → 1.0 every 10 s;
mean peak-to-peak of `a_src`/`a_size`/`a_motion`/`a_level` per segment.*

| SMOOTH | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| **today** | 1.599 | 1.599 | 1.544 | 1.215 | 0.323 |
| new, TOP 1.0 | 1.599 | 0.579 | 0.328 | 0.226 | 0.169 |
| new, TOP 0.5 | 1.599 | 0.894 | 0.578 | 0.417 | 0.322 |

Today the first **half** of the travel is bit-flat and four fifths of the effect
arrives in the last quarter. That is the same defect as §1.1, seen from the other
side: the law is in seconds, so at 0.5 Hz the useful part of the knob sits at the
top and at 0.03 Hz it has fallen off the end entirely. **One change fixes both**,
because both are the missing `× interval`.

`_smooth` has exactly one writer and one reader (`lane.cpp:349` → `:360`) — no
hidden contributors, unlike SHAPE.

---

## 2. The design

```
τ = smooth · TOP · interval
interval:  FLOW LFO      -> the lane cycle
           FLOW melody   -> one SLOT, floored at _note_min_samples
           STEP          -> one STEP of that lane
TOP:       TOP_TEXTURE   -> the four texture lanes   (see 2.2)
           TOP_MELODY    -> LANE_PITCH, = kFlowSlewFrac = 0.35
```

`τ` is the one-pole time constant (`onepole.h:16`, `k = 1/(τ·sr)`; set at
`lane.cpp:386` and its tick twin `:395-397` — the file's own comment calls those
"a matched pair, not independent code"). Linear in `smooth`; the taper comes from
the one-pole itself. **Measured on a patched build** at SHAPE 0.0, 0.5 Hz, seed
999, against the analytic prediction:

| frac | 0.05 | 0.1 | 0.2 | **0.35** | 0.5 | 0.8 | 1.0 |
|---|---|---|---|---|---|---|---|
| predicted dB | −0.4 | −1.5 | −4.1 | **−7.6** | −10.4 | −14.2 | −16.1 |
| **measured dB** | **−0.40** | **−1.44** | **−4.10** | **−7.58** | **−10.12** | **−13.52** | **−15.13** |
| phase | 17° | 32° | 52° | 65° | 72° | 79° | 81° |

### 2.1 Two tops, because no single value serves both cases

A note must arrive inside its own slot, which is what `kFlowSlewFrac = 0.35`
already encodes — set by arithmetic and **confirmed by ear** (`lane.h:269,275`).
`TOP_MELODY` reuses that constant rather than re-deriving it. Draft 4 deferred
this as "an ear question"; it is not one.

**`interval` is pinned to one SLOT in FLOW melody mode, not the phrase.**
Measured at the rate `test_flow_melody.cpp:341-344`'s floor case runs (14 Hz):
`_note_min_samples = 2880`, cap `= 0.35 × 2880 = 1008`. Under the *phrase*
reading τ = `0.3474 × 3428.6 = 1191` and the `kFlowSlewFrac` clamp **binds**;
under the *slot* reading τ = 149 and it does not. Draft 4 claimed the clamp
becomes "unconditionally dead code" — false under its own definition of
`interval`. With the slot reading and `TOP_MELODY = 0.35` the clamp becomes
redundant and may be folded in; that is a simplification, not a proof of
unreachability.

**Consequence, measured, and it is the reassuring one:** on the melodic lane this
law is nearly a no-op. Across the five setpoints of §1.1 the PITCH lane moves from
−0.00 dB today to between **−0.03 and −0.24 dB** — because the interval is one
slot of eight and the top is 0.35. The rework's audible content is the four
texture lanes.

### 2.2 `TOP_TEXTURE = 0.5`, decided by ear 2026-08-14

At `TOP_TEXTURE = 1.0` the right stop is τ = one full cycle: −16.1 dB, and §1.2
measures the lane at 0.169 of its raw travel. At 0.5 it is −10.4 dB and 0.322.

The owner asked for this to be measured before it was written down. Three renders
of `scratchpad/smooth_axis.json` — the current law, TOP 1.0 and TOP 0.5 — were
produced on patched builds and auditioned; **0.5 was chosen**.

What the measurement says, and it agrees: **`TOP_TEXTURE = 0.5` puts the right
stop within 0.3 % of where today's right stop already is** (0.322 vs 0.323 in
§1.2's table) while making every position below it monotonic and useful. So the
change is confined to the middle of the axis — which is where the complaint was.
1.0 buys 6 dB more ceiling at a stop nobody asked for.

This is a by-ear constant in the sense of `fireflow-by-ear-decisions`: revisable
by listening, not by argument, and **not** a value a later session should "fix"
toward 1.0 for symmetry with `interval`. The law does not depend on it.

### 2.3 In STEP the knob's strength varies per lane, and that is intended

In STEP every lane reports the deck's master rate and carries its own slot count
(`kLaneRatio` reappears as slots, `_apply_steps`). One step is therefore a
different length per lane, so one knob position is a different τ per lane.
Measured at master 8, deck A, SMOOTH 0.714:

| lane | SOURCE | LEVEL | MOTION | SIZE | PITCH |
|---|---|---|---|---|---|
| slots | 2 | 3 | 6 | 8 | 4 |
| attenuation | −7.80 dB | −5.10 dB | −1.93 dB | −1.19 dB | −0.62 dB |

**Decided: keep it.** The rule this law exists to enforce is "a value arrives
inside its own interval"; a two-slot lane has eight times the interval of a
sixteen-slot one and therefore needs eight times the glide to say the same thing.
Normalising to the deck's step count instead would restore a uniform knob and
reintroduce the original defect in miniature — fast lanes smoothed past their own
slot. Revision 6 did not mention this at all; it is named here so a later reader
does not file it as a bug.

Note the asymmetry this creates against FLOW, where the four texture lanes share
one cycle and the knob is uniform across them. That is a property of the two
worlds, not of the law.

### 2.4 The stored defaults are converted to preserve the factory sound

`SMOOTH_A = 0.836144507`, `SMOOTH_B = 1.0`. Under the new law those mean −14.6 dB
and −16.1 dB on the texture lanes (§1.1's probe), i.e. the factory patch would go
nearly static. **Decided by the owner: convert the stored values so the shipped
sound is preserved**, accepting that the knob then boots near its left stop —
which is honest, because that is where it effectively sits today.

The conversion is a formula, not a number, because it depends on §2.2:

```
smooth_new = (τ_today / T_ref) / TOP_TEXTURE
τ_today    = 0.00002 · 25000^smooth_old
T_ref      = the cycle of the deck's FASTEST texture lane (LANE_SOURCE)
```

Measured `T_ref` at the init patch: deck A 38.240 s, deck B 38.423 s; `τ_today`
0.0951 s and 0.5 s. At `TOP_TEXTURE = 0.5` that gives **≈ 0.0050 and ≈ 0.0260**.

**One value cannot preserve all four texture lanes**, because they have different
cycles and the old law had no cycle in it — that is the defect, stated as
arithmetic. Anchoring on the fastest lane preserves the lane the knob reaches
furthest today and leaves the three slower ones slightly smoother than they are
now. The residual is bounded by §1.1's table: on the init patch it is at most
0.026 × T, so no lane can be pushed past −0.11 dB of where it sits today.

---

## 3. Gates

Revision 6's G3 and G4 both lived in `tests/test_flow_audio.cpp`, deleted
2026-08-14 with the flow layer. G4 in particular was the *quiet-direction* gate —
the one an RMS ceiling cannot see. Its job is not optional here, because §2.4's
whole risk is the quiet direction, so it is rebuilt on what survives.

| # | Gate | Red when | Lives in |
|---|---|---|---|
| G1 | SMOOTH 0.25 gives the same τ/cycle ratio at 0.02 / 0.5 / 5 / 30 Hz within 2 %. *Measured on a patched build: p2p 0.487 / 0.488 / 0.488 / 0.487 — rate-invariant.* | the law is still absolute | `tests/test_lane.cpp` |
| G2 | The melody note floor still holds at 14 Hz | `_note_min_samples` was dropped | `test_flow_melody.cpp` (existing case) |
| G3′ | `deck_audible` still passes at all four frozen points, both decks, both modes | the conversion or the law left a deck near-silent | `test_param_impact.cpp` (**exists today**, `load_points()`) |
| G4′ | **At the INIT PATCH, every texture lane's p2p stays within 3 dB of today's**, per lane, over ≥ 8 cycles | §2.4's conversion failed — the quiet direction. G3′ cannot see this: a deck stays audible while its modulation dies | **new**, `tests/test_smooth_law.cpp` |
| G4″ | **At the four frozen points, every texture lane still moves** — p2p > 0.05 absolute | a lane stopped entirely | **new**, same file |
| G5 | **No NaN or non-finite sample over a full render at all five setpoints** | — | **new**, same file |

**G4′ and G4″ are deliberately different gates, and the reason matters.** A first
draft of this section applied one ±3 dB band to all five setpoints. That gate
cannot pass and would have been discovered only in implementation: the four
frozen points carry SMOOTH 0.276–0.764, drawn from the deleted terrain generator
against the *old* law, and §1.1 measures them landing between −5.3 and −13.8 dB
under the new one. That is the rework working, not failing. Only the init patch
is converted (§2.4), so only the init patch owes sound preservation; the frozen
points owe nothing but continued life, which is what G4″ asserts. The 0.05 floor
is set against §1.1's worst case (master 2, −13.81 dB, ≈ 20 % of raw travel) with
room to spare.

G4′ is stated per lane rather than as a median so a single collapsed lane cannot
hide behind four healthy ones.

**G5 is not this rework's business and is included deliberately.** The removal
took `test_flow_audio.cpp`'s whole-instrument NaN sweep with it and nothing
replaced it (`roadmap.md`, "Coverage lost and not replaced"). This spec is the
first work since to render every surviving setpoint anyway, so the assertion is
nearly free here and expensive to justify as a round of its own. It is scoped to
the five setpoints — it does not restore what was lost, which ran over filtered
seed populations that no longer exist.

**Vacuity check** (per `fireflow-vacuous-test-gates`): G1, G4′, G4″ and G5 each
need a proven RED. G1 goes red on today's `main` by construction. G4′ must be
shown red by deliberately mis-converting one stored default. G4″ must be shown
red by forcing `TOP_TEXTURE` far past its range (e.g. 8.0), which flattens the
lanes. G5's red is a lane forced to emit a NaN; if that cannot be arranged the
gate is vacuous and gets deleted rather than kept as decoration.

---

## 4. Blast radius

Corrected against the tree at `3783723`. **Four entries in revision 6 named files
that no longer exist** and are struck here rather than silently dropped, because a
reader coming from the roadmap will look for them.

**Live:**

- `engine/mod/lane.cpp:358-398` — `_update_slew`: the law (`:360`), the
  `kFlowSlewFrac` clamp (`:381-383`), and `:386` + `:395-397`, the matched
  per-sample/tick pair. **Change both halves or they diverge**; the file says so
  itself.
- `engine/mod/lane.h:279,287,293` — `kFlowPhraseSlots`, `kFlowNoteMinS`,
  `kFlowSlewFrac`; `TOP_TEXTURE` is new and belongs beside them.
- **`_fixed_slew` (`lane.cpp:353`, `lane.h:267`) — revision 6 never mentioned it.**
  It is a third path through the same function, pinning τ at a fixed 0.02 s. It is
  reachable only from the render host's `set_fixed_slew` scenario action
  (`host/render/scenario.cpp:131`); **no scenario in the repo uses it**, no VCV
  param drives it, and it is on no hardware panel. **Decision: leave it exactly as
  it is** — it is an absolute-seconds escape hatch, which is a coherent thing for
  a test fixture to want, and converting it would change a control nothing reaches
  while adding a second law to reason about. It is documented here so the next
  reader does not discover it as a surprise.
- **Three mirrors of the stored SMOOTH defaults, all of which must move together**
  (`fireflow-control-merge-init-trap`): `host/vcv/src/init_patch.hpp:10,30`,
  `host/vcv/res/gen_panel.py:598,643`, and `host/vcv/res/test_panel.py:2263,2287`.
  The third is the panel guard itself (`CMakeLists.txt:300-304`), so missing it
  **fails the build**, not the ear. `init_patch.hpp` is generated — edit
  `gen_panel.py` and regenerate; never hand-edit.
- Tests that change behaviour and must be re-baselined, not deleted:
  - `tests/test_lane.cpp:35-54` — "SMOOTH turns a step into a glide". **Revision 6
    listed this file without saying it reddens.** It runs STEP, 2 steps, 1 Hz, so
    a step is 0.125 s; at SMOOTH 0.5 today τ ≈ 3 ms and the case asserts the lane
    settles well inside a step. Under the new law τ = 0.5 × step = 62.5 ms and it
    does not settle. The case is still meaningful — it just needs its SMOOTH or its
    window restated.
  - `tests/test_flow_melody.cpp:564` — "STEP's slew is unchanged by the melody
    clamp". Goes RED **by design**: STEP's slew stops being absolute 0.5 s. Note
    the margin is thin — the case's 0.02 bound sits against a measured ~0.006
    today and roughly 0.024 under the new law — so re-baseline the number
    deliberately rather than by nudging until green.
- Render hashes: **`wave_formant_sweep` moves** (`set_smooth` 0.65).
  **`ctrl_identity` does not** — verified by parsing: `set_smooth` twice with
  **0.0**, which is passthrough under both laws.
- `shell/` and `bench/` recompile. `bench/workloads_mod.cpp:25` sets SMOOTH 0.5 on
  a 2 Hz FLOW lane — a **CPU** row, and the new law changes no per-sample work, so
  no bench re-measurement is owed. `bench/audition/init_patch.cpp:57` pushes the
  VCV init patch and inherits §2.4's conversion automatically.

**Struck — the file is gone:**

- ~~`engine/flow/taste.h:1000-1001` — the drone `{.5, .9}` conversion, "the single
  largest behavioural consequence in the spec"~~. Deleted 2026-08-14 with the
  terrain generator. Nothing sets SMOOTH per-terrain any more, and 49.2 % of
  terrains is not a live number because there are no terrains. **The question did
  not disappear, it moved**: the same "a high stored SMOOTH becomes a real
  attenuation" problem is now the init patch's, and it is §2.4. The by-ear
  rationale is in `docs/attic/taste-by-ear-notes.md` §1.3.
- ~~`tests/test_flow_audio.cpp:273,418` — G3 and G4~~. Deleted; replaced by G3′,
  G4′ and G5 in §3.
- ~~`host/vcv/src/flow_patch_bridge.hpp:77,98,380` — SMOOTH as a transferable base
  rule, a converter concern~~. Deleted; there is no converter and no second
  control surface to transfer to.
- ~~`docs/flow-fireflow-param-map.md:176`~~. Deleted with the layer it mapped.

**Also owed:** `docs/engine-map.md` §1 records a 0.2995 STEP/FLOW gap at SMOOTH
1.0, 2 Hz, produced by the `_flow_melody_on()` guard on the slew clamp. The law
above changes what that measurement means, so the map entry is re-measured in the
same round rather than left describing an engine that no longer exists.

---

## 5. Handover: what SHAPE turned out to be

Five drafts of SHAPE repairs were measured and none survived review. The findings
are solid and should not be re-measured; the repairs were not. **SHAPE belongs to
the Marbles/VARY round** (`roadmap.md`, "Marbles round — VARY as the character
axis", ⬜ unscheduled), for the reason in §5.3.

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

---

## 6. What this spec deliberately does not deliver

- **SHAPE.** §5, and it is the Marbles round's.
- **The `_flow_melody_on()` guard on SMOOTH's clamp** survives as a mode split on
  the melody path (`lane.cpp:361`). Once `TOP_MELODY` is in place the clamp is
  redundant (§2.1) and folding it in is a simplification the plan may take — but
  removing the *guard* is a separate question about mode symmetry, and
  `docs/engine-map.md` §7 owns it.
- **`_fixed_slew`** stays absolute, by decision (§4).
- **Any re-tuning of `TOP_MELODY`.** 0.35 is ear-confirmed and is reused, not
  re-derived.
