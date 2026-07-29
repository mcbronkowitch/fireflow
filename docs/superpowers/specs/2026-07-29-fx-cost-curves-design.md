# CPU cost curves: finding the settings that cost disproportionately

Design spec, 2026-07-29.

## 1. Why

The 2026-07-29 hardware run (`docs/bench/2026-07-29-1f7671d-system.{md,csv}`,
two accepted runs at `1f7671d`) measured the instrument over its block budget:

| workload | avg % | max % | anchored max % |
|---|---:|---:|---:|
| `instrument_worst` | 117.0 | 120.6 | **120.9** |
| `instrument_worst_bbd` | 128.6 | 133.2 | — |

The bench writes its own verdict: *"the 2x4 architecture does not fit."*

Hoisting the component rows onto the instrument — FX counted twice, once per
deck, and each FX row's own shell (`fx_none`, 2.56) subtracted before doubling
— accounts for it like this:

| item | points of the block |
|---|---:|
| FLUX, both decks | **34.4** |
| SYNTH, 8 voices | 35.8 |
| GRIT, both decks | 10.3 |
| reverb | 9.5 |
| modulation plane | 7.4 |
| FX shell + COMP | 6.6 |
| not attributed | 16.6 |
| **`instrument_worst`** | **120.6** |

The gate this round is judged against is `instrument_worst_bbd` under 100 %
(owner's decision, 2026-07-29): it is `instrument_worst` with STAGES at
maximum, DRIVE high and the FLUX rate on the shortest division — all positions
a player can actually dial. That is **34 points** to find, which is what the
whole of FLUX costs. It cannot come from one place.

**What this round is for.** Not the fixes. The existing rows are binary — FX on
or off, instrument worst or init — so they show sums and hide knees. This round
measures cost *across each suspect control's travel* so the expensive settings
become visible as settings, not as totals. The governing idea (owner,
2026-07-29) is to find **extreme settings that cost disproportionately** —
rarely used, expensively paid — rather than to shave the cost of everything.

## 2. Scope

**In scope.** Adding a `sweep` bench family and profile; four cost-vs-setting
sweeps; two ablation rows that close two specific open questions; one hardware
run; and a written reading of the curves that names a disposition per knee.

**Out of scope, deliberately.**

- *The fixes themselves.* Specifying them before the curves exist would mean
  inventing the numbers they are sized against. They are the next round, and
  §5 is the rule that will size them.
- *The 16.6 unattributed points.* Real, probably worth having, and a different
  question — the components have never summed to the whole in this repo (CPU
  hunt round 3 saw a 39-point gap). Chasing it here would double the round and
  blur its question.
- *Anything that changes the BBD's sound.* See §3.

## 3. Constraints

**Two levers are pre-authorised** (owner, 2026-07-29; both already named in
`bench/workloads_bbd.cpp`'s own comment):

- the clock ceiling `kClockMaxHz`, 32 kHz → 24 kHz — at 32 kHz the line runs
  1.33 ticks per audio sample, at 24 kHz exactly 1.0;
- `kMaxStages`, 16384 → 8192.

**Two levers are refused** (owner, 2026-07-29): FLUX must stay stereo — no
collapsing the two `BbdEcho` into one — and `kFiltOrder` stays at 3. Those are
the model's sound, and the round may not spend them.

*[Superseded in part, same day: see §9.8. Stereo FLUX is now an accepted,
ordered plan; `kFiltOrder` remains refused.]*

**A caution recorded at design time.** The two authorised levers act on the
tick loop, and the tick loop is only part of a line's cost: the three complex
poles in each direction advance every audio sample whether a tick happened or
not. Together they are expected to yield well under 5 points. If the sweeps
plus the wrapper work do not reach 34, the refused levers come back onto the
table as a decision, not as a surprise.

**Also standing.** No headroom is being reserved beyond the gate (owner's
choice): ZAP (M5k) and PULL (M5l) each need their own budget check before they
are built, because after this round there is no reserve to absorb them.

## 4. What gets measured

Nineteen rows in a new `sweep` family — eighteen if §4.6's second row proves
unbuildable without touching production code. All percentages below are of the
block budget of 960 000 cycles at 480 MHz, block size 96.

Whatever the final count, it must be identical in all four places §6 lists:
`run.py` fails the run if the measured row set differs from its expectation by
so much as one row, which is the intended behaviour and the reason a dropped
row is a documentation change, not just a deletion.

### 4.1 Sweep A — FLUX clock, 5 rows

`sweep_flux_rate_{0,3,6,9,11}`, indices into the 12-step division ladder
(`engine/mod/divisions.h`, `kFluxRateCount = 12`); index 11 is the shortest
division, which drives the clock onto its ceiling.

The most likely steep knee, and the one the `instrument_worst_bbd` row is
mostly made of: the clock drives the tick loop linearly until `kClockMaxHz`
clamps it. What this decides is whether 24 kHz is enough or whether the top of
the ladder needs reshaping.

### 4.2 Sweep B — STAGES, 4 rows

`sweep_stages_{512,2048,8192,16384}`. `Flux::set_stages` is geometric,
`512 * 32^n`, so these are norm 0.0, 0.4, 0.8, 1.0.

The suspicion here is **not** arithmetic — stage count does not change the tick
rate at a fixed clock — but memory. Four lines at 16384 stages hold 128 KB of
SDRAM; at 8192, 64 KB. The H7's D-cache is 16 KB. If the knee is here it is in
the cache, and then halving `kMaxStages` is not a sound compromise at all, it
is the actual fix.

### 4.3 Sweep C — voice count, 4 rows

`sweep_voices_{1,2,3,4}` per deck, driven through COLOR as
`setup_inst_worst` already does (`set_color(p, 1.f)` yields 4-note chords).

The four COLOR values are **not** 0.0 / 0.33 / 0.67 / 1.0 by assumption: they
must be derived from the chord layer's own norm-to-chord-size mapping, and each
row must confirm the voice count it actually got rather than the one it
intended. A row that believes it is measuring three voices while four are
sounding produces a curve with a knee that is not there.

35.8 points for eight voices is the single largest item in the table. The
question is whether it grows linearly or whether the last voice costs more than
the first.

### 4.4 Sweep D — reverb, 3 rows

`sweep_room_{lo,mid,hi}`, sweeping DIFF / SMEAR / MOD together, since
`setup_inst_worst` puts all three near maximum and they are one gesture in
practice.

### 4.5 Ablation E — the Flux wrapper, 1 row

`sweep_flux_lines_2ch`: two `BbdEcho` driven directly, at the stage count and
clock a default-initialised `Flux` computes, with no `Flux` around them.

Then `fx_flux_sdram − sweep_flux_lines_2ch − fx_none` is the wrapper's own
cost. Today that figure is an inference from two rows measured in different
contexts (~5.7 points per deck); this makes it a measurement. It matters
because the wrapper's per-sample work — two `fonepole` slews, two `std::fabs`
snaps, a `clampf`, and `bbd_clock_hz`'s division — runs every sample although
its inputs only move on the 96-sample control tick. That is the defect this
repo has had to fix three times (`daisysp::String`, `daisysp::Resonator`, the
mode bank), and unlike everything else in this spec, fixing it changes the
sound at **no** setting.

If `Flux` exposes no accessor for its computed clock, add one marked
test-only; `Compander::env_comp()` / `env_exp()` are the existing precedent for
that in this file's neighbourhood.

### 4.6 Ablation F — the `fx_grit` anomaly, 2 rows

`fx_grit` rose from 4.78 % to 7.70 % max between `518f639` and `1f7671d` with
an **identical checksum**, no commits to `engine/fx/grit.{cpp,h}` in that
range, FLUX provably not leaking in (`setup_fx` disables it with
`immediate = true`; `Flux::process` returns at `flux.cpp:280` when the switch
is idle), and `fx_none` — the same shell with everything off — unmoved.

Two rows, chosen so that between them they separate the three candidate
causes — GRIT itself, the shell around it, and the mere presence of the BBD's
memory:

- `sweep_grit_bare`: a bare `Grit` processed directly, no `PartFx` around it,
  at the settings `setup_fx(SEL_GRIT)` uses.
- `sweep_grit_no_bbd_mem`: the full `fx_grit` configuration, but with `PartFx`
  initialised so `Flux` has no echo memory. `Flux::process` returns at its
  `_buf_ok` guard before anything else, so this is `fx_grit` with the BBD's
  128 KB of SDRAM absent from the working set while every other line of the
  shell still runs. The implementer must first confirm that `Flux::init`
  tolerates null echo memory and leaves `_buf_ok` false; if it does not, this
  row is dropped rather than production code being changed to enable it.

The discriminator: `fx_grit − fx_none` is 5.14 today, against a historical
2.22. If `sweep_grit_bare ≈ 5.14`, GRIT itself costs that and the old figure
was measured on a smaller image — i.e. code layout, and the report's own
warning about a cross-build shift applies. If `sweep_grit_bare ≈ 2.2` but
`sweep_grit_no_bbd_mem ≈ 5.14`, the shell is the suspect. If
`sweep_grit_no_bbd_mem ≈ 2.2`, the cost is cache pressure from the BBD's
buffers and nothing in the FX code is wrong at all — which would also predict
sweep B's knee.

## 5. The decision rule

Every knee the round finds gets exactly one of three dispositions, and the
curve decides which:

**Leave it.** The control costs in proportion to what it gives. This is a
result, not a failure of the round, and it must be recorded as explicitly as
the other two.

**Reshape the range.** The top of the travel costs disproportionately and gives
little: move the endpoint, or bend the mapping so the expensive region is no
longer reachable. No runtime machinery, no ongoing cost, permanent. This is the
right instrument for a *single* control whose extreme is not wanted — both
pre-authorised levers are of this kind.

**Throttle from predicted cost.** `instrument_worst_bbd` is not one control at
its stop; it is eight at once — 8 voices *and* every FX *and* high diffusion
*and* FLUX at its clock ceiling *and* STAGES at maximum. Each may be affordable
alone. No per-control cap addresses co-occurrence, so if the curves show the
combination is the problem, the answer has to see the combination.

That can be deterministic, and must be. Cost here is a function of the control
values, not of measured load: the clock, the stage count, the active voice
count and the diffusion are all known at the 96-sample control tick, and this
round's curves *are* the coefficients of that function. Same patch, same
decision, every time — no control loop around the audio callback, nothing to
oscillate, no hysteresis needed, and testable in the desktop renderer without
hardware.

*Explicitly rejected:* a load meter that measures actual CPU and reacts. It
closes a loop around the audio callback, is not reproducible, is not testable
offline, and necessarily acts only once the overrun has already happened.

**Two properties any throttle must have.**

- *Continuity.* The modulation lanes move the controls continuously, so the
  estimate moves continuously and what gives way must move continuously in its
  own parameter. A clock ceiling that slides, or a diffusion that recedes, can
  do this. A voice count stepping 4 → 3 cannot.
- *A declared order.* When it engages, a control changes something it does not
  nominally command — turn COLOR up and the echo changes. From the player's
  side that is an instrument misbehaving. So the order in which things give way
  is fixed and written down, and the round must be able to state which control
  yields first and by how much.

If the curves show that reshaping alone reaches the gate, that is the better
outcome and the throttle is not built. The machinery is only justified by an
extreme that is musically wanted.

## 6. How the rows are built

The bench is a fixed table of `{family, name, setup_fn, proc_fn}`. A sweep is
therefore one row per sample point, not a parameterised run — which fits the
existing idiom: `setup_fx_grit` / `setup_fx_flux` / `setup_fx_comp` already
share a single `proc_fx`. One `proc` per sweep with four or five setups on it
keeps the addition small.

**The profile carries `system`.** A `sweep`-only profile would leave
`verdict()` without its `instrument_worst` anchor and print "undetermined" —
exactly what happened to the `bbd` profile, which is why the BBD numbers stood
for two days without a system verdict. `body` (`system` + `body`) is the proof
that a two-family image links.

**Determinism is a gate, not a nicety.** `run.py` compares per-row checksums
across the two runs and refuses evidence on any drift. Stateful rows must
settle in their *setup*, outside the measured window — `setup_bbd_ceiling`
runs 49 152 samples so the line is full and the compander envelopes have
converged. Every new row containing a BBD or the reverb needs the same.

**Four places must agree**, and a mismatch aborts before anything is flashed:

1. `bench/workloads_sweep.cpp` — the rows themselves;
2. `bench/families.cpp` — a `BENCH_FAMILY_SWEEP` block, plus the generator
   `write_bench_families.py` that produces `bench_families.h`;
3. `run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY` — the hand-maintained expectation
   of which rows the family supplies;
4. `bench/profiles.py` — the profile, declaring `wave_acceptance` (legal only
   because it carries `system`, which supplies `synth_2x4` and `wave_2x4`;
   `profiles.resolve` enforces this).

**Memory, and a correction.** An earlier draft of this spec said the
instrument-level sweeps should reuse the `Instrument` in `g_system_arena`.
They cannot: that arena is a static in `workloads_system.cpp`'s **anonymous
namespace**, invisible to any other translation unit. The choices are to
export it through a new shared header — which edits `workloads_system.cpp`
and therefore perturbs the very code layout §4.6 is investigating — or to give
`workloads_sweep.cpp` its own arena.

Take the second. `SerialArena` overlays its groups (`capacity` is the *max*
`sizeof`, not the sum), so a second arena costs one more max-sized `.bss`
block, and that maximum is set by the instrument-level sweeps. `mem.h` already
notes two `Instrument`s in the bench globals; this makes three, against 56 KB
free in an image at 78.6 % SRAM / 69.5 % SRAM_EXEC.

That is a genuine risk, so it is proved before nineteen rows are written
rather than discovered after: the first task scaffolds the family with a
single trivial row, links it, and records the SRAM delta.

## 7. Verification

The round is done when all of the following hold:

1. `python run.py --profile sweep` completes with exit code 0 from a clean git
   tree — meaning two runs, identical unique row sets and per-row checksums,
   identical QSPI digest and device fingerprint, and the `wave_acceptance`
   gate passed.
2. The accepted report and CSV are committed under `docs/bench/`.
3. `instrument_worst` appears in that report as the anchor, and its reading is
   compared against 120.6 % max in writing. It is **not** required to match:
   this is a different image, and the bench's own note warns that a cross-build
   layout shift moved a 29 000-cycle workload by about 7 %. A shift of more
   than a few points is itself a finding — the same finding §4.6 is chasing —
   and must be recorded, not absorbed silently.
4. A written reading is committed alongside, giving for each of the four
   sweeps the measured curve and a named disposition per §5, and for each of
   the two ablations an answer rather than a hypothesis.
5. `docs/roadmap.md` carries the outcome.

The desktop test suite must stay green throughout; the sweeps add bench code
only and must not touch `engine/`, with one permitted exception — a test-only
clock accessor on `Flux` if §4.5 needs it.

**Not a criterion:** that the instrument fits. This round produces the numbers
the next one is sized against. Reporting it as a fix would be false.

## 8. Risks

**The round may not find 34 points.** Stated in §3 and worth repeating: the
authorised levers are small, and if the wrapper work and the knees together
fall short, the refused levers — mono FLUX, filter order — return as an
explicit decision. That is a possible outcome, not a failure.

**A sweep row can measure an empty machine.** The failure mode is silent: a row
that has not settled reports a plausible number that is simply wrong, and the
checksum gate does not catch it because it is consistently wrong across both
runs. Mitigation is §6's settle requirement, applied per row, and a cross-check
that each sweep's most-expensive point is consistent with the corresponding
existing binary row.

**The image may stop linking.** `full` already does not link
(`bench/README.md:34`), and `sweep` + `system` adds both rows and a second
arena to an image at 78.6 % SRAM.

The fallback is **not** to drop `system` — losing the `instrument_worst`
anchor costs more than losing a curve. It is to drop sweeps **C and D
together**, because they are what sets the new arena's size: both need an
`Instrument`, while A, B, E and F need only a `Flux`, two `BbdEcho` and a
`Grit`. Dropping D alone, as an earlier draft of this spec said, would save
almost nothing — C would still pull the whole `Instrument` in.

If that fallback is taken, the voice-count and reverb curves move to a later
round measured under the `body` profile's precedent (`system` + one family),
and the round must say so in writing rather than quietly reporting four
sweeps' worth of conclusions from two.

## 9. Results (2026-07-29, `cd6dafd`)

Evidence: `docs/bench/2026-07-29-cd6dafd-sweep.md`, two accepted runs, gate
passed (row set, checksums, QSPI digest and device fingerprint all identical
across runs; `wave_acceptance` passed). Figures below are run 2's max % of
the 960 000-cycle block, the more conservative of the two accepted runs;
where it matters, both runs are named. §6's fallback was not taken — all
four sweeps shipped, plus both ablations.

Sweep C (voice count) does not appear as a curve: Task 6 found `setup_inst_worst`
saturates all four COLOR settings to 8 voices within ~150 blocks regardless of
the norm dialled, so four rows would have returned one number. The question
sweep C was built to answer is addressed in §9.4 instead, from rows that
already existed.

### 9.1 Sweep A — FLUX clock

| clock (Hz) | 4096 | 8192 | 16384 | 24576 | 32000 |
|---|---:|---:|---:|---:|---:|
| max % (one deck, shell included) | 18.94 | 19.48 | 20.68 | 21.78 | 22.66 |

Monotonic in the clock, no knee independent of it. The pre-authorised ceiling
lever, 32 → 24 kHz, is priced above as `22.66 − 21.78` = **0.88 points per
deck, 1.76 both** — but that reading is a stand-in: 21.78 is rate 8's
measured point, 24576 Hz, the nearest sampled rung to the 24 kHz target, not
24000 Hz itself. The fitted slope across the five rate rows (1.333e-4
points/Hz) puts the true 32000 → 24000 Hz saving at ≈1.07 points per deck,
≈2.13 both. So the lever is worth **at least 1.76, and ≈2.13 by the fitted
slope** — small either way, and it is a range move, not a runtime cost.

**Disposition: reshape the range.** The top of the ladder is exactly the
region §5 describes: it costs the most and, per §9.4 below, three of its
rungs (indices 9, 10, 11) are already indistinguishable from one another
because they clamp to the same 32 000 Hz ceiling. Moving the ceiling to
24 kHz is the textbook case — permanent, no runtime machinery, and it removes
reachable travel rather than reacting to it.

### 9.2 Sweep B — STAGES

Division 3, so the clock equals the stage count in the table below.

| stages | 512 | 2048 | 8192 | 16384 |
|---|---:|---:|---:|---:|
| max % | 18.45 | 18.63 | 19.48 | 20.68 |

The cross-check the spec asked for: `sweep_stages_16384` (16384 stages
clocked at 16384 Hz) and `sweep_flux_rate_6` (boot-default 8192 stages,
also clocked at 16384 Hz) both read **20.68**, identically. Doubling the
stage count at a fixed clock costs nothing measurable. The memory/cache
hypothesis in §4.2 is refuted — the cost in this sweep is the clock alone,
and stages help only indirectly, by letting a given delay time run at a
lower clock.

**Disposition: leave it.** There is no knee to reshape and nothing to
throttle: the control's own cost is flat. This also means halving
`kMaxStages` (the second pre-authorised lever) buys no CPU by itself — its
case, if made, has to be argued on SDRAM footprint or cache pressure
elsewhere, not on this curve.

### 9.3 Sweep D — room

| setting | lo | mid | hi |
|---|---:|---:|---:|
| max % (whole instrument) | 120.18 | 120.17 | 120.81 |

Span across the entire travel of DIFF/SMEAR/MOD together, one gesture: `120.81
− 120.17` = **0.64 points**. Flat.

**Disposition: leave it.** No knee anywhere in the travel; the room controls
are not a lever worth spending this round's budget on. Its settle is
conservative in the safe direction, not merely adequate: the reverb's loop
gain sits above unity (1.05 per branch) and is bounded by `SoftLimit`, so the
loop grows into the limiter and plateaus rather than asymptotically
approaching a steady state from below — reaching that plateau is faster than
settling a sub-unity line, so the "four fills" multiplier borrowed from the
BBD rows is over-generous for this topology, not under-generous.

### 9.4 Voice count — answered without a sweep

`sweep_voices_*` was removed (Task 6). The question — does the eighth voice
cost more than the first — is already answered by pre-existing `system` rows:

| workload | voices | max % |
|---|---:|---:|
| `synth_1_voice` | 1 | 5.67 |
| `synth_2_voices` | 2 | 9.91 |
| `synth_4_voices` | 4 | 17.83 |

About 1.45 points of fixed overhead plus ~4.2 points for the first
additional voice. The simple linear model (1.45 + 4.2/voice) predicts 18.25
points at four voices against 17.83 measured, and the marginal cost per
voice actually falls across the range measured — 4.24 points/voice from 1 to
2 voices, 3.96 points/voice from 2 to 4. Linear or slightly sub-linear, not
superlinear: the eighth voice does not cost more than the first.

**Disposition: leave it.** Nothing here is disproportionate; the control
costs in proportion to what it gives, and the sub-linear marginal cost only
strengthens that reading.

**Observation, not evidence** (from a reverted scratch test built to check
this sweep's own premise, Task 6): under `setup_inst_worst`'s mandated
DEPTH=1/DENSITY=1/RATE=0.8/VOICE_DECAY=1, COLOR is not a lever on voice count
— all four COLOR settings saturate to 8 active voices because MOTION
modulates COLOR and voices accumulate faster than the long decay frees them.
Anything that later tries to manage CPU by reasoning about chord size needs
to know this does not hold under dense settings. Labelled an observation
because the test that found it was reverted, not committed.

### 9.5 None of the four sweeps calls for a throttle

Per §5, the three dispositions are exhaustive and "leave it" must be stated
as explicitly as the others: sweep A gets **reshape the range**; sweeps B, D,
and the voice-count question all get **leave it**. None of the four,
individually, shows an extreme that is expensive *and* avoidable at that one
control — which is exactly what a throttle requires to be worth building.
The actual problem, per §5's own framing, is co-occurrence:
`instrument_worst_bbd` is eight controls at their worst simultaneously, and
no per-control cap found here addresses that. Whether a combination throttle
is needed is a question for the round that follows this one, once the
`sum(9.7)` below shows what is left to close after the fixes it does
authorise.

### 9.6 Ablation E — the Flux wrapper

`sweep_flux_lines_2ch` drives two bare `BbdEcho` at `Flux`'s own boot-computed
stage count and clock, no `Flux` wrapper around them. That makes the
wrapper's own cost a measurement rather than an inference:

```
wrapper / deck = fx_flux_sdram − sweep_flux_lines_2ch − fx_none
              = 19.42 − 9.16 − 2.55
              = 7.71
```

FLUX costs 16.87 points per deck above the bare FX shell
(`fx_flux_sdram − fx_none`). Of that, the two `BbdEcho` lines are **9.16**
and the wrapper is **7.71** — **46 %** of FLUX's cost above the shell is the
wrapper, not the model. Both decks: **18.3** points of model, **15.4** points
of wrapper.

The wrapper's per-sample work — two `fonepole` slews, two `std::fabs` snaps,
a `clampf`, `bbd_clock_hz`'s division, and a `std::pow` — runs every sample
although its inputs (RATE, STAGES, DRIVE) only move at the 96-sample control
tick. The `std::pow` is not a footnote alongside the slews: `PartFx::process`
calls `_flux.set_feedback(...)` unconditionally, every sample, whenever
either GRIT or FLUX is engaged (`part_fx.cpp:38`); with no dirty check on
DRIVE, that falls through to `Flux::apply_feedback` and `bbd_drive_gain`'s
`std::pow` (`flux.cpp:98-101`, `bbd.h:192`) every sample the gate is open.
That branch is live in `fx_flux_sdram`, dead in `fx_none`, and absent from
`sweep_flux_lines_2ch` (which drives the bare `BbdEcho`s directly, with no
`PartFx` around them) — so it sits inside this section's 7.71/deck (15.4
both) figure, and it is the largest single identified component of it.
Moving all of this to control rate changes the sound at **no** setting, at
any position of any control. This is the same defect class this repo has
already fixed three times (`daisysp::String`, `daisysp::Resonator`, the mode
bank).

### 9.7 Ablation F — the `fx_grit` anomaly, answered

`fx_grit` rose from 4.78 (`518f639`, 2026-07-26) to 6.53 here, at an
**identical checksum**. §4.6 named three candidates: GRIT itself, the shell
around it, or cache pressure from the mere presence of the BBD's memory. The
numbers rule two out and the third needed a correction en route:

- **GRIT itself is not the cause.** `sweep_grit_bare` (a bare `Grit`, no
  `PartFx` shell) is **1.53** — far below either the historical, same-image
  (`fx_grit − fx_none` = `4.78 − 2.55` = 2.23, 2026-07-26, both at `518f639`)
  or current (`6.53 − 2.55` = 3.98) gap. GRIT's own cost did not grow.
- **The clean three-way split the ablation was designed for did not hold.**
  Task 4 found `sweep_grit_no_bbd_mem` conflates two effects, not one: it
  removes the BBD's memory residency *and* the per-sample `set_feedback`
  work gated behind the same `_buf_ok` flag — because `PartFx::process`
  guards that work on `_buf_ok`, not on whether FLUX is engaged
  (`part_fx.cpp:38`, `flux.cpp:99`). A low reading from that row alone would
  be the work behind that gate plus any memory-residency effect; §9.2 shows
  the latter is not measurable, so the 1.60 below is attributable to the
  gated work.
- **The fourth cause, found by reading and not listed in §4.6, is what the
  numbers corroborate.** A `std::pow(10.f, db * 0.05f)` in `bbd_drive_gain`
  runs every sample, per deck, whenever the `_buf_ok` gate is open, with no
  dirty-check on DRIVE unlike the `set_intensity` call three lines above it
  (`bbd.h:192`). `fx_grit − sweep_grit_no_bbd_mem` = `6.53 − 4.93` = **1.60**
  per deck (**3.2** both) is the work behind that gate, and this repo's own
  `abl/micro_powf` bench prices a single `powf` at 198 cycles — 198 × 96
  samples ≈ 1.98 points per deck — the right neighbourhood for that 1.60.
  Separately, `fx_grit` itself fell from 7.70 (the previous accepted image,
  identical checksum) to 6.53 here: **1.17 points of pure code-layout
  drift**, the same phenomenon the bench's own note already warns about.
  This is the same `std::pow` as §9.6's wrapper finding, exercised here via
  GRIT's shared `_flux` object rather than FLUX itself: this saving is
  already inside §9.6's 15.4 points; it is the largest identified component
  of it, not an addition to it.

**Answer:** the rise between 07-26 and 07-29 is neither GRIT getting more
expensive nor an unresolvable "shell" mystery. It is
`std::pow`-under-a-stale-gate (≈1.60/deck measured, ≈1.98/deck predicted) plus
image-layout drift (1.17/deck). The same-interval check, done directly rather
than by comparing one derived delta to another: `518f639`'s `fx_grit` (4.78)
plus the pow finding (1.60) is 6.38, against `cd6dafd`'s directly measured
`fx_grit` (6.53) — **0.15 apart**. The fix, for the next round and not this
one (`engine/` is off limits here): cache `bbd_drive_gain`'s result in
`set_drive` and make `apply_feedback` a multiply. Control-rate work at
control rate, changing the sound at no setting — the same shape of fix as
§9.6's wrapper finding.

### 9.8 The sum, honestly

What this round actually authorises, added up:

| item | points, both decks |
|---|---:|
| wrapper to control rate (§9.6) | up to 15.4 |
| clock ceiling 32→24 kHz (§9.1) | 1.76 |
| STAGES halving (§9.2) | 0 |
| room (§9.3) | 0 |
| **total** | **17.16** |

The gate is `instrument_worst_bbd` under 100 %; it measured **132.79**, i.e.
**32.8 points** to find. 17.16 against 32.8 falls short by **15.64 points**
— about half the gap closes, not all of it. That shortfall is a different
number from, and should not be confused with, the **18.3 points** that §9.6
separately measured as the two `BbdEcho` lines' own cost, both decks — the
model's cost, not the wrapper around it. The two happen to be close in
magnitude but answer different questions: 15.64 is what is still missing
against the gate; 18.3 is what the model itself costs, of which this round
was authorised to spend none. Both point the same direction, though: every
lever this round was authorised to spend is already spent, and what remains
sits in the model itself. §3 said this could happen and named exactly where
the shortfall would have to be paid: "if the sweeps plus the wrapper work do
not reach 34, the refused levers come back onto the table as a decision, not
as a surprise." That is what has happened.

**Owner decision, 2026-07-29, superseding §3's refusal — "Erst den Mantel,
messen, dann auf mono."** Having seen the numbers above, the owner has taken
back half of what §3 refused. The order is fixed:

1. Move the `Flux` wrapper's per-sample work to control rate (§9.6) — up to
   15.4 points across both decks, no sound change at any setting.
2. **Re-measure before going further.**
3. Only then, collapse FLUX to a single mono `BbdEcho` per deck.

`kFiltOrder` stays refused — the owner accepted mono, not filter order. A
later reader must not treat §3's "two levers refused" as still standing in
full: one of the two — stereo FLUX — is now an accepted, ordered plan, dated
2026-07-29. Filter order remains off the table.

**The arithmetic for the accepted plan, done honestly rather than
compounded.** `sweep_flux_lines_2ch` is 9.16 for *two* lines at once, so a
single mono line saves roughly half that per deck: ≈4.6/deck, **≈9.2 both**.
Stacking every authorised and now-accepted lever:

| item | points, both decks |
|---|---:|
| wrapper to control rate | up to 15.4 |
| mono FLUX | ≈9.2 |
| clock ceiling 32→24 kHz | 1.76 |
| STAGES halving | 0 |
| room | 0 |
| **total** | **≈26.4** |

Against the 32.8-point gate, that is **short by ≈6.4** — the fully executed,
perfectly delivered accepted programme lands near **106 %**, not under
100 %. This is stated plainly and is not softened: three authorised fixes do
not currently sum to a fit.

**Two caveats on that estimate, both real and both cutting the same way.**
First, 15.4 is an upper bound that assumes *every* per-sample wrapper
operation can move to control rate; that has not been established, only
identified. Second, these figures are all read off `fx_flux_sdram`'s default
configuration, while the gate row `instrument_worst_bbd` runs FLUX at its
clock ceiling — where the model's tick-loop cost is higher and the wrapper's
fixed per-sample cost is not. So against the actual gate row, the mono saving
is likely larger than 9.2 and the wrapper saving is likely close to 15.4 as
estimated. That asymmetry is exactly why the owner put a measurement between
steps 1 and 3 instead of ordering both at once: compounding two estimates
instead of measuring between them is how this round's own 34-point starting
figure (from hoisted, non-additive component rows, §1) came to be wrong in
the first place, and re-measuring after the wrapper fix alone is what avoids
repeating it here.

**Not claimed:** that the instrument fits. It does not, on any accounting in
this section. What this round produced is the coefficients — wrapper 15.4,
mono ≈9.2 (pending re-measurement), clock ceiling 1.76 — that size the next
round's work, and an owner decision, taken from this evidence, that puts
stereo FLUX back into play with a measurement gate between the two steps
rather than a single compounded bet.

### 9.9 Consistency checks

- `instrument_worst` (this sweep, run 2, offline): **120.14**, against
  120.6 max reported at `1f7671d` two days earlier. A 0.46-point difference,
  well inside the cross-build layout shift this bench's own note already
  warns about (§7.3) — not a regression.
- `sweep_stages_8192` (19.48) vs `sweep_flux_rate_3` (19.48): same
  configuration reached two ways, agrees exactly.
- `fx_flux_sdram`, one of the three terms in §9.6's wrapper subtraction, runs
  with no settle beyond the runner's fixed 100-block warm-up, while the
  other two settle 96k–192k samples. That mixed subtraction is exonerated by
  a direct check rather than an assumption: `sweep_flux_rate_3` (19.48, at
  the same configuration, fully settled) agrees with `fx_flux_sdram` (19.42,
  unsettled beyond warm-up) to within **0.06** — the lack of settle on that
  one term is not moving the wrapper figure measurably.

### 9.10 Side finding — the rate ladder's dead zone

Independent of CPU: `sweep_flux_rate` samples clock indices 0/3/6/8/11
(4096/8192/16384/24576/32000 Hz). Indices 9, 10 and 11 all clamp to the same
32 000 Hz ceiling — same clock, same delay, same sound, same cost — so three
of the twelve rungs on the control are indistinguishable from one another.
This holds at the boot default of 8192 stages only. The clamp point is
`stages/(2t)`, so it moves with STAGES: at STAGES's maximum (16384) the dead
zone is wider still — index 6 (16384 Hz unclamped at 8192 stages) already
clamps at 16384 stages — and at STAGES's minimum (512) it vanishes entirely,
since every rung's unclamped clock then sits well under the ceiling.
Applying this round's own 24 kHz ceiling lever (§9.1) would make it **four**
at the boot default: index 8 sits at 24576 Hz, just above 24 kHz, so lowering
the ceiling clamps it too. The clock-ceiling lever buys CPU by lengthening
the ladder's own dead zone; index 8 is exactly the point where that trade
becomes visible on the control surface, not just in the profiler.

### 9.11 Carried into the next round, not fixed here

Two real gaps in row identity, found on review, both one-line changes with
an exact precedent in an adjacent row, and both deferred rather than fixed
in this round: fixing either moves the affected row's per-row checksum,
which would invalidate the accepted hardware evidence in
`docs/bench/2026-07-29-cd6dafd-sweep.md` and force a re-measurement this
round's findings do not otherwise need.

- **`setup_flux_rate` folds a guard that cannot distinguish the failure it
  exists to catch.** It folds `stages_achieved` into the checksum, which is
  8192 for all five rate rows *and* is also `_stages_now`'s declared
  initialiser — so the guard reads identically for a correct `Flux`, one
  that never advanced, and one that was never engaged. The swept variable,
  the clock, is never read back into the checksum at all, although
  `Flux::clock_hz()` is public and the adjacent ablation row
  (`setup_flux_lines_2ch`) already folds exactly that. Both of this file's
  prior Sweep A defects (the zeroed `FXT_FLUX_TIME` value, the duplicate
  rate-9/rate-11 clock) were clock defects that a stage-count guard cannot
  see.
- **`proc_sweep_room` folds a narrower guard than the precedent it has.** It
  folds only `reverb_asleep()`, not `active_voices()`, although the eight
  voices `setup_room` triggers are ~36 of this row's ~120 points — and the
  system family's `proc_inst` folds exactly that, having been burned once by
  "a '1 voice' row measuring 2.8 voices."

Recorded here so neither is rediscovered from scratch in the next round.
