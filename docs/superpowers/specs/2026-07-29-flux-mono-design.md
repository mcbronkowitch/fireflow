# FLUX: one BBD line per deck

**Date:** 2026-07-29
**Status:** design, approved by the owner
**Predecessor:** `docs/superpowers/specs/2026-07-29-flux-control-rate-design.md`
**Evidence:** `docs/bench/2026-07-29-4d1e929-sweep.md`

---

## 1. What this round is

Step two of the owner's order — *first the wrapper, then measure, then mono*.
The wrapper round took 7.55 points off the gate row (`instrument_worst_bbd`
132.79 % → 125.24 %) at no musical cost. This round collapses FLUX's stereo
`BbdEcho` pair to a single line per deck, which the previous round's evidence
prices at 9.16 points across the two decks (`sweep_flux_lines_2ch`, the bare
two-channel pair, measured 9.17 — so one line is ~4.58 per deck).

Unlike the wrapper round, **this one changes the sound.** §3 and §4 say
exactly how.

**Panning is deliberately not in this round.** The owner's decision, taken
before the design was written: build mono, measure it, listen to it, and only
then decide whether the lost width needs recovering and in what form. A
panning strategy chosen before hearing what is actually missing would be
solving an imagined problem. §6 defines the listening material that makes the
decision possible; a follow-up round, if wanted, gets its own spec.

## 2. What is actually lost, stated precisely

FLUX today is not fake stereo. `_echo_l` and `_echo_r` are two identical
processors — same clock, same stage count, same feedback coefficient, same
drive (`flux.cpp`, the two `Process` calls). They **preserve** whatever width
arrives and **generate** none of their own.

Width does arrive: synth voices pan individually with a slow per-voice drift
(`engine/synth/voice.cpp`, `set_pan` and the `_drift_pan_phase` term), and
`BodyVoice` does the same. So the loss is concrete, not theoretical, and it
has a precise shape:

**The dry signal keeps its position; the repeats move to the centre.** A note
played hard left stays left, and its echo no longer trails left with it.

Two things partly mask this, and both matter when judging:

- The echo feeds the reverb send, and the reverb is a true stereo Oliverb
  (`engine/fx/reverb.cpp`), so a centred echo comes back with width — but only
  in the wet path, and only when `FXT_REV_SEND` is up. At a dry patch the loss
  stands in full. §6 turns this into the experiment.
- The excitation-bus tap already sums the echo to mono
  (`part_fx.cpp`, `echo_mono = 0.5f * (...)`), so that path is indifferent to
  this change. Its meaning is unchanged and it gets slightly cheaper.

## 3. What "mono" means at the boundary

One `BbdEcho` per deck. Input is the mono sum of the post-GRIT signal times
the soft-switch send; the single output is added to both channels:

```cpp
const float in = 0.5f * (l + r) * send;
const float e  = _echo.Process(in, hz);
// ... gate, exactly as today ...
l += e * _mix_lin;
r += e * _mix_lin;
```

**The 0.5 is a decision, not a formality.** It holds centred material at
exactly today's echo level and lets hard-panned material feed the echo **6 dB
quieter** than it used to. The alternative, `1/sqrt(2)`, is power-preserving
but lifts centred material by 3 dB — and centred is the normal case that every
setting the owner has dialled in by ear currently sits on. Leaving the normal
case untouched and naming the edge case is the better trade. The 6 dB is a
real consequence and §6's listening pass is where it gets judged.

## 4. The consequence that is not about width

Collapsing the pair changes **where DRIVE bites**, and this has nothing to do
with stereo.

`BbdEcho` runs a saturator, a compander and a feedback loop
(`engine/fx/bbd.h`). Today each channel gets its own set. Afterwards one set
sees the sum. For material with any width the summed signal is hotter than
either channel was, so saturation sets in earlier on the knob, the compander's
envelope tracks a different signal, and the feedback loop is one loop instead
of two independent ones.

This is not a defect to correct. A mono BBD pedal has one chip, one compander
and one loop, and that is what this becomes. But it means **DRIVE may need a
by-ear pass after this round**, and a reviewer or a later session must not
"fix" a changed DRIVE character back toward the stereo behaviour. The
0..+12 dB range (`bbd_tuning::kDriveLoDb`/`kDriveHiDb`) was tuned by
measurement against the old topology and its own comment already records that
the constraint which picked it no longer binds.

## 5. The signature change, and its blast radius

`FxMem::echo` loses a dimension:

```cpp
float* echo[PART_COUNT] = { nullptr, nullptr };   // was [PART_COUNT][2]
```

and `Flux::init`, `PartFx::init` and `Part::init` each lose their second
buffer pointer. This is mechanical and the compiler flags every site. The
known ones:

| file | what changes |
|---|---|
| `engine/fx/flux.{h,cpp}` | `_echo_l`/`_echo_r` → `_echo`; `init` takes one buffer; `process` sums and fans out; `apply_feedback`, `set_drive`, `set_stages` push to one line |
| `engine/fx/part_fx.{h,cpp}` | `init` signature |
| `engine/parts/part.{h,cpp}` | `init` signature (the two defaults become one) |
| `engine/instrument.{h,cpp}` | `FxMem::echo` and the two wiring lines |
| `host/render/main.cpp` | `s_echo[PART_COUNT][2][…]` → `s_echo[PART_COUNT][…]` and the wiring loop |
| `host/vcv/src/Spotymod.cpp` | the `echo[…][2][…]` member and its wiring |
| `bench/mem.cpp` | `g_echo[2][2][…]` → `g_echo[2][…]`, halving 128 KB of SDRAM to 64 KB |
| `tests/test_flux.cpp` | `s_buf_l`/`s_buf_r` → one buffer |
| bench workloads that build a `PartFx` or `Flux` directly | whatever the compiler flags |

Two notes on this table:

- **`host/vcv` must be built only via `./build-local.sh`.** The system `g++`
  on this machine is the ARM cross-compiler and a hand-rolled build fails with
  a misleading "MinGW not found".
- **`Spotymod.cpp`'s comment on that declaration is already wrong** — it says
  "~3.8 MB of echo buffer", a figure from before the BBD redesign shrank
  `kMaxSamples` to `kMaxStages/2`. The true figure today is 128 KB, and after
  this round 64 KB. Correct it while touching the declaration; leaving a
  three-orders-of-magnitude error in place next to an edited line is worse
  than the edit.

`Flux::kMaxSamples` itself does not change. One line still holds
`kMaxStages/2` cells and STAGES keeps its full 512..16384 range.

## 6. The listening material, and why its settings are load-bearing

The owner decides the panning question by ear, so this round's job is to make
the decision possible. **Two render pairs, before and after:**

- **The hard case — `host/render/scenarios/flux_mono_dry.json`.** The reverb
  recovers width from a centred echo, so leaving it running means judging how
  well the reverb hides the loss rather than what FLUX lost. Kill it twice,
  because one of the two is a mute and the other is the intent:
  `{"action":"set_reverb_mix","value":0.0}` and
  `{"action":"set_fx_target_base","part":0,"slot":3,"value":0.0}` — slot 3 is
  `FXT_REV_SEND` (`engine/fx/part_fx.h`).
- **The realistic case — `host/render/scenarios/flux_mono_verb.json`.**
  Identical in every other respect, with `set_reverb_mix` at its scenario
  default and slot 3 at **0.5**. What the instrument actually sounds like in
  use.

The two files must differ **only** in those reverb lines. If they differ in
anything else, the comparison between the pairs stops meaning what §6 says it
means.

**The difference between the two pairs is itself the answer** to "do we need
panning at all". If the hard case is obviously narrower and the realistic case
is not, the reverb is doing the job and a panning round may be unnecessary.

Both scenarios need genuinely panned source material — otherwise a mono-summed
echo is indistinguishable from the stereo one by construction and the test
proves nothing. Use a chord wide enough that per-voice pan drift has spread
it, high FEEDBACK for a long tail, and a DRIVE sweep so §4's saturation
change is audible in the same file.

The existing `host/render/scenarios/bbd_bloom.json` is close but not
sufficient: it sets no `set_rev_send`, so the reverb's contribution is
whatever the default is rather than a controlled 0. This round adds two new
scenarios rather than editing `bbd_bloom.json`, which is a committed listening
reference for the BBD design and must keep producing what it produced.

The four resulting files are handed to the owner. **No numeric threshold gates
this** — it is a listening test, and the owner's ear is the instrument.

## 7. Verification

**Desktop suite:** no new failure. `tests/test_seed_audition_init.cpp` is red
on `main` already and is not this round's to fix. `tests/test_flux.cpp` will
need its buffer declarations updated; any test asserting on two independent
channels must be re-read rather than mechanically patched — if a test claimed
something about `_echo_r` specifically, its claim has changed.

**The two stored render-hash gates are again unaffected**, for the same reason
as last round: `ctrl_identity.json` never turns FLUX on and
`wave_formant_sweep.json` turns FLUX and GRIT explicitly off, so a changed
FLUX reaches no sample in either. Neither hash may be re-baked. If either
moves, that is a real regression, not an expected consequence.

**Hardware re-measurement:** `python run.py --profile sweep` — never without
`--profile`. Per `bench/README.md`, and this cost a run last time: build
first, then rebind the QSPI receipt, then measure.

Per-row checksums **will move everywhere FLUX sounds**, because the echo is a
different signal now. That is expected. What must hold exactly is the bench's
own cross-run comparison of two runs of the same firmware.

## 8. What the measurement is actually being asked

The naive expectation is 9.16 points, from halving the 9.17 that
`sweep_flux_lines_2ch` measured for the bare stereo pair, times two decks.

The real question is different, and it is the one the previous round left
open: **does the load factor hold?** That round found the same code change
worth 1.49 points per deck in an isolated row, 2.16 on `instrument_worst` and
3.78 on `instrument_worst_bbd` — the saving grew with how loaded the machine
was, measured in both `pct_max` and `avg_cyc`, with the operating point ruled
out as a confound. The proposed mechanism (a per-sample libm call's
instruction-cache footprint) was explicitly labelled a hypothesis.

Removing a whole `BbdEcho` is a different kind of removal — bulk arithmetic
and an SDRAM buffer, not a libm call — so it is a genuine test of whether the
load factor generalises or was specific to the `pow`. **State the answer
either way.** If the gate row saves close to 9.16, the wrapper round's
multiplier did not generalise and the hypothesis is weakened. If it saves
materially more, that is the second independent observation of the same
effect and it changes what the remaining gap is worth.

The gate row stands at 125.24 %. Even a generous mono result leaves it above
100 %, and the round should say so plainly rather than imply otherwise.

## 9. Global constraints

- Work in the fork at `C:\Users\bernd\Documents\AI\Spotykach`, on a branch,
  never on `main` directly.
- Commit trailer is exactly
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
  with nothing after it.
- `source env.sh` before any cmake or ctest invocation.
- Build `host/vcv` ONLY via `./build-local.sh` (§5).
- Never run `python run.py` without `--profile` (`bench/README.md`).
- Build, then rebind the QSPI receipt, then measure (`bench/README.md`).
- The bench refuses hardware evidence from a dirty git tree.
- No render scenario that already exists may be modified, and neither stored
  render hash in `CMakeLists.txt` may be re-baked (§7).
- No bit-exactness or checksum-against-stored-file gates. This round changes
  the audio deliberately; the listening pass is the judgement.
- Do not tune DRIVE, the compander, or `bbd_tuning` constants in this round
  (§4). If mono makes them wrong, that is a finding for the owner's ear, not
  a fix to slip in.

## 10. Results

**Evidence:** `docs/bench/2026-07-29-4d1e929-sweep.csv` (before, commit
`4d1e929`) against `docs/bench/2026-07-29-1ba3f18-sweep.csv` (after, commit
`1ba3f18`). All figures below are `pct_max` from **run 2** of each capture,
verified by hand against the CSVs rather than carried over from the plan.
`avg_cyc` agreed in direction on every row in the table — every row whose
`pct_max` fell also has a falling `avg_cyc`, and the two rows whose `pct_max`
rose (`fx_grit`, `sweep_grit_no_bbd_mem`) also rose in `avg_cyc` — so nothing
below is an artifact of one column disagreeing with the other.

### 10.1 Per-row table

| row | before | after | Δ |
|---|---:|---:|---:|
| `instrument_worst_bbd` (the gate) | 125.24 | 112.88 | **−12.36** |
| `instrument_worst` | 115.83 | 108.03 | −7.80 |
| `fx_flux_sdram` (one deck, isolated) | 17.93 | 13.27 | −4.66 |
| `fx_grit` | 5.16 | 5.48 | **+0.32** |
| `sweep_flux_lines_2ch` | 9.17 | 9.17 | 0.00 |
| `sweep_flux_rate_0` | 17.37 | 12.81 | −4.56 |
| `sweep_flux_rate_3` | 18.00 | 13.13 | −4.87 |
| `sweep_flux_rate_6` | 19.17 | 13.74 | −5.43 |
| `sweep_flux_rate_8` | 20.38 | 14.33 | −6.05 |
| `sweep_flux_rate_11` | 21.32 | 14.79 | −6.53 |
| `sweep_stages_512` | 16.85 | 12.56 | −4.29 |
| `sweep_stages_2048` | 17.05 | 12.67 | −4.38 |
| `sweep_stages_8192` | 17.98 | 13.12 | −4.86 |
| `sweep_stages_16384` | 19.13 | 13.75 | −5.38 |
| `sweep_grit_bare` | 1.53 | 1.53 | 0.00 |
| `sweep_grit_no_bbd_mem` | 4.61 | 4.91 | **+0.30** |
| `sweep_room_lo` | 116.04 | 106.70 | −9.34 |
| `sweep_room_mid` | 116.29 | 106.96 | −9.33 |
| `sweep_room_hi` | 116.84 | 107.26 | −9.58 |

Every figure the design carried into this task ahead of measurement checked
out against the CSVs exactly as stated, with one figure worth a note rather
than a correction: §8's "naive expectation" of **9.16** points is derived from
`sweep_flux_lines_2ch`'s run-1 (before-round) reading of 9.16, halved and
doubled back to itself; this task's run-2 reading of the same row is 9.17 in
both the before and after capture. The two differ only in the bench's own
±0.01 run-to-run rounding on this row and change nothing that follows — the
naive expectation is 9.16–9.17 points either way.

### 10.2 The gate

`instrument_worst_bbd` moved from 125.24 % to **112.88 %** of the block
budget — **12.36 points**, against a naive expectation of 9.16 (half of
`sweep_flux_lines_2ch`'s 9.17, times two decks). Across both rounds of this
program: **132.79 → 125.24 → 112.88**. **12.88 points remain** to reach
100 %. That is an improvement, and it is not a pass. The instrument is still
over budget by a comfortable margin, and this round does not close the gap by
itself.

### 10.3 Does the load factor generalise? No — and the evidence is deflationary.

§8 asked directly whether the wrapper round's load-dependent saving would
repeat for a structurally different kind of removal (bulk arithmetic and an
SDRAM buffer, not a `libm` call). Per-deck savings implied by each row:
isolated `fx_flux_sdram` **4.66**, `instrument_worst` **3.90** (half of 7.80),
`instrument_worst_bbd` **6.18** (half of 12.36). That sequence — 4.66, then
3.90, then 6.18 — is **not monotonic**: the middle figure sits *below* the
isolated row, where the wrapper round's clean 1.49 / 2.16 / 3.78 rose at every
step.

The reason is in the sweep rows, and it is the primary explanation rather
than a footnote: **this round's saving grows with the operating point, not
with load.** `sweep_flux_rate_0` saves 4.56 points per line while
`sweep_flux_rate_11` saves 6.53; `sweep_stages_512` saves 4.29 while
`sweep_stages_16384` saves 5.38. A bigger, faster BBD line costs more to run,
so removing one of its two copies saves more in absolute cycles — the entire
rate ladder (4.56 → 4.87 → 5.43 → 6.05 → 6.53) and stage ladder
(4.29 → 4.38 → 4.86 → 5.38) climb with their own knob, independent of
anything else running. `instrument_worst_bbd` runs FLUX at its hottest
reachable point (STAGES at 16384, clock at the 24 kHz ceiling), and its
per-deck saving of 6.18 sits **inside** the 4.56–6.53 range the isolated sweep
rows already show at that same operating point. No load multiplier is needed
to explain the gate row at all — it is accounted for by "which point on the
knob," not "how loaded is the machine."

This **weakens the wrapper round's instruction-cache hypothesis as a general
law**, and the two rounds legitimately differ for a stated reason: the
wrapper round's own evidence found its per-deck saving **flat** across STAGES
and RATE (§9 of the cost-curves spec — "STAGES is flat at a fixed clock,
refuting a cache hypothesis"), which is exactly what let it rule the operating
point out as a confound and attribute the load-scaling it did see to a cache
effect. This round's per-deck saving is **not flat** across STAGES or RATE —
it visibly climbs with both — so the same ruling-out step is not available
here, and the honest reading is that operating-point sensitivity, not an
icache mechanism, explains everything this round measured. The hypothesis
stays confined to the wrapper round's `std::pow` call site; it does not
license predicting the size of the next removal.

### 10.4 Two rows got more expensive

`fx_grit` rose from 5.16 to **5.48** (+0.32) and `sweep_grit_no_bbd_mem` rose
from 4.61 to **4.91** (+0.30) — both rows that run no FLUX line at all. That
is roughly 6 % on a 5-point row (0.32/5.16 = 6.2 %, 0.30/4.61 = 6.5 %),
consistent with the icache/layout drift this project's own bench notes have
already documented — the same evidence file's footnote records "a cross-build
layout shift that moved a 29K-cycle workload by about 7 %." **Consistent is
not proven.** This is stated as measured; the layout explanation is offered
as the candidate and is labelled as a candidate, not a conclusion, because no
row in this capture isolates layout drift from a genuine cost change on these
two rows in particular.

### 10.5 The checksum evidence

`sweep_flux_lines_2ch` returned checksum `45b6f7aa` in both the before and
after captures — byte-identical — which Task 4's review **predicted in
advance**, on the grounds that the row's executable arithmetic was untouched
by the mono collapse (it deliberately keeps two independent bare `BbdEcho`
lines for comparability with the earlier round's figures). That prediction
holding is what confirms the row still measures what its name says rather
than having silently started measuring something else; it is worth recording
as a method note, not just a fact, because it is the thing that lets every
other number in this section be trusted.

`sweep_grit_bare` (`f57bd5c9`) and `sweep_grit_no_bbd_mem` (`9ddc20e9`) also
held their checksums across both captures. The second is the more notable of
the two: its **cost** moved (+0.30 points, §10.4) while its **checksum** did
not — precisely the signature of layout drift rather than changed work, since
a genuine arithmetic change on this row would have produced a different
checksum along with the different cost.

### 10.6 The listening result, and the open decision

From the render pair (Task 5, verbatim):

```
dry   side/mid  0.7394 -> 0.1675   (22.7 % of before)
verb  side/mid  0.7671 -> 0.3063   (39.9 % of before)
```

The reverb roughly doubles what survives (22.7 % dry vs. 39.9 % with the room
engaged), so §6's assumption — that the reverb recovers some of what a
centred echo loses — holds. But even in the realistic case, **more than half
the width is gone**: 39.9 % of before is well short of parity. The residual
width that remains is carried by the **dry** path, which is still panned per
voice exactly as before; the echo itself is centred in both renders. The
owner's panning decision is still open, and this round does not make it —
build mono, measure it, listen to it, as §1 set out. A follow-up round, if
the listening pass calls for one, gets its own spec.
