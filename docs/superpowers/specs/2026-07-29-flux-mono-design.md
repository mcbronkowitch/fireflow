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
