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
phase increment, a wrap, one `std::sin`, two multiplies, per sample. It
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
measuring two engines and a crossfade. Setup must run the switch to completion
and **assert `engine_id() == ENGINE_TEST_TONE`** before the measured window
opens.

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
| **`fx_flux_hot`** | The `fx_flux_sdram` configuration with FLUX at the deck's operating point: `set_stages(1.f)`, `set_flux_rate(kFluxRateCount - 1)`, `FXT_FLUX_FB` 0.9. Everything else identical to `setup_fx_flux`, so the difference against `fx_flux_sdram` isolates the operating point and nothing else. |
| **`tone_solo`** | One `TestToneEngine`, driven through an `IPartEngine*` — `process_in` then `process`, in `Part::process`'s order — with `set_targets` pushed once per 96-sample control tick, as `Part::_control_tick` does. Prices the shell's engine so it can be subtracted. |
| **`deck_shell`** | A whole `Part` at the gate's operating point (FLOW, RATE 0.8, DENSITY 1.0, the same seed base), with `set_engine(ENGINE_TEST_TONE)` settled and every FX block off at **`fx_none`'s exact operating point**. Prices `Part`-level code plus the FX shell plus the tone. |

### 4.1 The arithmetic

Per deck, in one run, `pct_max`:

```
Part-level code   =  deck_shell − fx_none − tone_solo

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
7. Each row carries a self-check assert that would **fail** under the specific
   mistake it guards — not a band so wide the bug would pass. Round 2 shipped
   one of those and had to correct it.
8. Desktop suite acceptance is "no new failure". `tests/test_seed_audition_init.cpp`
   is already red on `main`.

## 9. Results

*To be written after the run. Every figure must cite the CSV; every causal claim
must cite the source. §5's predictions are to be reported as held or falsified,
including the ones that were wrong.*
