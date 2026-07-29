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

**Memory.** The instrument-level sweeps (C and D) use the `Instrument` already
in `g_system_arena` rather than constructing a second one; an `Instrument` is
large and the image is at 78.6 % SRAM / 69.5 % SRAM_EXEC.

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
(`bench/README.md:34`), and `sweep` + `system` adds rows to an image at 78.6 %
SRAM. If it overflows, the fallback is to drop sweep D (reverb, 3 rows, the
least suspicious of the four) rather than to drop `system` — losing the anchor
would cost more than losing one curve.
