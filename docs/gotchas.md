# Gotchas — traps the source does not show

Moved out of session memory on 2026-08-19 so that every agent — including
subagents, which never see the session memory — reads the same trap list.
Each entry carries the date it was measured or diagnosed. Line numbers are
avoided on purpose: the named symbol or quoted expression is the anchor (see
`docs/engine-map.md`, "How to read a citation"). The modulation layer's
measured behaviour maps live in [`docs/engine-map.md`](engine-map.md); this
file is the cross-cutting trap list. Values that *sound* wrong but were tuned
by ear are a different list: [`docs/by-ear-decisions.md`](by-ear-decisions.md).

## Build, bench & test rig

- **Byte-identity comparisons require a matching `CMAKE_BUILD_TYPE`.** A
  Debug-vs-Release mismatch once falsely reported all 8 scenarios as "moved" —
  a false alarm that cost a whole session.
- **Float `_phase` drift (~0.0006/cycle)** makes any test that samples a target
  stream across multiple cycles flaky — it jitters right at the loop's hard ±1
  phase transitions. Use drift-immune comparisons: two identically-seeded
  instances (which drift identically), or a snapshot of a frozen buffer, never
  cycle-to-cycle stream comparison.
- **COLOR-0 byte-identity depends on an RNG short-circuit.**
  `SynthEngine::trigger_chord` bails out at `n <= 1` *before* drawing
  `_stab_rng`. Any change to chord density logic that moves where that check
  happens desyncs the stab RNG stream and silently breaks every COLOR-0
  baseline render.
- **Groove-zone determinism is by design, not a bug.** Zone-1 (melody-only,
  `|VARIATION| < 0.25`) STEP lanes still draw the groove-mutation dice every
  wrap even though it always fails there — so the RNG stream differs from any
  earlier release at the same seed, and renders are not byte-comparable across
  versions of the groove-zone code. Don't chase this as a determinism
  regression.
- **Source-text guards can be disarmed by code that merely LOOKS like other
  code.** `res/test_panel.py`'s mutation tests use `cpp.replace(before, after, 1)`,
  which hits the FIRST occurrence. A new function that duplicated
  `roundedEngineState`'s exact rounding expression silently redirected an
  unrelated mutation to the wrong site. When adding code near a pinned
  expression, make it textually distinct (e.g. `std::lround` vs
  `static_cast<int>(std::round(...))`) or extract the shared helper.
- **A new regression-test guard is not proof until it has been red-proven.**
  Two guards in `res/test_panel.py` were vacuous on arrival — they asserted
  against exact strings the generator never emitted (`width="42.000"` vs
  `42.0`, collar `r+1.0` vs `r+0.85`) and would have passed regardless. Force
  a guard to fail once before trusting it.
- **`engine/util/fast_sin.h`'s header comment is wrong**: it claims ~10–15
  cycles per call, but measured cost at the actual call sites is ~50–65
  cycles. Don't trust that docstring for budget arithmetic.
- **`bench/run.py` names result files by the HEAD commit hash.** Measuring
  before committing silently mislabels the result file — always commit code
  first, then run the bench.
- **The QSPI receipt must be rebound AFTER an explicit build, not before.**
  `run.py --no-build --program-qspi --build-only` binds the bank digest to
  whatever `bench.elf` is lying around; if the measuring run then relinks
  anything, the run aborts with "QSPI verification receipt does not match
  current payload (artifacts)". Working order after any engine change:
  `--profile X --build-only`, then `--profile X --no-build --program-qspi
  --build-only`, then the plain run. The other order looks like a corrupt
  bank and is not one.
- **Bench rows can shift ~7 % from icache layout alone** when a new
  translation unit links into the binary (observed on small rows with no
  engine change). Only compare bench rows measured in the same build/run,
  never across commits with unrelated code churn.
- **The `instrument_worst_bbd_dtcm` decision gate crossed 100% of budget from
  adding one bench row, with zero behaviour change.** Measured same-board
  A/B (`--profile system --transport usb`, board `seed`), commit
  `e122e6e` (before Task 10 added a new bench row) vs. `48f8665`
  (after, the commit the run actually measured -- `2cc9267` only recorded
  the capture in docs afterward; the row itself, `inst_edge_synth_bbd`, was
  removed 2026-08-20 along with EDGE): max% went 98.51/98.56 → 101.07/101.11,
  checksum identical (`07fb14fc`) both before and after — confirming pure
  code-layout cost, not an engine regression. This is the icache-layout
  entry above made concrete: the gate itself, not just an arbitrary row, is
  now reading over budget as of `2026-08-20`, and it will read differently
  again the next time anything links into `bench/build/bench.elf` -- never
  linked into the shell firmware. Before capture:
  `docs/bench/2026-08-20-e122e6e-system-axi-o2-usb.md`; after capture:
  `docs/bench/2026-08-20-48f8665-system-axi-o2-usb.md`.

## Modulation layer (`engine/mod/`)

- **The lane phase accumulator has a hard slow-rate cliff no rate observer can
  see.** `ModLane::_phase` is a `float` accumulated per sample
  (`_phase += _phase_inc * (1.f + _ev_rate)`), and `LANE_PITCH` runs that path
  every sample while the texture lanes use `tick()` (96 samples at once, 96×
  the headroom). Below roughly **1.4e-3 Hz at 48 kHz** the increment is under
  half an ulp and the lane **freezes** (simulated: 0.00125 Hz stalls at phase
  0.50). Just above it there is a worse band where every add rounds up to a
  full ulp and the lane runs up to 2× *faster* than commanded. `kRateFreeMin`
  is 0.02 Hz, so the margin is only ~14× and DRIFT's `_ev_rate` eats 20 % of
  it. Any feature that divides the lane rate must budget against this.
  Crucially, `rate_hz_for_test()` reports the *commanded* rate and stays
  perfectly correct while the lane is stopped — gate on **wraps per unit
  time**, never on Hz.
- **Never implement "slower" as "smaller step" in a float32 accumulator.**
  Below a certain size the per-update increment falls under one ulp, the
  addition rounds to a no-op, and the value stops moving *completely* rather
  than moving slowly. Measured in SWARM 2026-08-17/18 (engine withdrawn; the
  arithmetic is not): the lowest settings were 100 % frozen, and in exact
  double arithmetic the observed period was invariant across a 3000× sweep of
  the step constant — the step never controlled the rate at all. The fix that
  worked: give the modulation a real time base by **correlating the draws**
  (hold one target and interpolate across H updates). Gate motion by a
  not-frozen statistic (fraction of non-zero per-tick deltas, or excursion in
  cents) alongside any period measurement, and prove the gate reddens when the
  mechanism is deleted outright. Sample the value the engine actually
  produces, not the target.
- **A `fonepole` + snap-to-target idiom needs a tolerance above the float32
  stall floor**, which is `~8.9e-11 · sample_rate` for a 3 ms coefficient.
  FLUX's gate snap shipped at `1e-6`, *below* the floor at every sample rate,
  so the branch that was supposed to switch itself off never did. The same
  tolerance also swallows the first step *away* from the target, creating a
  dead zone at the shallow end of whatever control drives it — the two come
  as a pair and cannot both be tuned away.
- **`ModLane::set_step()` does not regenerate the groove — the next cycle wrap
  does.** Read `pattern_for_test(...).pattern_groove` right after the call and
  you get the *previous* cell (measured 2026-08-15). Two consequences that
  look like bugs and are not: `pattern_groove.len` equals the **STEPS count**
  once settled (not `pg_target_len()`'s constant 8, which sizes the pitch
  motif), and a probe or test that inspects a groove must run the lane to a
  wrap first or it measures a stale table. The settled firing set is exactly
  `{ slot : rank_of_slot[slot % L] < clamp(round(DENSE*L), 1, L) }`, slot 0
  pinned to rank 0 — see `docs/superpowers/specs/2026-08-15-step-accent-design.md` §2.
- **Which lane controls are reachable depends on the deck's mode AND engine.**
  Before calling a lane-level control broken or fixed, check the lane's flag
  state against [`docs/engine-map.md` §1](engine-map.md) — several controls
  have no path at all in some of the six behaviours. (A 2026-08-09 finding
  that SONG/FORM/VARY are inert in FLOW was itself narrowed on 2026-08-13 when
  the flow-melody engine added `!_flow_melody` to the `_wrap_events` gate; §1
  is the current authority, not old session notes.)

## Engines & parts

- **`apply_range` is unipolar/affine for range ≤ 0.5**: `r·v+r`, so output
  only rises from the base — a high PITCH base pins the top note (keep it low,
  ~0.15–0.3). For MOD-scaled texture/FX targets the same math means a DC lift
  is lost when `range` (the MOD·RANGE product) drops below 0.5 — if a part
  sounds flat, raise the target BASE, don't raise MOD.
- **`Part::set_depth` / `Instrument::set_depth(int,float)` is the lane MOD
  macro** (scales the 4 texture lanes + FX targets) — unrelated to the reverb
  DEPTH knob removed in M4.9. Don't confuse the two in old code or specs.
- **In scenario JSON, PROBABILITY 0 only blocks a lane's own fires — a Part
  still triggers whenever its PITCH lane fires**, regardless of other lanes'
  probability. `probability: 0` silences everything *except* pitch-driven
  triggers (the `m7_bloom` scenario relies on exactly this).
- **`DeLine::Write` decrements**, so a constant read offset behind the write
  head is exactly 1× forward playback at that offset — the mechanism the
  DUST/ROT `TapBank` design relies on, and easy to get backwards when
  reasoning about tape-delay taps.
- **A BBD line has unity steady-state pitch at every stage count.**
  `BbdLine::Process` writes on even ticks and reads on odd ticks through the
  same index at `f_clk`, so content is written and read at the same rate.
  Pitch moves only *while the clock is changing* — charge written at `f₁` and
  read at `f₂` comes out at `f₂/f₁`, for exactly one delay period. Two
  consequences, both got wrong once (a 2026-07-31 spec draft inverted them and
  was reverted): **transposition is only audible with feedback up** (a
  circulating grain tracks `f_now / f_at_entry`; without recirculation you
  hear the first pass, always unity — so the PITCH lane is gated by the
  FEEDBACK lane), and **each repeat is mostly silence** (~75–100 ms of content
  per 250 ms repeat, shrinking with every upward step) — that gappy character
  is the instrument's signature, not an artefact. This is why "RATE bends
  stored pitch, STAGES is a brightness axis" is confirmed by ear.
- **Master DRIVE controls WHEN the master saturates, not how much.** Measured
  2026-08-03 on the master `Limiter` (`engine/fx/limiter.h`): once the gain
  ride engages (`_peak > 1`) it normalises the peak to exactly 1.0, so
  `shape()` always sees the same peak — measured −53 dB of dirt at DRIVE 0,
  −27 dB at 0.3, then only −27 → −24 dB across the remaining 70 % of travel
  while the output gets *quieter*. DRIVE is a **threshold** control. Below the
  ride there is a second regime where distortion climbs with source level —
  heard as "clips, then pulls back and the clipping disappears" (the ride
  engaging). Root cause: `shape()` is one shaper doing two jobs, DRIVE
  character *and* ceiling, exactly linear below the knee. A restructure
  (saturator ahead of the follower) was measured and deliberately **not**
  taken — it is never bit-transparent once DRIVE > 0. Revisit only as a
  voicing decision, not as a bug fix.
- **CHOKE's inhibit is binary at every stage.** `instrument.cpp` sets
  `set_inhibit(window)` and `Part::_note_suppressed` then swallows every note;
  the magnitude is never consulted, only the sign and `amt > 0`. In the free
  mode `window = gate() || flow()` and `Part::flow()` is `!_step_on`, so the
  window never closes at all. A control whose inhibit is binary at every stage
  has no gradient for any generator or macro to sit on. (The flow-layer side
  of this — the terrain draw that muted one deck on 45 of 52 terrains, fixed
  2026-08-13 — went to `docs/attic/` with that layer.)
- **BODY goes silent below FILT −0.5 because its FILTER is a timbre parameter,
  not an attenuating filter.** Open as of 2026-07-28: diagnosed and measured,
  no fix written. FILT's left half fades a voice to silence via `_filt_gain`
  (`engine/synth/synth_engine.cpp`), assuming the cutoff sweep has already
  removed most of the loudness — true for `VoiceT` (real SVF lowpass), false
  for `BodyVoice`, whose `set_cutoff_hz` maps Hz to `_brightness`, a timbre
  parameter. Measured at FILTER lane 0.5 relative to FILT 0: BODY −2.8 dB at
  −0.40, −8.8 at −0.50, silence at −0.60 (SYNTH: −20.5 / −26.5 / silence), so
  BODY's whole loudness change is compressed into 0.2 of knob travel. Also:
  the last 40 % of the knob (−0.6 to −1.0) is dead on *every* engine, and
  BODY's sweep is non-monotonic. Three fix directions were put to Bastian
  (couple output level to `_brightness` — recommended; a per-engine fade
  window; no fade to silence at all — breaks the invariant in
  `synth_engine.h` that full-left is silent), **none chosen**. The
  `kBrightTiltDb` tilt shipped later is a separate, closed decision — see
  [`by-ear-decisions.md`](by-ear-decisions.md).
- **When one control grows a second mechanism, the risk is the shared state,
  not the shared mechanism.** LINK's two halves are mutually exclusive by
  construction, and the plan guarded the accumulator they share. What broke
  twice was the *sibling* state written by the older half's code
  (`_drag_phase`, `_gate_target`) — the transition between two mutually
  exclusive modes is itself a state that neither mode owns. Before planning
  work of this shape: list every field both halves touch and name a single
  owner for each.

- **`SvfLp::SetRes` maps through `r^0.25`, so "a touch of resonance" is not a
  small number.** `_res_damp = 2*(1 - powf(res, 0.25f))`, which means `0.15`
  already sits 62 % of the way to full resonance and lands the damping ratio at
  0.38 — well under the 0.707 where a two-pole stops peaking. FEED picked 0.15
  as a deliberately conservative value for a filter on its output and measured
  **1.58× its own saturation ceiling, +4.0 dB**; at `0` the same sweep reads
  0.973×. Nothing in `svf_lp.h` warns about it, because both existing callers
  (Voice, SamplerEngine) hand it a player-facing RESONANCE knob where the warp
  is the point. A new caller that wants "almost none" wants **0**, and should
  measure rather than trust the number's appearance. Full table:
  [`engine-map.md` §9](engine-map.md).
- **A sampler deck reads DPTH's base HALVED, not at face value.** Every other
  engine reads `LANE_MOTION`'s base exactly as the host writes it;
  `Part::_control_tick` scales it by `sampler_cfg::kMotionBaseScale = 0.5f`
  first (`sampler_config.h`), so DPTH 1.0 on a sampler deck is base 0.5 — the
  same degenerate all-uniform jitter state the base sat at unconditionally
  before DPTH existed (see [`by-ear-decisions.md`](by-ear-decisions.md) for
  why that state is degenerate). Comparing a sampler deck's scatter against
  another engine's DPTH reading by knob position alone is off by 2×.


## Host (VCV)

- **The host still never writes the LANE_LEVEL target base — LANE_MOTION was
  fixed 2026-08-19.** `Fireflow.cpp` calls `set_target_base` for
  `LANE_SOURCE` (always), `LANE_PITCH` (BBD parts only), `LANE_SIZE`, and now
  `LANE_MOTION` too (the DPTH knob, on every engine — spec
  `2026-08-19-voice-knobs-dpth-edge-design.md` §3). LANE_LEVEL alone still
  sits on `Part`'s compiled-in default (`0.8`, the `_base` array in
  `part.h`), so an engine control designed against IT is unreachable while
  playing, for exactly the reason this entry used to apply to LANE_MOTION as
  well — SWARM was built against an unwritten base, and render scenarios do
  not reveal the gap because they write lane bases directly. When a design
  puts an engine parameter on a modulation lane, first check whether the host
  writes that lane's base; either wire a knob to it in the same round or
  design against a lane that is actually driven. Do not accept a render as
  evidence that a control is playable.
- **`PanelCtl::id` is a DIFFERENT enum in each generated table** — a `ParamId`
  in `kParamCtls`, an `InputId` in `kInputCtls`, an `OutputId` in
  `kOutputCtls`, a `LightId` in `kLightCtls`, all starting at 0. Any lookup
  keyed on a bare `int` must be told which table it walks. Shipped a bug
  2026-08-03: a caption lookup applied to all tables collided
  `ParamId[5] = MELODY_A` with `OutputId[5] = GATE_B`, and deck B's gate jack
  drew `VARY`. Live collisions: `REC_A_L`/`REC_B_L` (LightId 2/3) sit on
  `DENSITY_A`/`SMOOTH_A` (ParamId 2/3). `Fireflow.cpp` carries a `dynamic`
  flag on the caption helper and a separate `SamplerOnly<W>` mixin keyed on
  the ENGINE *param* id for exactly this reason — do not "simplify" either
  away.
- **Rack's `Param::setValue` does NOT clamp, and `paramsFromJson` writes
  straight through it.** `Rack-SDK/include/engine/Param.hpp` is a bare
  `this->value = value;` — a `configSwitch`'s declared range constrains the
  *widget*, not the stored value, so a hand-edited patch can put any float on
  a snapped switch. Guard at the reader, never assume a param is in range
  because you configured it that way. (Found via an out-of-enum value on a
  snapped switch that made a lookup match nothing and silently return its
  default forever after.)
- **Merging or remapping controls silently changes the factory sound unless
  the stored init value is CONVERTED.** The init snapshot (`INIT_DEFAULTS`,
  from FM-INIT.vcvm via `gen_panel.py`) is a list of bare numbers keyed by
  name; nothing ties a number to the semantics of the control that reads it.
  This bit **four times** in the 2026-08-09 control-reduction branch alone:
  SONG swallowed FORM (init 0 now meant a different rung), STEPS swallowed the
  STEP pad (the factory patch silently booted *in* step mode — found two tasks
  later; since FM-INIT.vcvm, 2026-08-21, deck B boots stepped **on purpose**,
  `STEPS_B == 8` — do not "repair" that back to 0),
  FLUXRATE became a raw 12-detent index (a stale normalised float
  would have rounded to index 0), and for COUPLE the *plan text itself*
  prescribed the wrong value. Before deleting or remapping any control:
  `git show <pre-branch-sha>:host/vcv/src/init_patch.hpp | grep -i <NAME>`,
  work out what the old value *produced*, then solve for the new value that
  produces the same thing — state the arithmetic and have it checked. Two
  cases have no formula and need a decision: DETUNE (per-deck scales that only
  agree at full deflection) and LVL/COMP (the old compressor make-up gain WAS
  the factory loudness).
