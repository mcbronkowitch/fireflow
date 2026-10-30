# CPU round 2 on Rev A: the engine kernels (SYNTH, WAVE, SAMPLER)

Follows the CPU hunt (spec `2026-10-29-cpu-hunt-remote-design.md`, record
`docs/hardware/cpu-hunt-measured.md`); this is its §4 row 11, "per-engine
kernel work". Same board (Rev A #1), same remote tooling
(`tools/remote_run.py`, `tools/remote_lib.py`), same measurement image
(`SHELL_REMOTE_MEASURE=1`) and toggle word (`engine/util/probe_toggles.h`).

Every number in this spec is either quoted from a board capture (source
named) or marked **unmeasured**. No candidate below carries a cycle
estimate: Phase A (§5) measures what sizes them, and the plan orders them
from those measurements, not from this text.

## 1. Intent

Bastian wants a **third sound source** beside the two decks: a simple
generative granular element. It cuts a sample (read from the SD card) into
pieces and every few seconds plays **one** piece, never several at once,
into the master reverb. Both decks stay free to run their own engines,
e.g. SYNTH on both, with this element as atmosphere in the background.
Neither the element nor the SD firmware is built in this round. This round
makes room for it in the audio callback.

Where the board stands (main `47f3901b`, both class-2 savings on by
default):

- Bastian's own patch: 10 minutes on the play image without a click,
  `cb_max` 853 706 (`cpu-hunt-measured.md` §6).
- The 20 worst listener workloads (listener image `dbb1b7e`, 60 s each,
  `captures/cpu-hunt-2026-10-30-top20-class2.csv`): worst `frame_max`
  944 258 (SAMPLER_SYNTH_top1), median 878 505, **9 of 20 above 880 000**.
  Clicks start at a callback window of ~945 000 (§3 of the record).

Those 9, by `frame_max` max: SAMPLER_SYNTH_top1 944 258, WAVE_SYNTH_top2
942 907, SAMPLER_WAVE_top1 937 971, SYNTH_WAVE_top1 937 063, BODY_WAVE_rnd1
909 279, WAVE_WAVE_rnd8 905 937, WAVE_WAVE_rnd9 898 757, BBD_SYNTH_top2
891 866, SAMPLER_SYNTH_top3 882 585. **Every one of them has a SYNTH or
WAVE deck** (WAVE in 6, SYNTH in 5); SAMPLER is in 3.

## 2. What Bastian decided (2026-10-30)

- **Why:** headroom for the third element (§1). The reserve also gives the
  main loop time; it will read the SD card. On the hardest workloads the main
  loop is already starved today (`cpu-hunt-measured.md` §4: 49 reports in
  60 s instead of ~60).
- **Sound may change.** "ja klangänderungen sind kein problem". Each change
  that alters the output is still heard blind against the original, as in
  the class-2 batch of the last round (*Claude's call*, carried over from
  that round's process; Bastian can waive it).
- **Firmware only is enough** for sound-changing savings: "wenn es
  einfacher ist dann reicht firmware". VCV and the desktop render keep
  today's sound.
- **More reserve rather than less.** Asked whether the round should stop at
  880 000 or keep going at the price of more listening: "wie gesagt wenn es
  sich leicht verändert kein problem". So Phase B works the whole ranked
  list, class 2 included, and 880 000 is the floor of success, not the stop
  (§3).
- **S-block is not in this round** (Claude's proposal; Bastian's answer above
  did not take it up, so the default stands until he says otherwise). If
  Phase A shows it is needed, it gets its own spec.
- The element itself plays one piece at a time, fed from SDRAM: the main
  loop loads the next piece from the card during the pause, with no live
  streaming. That is Claude's proposal from the conversation, and Bastian
  did not object. It belongs to the element's own project and binds nothing
  here except the reserve check (§3).

## 3. Goals, non-goals, anti-goals

**Goal (the floor of success).** The 20-workload listener batch of §1 rerun
on the round's final listener image: **worst `frame_max` ≤ 880 000**. That
leaves ≥ 65 000 cycles under the click line on the worst workload of today's
set.

**Stop.** Not at 880 000 (§2). Phase B works the ranked list to its end,
both classes. A candidate is dropped, not chased, when its section saving
stays inside the run spread after the rounds of §5.2. The round ends when the
list is done, or earlier if Bastian says the reserve is enough.

**Reserve check (at the end, reported, Bastian judges).** Phase A prices one
active sampler grain per sample (§5.1). It takes the price from an ablation
delta (all grains skipped against all grains run, divided by the active
grain-samples), so the probe stamps are in both arms and cancel. At the end
the record gives three numbers side by side: 96 × that price, the final
batch's worst `frame_max`, and 945 000. No margin is set here. The element's
real code is unbuilt (§9), and how much room is enough is Bastian's call
when he sees the numbers.

**Non-goals.**
- The third element, the SD firmware, SDMMC bring-up (`reva-bring-up.md`
  §11 stays deferred).
- FX chain, mix, reverb, modulation, law: the last round's ground. A saving
  found there in passing is noted, not built.
- BBD, BODY and FEED kernels. BODY and BBD appear in the 9 only paired with
  SYNTH or WAVE.
- VCV/desktop CPU. Class-1 savings reach VCV as a side effect; nobody
  measures it.

**Anti-goals** (a failure even if the number is met):
- A sound change Bastian did not hear blind, or one he heard and rejected.
- A by-ear decision undone (`docs/by-ear-decisions.md`; for the sampler
  `kSpawnHeadroom`, `kFbSatKnee`, the MOTION clamp).
- Restructuring the engine for its own sake (memory: "the code is not badly
  structured, it is undescribed"). A structural change is a candidate only
  where Phase A shows that the structure itself is the cost (§6, S-block).
- VCV or the desktop render changing sound because a firmware-only saving
  leaked into the shared path.
- BODY's output changing because `SynthEngineT` (shared with BODY) changed.

## 4. Classes of saving

As in the last round (its spec §4), with Bastian's two decisions applied:

1. **Same result, cheaper.** Bit-identical on the desktop, shown by a test
   against the old code that fails when the new path is sabotaged (the RED is
   shown once). Shared path: the change is in `engine/` for every host. It is
   judged by its section's window means on the measurement image (the ruling
   of `cpu-hunt-measured.md` §5).
2. **Audible only in A/B.** The output changes. **Firmware-only**: a `SHELL_*`
   build switch, on by default in `shell/`, compiled the old way everywhere
   else, the same pattern as `SHELL_REVERB_HALF` / `SHELL_FAST_MATH`
   (`shell/README.md`, "Turning a CPU saving back"). With the switch at 0 the
   firmware's code is the old code instruction for instruction (the shell
   checks that the same way as for the reverb). It is judged by section means
   for the cycles and by Bastian's blind A/B for the sound, batched.
3. **Plainly audible** (fewer voices, a lower grain cap, a missing
   oscillator). Listed in the record with a measured price, not built unless
   Bastian asks.

Savings that only skip idle voices or grains lower the mean, not the worst
block. They count only where Phase A shows the hot blocks have idle voices
or grains (§5.1 records active counts).

**BODY.** `SynthEngineT` serves SYNTH, WAVE and BODY; `VoiceT` serves SYNTH
and WAVE only. A change to `SynthEngineT` either leaves BODY bit-identical
(desktop test) or is gated to `VoiceT` instantiations.

## 5. How the round runs

### 5.1 Phase A: measure the kernels on today's main

No candidate is built before this. Base: the measurement image of current
main (`SHELL_REVERB_HALF=1`, `SHELL_FAST_MATH=1`, the shipping switch set;
`docs/gotchas.md` "bench/ prices a reverb and a math the board no longer
ships"). Every figure from §1 or from older captures is re-measured here
before it is used. That includes the WAVE voice bins of fault R4, which
predate fast-math.

What it must print, per deck, as window means and in the run's hottest
block:

- **SYNTH split.** `ProbeEng` (`engine/synth/synth_engine.h`) already fills
  pending, ctrl, voices and the five voice bins for `VoiceT<MorphOsc>`.
  `shell/cb_profile.cpp` reads only `Instrument::probe_wave()`, so the
  SYNTH bins have never been printed. The shell reads the active engine of
  either kind.
- **WAVE split.** As today, re-measured on the shipping switch set.
- **SAMPLER split.** It has no internal probe today. New `SPKY_PROBE_MARKS`
  stamps (compiled only into the measurement image, the same idiom as
  `ProbeEng`) are added: the control tick, scheduling and spawn, the grain
  loop and the post stage (normalisation, the two `SvfLp`, the level). The
  stamps also record the number of active grains summed over the block's
  samples. From those: **cycles per active grain-sample**, the input of §3's
  reserve check.
- **Inside a voice and a grain: ablation toggles.** These follow the FLUX
  probe of the last round (bits 1-3, measured, then reverted). Each skips
  one stage on the measurement image only, so its delta prices the stage.
  Voice: the second oscillator, the sub oscillator, the `SvfLp`, the
  envelope and pan. Grain: all grains skipped (the
  price of §3's reserve check), the Hann window, the fold in
  `SampleBuffer::read_linear`, and the buffer read itself, redirected to a
  small SRAM array as FLUX bit 3 did with DTCM. That last one tells how much
  of a grain is SDRAM. The ablations change the sound. They exist only to
  price the stages and are reverted after Phase A. Their bits are recorded
  as USED in `probe_toggles.h`.
- **Placement.** The map's free `ITCM_HOT` and `SRAM_EXEC` on the shipping
  switch set (the §2 figures of the record are from `7dc7023`, before class
  2). SYNTH's `_update_control` and `VoiceT<MorphOsc>::_apply_freq` are not
  in `shell/itcm_hot.ld` (WAVE's are, tier 2). Whether that costs anything
  is one of the Phase A readings.

Workloads: the 9 of §1, plus `bastian`, plus enough more pairings that each
of the three engines sits on both decks at least twice. Rounds: at least 3 ×
30 s per state, interleaved, because the engine section's run-to-run spread
is ±10-18 k (`cpu-hunt-measured.md` §5, the write-allocate runs).

Phase A ends with a table in the record: per engine, the share of each
stage, active voices or grains in the hot blocks, cycles per grain-sample.
From that table the candidate list of §6 is ranked and sized in the record,
and Bastian sees the ranking before Phase B starts. This is a checkpoint,
not a vote: Claude proceeds unless he objects.

### 5.2 Phase B: the candidate loop

Highest measured share first. Per candidate: toggle bit or build switch
(left to the builder), desktop test (class 1: bit-identity with a shown
RED; class 2: the switch-off build equals the old code), board A/B of
section means on the measurement image, ≥ 3 rounds × 30 s, interleaved.
Then adopt, revert, or move the candidate to the listening batch. Bits are
claimed from 10-29 and never reused (`probe_toggles.h`). After every few
adopted savings, the 20-workload listener batch is run once to see whether
the section savings reach the worst blocks. Per workload, one 60 s
`frame_max` is no verdict (`docs/gotchas.md`); across the batch it is.

### 5.3 The listening batch

All class-2 candidates that saved cycles go to Bastian in one session:
blind pairs, level-matched, A/B at random, on listener images swapped by
reflash, using the method and tools of the last batch (`cpu-hunt-measured.md`
§6). Patches: `bastian` plus workloads where the changed engine is
prominent. He decides per candidate. Accepted ones stay on by default, each
with its own `SHELL_*=0` way back.

### 5.4 Landing and ending

The usual path to `main`: branch, desktop suite in Release
(`ctest -E "^reva_"`), review, merge after Bastian's go-ahead. Then:

- the 20-workload listener batch on the final image (the goal of §3);
- the reserve check of §3;
- `bastian` 10 minutes on the final play image through the Twin, with the
  click detector, as in §6 of the last record.

Results go into `docs/hardware/cpu-hunt-measured.md` as §7 onward (or a
sibling record; left to the builder). Measured engine facts go into
`docs/engine-map.md` "Where a board block goes", traps into
`docs/gotchas.md`, status into `docs/roadmap.md` (row 11 closed or narrowed).

If the candidates run out before the goal, the round ends with the
remaining gap, the class-3 list with prices, and S-block (§6) as the next
lever, put to Bastian.

### 5.5 What needs Bastian

- **BOOT + RESET once, by hand.** The board runs the play image, which has
  no remote, so the first remote flash needs him (his permission for remote
  flashing stands: pairs, QSPI first, clean build from a committed tree, the
  persisted patch protected).
- The Apollo Twin: ask before each use.
- The listening batch (§5.3).
- The go-ahead for the merge.

## 6. Candidates (mechanisms from the code; sizes come from Phase A)

Each row names what Phase A reading decides whether it is worth building.

**SYNTH and WAVE (`VoiceT`, `MorphOsc`, `WtOsc`, `SvfLp`, `Env`)**

| # | candidate | class | sized by |
|---|---|---|---|
| V1 | Per-sample invariants out of `MorphOsc::process`: `seg = _morph * 3`, the segment choice and the `dt` clamp change only at control rate; compute them in the setters | 1 | voice bins, osc ablation |
| V2 | Idle voice cost. An idle voice is still called and returns (28-31 cycles per call on `bastian`, measurement image `50250a0`, before fast-math, CBP_WIN10). Test `active()` in `SynthEngineT::process`'s loop before the out-of-line call. Gated to `VoiceT`, or BODY shown bit-identical | 1 | active-voice count in hot blocks |
| V3 | The sub oscillator's `fast_sin` re-wraps a phase that is already in [0, 1). A pre-wrapped variant skips the `floor` and is bit-identical for that range | 1 | sub ablation |
| V4 | SYNTH's control code into ITCM tier 2 (`_update_control`, `_apply_freq`), as WAVE's already are | 1 | placement reading, ctrl split |
| V5 | Control-tick `std::pow(2, ct/1200)` per oscillator per tick (the drift moves `ct` every tick, so the guard in `set_detune_cents` rarely holds): a cheaper exp2 | 2 | ctrl split (WAVE ctrl was 10 191 per block in fault R4, `…-r4-wave-split.txt`, pre-fast-math) |
| V6 | `SvfLp` runs two passes per sample (2× oversampled); one pass with retuned coefficients | 2 | filter ablation |
| V7 | WAVE's mip crossfade (96 samples, both oscillators). Skip it when the struck voice's envelope is below a floor (nothing to click), or shorten the ramp. The struck voice's crossfade is what lifts WAVE's hot block (fault R4; crossfade bins 451-531 against decay 368-370 per voice call, `…-r4-xfade-cheap.txt`, pre-fast-math) | 2 | voice bins, crossfade count in hot blocks |

**SAMPLER (`SamplerEngine::process`, `Grain`, `SampleBuffer::read_linear`)**

| # | candidate | class | sized by |
|---|---|---|---|
| G1 | `read_linear`'s fold: `frame - fsz * floor(frame / fsz)` runs on every grain-sample. A fast path for an already in-range `frame` skips the division. Bit-identity at the edge (`frame` just below `fsz`) must be shown by test, or the fast path's bound tightened until it is | 1 | fold ablation |
| G2 | The Hann window divides per grain-sample in the ramps (`_i / _atk`, `from_end / _dec`, `_rel_ctr / _rel_len`); reciprocals latched at spawn or release | 2 | window ablation |
| G3 | `read_linear` is an out-of-line call per grain-sample (204 B in ITCM); inline it into the grain loop | 1 | grain split; ITCM room |
| G4 | SDRAM traffic: the buffer holds float stereo frames (8 B). Store int16 stereo (4 B), halving the lines a grain touches. Changes the recorded material's resolution and the record path | 2 | the SRAM-redirect ablation (only if SDRAM is a large share) |

**Structural, only on evidence**

| # | candidate | class | sized by |
|---|---|---|---|
| S-block | Render each voice or grain over a run of samples instead of one sample per call. That keeps its state in registers and removes the per-sample call chain (`Part::process` → virtual `IPartEngine::process` → per-voice `process`). Events land mid-block (strikes, chord strews, gate falls), so a run ends at the next event. This touches the engine contract `Part` relies on (`Part::_ctrl_ctr` phase lock, `part.h`) | 1 | **not built in this round** (§2). Phase A and V1-V4 record the evidence for or against it: how much of a voice-sample is call and state traffic. If the round ends short of the goal, that evidence opens its own spec |

**Class 3, priced not built:** 3 voices instead of 4 per SYNTH/WAVE deck;
a lower grain ceiling; WAVE without its second oscillator.

## 7. Constraints

- Firmware images are built by `shell/`'s make with the ARM toolchain; the
  firmware build never mixes with `env.sh` (CLAUDE.md). Clean build per image, `cmp` before flashing (memory
  `fireflow-bench-stale-object-trap`). The image's own boot print (build
  stamp) is the only proof of what runs (memory
  `fireflow-dfu-readback-useless`).
- `engine/` carries no hardware type; placement lives in `shell/` and the
  linker scripts (`shell/itcm_hot.ld`, `shell/itcm_placement.py`).
- The probe word is read once per block into `Instrument::_probe_tog`. A
  candidate tests that member and never `toggles()` itself
  (`probe_toggles.h`, enforced by `shell/test_remote_wiring.py`). Bits 30-31
  are the shell's.
- `SHELL_FAST_MATH=2` stays refused (`shell/math_guards.py`). A candidate
  must not need finite-math folding.
- Desktop bit-identity is x86/clang without FMA. The board contracts
  multiply-adds, so board bit-identity is not shown (record §5). This
  matters for no verdict, but the record says so.
- Desktop suite: `ctest -E "^reva_"` unless `hardware/` is touched. A class-2
  switch also gets a desktop build with the switch on that passes the suite.
- WAVE's `kVoices == 4` and `static_assert(kVoices <= 4)` stay
  (`synth_engine.h`).

## 8. Left to the builder

- Toggle versus build switch per candidate; switch names.
- Which ablations Phase A actually needs once the first splits are in.
- The extra workloads of §5.1 and the number of rounds beyond 3.
- The record's layout, and where in it Phase A's table sits.
- Whether a class-1 candidate whose section saving stays inside the spread
  after 3 rounds gets more rounds or is dropped.

## 9. Risks named, not solved

- **Section savings that never reach the worst block.** The worst blocks
  are hot because of strikes, sample 0 and the law (`engine-map.md`
  2026-10-30). A kernel saving lowers every sample, which helps, but the
  batch check of §5.2 is what proves it.
- **The 60 s batch is noisy.** One workload's `frame_max` moved ±135 k
  between runs. The goal is read on one batch, and a single workload at
  881 k can fail it by chance. If that happens, the workload gets a repeat
  before the round claims anything either way.
- **The reserve check rests on a model.** It uses one sampler grain as the
  third element's price. The element's real code (window, pitch, reverb
  send, SD loading in the main loop) is not built, and its cost is measured
  only when it is.
- **S-block is the big lever and the big risk.** If the per-voice rows do
  not reach the goal, the honest answer may be "restructure the per-sample
  engine contract". That decision is Bastian's, made on Phase A's numbers,
  not in this spec.
