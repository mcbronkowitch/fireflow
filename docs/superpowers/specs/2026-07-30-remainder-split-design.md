# Splitting the remainder: compromise-free work, or contention?

**Date:** 2026-07-30
**Status:** design, awaiting owner approval
**Predecessors:** `2026-07-29-instrument-ablation-design.md` (round 1),
`2026-07-29-deck-interior-design.md` (round 2)
**Evidence to date:** `docs/bench/2026-07-29-c4ae8db-ablate.csv`

---

## 1. The question

Round 2 left **6.85 unpriced points per deck** and, in its corrected §8.1, said
plainly that this is *not yet* `Part` structure: about 2.29 of it is FLUX's own
operating-point error, in-deck block contention is unseparated, and roughly 4.5
points are genuinely unattributed.

The owner's question is sharper than "where do the points go", and it decides
what happens next:

> **Which of those points can be taken without a change anyone can hear?**

The two known cuts both cost sound. One voice per deck is worth ≈7.9 points but
takes four-voice polyphony to three. Reducing FLUX's STAGES or its rate is worth
something similar and changes the echo. The remainder is the only candidate
where the answer might be "nothing changes except the cycle count" — and it is
also the largest, at ≈13.8 points across the two decks against a 10.10-point
gap.

But "might" is doing real work in that sentence, and this round exists to remove
it. The remainder splits into two kinds of cost with opposite prospects:

- **`Part`-level code** — the per-sample loop, the control tick, the chord and
  quantizer path, the `_engine_fade` multiply. If this is where the points are,
  they are recoverable by guarding recomputes and hoisting invariants. **No
  sonic cost.**
- **In-deck contention** — what the blocks cost each other through the D-cache
  and the SDRAM bus when they run inside one `Part` instead of in isolation.
  This is not code that can be deleted. Recovering it would mean restructuring
  memory access, which is a different and much larger project.

**This round measures which of the two dominates.** It is diagnostic, like both
predecessors: it changes no engine code and optimises nothing.

## 2. Why FLUX has to be re-priced first

The remainder is a subtraction residue, so it collects every under-charge in the
rows subtracted from it. One such under-charge is already known and sized.

`setup_fx_flux` (`bench/workloads_system.cpp`) never calls `set_stages` or
`set_flux_rate`, so `fx_flux_sdram` prices FLUX at STAGES 8192, rate index 3 and
feedback 0.7. A deck runs `set_stages(1.f)` → **16384**,
`set_flux_rate(kFluxRateCount - 1)` → **index 11**, and `FXT_FLUX_FB` **0.9**
(`bench/workloads_instr.cpp`, `setup_instr_part_common`). Round 1 priced the two
axes separately in the `sweep` family and put the pair at **+2.29 points per
deck**, with the caveat that they were never measured together.

Until that is a measurement rather than an estimate carried across two builds,
any statement about the size of the remainder is carrying ±2.29 of slop — which
is the same order as the whole quantity this round is trying to split. So the
FLUX row is not an optional extra here; it is load-bearing.

**Re-pricing FLUX does not save anything.** It moves points from an
unattributed residue into a named block. Whether those points are then
*recoverable* is a separate question and an owner's question, because it means
running FLUX at a cheaper operating point, which is audible.

## 3. What reading establishes before anything is measured

### 3.1 `ENGINE_TEST_TONE` exists and is selectable

`EngineId` has `ENGINE_TEST_TONE = 0` (`engine/parts/engine_iface.h:11-17`) and
`Part::set_engine(EngineId)` is public (`engine/parts/part.h:93`).
`TestToneEngine` (`engine/parts/test_tone_engine.h`) is a sine oscillator: a
phase increment, a **single-precision divide** (`_freq / _sr`), a branchless
wrap, one `sinf` and three multiplies, per sample. The divide is named because
it is the second-costliest operation in the body after `sinf` — roughly 14
cycles on this core — and the first version of this section omitted it. It
overrides `process` and `set_targets`; `trigger` is empty; it does **not**
override `process_in`, so it inherits `IPartEngine`'s empty body — the same
situation §2.2 of round 2 established for `SynthEngineT`.

This makes it the right engine for a shell row: it is the smallest thing a
`Part` can legitimately drive, and it is short enough to read in full.

### 3.2 The switch is faded, so the shell must settle before it is measured

`Part::set_engine` does not swap immediately. `_switching` is set, and the swap
completes through `_engine_fade` (`engine/parts/part.cpp:386-393`), which
re-pushes `set_flow`, `set_hold`, `set_gate` and `set_cycle` into the
freshly-selected engine. A shell row that measures during the fade would be
measuring two engines and a crossfade.

**Corrected after task 3: the `engine_id()` assert alone does not establish
this**, and the first version of this section said it did. The fade is **385
samples** end to end (`engine/fx/fx_util.h:82-105`): `hold` forces
`_iterator = 191` and hands to `fall`, whose 191 decrements reach idle on call
192 — where the swap lands, because `Part::process` tests `is_idle()` *after*
calling `_engine_fade.process()` (`part.cpp:383-384`) — and then `rise` takes a
further 191 calls before `hold` returns exactly 1.0 on call 385. So
`engine_id()` flips **193 samples before the fade finishes**. It rules out
measuring two *engines*; it does not rule out measuring a *crossfade*.

What establishes it is the settle depth, and task 3 pins that at compile time
with `static_assert(kInstrSettleBlocks * kBlock > kEngineFadeSamples)` —
19200 > 385, so a shortened settle cannot compile. `Part` exposes no readback of
the fade at all: task 3 checked every public observer in `part.h`, and the one
level-ish getter downstream, `PartFx::tape_tap()`, is forced to exactly `0.f`
whenever FLUX is disengaged (`engine/fx/part_fx.cpp:85`), so on this row it
carries no information. Adding a getter was not an option — `engine/` is locked
(§7).

### 3.3 A shell still pays the FX shell and the engine

With every FX block off, `PartFx::process` still runs — that is exactly what
`fx_none` prices, at 2.54 per deck. And the test tone still costs a `std::sin`
per sample, which is **not** free on this target and whose size this project has
never measured.

So a shell row's cost is `Part`-level code **plus** the FX shell **plus** the
tone. Two of those three are already priced or will be, which is what makes the
third readable. This is why the round needs three rows and not two: without
`tone_solo`, `deck_shell` is uninterpretable.

## 4. The ladder

Three new rows in the existing `instr` family and `ablate` profile. All three
are additions; **no existing row is modified**, because changing a row's setup
changes its checksum and destroys comparability with the two rounds of evidence
already committed.

| row | what it is |
|---|---|
| **`fx_flux_hot`** | The `fx_flux_sdram` configuration with FLUX at the deck's operating point on **all four** axes: `set_stages(1.f)`, `set_flux_rate(kFluxRateCount - 1)`, `FXT_FLUX_FB` 0.9 **and `set_drive(0.85f)`**. DRIVE was left out of the first version of this row and added after task 1's review — see §6.6, which explains why it is not optional. A 200-block settle is required (§6.7). Everything else identical to `setup_fx_flux`. |
| **`tone_solo`** | One `TestToneEngine`, driven through an `IPartEngine*` — `process_in` then `process`, in `Part::process`'s order — with `set_targets` pushed once per 96-sample control tick, as `Part::_control_tick` does. Prices the shell's engine so it can be subtracted. |
| **`deck_shell`** | A whole `Part` at the gate's operating point (FLOW, RATE 0.8, DENSITY 1.0, the same seed base), with `set_engine(ENGINE_TEST_TONE)` settled and every FX block off at **`fx_none`'s exact operating point**. Prices `Part`-level code plus the FX shell plus the tone. |

### 4.1 The arithmetic

Per deck, in one run, `pct_max`:

**Corrected after the run — §9.2.** The formula below originally omitted
`deck_mod_hot` from the `Part`-level line. `Part::process` runs `_mod.process()`
every sample (`engine/parts/part.cpp:378`), so **`deck_shell` contains the
modulation plane**, and subtracting `deck_mod_hot` only in the `remainder'` line
charged it twice. The corrected form:

```
Part-level code   =  deck_shell − fx_none − tone_solo − deck_mod_hot

remainder'        =  instr_part_1
                     − deck_engine_hot            (voices, faithful)
                     − deck_mod_hot               (modulation, faithful)
                     − fx_none                    (FX shell)
                     − (fx_grit     − fx_none)    (GRIT)
                     − (fx_flux_hot − fx_none)    (FLUX, now faithful)
                     − (fx_comp     − fx_none)    (COMP)
                     − Part-level code

                  =  in-deck contention + anything still unnamed
```

Every difference is taken inside a single run. No baseline number from an
earlier build enters any of them.

### 4.2 What each outcome means

| if | then |
|---|---|
| `Part`-level code is large | There are compromise-free points, and they are in code this project wrote. Next round is a fix round: guard recomputes, hoist invariants. |
| `remainder'` is large | The points are contention. Not recoverable by deleting code; the voice cut becomes the realistic route to 100 %. |
| `fx_flux_hot − fx_flux_sdram` ≫ 2.29 | Round 1's estimate undershot, and the remainder was smaller than round 2 reported. |
| `remainder'` is near zero or negative | Everything is now attributed, and contention is nil or negative — blocks are no more expensive together than apart. |

## 5. Pre-registration

Round 2's §8.2 was corrected for citing predicted ranges that appear in no
committed document — they were formed after the numbers were known. These are
registered **here, before the build**, and this section is not to be edited
after the run. The result section records which held.

| quantity | predicted | falsified if |
|---|---|---|
| `fx_flux_hot` | 14.5 – 16.5 | outside 13.5 – 17.5 |
| `fx_flux_hot − fx_flux_sdram` | +1.5 – +3.5, straddling round 1's +2.29 | outside +0.5 – +4.5 |
| `tone_solo` | 0.5 – 3.0 (deliberately wide: `std::sin`'s cost on this target has never been measured here) | > 5.0 |
| `deck_shell` | 4.5 – 9.0 | > 12.0 |
| `Part`-level code | 1.5 – 4.5 | ≤ 0, which would mean something is double-subtracted and the method is broken |
| `remainder'` | 0 – 3.0 | < −1.0, which would be contention large and negative |

**The sharpest test is the sign of `Part`-level code.** It is a difference of
three measured rows, and if it comes out at or below zero the ladder has an
error in it — that outcome is to be reported as a broken method, not written up
as "the `Part` costs nothing".

### 5.1 Amendment, 2026-07-30, after task 1's review

§5 above is **not edited** — it stands as registered before the build. This
subsection records that the subject of one prediction changed after
registration, and why.

`fx_flux_hot` as registered was a **three-axis** row at DRIVE 0. Task 1's review
established that DRIVE is coupled to FEEDBACK and that DRIVE 0 puts the echo in
a self-oscillating regime a deck never runs, which `fast_tanh`'s magnitude
branch makes *cheaper* rather than merely different (§6.6). The row now sets
DRIVE 0.85 and spans four axes.

The registered band of 14.5 – 16.5 was formed against the three-axis row, so it
no longer applies unchanged. Re-registered for the four-axis row, before the
build:

| quantity | predicted | falsified if |
|---|---|---|
| `fx_flux_hot` (four axes) | 15.0 – 17.5 | outside 13.5 – 19.0 |
| `fx_flux_hot − fx_flux_sdram` | +2.0 – +4.5, against round 1's two-axis +2.29 | outside +0.5 – +6.0 |

The band moves **upward** rather than widening symmetrically, for a stated
reason: at DRIVE 0.85 the loop sits at 0.334 and the saturator evaluates its
rational form, where the withdrawn DRIVE-0 configuration would have clamped and
taken `fast_tanh`'s early return. If the measured figure comes in *below* the
old 14.5, that reasoning is wrong and the result section must say so.

### 5.2 Second amendment, 2026-07-30, after task 1's fix round

**§5.1's band shift was derived from an inverted mechanism and is withdrawn.**
§5 and §5.1 remain unedited; this supersedes §5.1's *reason* and its band, and
records the error rather than hiding it.

§5.1 argued that DRIVE 0 puts the echo in a self-oscillating regime a deck never
runs, so the four-axis row should cost *more*. Reading refutes both halves:

- **The loop gain is DRIVE-independent at the panel, by design.** `BbdEcho`'s own
  small-signal loop gain is `feedback_ * g` — its header says so in as many words
  (`engine/fx/bbd.h:531-536`) — and `Flux::set_feedback` divides
  `bbd_drive_gain()` back out of the coefficient it hands down (`flux.cpp:180`,
  `_fb_norm * _fb_scale` with `_fb_scale = 1.2 / bbd_drive_gain`). The product is
  therefore `0.9 x 1.2 = 1.08` at DRIVE 0 and `0.334 x 3.236 = 1.081` at DRIVE
  0.85. **Both bloom identically**; keeping the bloom point fixed across DRIVE is
  precisely what that division exists to do (`bbd.h:538-546`, and the measured
  0.57 -> 0.56 note at `bbd.h:171-174`). There is no regime difference.
- **The cost direction is the opposite of what §5.1 claimed.**
  `sat_in_ = bbd_drive_gain(norm) / kSatCeil` (`bbd.h:562`), so `fast_tanh`'s
  early return triggers at `|x| >= 3.282` at DRIVE 0 and at `|x| >= 1.014` at
  DRIVE 0.85. `test_input()` is a full-scale LCG signal in +-1
  (`bench/mem.cpp:81-93`), and the saturator's argument is `in + fb_state_ *
  feedback_` with `fb_state_` bounded by `sat_out_ = 0.9`. At DRIVE 0 that
  argument reaches ~2.19 after scaling and **never** clamps -- the Pade form runs
  every sample. At DRIVE 0.85 it reaches ~4.67 and clamps often, taking the
  cheap path. So the deck's DRIVE is, if anything, **cheaper per sample**.

**The fix itself stands unchanged.** The row must set DRIVE 0.85 because a deck
runs 0.85 -- faithfulness is the reason, and it was always the only sound one.
Cost direction was never a good argument for it.

Re-registered, before the build, with **no direction claimed**:

| quantity | predicted | falsified if |
|---|---|---|
| `fx_flux_hot` (four axes) | 13.5 - 17.5 | outside 12.5 - 19.0 |
| `fx_flux_hot` - `fx_flux_sdram` | +0.5 - +4.5, against round 1's two-axis +2.29 | outside -0.5 - +6.0 |

The band is wide and symmetric on purpose. **This round has now had two
mechanism-derived directional predictions refuted by reading** -- §6.6's original
"cost is zero" and §5.1's "therefore higher" -- both written by the coordinator,
both from the same habit of deriving a cost *direction* from a mechanism. Reading
is reliable for what the code does and unreliable for what it costs. The
measurement settles cost; predictions here are bounds, not directions.

**Secondary check, free to compute:** `deck_shell` must be strictly less than
`instr_part_1`, and by roughly the engine and FX difference. A shell that comes
out anywhere near 46 points means `set_engine` did not take effect and the row
is still running a `SynthEngine`. The `engine_id()` assert of §3.2 should catch
that first; if the assert passes and the number is still high, believe the
number and investigate.

## 6. Limitations, stated before the measurement

These are consequences of the method, not defects to be discovered later. Round
2's chief review finding was a residue labelled with a mechanism nobody had
measured; this section exists so that cannot happen again.

1. **`Part`-level code is measured *uncontended*.** `deck_shell` runs the
   `Part`'s own work with almost nothing competing for cache or SDRAM. Inside a
   full deck that same code may cost more. So the subtraction attributes *all*
   contention — including the `Part` code's own share of it — to `remainder'`.
   `Part`-level code as measured here is therefore a **floor** on what the code
   costs in situ, not an estimate of it.
2. **`remainder'` is contention plus interaction, not contention alone.** It is
   still a residue. It may not be labelled "contention" in the result section
   without that qualifier.
3. **Three new rows move the code layout.** Round 2's §8.5 showed the gate
   moving 0.67 points and `fx_grit` 2.84 % of `avg_cyc` at unchanged checksums,
   which **loosened** round 1's ±2 % small-row bound rather than confirming it.
   Consequently: no claim in this round may rest on a cross-build difference,
   and the result section must report which shared-row checksums held.
4. **The tone is not the synth.** Subtracting `tone_solo` from `deck_shell`
   removes the tone's cost, not "the engine's" cost. Nothing here prices what a
   `SynthEngine` costs *inside* a `Part` beyond what `deck_engine_hot` already
   established.
5. **`fx_flux_hot` prices the two axes together**, which is what a deck runs and
   what round 1 could not do. If it disagrees with round 1's additive +2.29,
   this round's figure supersedes it — but the two are from different builds, so
   the disagreement itself is subject to point 3.
6. **`fx_flux_hot` must set DRIVE, and the first version of it did not.** Task 1
   priced STAGES, the flux rate and FEEDBACK but left DRIVE at `Flux::init`'s
   `set_drive(0.f)`, while a deck runs `set_drive(0.85f)`
   (`bench/workloads_instr.cpp:108`). Task 1's review established that this is
   not a documentation footnote but a regime error, and the row was corrected:

   `Flux::set_drive` rewrites `_fb_scale = 1.2f / bbd_drive_gain(d)`
   (`engine/fx/flux.cpp:199`), so DRIVE and FEEDBACK are **coupled**. At the
   deck's DRIVE 0.85, `bbd_drive_gain` is 3.236 and the echo's actual
   coefficient is `0.9 × 1.2 / 3.236 = 0.334` — comfortably stable. At DRIVE 0
   it is `0.9 × 1.2 = 1.08`, above unity: a self-oscillating loop bounded only
   by the saturator. Those are two different regimes, and they do not cost the
   same, because `fast_tanh` **branches on magnitude** — it early-returns ±1 for
   `|x| >= 3.646739f` and skips the Padé numerator, denominator and divide
   (`engine/util/fast_tanh.h:36-37`). A clamped, oscillating loop takes the
   cheap path; a stable one at 0.334 evaluates the rational form. So DRIVE 0
   would have made the row **cheaper than the FLUX a deck actually runs**, which
   is the opposite of what this row exists to establish.

   The earlier claim in this section — that DRIVE "adds no operation" so its
   cost "is **zero**" — was wrong on both counts and is withdrawn. The correct
   statement about the *operation set* is that DRIVE adds none and can only
   remove one (the clamp branch), so its direct cost is ≤ 0; but via the
   coupling it moves which branch the saturator takes on every sample, and that
   is not bounded by reading.

   **Corrected again after task 1's fix round -- see §5.2.** The "two different
   regimes" framing above is also wrong and is withdrawn. `Flux::set_feedback`
   divides `bbd_drive_gain()` back out (`flux.cpp:180`), so the panel-level loop
   gain is 1.08 at DRIVE 0 and 1.081 at DRIVE 0.85 -- DRIVE-independent by design
   (`bbd.h:531-546`). The saturator's *threshold* does move, and in the direction
   that makes the deck's DRIVE the **cheaper** path, not the dearer one. **The row
   must set DRIVE 0.85 because a deck runs 0.85** -- faithfulness, not cost
   direction. Read the two paragraphs above as a record of arriving at the right
   requirement via two wrong reasons.

   With DRIVE set, `fx_flux_hot − fx_flux_sdram` spans **four** axes, where
   round 1's +2.29 estimate spanned two (STAGES and rate only —
   `2026-07-29-instrument-ablation-design.md` §8.3, and note there is no DRIVE
   row anywhere in the `sweep` family). The comparison against +2.29 is
   therefore four-axes-against-two and must be reported that way.
7. **The row needs a settle, and 100 blocks of warm-up is not enough.** Also
   from task 1, and confirmed independently in its review. `_stage_current`
   slews at `_dt_coef = 1/1440` (`engine/fx/flux.cpp:18`), so
   `8192 · (1 − 1/1440)^n < 1` first holds at n = 12972 samples = **135.1
   blocks**, against the runner's `kWarmupBlocks` of 100 (`bench/workload.h:11`,
   applied at `bench/runner.cpp:28`). Without a settle the first ~35 *measured*
   blocks would price a BBD whose line length was still moving. `clock_hz()` is
   also written only inside `Flux::process` (`flux.cpp:370`) and would still be
   its `0.f` initialiser (`flux.h:144`), so the row's own asserts could not
   evaluate. `fx_flux_sdram` needs no settle because it moves neither axis and
   `init` snaps both slews (`flux.cpp:75,78`) — so the settle does not reduce
   comparability, it is what puts both rows in a settled state.

8. **`Part` has a second `set_targets` path, and only `deck_shell` pays it.**
   Found by task 2's implementer; neither this design nor the plan had mentioned
   it. The control raster is `if (_ctrl_ctr == 0) { … _control_tick(); } else if
   (fired) { _control_tick(); }` (`engine/parts/part.cpp:439-445`), so a lane
   fire triggers an **extra** tick — and therefore an extra
   `_engine->set_targets` (`part.cpp:335`) — outside the 96-sample raster. At the
   gate's RATE 0.8 a fire lands roughly every 6908 samples, about 72 blocks, so a
   deck makes ~1.4 % more `set_targets` calls than `tone_solo` does.

   **It is harmless because of its size, not because it is correctly
   attributed** — an earlier version of this point claimed the latter and was
   refuted in review. The extra tick is a whole `_control_tick()`, whose last two
   acts are `_engine->set_targets` (`part.cpp:335`) and the FX target-cache fill
   (`part.cpp:336`); neither is `Part`-level, so `deck_shell − tone_solo` really
   does charge one extra *engine* push and one extra *FX* cache fill to
   "`Part`-level code". The bound is what makes that acceptable:
   `TestToneEngine::set_targets` is ~76 bytes including a `powf`, so at most a
   few hundred cycles, once per 6908 samples — of order **7e-4 points**, four
   orders of magnitude under §5's prediction band and far under the layout drift
   §6.3 already concedes. Reproducing it in `tone_solo` would need a live
   `lane_fired()` edge, i.e. `deck_mod_hot`'s modulator running inside the
   measured loop, which §3.2 rules out because it would charge that row twice.
   §9 must not describe `tone_solo` as "the same engine driving as a deck does"
   without this qualifier.
10. **§4.1's three-term subtraction removes the harness overhead twice.** Found
   in review, and it is a defect in this design rather than in any row. Write
   each row as harness plus parts: `deck_shell = H + P + F + E`,
   `fx_none = H + F`, `tone_solo = H + E`. Then
   `deck_shell − fx_none − tone_solo = P − H`. The per-row harness — the
   `bl test_input()`, the loop counter, compare and branch, the accumulation,
   the prologue — is subtracted twice and present once, so **`Part`-level code
   comes out low by one `H`**. Measured off `proc_tone_solo`'s disassembly, `H`
   is of order **0.08 points** against §5's 1.5–4.5 band: a 2–5 %
   underestimate. It does not threaten the sign test, and it pushes in the same
   direction as §6.1's floor, but §6.1 attributes that floor to contention
   alone, so this is a second and independent reason the figure is a floor. §9
   must say so when it shows the arithmetic.
9. **`tone_solo` holds its pitch target constant; a deck's walks.**
   `TestToneEngine::set_targets` calls `std::pow(8.f, p)`
   (`engine/parts/test_tone_engine.h:22`) once per control tick. This row pushes
   one fixed value, where a deck's PITCH target moves along the quantizer
   staircase. Whether `powf`'s cost depends on its argument is **not** something
   reading settles, and per §5.2's rule this design does not guess a direction.
   The exposure is bounded — one `powf` per 96 samples — and it is named here so
   §9 reports it as an approximation rather than discovering it later.

## 7. Non-goals

- **`engine/` is locked.** `git diff main -- engine/` must be empty at every
  commit. The owner chose a strictly diagnostic round.
- **No fixes, even obvious ones.** One candidate is already open and is *not*
  to be touched here: `Flux::set_rhythm` (`engine/fx/flux.cpp:299`) runs
  `update_thin_pattern()` and `derive_intervals()` unguarded, twice per control
  tick, including at LINK 0 where nothing has changed. If `deck_shell` shows
  `Part`-level code is hot, this is the first thing a *later* fix round should
  look at. Naming it is in scope; guarding it is not.
- **No re-measurement of anything rounds 1–2 settled.** The four engines cost
  memory and no CPU; inter-deck contention is nil at deck granularity; DENSITY
  and `set_tempo_bpm` are inert at the gate's operating point. Do not spend a
  row rediscovering these.
- **No bit-exactness gate.** Cross-run checksum agreement within this round is
  required, as always; comparison against stored bytes is not.

## 8. Protocol

Unchanged from round 2, and it is not optional:

1. Register each row at all four points: the `workloads_instr.cpp` table,
   `run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY`, `bench/test_run_contract.py`,
   and — only if a new family were introduced, which it is not — `families.cpp`,
   the `Makefile` and `profiles.py`. `run.py` compares the row *set*, not its
   order.
2. `source ./env.sh` in the same shell command as any cmake/ctest/bench call.
3. **Never** run `python bench/run.py` without `--profile`; the default `full`
   fails to link by design.
4. Build, then rebind the QSPI receipt, then measure — in that order. A
   "verification receipt does not match current payload (artifacts)" error names
   the artifacts, not the bank: it is a stale binding, not corrupt flash.
5. The bench refuses hardware evidence from a dirty tree. Commit before
   measuring.
6. Two runs; every shared checksum must match across them.
7. **Re-verify in the *measured* build that `tone_solo`'s and
   `deck_engine_hot`'s three engine calls are still indirect** — one
   `objdump -d` on `proc_tone_solo`, checking for `blx` through vtable offsets
   12 / 44 / 20. This is not paranoia: in the task-2 build GCC devirtualised
   *and* fully inlined `setup_tone_solo`'s `g.engine->init()` call, sixty lines
   above the measured loop. That instance is harmless, but it proves the
   compiler will do it here when it can, and adding `deck_shell` shifts layout
   and inlining budgets. Nothing in the harness guards this; only a look at the
   object code does.
8. Each row carries a self-check assert that would **fail** under the specific
   mistake it guards — not a band so wide the bug would pass. Round 2 shipped
   one of those and had to correct it.
8. Desktop suite acceptance is "no new failure". `tests/test_seed_audition_init.cpp`
   is already red on `main`.

## 9. Results

**Evidence:** `docs/bench/2026-07-30-ccd5f12-ablate.{md,csv}`, run 2, `pct_max`
unless stated. Two runs; every difference below is taken inside one run, and no
figure from an earlier build enters any of them.

### 9.1 The answer: it is `Part`-level code, not contention

| per deck | `pct_max` | `pct_avg` |
|---|---:|---:|
| voices — `deck_engine_hot` | 18.57 | 17.93 |
| modulation — `deck_mod_hot` | 3.76 | 3.50 |
| FX, FLUX now faithful | 18.48 | 18.35 |
| **`Part`-level code** | **4.00** | **2.65** |
| sum of parts | 44.81 | 42.43 |
| `instr_part_1` | 45.65 | 43.54 |
| **remainder' = contention + unnamed** | **0.84** | **1.11** |

**The deck is now accounted for.** `Part`-level code is **4.00 points per deck,
8.00 across the instrument**, against a gap of **10.51** in this build. What is
left over — contention plus anything still unnamed — is **0.84 per deck, 1.68
across the instrument**.

That answers the question §1 was written to ask. Of round 2's 6.85-point
residue, the part that is **code this project wrote, carrying no sonic cost**, is
the large majority; the part that is blocks costing each other cache and bus —
the part no amount of guarding recomputes would recover — is under a point per
deck. **Contention was not the answer.**

Two figures must travel with that headline. `Part`-level code is a **floor**
(§6.1): `deck_shell` runs the `Part`'s own work with almost nothing competing
for cache or SDRAM, so all contention lands in `remainder'`, and §6.10's harness
term biases it by roughly another +0.08. And the `pct_avg` reading is 2.65, not
4.00 — `deck_shell` has a 15 % avg-to-max spread (10.79 → 12.46) where `fx_none`
and `tone_solo` are nearly flat, so subtracting flat rows from a spiky one gives
a larger figure in `pct_max` than in `pct_avg`. **The honest range for
`Part`-level code is 2.65–4.00 per deck, 5.3–8.0 across the instrument.** The
spread itself is a finding: something in a `Part` costs once per block, and the
control raster — chord, quantizer, FX target cache — is where to look.

### 9.2 §4.1's formula was wrong, and it falsified two predictions on its own

**Reported before interpretation, because §5 required it.** As registered, two
predictions failed:

| quantity | registered | measured | verdict |
|---|---|---:|---|
| `deck_shell` | 4.5 – 9.0, falsified > 12.0 | **12.46** | **falsified** |
| `remainder'` | 0 – 3.0, falsified < −1.0 | **−2.92** | **falsified** |
| `Part`-level code | 1.5 – 4.5, falsified ≤ 0 | **7.76** | outside band, not falsified |
| `fx_flux_hot` (§5.2) | 13.5 – 17.5 | 14.76 | held |
| `fx_flux_hot` − `fx_flux_sdram` (§5.2) | +0.5 – +4.5 | **+1.30** | held |
| `tone_solo` | 0.5 – 3.0 | 2.16 | held |

**One root cause explains all three misses, and it is in §4.1, not in any row.**
`Part::process` calls `_mod.process()` every sample
(`engine/parts/part.cpp:378`), so `deck_shell` — a whole `Part` — **contains the
modulation plane**. §4.1 subtracted `deck_mod_hot` only in the `remainder'` line,
so the modulation was charged **twice**: once inside `Part`-level code and once
beside it. That inflated `Part`-level code by `deck_mod_hot` (3.76) and drove
`remainder'` the same amount negative.

The three misses collapse to one when the modulation is removed once:

- `deck_shell` − `deck_mod_hot` = **8.70**, inside the registered 4.5–9.0. The
  band was right about the quantity; §4.1 was wrong about the row.
- `Part`-level code = 12.46 − 2.54 − 2.16 − 3.76 = **4.00**, inside 1.5–4.5.
- `remainder'` = **+0.84**, inside 0–3.0.

§4.1 now carries the corrected formula with the error recorded in place. §5,
§5.1 and §5.2 are untouched, as registered. **The measurement was not at
fault: every row returned what it was built to return, and the defect was in
the coordinator's arithmetic.** This is the tenth such defect in this round's
own documents against zero in the three rows.

### 9.3 FLUX's operating point costs less than round 1 estimated

`fx_flux_hot` − `fx_flux_sdram` = **+1.30** (`pct_max`; +1.34 `pct_avg`) — the
four-axis difference, measured together on one build. Round 1 estimated **+2.29**
from two axes swept separately on a different build
(`2026-07-29-instrument-ablation-design.md` §8.3), so **round 1 overestimated
FLUX's share of the residue by roughly a point per deck**, and round 2's §8.1,
which carried that estimate forward, overstated it correspondingly. The
comparison is four-axes-against-two and across builds, so it is subject to §6.3;
the within-run +1.30 is the figure to use.

Consequence for round 2's arithmetic: its 6.85-point residue is now split into
FLUX's real +1.30, `Part`-level code at 2.65–4.00, and 0.84 of contention plus
unnamed — with the balance absorbed by this build's drift (§9.5) and by the
harness terms of §6.10.

### 9.4 What the rows themselves showed

- **`tone_solo` = 2.16.** Two virtual dispatches, one `sinf`, one `vdiv` and
  three multiplies per sample, plus one `powf` per 96 samples (§6.9's
  unbounded term). It is 17 % of `deck_shell`, so the shell is not
  tone-dominated.
- **`deck_shell` = 12.46 against `instr_part_1`'s 45.65.** The reader's sanity
  check of §4 passes with room: a figure near 46 would have meant the engine
  switch never took. It did — `engine_id() == ENGINE_TEST_TONE` held, and the
  fade is cleared 49.9× by the 200-block settle (§3.2).
- **Dispatch re-verified in the measured build**, as §8 item 7 requires.
  `objdump -d` on `proc_tone_solo` shows three `blx` through vtable offsets
  **12 / 44 / 20** with the engine pointer reloaded from `[r4, #20]` before each;
  `proc_deck_engine_hot` likewise. Nothing devirtualised in the build these
  numbers come from.

### 9.5 Checksums held; the drift did not shrink

**All 20 rows shared with `2026-07-29-c4ae8db-ablate.csv` returned identical
checksums**, in both runs of this build and against the previous one. The two
runs differ in cycles on 15 rows and in checksum on none.

| row | c4ae8db | ccd5f12 | Δ `pct_max` | Δ `avg_cyc` |
|---|---:|---:|---:|---:|
| `instrument_worst_bbd` (gate) | 110.10 | 110.51 | +0.41 | +0.31 % |
| `instr_part_1` | 46.00 | 45.65 | −0.35 | −0.80 % |
| `fx_flux_sdram` | 13.12 | 13.46 | +0.34 | **+2.65 %** |
| `mod_plane_2x_center` | 7.42 | 7.25 | −0.17 | **−2.56 %** |
| `oliverb_solo_sram` | 9.66 | 9.49 | −0.17 | −1.90 % |
| `fx_comp` | 3.26 | 3.36 | +0.10 | +1.29 % |
| `deck_engine_hot` | 18.61 | 18.57 | −0.04 | −0.03 % |
| `synth_2x4` | 35.83 | 35.82 | −0.01 | −0.02 % |

Round 2 found the predecessor's ±2 %-on-a-small-row bound **loosened**; this
round does not tighten it. Two rows again exceed ±2 % of `avg_cyc` at unchanged
checksums, across a build change of three appended rows. **The gate moved +0.41
and the gap is 10.51 here, not 10.10** — which is why every figure in §9.1 comes
from one run and none from a subtraction across builds.

### 9.6 What is worth doing next

1. **A fix round on `Part`-level code — the first non-diagnostic round in this
   sequence.** 5.3–8.0 points across the instrument, no sonic cost, and now
   named rather than inferred. Start where the once-per-block spread points: the
   control raster. The candidate already on the table is
   `Flux::set_rhythm` (`engine/fx/flux.cpp:299`), which runs
   `update_thin_pattern()` and `derive_intervals()` unguarded twice per control
   tick, including at LINK 0 where nothing has changed (§7 forbade touching it
   here). Guarding recomputes on "did the value move" is the pattern.
2. **Stop asking about contention.** 0.84 per deck is inside the noise this
   project has been fighting all along, and three rounds have now failed to find
   contention anywhere: nil between decks (round 1), nil at deck granularity
   (round 2), under a point inside a deck (here). It is not where the budget
   went.
3. **The voice cut remains the fallback**, at ≈7.9 points for four-voice
   polyphony going to three (round 2 §8.3) — and it is now clearly the *second*
   option, not the first, because `Part`-level code is of comparable size and
   costs nothing to hear.

**The gap is 10.51 and the compromise-free bucket is 5.3–8.0.** That is not a
guaranteed close, and none of those points is free to take — a `Part`'s
per-sample loop cannot be deleted, only tightened. But for the first time in
three rounds the largest named quantity is code rather than a residue.
