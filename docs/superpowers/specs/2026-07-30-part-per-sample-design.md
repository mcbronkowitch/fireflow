# Design: the per-sample cost of `Part::process`

**Round 4 of the CPU sequence — the first non-diagnostic round.**
**Branch:** `perf/part-per-sample`
**Date:** 2026-07-30
**Predecessors:** `2026-07-29-instrument-ablation-design.md` (round 1),
`2026-07-29-deck-interior-design.md` (round 2),
`2026-07-30-remainder-split-design.md` (round 3)

---

## 1. Why this round exists

Round 3 answered the question the sequence was opened to ask. The deck's residue
is not contention; it is code. Its §9.1, quoting
`docs/bench/2026-07-30-ccd5f12-ablate.csv` run 2:

| per deck | `pct_max` | `pct_avg` |
|---|---:|---:|
| voices — `deck_engine_hot` | 18.57 | 17.93 |
| modulation — `deck_mod_hot` | 3.76 | 3.50 |
| FX at the deck's operating point | 18.48 | 18.35 |
| **`Part`-level code** | **4.00** | **2.65** |
| contention + unnamed | 0.84 | 1.11 |

The gate `instrument_worst_bbd` reads **110.51** `pct_max` / **106.50** `pct_avg`
in that build, so the overrun is **10.51** / **6.50**. `Part`-level code doubled
across the instrument is **5.3–8.0**, three quarters of it, and it carries no
sonic cost by construction: it is the `Part`'s own bookkeeping, not a voice, not
a filter, not a reverb tail.

Three rounds have been diagnostic. This one changes code. `engine/` is open for
the first time, which is also the first time a mistake here can be *heard*.

## 2. What round 3 got wrong about where to start

Round 3's §9.6 nominated `Part::_control_tick` (`engine/parts/part.cpp:182-374`)
and told the next round to start there. **That is the wrong half**, and the
reason is a cadence, not a cost estimate:

- `_control_tick` runs **once per 96 samples**. `SynthEngine::kCtrlInterval` is
  96 (`engine/synth/synth_engine.h:36`) and the bench's block is 96
  (`bench/workload.h:8`), so exactly one tick lands in each measured block.
- The body of `Part::process` runs **96 times** per block.

For the tick to hold a comparable share of `Part`-level code, one tick would have
to cost about ninety-six times one loop pass. The tick is 1404 bytes
(`Part::_control_tick`, `nm` on the measured ELF) of table and integer work with
**no libm call on the synth path** — `engine/pitch/quantizer.h` and
`engine/pitch/chord.h` are header-only and contain no `pow`, `exp`, `log`, `sin`
or `cos`, and `target_raw`'s single `std::pow` (`part.cpp:100`) is reached only
when `slot == LANE_SOURCE && _engine_id == ENGINE_SAMPLER`. The loop pass is 200
instructions. A 96× ratio between those two is not credible.

**That is an argument from cadence and code size, not a measurement, and this
design does not treat it as one.** §5 registers a test that fails loudly if the
per-sample half is *not* where the cost is.

Two further §9.6 candidates are withdrawn here, both from reading the measured
ELF:

- **`Flux::set_rhythm` is a small fish.** It is 180 bytes *in total*, with
  `derive_intervals` inlined into it. Round 3 already corrected §7's claim that
  it is in `Part`-level code at all (it is called only from
  `engine/instrument.cpp:96-97`); what is added here is that even in its own
  bucket it is too small to matter. `update_thin_pattern`
  (`engine/fx/flux.cpp:226-237`) is a two-iteration loop with one float divide
  each — of order a hundred cycles per block, unmeasurable.
- **`_mod.master_hz()` and `_mod.lane_fired()` are not targets.** Both are inline
  one-line getters (`engine/mod/super_modulator.h:74` and `:69`) — a load and a
  compare.

## 3. What the disassembly shows

Read from `bench/build/bench.elf` as linked for the round-3 measurement, so the
code below is the code those numbers came from.

### 3.1 There is no loop in `Part::process`

`arm-none-eabi-objdump` shows backward branches at `0x2400e9d4` and `0x2400e8b0`,
which look like a loop in a function that processes one sample. They are not.
`Part::process` returns at `0x2400e9b8`
(`ldmia.w sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}`); everything above that
address is out-of-line **cold** blocks that rejoin the hot path — ordinary block
reordering. The hot region is `0x2400e738`–`0x2400e9b8`, 640 bytes.

This is recorded because it was briefly mistaken for a loop during design, and a
loop there would have invalidated every per-sample figure in this document.

### 3.2 The hot region is 200 instructions, and the overhead is structural

| per sample, ×96 per block | count | note |
|---|---:|---|
| instructions in the hot region | **200** | 120 of them 32-bit encodings |
| accesses to the `Part` object at a large offset | **22** | base re-derived every sample |
| register save / restore instructions | **6** | see below |
| 4-way `tbh` dispatch on the engine-fade state | 1 | `0x2400e764` |
| `vmul` by the fade value | 2 | at hold the value is exactly 1.0 |

The prologue and epilogue are:

```
2400e738:  stmdb  sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
2400e748:  vpush  {d8-d9}
2400e74c:  sub    sp, #28
   ...
2400e9b2:  add    sp, #28
2400e9b4:  vpop   {d8-d9}
2400e9b8:  ldmia.w sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
```

Nine core registers and two double-precision registers saved and restored, plus a
28-byte frame, **for one sample**. On Cortex-M7 a nine-register `STM`/`LDM` is
roughly one cycle per register plus overhead and the `LDM`-to-`pc` form pays a
pipeline refill, so the pair is of order **20–35 cycles**. Multiplied by 96 that
is **1,920–3,360 cycles per block = 0.20–0.35 % of the block budget per deck,
0.40–0.70 % across the instrument** — for work that is pure bookkeeping and that
one call per block would pay once.

That range is an ISA figure, not a fit, which is why this design is willing to
state it. It is still an upper bound in one respect: the M7 dual-issues and has a
store buffer, so some of it overlaps neighbouring work.

The object accesses have a visible cause. The `Part` object is large enough that
the compiler emits `add.w r4, r0, #20480` on entry and then reaches the
per-sample hot state — `_ctrl_ctr`, `_gate_ctr`, `_last_gate`, `_switching`, the
fade state — at offsets around `0xd28`–`0xd4c` *beyond* that base. Every touch is
a 32-bit `ldr.w`/`str.w`, and the base is recomputed on every one of the 96
calls.

### 3.3 What the four indirect calls in the hot path are

Identified by vtable offset, so that later work does not mistake one for
`Part`-level cost:

| address | vtable offset | call | cadence |
|---|---|---|---|
| `0x2400e8ee` | 24 | `set_cycle(1.f/hz)` | cold — only when `master_hz` moves |
| `0x2400e942` | 48 | `set_gate(g)` | cold — only on a gate edge |
| `0x2400e96e` | 44 | `process_in(inL, inR)` | every sample |
| `0x2400e97c` | 20 | `process(outL, outR)` | every sample |

The last two are charged to `tone_solo` (round 3 §3.2) and therefore subtract out
of `Part`-level code. `PartFx::process` (`0x2400e9ae`) subtracts via `fx_none`,
and `SuperModulator::process` (`0x2400e756`) via `deck_mod_hot`.

### 3.4 A dead store in `_control_tick`

`part.cpp:183` writes `_tg[LANE_PITCH] = target_raw(LANE_PITCH)`. `part.cpp:210`
overwrites it with `clampf(_pitch_q + _detune_cents * (1.f/3600.f), 0.f, 1.f)`.
Between the two, nothing reads `_tg[LANE_PITCH]`: `:185` calls
`pitch_pre_quant()`, which computes `target_raw(LANE_PITCH)` *again* from members
rather than reading `_tg`, and `:192` passes that value to `_quant.process` as an
argument. So `target_raw` (172 bytes) runs six times per tick where five would
do, and the sixth result is discarded.

This is small — one call per block, of order a hundred cycles — and is included
because it is free and provably neutral, not because it moves the total.

### 3.5 There is room to grow code

Checked because §4's largest item grows `.text`, and the bench's memory line
reads 97.83 %, which looks like no headroom at all. It is the wrong region.

Figures as the linker reports them (`--print-memory-usage`), not as computed from
`alt_sram.lds` — the `SRAM` region ends up 512 bytes smaller than its `MEMORY`
length, so the computed share would be wrong:

| region | region size | used | share |
|---|---:|---:|---:|
| `SRAM_EXEC` — carries `.text` | 262,880 | 194,976 | **74.17 %** |
| `SRAM` — carries the bench arena `.bss` | 261,408 | 255,744 | 97.83 % |

`alt_sram.lds:16-17` defines the split and `:39-55` places `.text` in
`SRAM_EXEC`. The 97.8 % figure belongs to the arena, which is data and cannot be
affected by inlining. **SRAM_EXEC has about 66 KB free**, against the 1–4 KB that
§4's item 4 would add.

## 4. The candidates

Stage 1 is bit-exact: every item must leave the audio unchanged bit for bit, so
the bench's own checksums are a hard gate. Stage 2 is entered only if Stage 1
falls short, and every Stage 2 item needs a listening decision.

### Stage 1, bit-exact

**Item 4 first.** It is both the largest single item and the only one whose value
can be predicted from the ISA, which makes it this round's instrument as well as
its first fix. The others follow only if it behaves as §5 registers.

4. **Make the per-sample body available for inlining at every call site.** The
   prologue and epilogue of §3.2 exist because `Part::process` is an out-of-line
   call in another translation unit. Moving the per-sample body where callers can
   inline it removes the register save/restore entirely and makes the object base
   loop-invariant in the caller. Predicted saving: §3.2's 0.40–0.70 % across the
   instrument. Cost: `.text` growth at each inlined call site, bounded by §3.5's
   66 KB.

   **The mechanism must be "visible to every caller", not "special-cased for
   `Instrument`".** `deck_shell` calls `part.process(...)` directly from
   `bench/workloads_instr.cpp`, not through an `Instrument`, so a change that only
   helps `engine/instrument.cpp` would leave `deck_shell` unchanged — and
   `deck_shell` is the row that isolates one `Part`. §5.1 predicts a movement on
   both rows, and that prediction is only coherent if the body is inlinable
   wherever it is called.

1. **Group the per-sample hot members.** `_ctrl_ctr`, `_gate_ctr`, `_last_gate`,
   `_switching`, `_note_suppressed`, `_last_master_hz` and the engine-fade state
   sit past a 20 KB base (§3.2). Declaring them together near the start of the
   object shortens the encodings and puts them on one or two cache lines. **No
   logic changes at all** — this is a declaration reordering, bit-exact by
   construction.

2. **Remove the dead `target_raw(LANE_PITCH)`** (§3.4).

3. **Skip the fade multiply at hold.** `_engine_fade.process()` returns exactly
   1.0 at hold (`engine/fx/fx_util.h:82-105`, and the M1.6 bypass invariant
   depends on it), so `outL *= fade; outR *= fade;` is two wasted `vmul`. Guarding
   them on the fade being at hold is bit-exact because `x * 1.0f == x` for every
   float including the specials this signal path can carry.

### Stage 2, numerically different — only if Stage 1 falls short

Not designed here beyond naming the shape and its hazard, because whether it is
needed depends on Stage 1's measurement.

- **A true block entry point**, so that the counters and the fade state stay in
  registers across 96 samples rather than being reloaded.
  **The hazard, stated now: this is not bit-exact.** `Instrument::process`
  interleaves the two decks *per sample*, and CHOKE reads
  `_parts[pri].gate()` and `max_voice_env()` each sample to decide the yielding
  deck's window (`engine/instrument.cpp:110-121`). Processing one deck's whole
  block before the other changes CHOKE's sample-accurate behaviour. Any such
  change goes to the owner's ear, not to a checksum.

## 5. Pre-registration

**Registered before the first build. Do not edit this section, including in §9.**
Round 3's §9.2 found that its registered "sharpest test" was structurally the
wrong instrument, because the quantity it tested was the one inflated by the
error. The lesson applied here: the test below is on the item whose value is
predicted independently, and it is two-sided.

### 5.1 The model test — item 4 alone

Item 4 removes §3.2's register save/restore and nothing else. Two independent
predictions, and **both** must hold for the model behind this round to stand.

**Static, and exact.** In the build after item 4, the per-sample path reached from
`Instrument::process` contains **no** nine-register `stmdb`/`ldmia` pair and no
`vpush {d8-d9}`/`vpop {d8-d9}` pair per sample. This is verified with `objdump`,
not with the bench, and it is a yes/no question about the emitted code.
**Falsified if the pair is still emitted per sample** — in which case the change
did not do what it claims and no cycle figure from it means anything.

**On hardware.** Against the same-session baseline of §6.2:

| quantity | predicted | falsified if |
|---|---|---|
| `deck_shell` (one `Part`) | −0.20 to −0.35 | outside −0.05 … −0.60 |
| `instrument_worst_bbd` (two `Part`s) | −0.40 to −0.70 | outside −0.10 … −1.20 |
| every control-group row (§6.2) | 0.00 ± drift | any row moves more than the largest control-group mover |
| every shared checksum | unchanged | **any** checksum changes |

A checksum change is not a near-miss; it means a bit-exact change was not bit
exact, and the item is reverted rather than reinterpreted.

### 5.2 The residue test

Round 3's §9.2 concluded that a future round should register the **residue's**
behaviour rather than a component's sign. The residue here is the part of
`Part`-level code that Stage 1 does *not* recover.

`Part`-level code is 2.65–4.00 per deck. Only three of Stage 1's four items can
be predicted at all:

- item 4: **0.20–0.35** per deck, from the ISA (§3.2);
- items 2 and 3 together: **0.02–0.06** per deck — each removes a countable
  handful of instructions at a known cadence;
- **item 1: no prediction is registered.** It changes no instruction count. It
  changes instruction *encodings* and data-cache locality, and this project's own
  rule (round 3 §5.2, after two directional predictions were refuted) is that a
  cost direction is never derived from a mechanism. Item 1 could be worth nothing
  or could be worth more than item 4, and reading cannot say which.

**So the predictable part of Stage 1 recovers 0.22–0.41 per deck — between 6 %
and 16 % of `Part`-level code — leaving a residue of 2.2–3.8 per deck.** That is
registered as the expected outcome, not as a disappointment: if Stage 1 lands
there, the round has confirmed its model and established that the remaining cost
is in the 200 instructions themselves rather than in the call overhead around
them.

**Falsified if items 2, 3 and 4 together recover more than 1.00 per deck.** That
would mean the per-sample overhead is far larger than the ISA allows and the
disassembly in §3 is being read wrong. **Item 1 cannot falsify this**, by the
same reasoning that gives it no prediction — a large yield there is a surprise
about layout, not a failure of this model, and §9 must report it as such rather
than folding it into the model's score.

### 5.3 What this round does not predict

No prediction is made about how much of `Part`-level code is ultimately
recoverable, and none about Stage 2. Round 3's rule stands: this project's
reading is reliable for what code *does* and unreliable for what it *costs*, so
directions are not claimed where the bench can settle them.

## 6. Limitations

1. **The expected effect is at the edge of what this bench resolves.** Item 4's
   predicted 0.40–0.70 on the gate sits against a cross-build layout drift that
   round 3 §9.5 measured at **+0.41** on that same row, at unchanged checksums,
   for a build that only appended bench rows. The evidence file itself records
   "a cross-build layout shift that moved a 29K-cycle workload by about 7 %".
   **Every before/after in this round is necessarily a cross-build comparison**,
   which is exactly what round 3 §6.3 forbade resting a claim on. §6.2 is the
   mitigation, and §5.1's static test is the reason the round does not depend on
   the hardware delta alone.
2. **The control group is what makes the comparison legitimate.** A change inside
   `Part` cannot affect a row that constructs no `Part`. Those rows —
   `empty_callback`, `mod_plane_2x_center`, `synth_*`, `wave_2x4`, `fx_*`,
   `oliverb_solo_*`, `grain_read_*`, `echo_walk_*`, `sampler_win_*`,
   `deck_engine_hot`, `deck_mod_hot`, `tone_solo` — measure this build pair's
   drift directly. The implementer must **verify from the workload sources** which
   rows construct a `Part` rather than trusting this list. A touched-row movement
   is only evidence if it exceeds the largest control-group movement.
3. **Baseline and fixed builds must come from one session**, two runs each, same
   toolchain, same tree state except the change under test. A baseline inherited
   from round 3's `ccd5f12` build would fold that build's drift into every
   difference.
4. **`Part`-level code was measured uncontended** (round 3 §6.1) and is a floor.
   Savings measured on `deck_shell` may read differently on the gate, where the
   same code runs against a full cache and SDRAM load. This round reports both
   and does not average them.
5. **Bit-exactness is a gate here, not a general project rule.** This project has
   no checksum-against-a-stored-file requirement, and renders are sanity checks.
   What is used here is the bench's own cross-run and cross-build comparison of
   rows whose inputs did not change — which round 3 §9.5 already relied on.
6. **Item 1 is bit-exact but not free of risk.** Reordering members changes the
   object layout, and anything that assumes a layout — a memcpy over a range, a
   serialised snapshot, a `static_assert` on an offset — would break. The
   implementer must search for such assumptions before reordering, and report
   what was found.
7. **Item 3 rests on the fade returning exactly 1.0 at hold.** That is asserted by
   `engine/fx/fx_util.h` and by the M1.6 bypass invariant, and round 3 §3.2
   worked through the 385-sample fade arithmetic. It must be re-verified against
   the source, not taken from this sentence.
8. **`pct_max` and `pct_avg` disagree on `deck_shell` by 15.4 %** because of the
   per-note rare events round 3 §9.1 identified (`trigger_manual()` per 250
   blocks, a lane fire per ~72 blocks). None of Stage 1's items touch those
   events, so this round quotes both metrics and expects the saving to appear in
   both.
9. **§5.1 failed to name the metric its bands are read on. That is a defect in
   this document's own pre-registration**, and it is recorded here rather than
   repaired in place, because §5 must stay byte-identical.

   The choice is made **after the baseline run and before any `engine/` change
   exists**, so it cannot be tuned to a result. The baseline
   (`docs/bench/2026-07-30-7272b27-ablate.csv`, commit `f6e4b2c`) rebuilt round
   3's source unchanged — all 23 checksums identical — and therefore measures this
   bench's noise floor directly:

   | metric | largest movement at identical source | on the gate | on `deck_shell` |
   |---|---:|---:|---:|
   | `pct_max` | **0.25** (`instrument_worst_bbd`) | +0.25 | −0.01 |
   | `pct_avg` | **0.02** (`fx_flux_hot`) | 0.00 | 0.00 |

   **§5.1's bands are therefore read on `pct_avg`.** On `pct_max` the gate's own
   drift (0.25) covers a third of item 4's predicted 0.40–0.70 and the whole of
   the falsification band's lower edge, so a `pct_max` reading could neither
   confirm nor refute the prediction. On `pct_avg` the predicted effect is an
   order of magnitude above the floor.

   `pct_max` is still reported for every row, per item 8, and any disagreement
   between the two metrics is a finding to be stated rather than resolved by
   picking the friendlier one.

   This also strengthens §6.1: the cross-build drift that round 3 §9.5 warned
   about, and that §6.1 called the round's central threat, is a **`pct_max`**
   phenomenon. On `pct_avg` this build pair drifted by at most 0.02.

## 7. Non-goals

- **No Stage 2 work without a Stage 1 measurement.** The block entry point is the
  only item with real sonic risk, and it is not started on a prediction.
- **No new bench rows.** Round 3 §9.6 item 2 asked for a GRIT+FLUX row to repair
  the FX ladder's double-charged `C`. That repairs a *decomposition*; this round
  compares a row to itself and does not need it. It stays on the list.
- **No voice cut.** Round 2 §8.3's ≈7.9 points remain the fallback, and remain
  second.
- **No change to `Flux::set_rhythm`** (§2: too small to measure).
- **No behavioural change whatsoever in Stage 1.** Not "inaudible" — none.

## 8. Review requirements

The two-reviewer split of rounds 2 and 3 stays, because it caught different
defect sets both times, but the emphasis moves: this round changes `engine/`, so
the code reviewer's first job is no longer style but **whether each Stage 1 item
is actually bit-exact**.

1. A code reviewer on the `engine/` diff, asked specifically to find any item
   whose claim to bit-exactness is false, and to check §6.6's layout assumptions.
2. A prose auditor with the CSV as ground truth, asked to check every figure in
   §9 against it, to confirm no figure crosses a build boundary undeclared, and
   to confirm §5 was not edited.
3. The static test of §5.1 re-run by the reviewer, not taken from the
   implementer's report.

Round 3 produced thirteen text-level defects, nearly all in the coordinator's own
design prose, against zero in the three measurement rows. This document is
therefore the more likely place for an error than the code it describes.

## 9. Results

Four builds, all measured in one session on one board — §6.3's requirement, which
round 3 could not meet — two runs each, all committed under `docs/bench/`:

| build | contents | evidence file |
|---|---|---|
| `7272b27` | baseline: round 3's source, rebuilt and re-measured | `2026-07-30-7272b27-ablate.{csv,md}` |
| `dc17cdc` | item 4 alone (per-sample body made inlinable) | `2026-07-30-dc17cdc-ablate.{csv,md}` |
| `86cf817` | + items 2, 3 and 1, one build | `2026-07-30-86cf817-ablate.{csv,md}` |
| `cd639ec` | item 3 reverted — **the final state** | `2026-07-30-cd639ec-ablate.{csv,md}` |

**Metric.** Every figure below is **`pct_avg`, run 2**, with `pct_max` alongside.
That is §6.9's choice, made after the baseline run and before any `engine/` change
existed. It reverses the plan's task-6 instruction to quote `pct_max` unless
stated — the plan was written before §6.9 existed, and this section says which it
uses rather than following the older sentence silently. Every before/after in this
round crosses a build boundary (§6.1) and the pair is named at every figure.

### 9.1 The headline

`instrument_worst_bbd`, the gate, run 2 of each build:

| build | `pct_avg` | `pct_max` |
|---|---:|---:|
| `7272b27` baseline | 106.50 | 110.76 |
| `dc17cdc` item 4 alone | 104.40 | 108.57 |
| `86cf817` + items 2, 3, 1 | 105.36 | 109.32 |
| `cd639ec` final | **104.91** | **108.69** |

The overrun went **6.50 → 4.91** on `pct_avg` and **10.76 → 8.69** on `pct_max`.

**The saving is about 1.6 points, ± 0.5.** The tolerance is not decoration. The
three builds that all contain item 4 read the gate at 104.40, 105.36 and 104.91 —
a spread of **0.96** — and none of the three differs from the others by any change
to the per-sample call boundary. So "−1.59" carries one significant figure, not
three, and §9.4 is where that number comes from.

### 9.2 The grouping: `Instrument::process` versus a direct call

This is the round's main interpretive finding, and it is a fact about the bench
rows rather than about the change. Final (`cd639ec`) against baseline
(`7272b27`), run 2 of each, every row that contains a `Part`:

**Rows that run their `Part`s inside `Instrument::process`.** `Instrument::process`
(`engine/instrument.h:265`) takes a whole block and owns the sample loop
(`engine/instrument.cpp:76`), calling `_parts[pri].process(...)` at `:112` and
`_parts[yld].process(...)` at `:125` — two `Part` bodies per sample, interleaved.
`instrument_init`, `instrument_worst` and `instrument_worst_bbd` all reach it
through `proc_inst` (`bench/workloads_system.cpp:329-334`), `instr_noverb` through
`proc_instr_noverb` (`bench/workloads_instr.cpp:655-660`).

| row | `pct_avg` | `pct_max` |
|---|---:|---:|
| `instrument_init` | −2.71 | −2.86 |
| `instrument_worst_bbd` | −1.59 | −2.07 |
| `instr_noverb` | −1.54 | −1.38 |
| `instrument_worst` | −1.38 | −1.12 |
| **mean** | **−1.81** | **−1.86** |

**Rows that call `part.process(...)` directly from a bench proc's own loop.**
`proc_instr_part_1` and `proc_instr_part_2` (`bench/workloads_instr.cpp:733-771`)
and `proc_deck_shell` (`:1677-1692`) each write their own `for (size_t i = 0; i <
kBlock; ++i)` and call `Part::process` from it, with no `Instrument` anywhere.

| row | `pct_avg` | `pct_max` |
|---|---:|---:|
| `instr_part_2` | −1.25 | −1.22 |
| `instr_part_1` | −1.16 | −1.21 |
| `deck_shell` | **+0.20** | **+0.30** |
| **mean** | **−0.74** | **−0.71** |

**The split above is a description of two builds, not a mechanism, and an
earlier draft of this section overstated it.** That draft read "every row that
improved by more than a point runs its `Part` inside `Instrument::process`'s
loop". On the item-4-only build (`dc17cdc`) that was true: the four `Instrument`
rows moved −1.42 … −2.10 `pct_avg` while the three direct-call rows read −0.75
(`instr_part_1`), +0.36 (`instr_part_2`) and +0.18 (`deck_shell`), so no
direct-call row reached a point. **In the final state it is false, and the table
directly above refutes it.** `instr_part_2` (−1.25) and `instr_part_1` (−1.16)
are direct-call rows — each writes its own `for (size_t i = 0; i < kBlock; ++i)`
and calls `g.a.process(...)` from it (`bench/workloads_instr.cpp:733-771`) — and
both improved by more than a point. Generalising a grouping that held on one
build into a mechanism was the error; the grouping dissolved on the next build.

**What the data supports is much narrower: exactly one row failed to move.**
`deck_shell` read **+0.20**, and the `Instrument`-versus-direct-call split does
not explain it, because the same mechanism would have to distinguish
`instr_part_1` — one `Part`, driven from a bench loop, −1.16 — from `deck_shell`
— one `Part`, driven from a bench loop, +0.20 — and it cannot. Nor is +0.20 a
demonstrated regression: it sits inside the 0.36 of control-group movement §9.3
measures. It is a row that **did not move**, and no mechanism is named for that
here.

`Part::process` has exactly one caller in **shipping** code: `Instrument::process`,
at `engine/instrument.cpp:112` and `:125`. It is not the only caller outside the
bench. `Part` declares three `process` overloads (`engine/parts/part.h:244`,
`:322`, `:325`), the 4- and 2-argument forms forwarding to the 6-argument body,
and `tests/test_part.cpp`, `tests/test_choke.cpp`, `tests/test_mod_tide.cpp` and
`tests/test_sampler_part.cpp` call them extensively, on `Part`-typed locals and
on `Part&` helper parameters.
Tests are not bench, so "exactly one non-bench caller" is false as
written; the claim that holds is "exactly one caller in shipping code", and it is
the one this section's argument needs. Both hosts that drive the engine call
`Instrument::process` (`host/render/main.cpp:102`,
`host/vcv/src/Spotymod.cpp:637`). Nothing under `src/` reaches `engine/` at all —
`engine/instrument.cpp` is compiled only into the `spky_tests` and `render`
targets (`CMakeLists.txt:71`, `:161`) plus the bench and the VCV host — so the
production path is `Instrument::process` and the Daisy firmware shell that will
carry it is still M6.

**So §5.1 registered its sharpest row-level prediction on the row that cannot see
the change.** For an item at the *call boundary*, `deck_shell`'s virtue — that it
strips the `Instrument` away — is exactly what makes it unrepresentative: it
replaces the production call shape with a bench loop. §4 item 4 anticipated the
failure mode "only `Instrument` benefits" and required the mechanism to reach
every caller; it does reach every caller (§9.5's static test holds at both sites),
and `deck_shell` still did not move. The lesson is about instrument choice, not
about the mechanism: **a row that isolates a component does not necessarily
reproduce how that component is called**, and when the change *is* the call, the
second property is the one that matters. Round 3 chose `deck_shell` for a
different question and chose well for it; this round inherited it for a question
it does not fit.

**The same error is one level up, in the two hosts, and this round did not notice
it until after the measurement.** `Instrument::process` owns the sample loop
(`engine/instrument.cpp:76`), and **both hosts that exist today call it with
`n = 1`**: `host/render/main.cpp:102` renders one sample per call
(`inst.process(&in_l, &in_r, &l, &r, 1)`) and `host/vcv/src/Spotymod.cpp:637`
does the same inside Rack's per-sample callback. Only the bench passes
`kBlock` = 96 (`proc_inst`, `bench/workloads_system.cpp`). So at `n = 1` the
loop body runs once per call and **`Instrument::process`'s own** nine-register
`stmdb`/`ldmia` pair and `vpush {d8-d13}` — the ones §9.5 verifies execute once
per block on the bench — are paid **per sample**.

What still holds at `n = 1`: the two `Part::process` prologue/epilogue pairs per
sample are gone, because the body is now inlined into the caller rather than
called. That is the removal §5.1's static test registered and §9.5 verified, and
it is §3.2's **0.40–0.70 for the instrument**.

What does **not** hold at `n = 1`: any part of the saving that comes from state
being kept in registers *across* the 96 iterations, since there are no 96
iterations. That is exactly §9.11's unverified candidate for the unexplained
majority of the measured 1.59.

**So the bench measured a saving that the two callers which exist today would
only partly see.** No figure is claimed for how much they would see — it is not
measured, and this round has no host-side instrument that could measure it. The
bound is structural: at least the inlining of the two `Part` bodies, at most the
full 1.59, and the split between them is unknown for the same reason §9.11's
residue is unexplained.

**But the product is not one of those two callers, and it is already the right
shape.** Checked after this section was first written, because the wording above
invites the opposite conclusion: `app.cpp:117` sets `block_size = 96` — the
bench's block exactly — `app.cpp:118-119` hands it to `_hw.Init` and
`_core.init`, `app.cpp:137` to `audio.SetBlockSize`, and `AppImpl::ProcessAudio`
(`app.cpp:183-199`) passes `size` straight through to `_core.process(in, out,
size)`. The Daisy audio callback is block-based at 96 by construction. So when
M6 wires `_core.process` to `Instrument::process`, the block arrives without
anything being buffered and without a latency cost, and the full measured saving
applies. `app.cpp` does not reference `engine/` today, which is what M6 is for.

**On this point the bench is a faithful model of the product, not an optimistic
one** — same block size, same cadence. What the `n = 1` finding actually bounds
is the two **development** hosts, and neither is the product. VCV cannot do
better in principle: Rack calls a module once per sample, so a block would have
to be buffered, costing up to 96 samples of latency on the audio *and* on the CV
taps, and shifting when RESET and parameter pushes take effect. `host/render`
could batch, but it applies scenario events at exact sample indices
(`apply_event` inside its loop, `host/render/main.cpp:95-98`) and taps the lanes
on a decimated schedule, so batching would move event timing and change the
rendered output — which `ctrl_identity` hashes.

**Record the `n = 1` observation next to the `deck_shell` lesson, because it is
the same class of error:** a call site that does not reproduce how the component
will be called. `deck_shell` gets the loop right and drops the caller; the two
hosts keep the caller and drop the block. Both belong on the checklist for
whoever chooses round 5's instrument. What differs is the consequence: the
`deck_shell` mismatch cost this round its cleanest single-`Part` reading, while
the host mismatch costs the *hosts* and not the firmware.

### 9.3 The control group, and what counts as a saving

**Membership, verified from the sources rather than from §6.2's list, as §6.2
requires — and §6.2's list is wrong.** Sixteen of the 23 rows run no `Part` body.
Twelve are `system` rows, enumerated against the row table at
`bench/workloads_system.cpp:356-372`:

- **Nine** are covered by four group types that declare no `Part`: `SynthGroup`
  (`:24-27`) for `synth_1_voice`, `synth_2_voices` and `synth_4_voices`;
  `SynthPairGroup` (`:29-31`) for `synth_2x4`; `WavePairGroup` (`:33-35`) for
  `wave_2x4`; and `FxGroup` (`:37-40`) for `fx_none`, `fx_grit`, `fx_flux_sdram`
  and `fx_comp`.
- `empty_callback` allocates nothing.
- `oliverb_solo_sram` uses **no arena group at all**: `setup_reverb` and
  `proc_reverb` (`:226-250`) operate on a file-static `AmbientReverb` returned by
  `reverb_sram()`.
- `mod_plane_2x_center`'s `ModGroup` **does** declare two `Part`s — the
  correction below.

An earlier draft of this paragraph put all eleven non-`empty_callback` rows under
the four group types, which silently absorbed the last two. The whole point of
the paragraph is that membership was verified from source rather than from
§6.2's list, so a miscount inside it is self-defeating; the enumeration is
therefore spelled out row by row here and the group types are counted at **nine**,
not eleven.

Four are `instr` rows whose groups hold a `SuperModulator`, a `SynthEngine`, a
`PartFx` and a `TestToneEngine` respectively (`bench/workloads_instr.cpp:151`,
`:233`, `:339`, `:456`) — `deck_mod_hot`, `deck_engine_hot`, `fx_flux_hot` and
`tone_solo`. §6.2's list names none of the `instr` rows by that name except
`deck_engine_hot`, `deck_mod_hot` and `tone_solo`; `fx_flux_hot` is reachable
only through its `fx_*` glob, which sits among the `system` rows, so whether
§6.2 meant to include it cannot be read off the sentence. It is included here,
and it moved 0.00.

The correction: **`mod_plane_2x_center` does construct two `Part`s** —
`ModGroup` at `bench/workloads_system.cpp:18-22` declares `Part hook_a, hook_b`
at `:21`, initialised at `:73-74`, because `Center::update` needs somewhere to
write its hooks (`:91-92`). It never calls `Part::process`. §6.2 lists it as a row that
"constructs no `Part`", and the plan's task-3 criterion — "a row is in the control
group only if its setup constructs no `Part`" — excludes it. Under the criterion
§6.2 actually reasons from (a change inside `Part` cannot reach the row) it is a
control for item 4 and *not* a control for item 1, which changes `Part`'s layout
and therefore what `Center::update` reaches through those two hooks. It moved
+0.09 `pct_avg` overall and read 7.23 against the baseline's 7.02 in `86cf817`,
the build where item 1 landed. Nothing in this section depends on which way it is
classified: with it the control mean is −0.02 `pct_avg` over 16 rows, without it
−0.03 over 15, and it is not the largest mover either way.

**Control drift, final against baseline:** mean **−0.02** `pct_avg` / −0.03
`pct_max`, spread **−0.36 … +0.16** / −0.37 … +0.06. The largest movement is
`fx_flux_sdram` at 0.36 / 0.37.

**Against that bar:**

- All four `Instrument` rows (1.38–2.71) exceed 0.36 by between 3.8× and 7.5×.
  These are savings.
- `instr_part_1` (1.16) exceeds it by 3.2× and `instr_part_2` (1.25) by 3.5×.
  These are savings.
- **`deck_shell` (+0.20) does not exceed it.** It is therefore neither a saving
  nor a demonstrated regression — it is inside drift. §5.1's band for it is still
  falsified (§9.5), because the band was two-sided and +0.20 is outside it; but no
  claim is made here that `deck_shell` got slower.

### 9.4 What this bench resolves, measured rather than assumed

**Run to run, inside one build,** `pct_avg` moves by at most **0.04** on any of
the 23 rows in any of the four builds (largest: `mod_plane_2x_center`, 0.04 in
`7272b27` and in `86cf817`). The gate reads identically in both runs of all four
builds — 106.50/106.50, 104.40/104.40, 105.36/105.36, 104.91/104.91. On `pct_max`
the same gate spans 0.06–0.15 between runs. That part of §6.9's conclusion holds.

**Across builds, on rows that contain no `Part`,** it is much worse. `fx_grit`
reads 5.41 in both runs of the baseline and 5.90 / 5.88 in the two runs of
`86cf817` — a **cross-build movement of 0.47–0.49** on a row whose group holds a
`PartFx` and no `Part` (`bench/workloads_system.cpp:37-40`), stable to 0.02
within each build. Nothing in `Part` can reach it.

**On the gate itself,** the three builds that all contain item 4 spread **0.96**
(104.40, 105.36, 104.91). And the step from `dc17cdc` to `86cf817` moved the four
`Instrument` rows by **−1.00, −0.59, +0.96 and +1.11** — a spread of **2.11
points** — from a `.text` change of **−368 bytes** (the `dc17cdc` build's
195,032 B → `86cf817`'s 194,664 B). Neither figure is in `d93fa55`'s commit
message, which records the item-4-alone measurement and no size: 195,032 is
recorded in `dc17cdc`'s message and again in `b533372`'s, and 194,664 in
`86cf817`'s.

**§6.9's inference that `pct_avg` is nearly drift-free was overreaching, and this
document should say so about itself.** §6.9 read the noise floor off the
`ccd5f12`/`7272b27` pair. That pair *is* the right instrument for run-to-run and
board noise — all 23 checksums identical, `pct_avg` differing by at most 0.02 run
2 against run 2 — but the two builds compile **identical source**, so their `.text`
layouts are identical and the pair could not have exhibited layout drift at all.
It was then used to argue that "the cross-build drift that round 3 §9.5 warned
about … is a `pct_max` phenomenon". Once `.text` actually moves, that is false:
control rows drift up to 0.49 on `pct_avg`.

Two smaller corrections to that table while it is under discussion. Its largest
`pct_avg` movement, 0.02, belongs to `instrument_init`, not to `fx_flux_hot`,
which moved 0.01; and if all four runs of the two builds are compared rather than
run 2 against run 2, the largest `pct_avg` movement is 0.04
(`mod_plane_2x_center`). The `pct_max` figures in that table check out (0.25 on
`instrument_worst_bbd`, −0.01 on `deck_shell`).

**§6.9's conclusion survives its faulty argument.** `pct_avg` is still the metric
to read the bands on, because it is better on both noise-floor comparisons — and
the run-to-run one is stronger than an earlier draft of this sentence made it.
That draft paired "0.04 against 0.15", but the two numbers are not the same
measurement: **0.15 is the gate's worst run-to-run `pct_max`** (in `86cf817`;
0.06–0.15 across the four builds), while **0.04 is the worst run-to-run
`pct_avg` over all 23 rows** (`mod_plane_2x_center`, in `7272b27` and
`86cf817`). Compared like for like on the gate itself, run-to-run `pct_avg` is
**0.00 in all four builds** against `pct_max`'s 0.06–0.15; compared like for like
over all 23 rows, `pct_avg`'s worst is 0.04. Either pairing favours `pct_avg`,
and the on-the-gate one favours it more. The second comparison stands as
written: 0.02 against 0.25 across the identical-source
pair. `pct_max` is no better on the layout-drift comparison either
(`fx_grit` 0.49 against 0.47). One place `pct_max` is *narrower*: the gate's
spread across the three changed builds is 0.75 on `pct_max` against 0.96 on
`pct_avg`. So `pct_max` is not uniformly worse, and the reason for preferring
`pct_avg` is the noise floor, not a blanket claim.

**Resolution, stated as a number for whoever plans the next round: this bench
cannot demonstrate a change smaller than about 0.5 points on the gate.** Not
because of run-to-run noise, which is an order of magnitude below that, but
because every comparison it can make is cross-build and cross-build layout drift
is of that size. That is a property of the instrument, not of any change measured
through it.

### 9.5 §5.1's predictions

**The static test — held.** Re-verified here on the final ELF
(`bench/build/bench.elf` as linked at `cd639ec`) rather than taken from the
implementer's report, per §8.3.

`spky::Part::process` **has no symbol in the ELF**, and no `bl` targets it from
either site. `Instrument::process` (`0x2400fe50`, 3420 B) contains exactly four
save/restore instructions:

```
2400fe50:  stmdb   sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
2400fe54:  vpush   {d8-d13}
2400fe58:  sub     sp, #92
   ...
24010b2e:  add     sp, #92
24010b30:  vpop    {d8-d13}
24010b34:  ldmia.w sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
```

and `proc_deck_shell` (`0x24004ab8`, 620 B) exactly four:

```
24004ab8:  stmdb   sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
24004ace:  vpush   {d8-d9}
24004ad2:  sub     sp, #36
   ...
24004cb0:  add     sp, #36
24004cb2:  vpop    {d8-d9}
24004cb6:  ldmia.w sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
```

There is no other `stm`, `ldm`, `vpush` or `vpop` in either function, and neither
contains a `tbb`/`tbh`. The lowest backward-branch target inside
`Instrument::process` is `0x2400fe7a` and inside `proc_deck_shell` `0x24004ae8`,
both above their prologues, so **no save/restore lies inside any loop in either
function**: each pair executes once per block, not 96 times. Against the baseline
ELF, where `Part::process` was a 1252-byte out-of-line symbol at `0x2400e738`
whose own prologue and epilogue §3.2 quotes, that is the removal item 4 claimed.
`Part::_control_tick` is still out of line — `T` at `0x2400f20c`, 1444 B, called
at `0x240107dc`, `0x2401085a`, `0x240109d8` and `0x240109e6` from
`Instrument::process` and at `0x24004c36`/`0x24004c5a` from `proc_deck_shell` —
as the plan's task 2 required.

**`deck_shell`, predicted −0.20 … −0.35, falsified outside −0.05 … −0.60 —
FALSIFIED.** Measured **+0.20**. §9.2 gives the reason and §9.3 the caveat that
the +0.20 is itself inside drift.

**The gate, predicted −0.40 … −0.70, falsified outside −0.10 … −1.20 —
FALSIFIED, in the favourable direction, on both readings.** Item 4 alone measured
**−2.10** (`pct_avg`; −2.19 `pct_max`) and the final state **−1.59** (−2.07). Both
are past the far edge of the falsification band, item 4 alone by a factor of 1.75.
§9.11 is what that costs the round.

**Control group ≈ 0 — HELD, but the condition could not have failed.** Mean
−0.02 `pct_avg` over the 16 rows, largest mover 0.36 (§9.3), and the predicted
movers all exceed it except `deck_shell`. **§5.1's registered falsification
condition for this row is, however, self-referential:** "any row moves more than
the largest control-group mover". For a control-group row that is "moves more
than itself or than the largest of its peers" — the largest control-group mover
is by construction not larger than the largest control-group mover, so no
control-group reading can falsify it, whatever the drift turns out to be. Had
`fx_flux_sdram` moved 5.00 instead of 0.36 the condition would still have read
HELD. **A condition that cannot fail is not a prediction**, and this is the
second defect in §5.1's pre-registration alongside §9.6's "a registered band
without a registered metric is not a registered prediction" — same section, same
class, and both recorded rather than repaired, because §5 must stay
byte-identical. What the control group actually delivered is not a passed test
but a *scale*: 0.36, the bar §9.3 measures the touched rows against. That is the
useful output, and a future round should register it that way — as a threshold
named in advance, e.g. "no row that constructs no `Part` moves more than 0.25" —
rather than against itself.

**Every shared checksum unchanged — HELD.** §9.7.

**The model behind this round stands in its static half and fails in its
quantitative half.** §5.1 required *both* predictions to hold. The emitted code is
exactly what §4 item 4 said it would be; the cost it removes is two to four times
what §3.2's ISA argument allows. That is not a refutation of §3.2 — the
save/restore is gone and it did cost something — but it means §3.2 was not the
whole cause, and the rest is unidentified (§9.11).

### 9.6 §5.2's criterion, and §5.1's missing metric

**§5.2's criterion: "falsified if items 2, 3 and 4 together recover more than 1.00
per deck."** Two things have to be settled before a verdict: which measurement is
the criterion's quantity, and whether the bench can resolve the margin.

The criterion excludes item 1 explicitly and by design. No build isolates items
2 + 3 + 4: `b533372` (items 4 + 2) and `ea32381` (items 4 + 2 + 3) were built but
never measured, and the two builds that were measured are `dc17cdc` (item 4 alone)
and `86cf817` (items 4 + 2 + 3 + 1). So the closest available reading is **item 4
alone: 1.05 per deck** (gate −2.10 `pct_avg` over two `Part`s; 1.10 on `pct_max`).
Items 2 and 3 are predicted at 0.02–0.06 per deck combined, well under this
bench's floor, so items 2 + 3 + 4 cannot be meaningfully below item 4 alone.

**On the point reading the criterion is exceeded: 1.05 against 1.00.** But the
margin is 0.05 per deck, i.e. 0.10 on the gate, against a cross-build gate spread
of 0.96 (§9.4). **The criterion is therefore not resolvable by this bench**, and
the honest verdict is that: exceeded on the numbers as read, by a margin an order
of magnitude inside the instrument's uncertainty.

What the criterion was *for* is settled even so. §5.2 said a value above 1.00 per
deck "would mean the per-sample overhead is far larger than the ISA allows and the
disassembly in §3 is being read wrong." The first half is confirmed — 1.05 against
§5.2's 0.20–0.35 is three times the upper end — and §9.5's static test rules out
the second: the disassembly said the pair would go, and it went. The disassembly
was read right about *what*; the ISA argument was wrong about *how much*. That is
§9.11.

For completeness: the final state recovers **0.80 per deck** on `pct_avg` (1.04 on
`pct_max`), which is below 1.00 on `pct_avg` and above it on `pct_max`. Neither
figure is the criterion's quantity — the final state contains item 1 and lacks
item 3 — and neither is quoted as a verdict on it.

**§5.1 named no metric for its bands. §6.9 recorded that as a defect in this
document's own pre-registration; here is what it cost.** Nothing, as it happens,
and only by luck: both hardware bands are falsified on `pct_avg` and on `pct_max`
alike, and the control-group prediction holds on both. Had the gate moved by
−0.30, the two metrics would have disagreed about whether the prediction held and
§5.1 would have offered no way to choose — with the choice then being made after
the result was known, which is the whole thing pre-registration exists to prevent.
**A registered band without a registered metric is not a registered prediction.**

### 9.7 The audio is unchanged, and this is the first round that could have broken it

**All 23 rows return byte-identical checksums in all four builds, in both runs of
each, and identical to round 3's `ccd5f12`** — 23 rows × 4 builds × 2 runs, plus
the round-3 pair, one checksum per row throughout. Verified across the five CSVs
directly, not from the per-build gate ledgers.

That is this round's strongest single result, and it is what makes "bit-exact" a
measurement here instead of an argument. Rounds 1–3 could not break the audio
because they could not touch it — they added bench rows. This round moved a
per-sample body between translation units, deleted a call inside the control tick
and reordered `Part`'s members, all on the audio path, and the outputs did not
change in a single bit at any of the 23 operating points the `ablate` profile
covers. §6.5 stands: this is the bench's own cross-run and cross-build comparison
of rows whose inputs did not change, not a checksum against a stored file, of
which this project has none.

**Independently confirmed on the desktop renderer, on a different compiler and a
different code path.** The repo does carry two byte-identity render gates —
`ctrl_identity` and `wave_formant_sweep` (`CMakeLists.txt:183` and `:193`), which
render a scenario through `host/render` and SHA256 the whole WAV — and they had
never been run on this branch. They have now been run on `main` (`a93327e`) and
on the branch (`0eed246`), in worktrees built from the same toolchain:

| build type | scenario | `main` `a93327e` | branch `0eed246` |
|---|---|---|---|
| default (`Debug`) | `ctrl_identity` | `4296cf27…e59a7` | **identical** |
| default (`Debug`) | `wave_formant_sweep` | `82aac4d6…706b5` | **identical** |
| `-O3` (`Release`) | `ctrl_identity` | `2b73f248…b699cd` | **identical** |
| `-O3` (`Release`) | `wave_formant_sweep` | `82aac4d6…706b5` | **identical** |

**`main` and the branch render the same bytes at both optimisation levels.** Both
gates pass on the branch at the build type the roadmap's "Build & verify" block
documents (`cmake -S . -B build`, which resolves to `Debug` here).

This matters because it is *independent* of §9.7's bench checksums in every way
that could hide a shared cause: a different compiler and target (clang/x86-64,
not `arm-none-eabi-gcc`), **no `-ffast-math`**, no `-O` level at all in the
documented configuration, a different code path (`host/render`, not the bench
runner), and a full rendered WAV hashed with SHA256 rather than a per-row
accumulator folded to 32 bits. The code reviewer identified `-ffast-math`
reassociation across the former call boundary as the one plausible way item 4
could have altered output and found no site where it applies; this measures the
question instead of arguing it, and the `-O3` row measures it a second time at a
different optimisation level.

**One caveat, and it is not this branch's.** `ctrl_identity`'s stored constant is
**optimisation-level dependent**: it matches the default `Debug` build on both
`main` and the branch, and does *not* match either at `-O3`, where both render
`2b73f248…`. A build directory configured `-DCMAKE_BUILD_TYPE=Release` therefore
fails the gate on `main` as well. That is a **pre-existing property of the gate**,
not a finding about this branch — the constant pins a build configuration the
test does not itself pin — and it is worth its own fix (pin the build type, or
store a tolerance-based comparison). It is out of this round's scope.
`wave_formant_sweep` is stable across both optimisation levels on both commits.

### 9.8 Sizes and symbols

`.text` in `bench.elf`, from the commit messages that recorded each build and
re-checked here on the final ELF with `arm-none-eabi-size -A`:

| build | `.text` | `SRAM_EXEC` of 262,880 |
|---|---:|---:|
| baseline | 189,944 | 194,976 — 74.17 % |
| `dc17cdc` item 4 | 195,032 | 200,064 — 76.10 % |
| `b533372` item 2 | 195,072 | 76.12 % |
| `ea32381` item 3 | 195,200 | 76.17 % |
| `86cf817` item 1 | 194,664 | 75.96 % |
| `cd639ec` final | **194,528** | **199,560 — 75.91 %** |

Net **+4,584 bytes** of `.text` against the ≈66 KB of free `SRAM_EXEC` §3.5
measured, so §3.5's headroom check was the right one and had room to spare. `SRAM`
— the bench arena, which is data — is unchanged at 255,744 B / 97.83 % throughout,
as §3.5 predicted it must be. These are the only figures in §9 not locatable in a
`docs/bench/*.csv`; the linker's `--print-memory-usage` output is not part of the
evidence files, and the commit messages are the record.

Symbols, baseline against final:

| symbol | baseline | final |
|---|---|---|
| `spky::Part::process` | 1252 B at `0x2400e738` | **absent** |
| `spky::Part::_control_tick` | 1404 B | 1444 B, still out of line |
| `spky::Instrument::process` | 2204 B | 3420 B |
| `spky::SoftSwitch::process(bool)` | absent (inlined) | 392 B, weak |

`_control_tick`'s +40 bytes are item 2's, and are growth rather than shrinkage:
`b533372` records that gcc peels the skipped index into two copies of the loop
body. `SoftSwitch::process` is new as a symbol. It is the four-way
`switch (_stage)` of `engine/fx/fx_util.h:83-104`, which the baseline emitted as
the `tbh [pc, r3, lsl #1]` at `0x2400e764` that §3.2's table counts, inlined into
`Part::process`; in the final ELF the same four-way dispatch is a `tbb [pc, r3]`
at `0x2400493a` inside a called function, reached once per `Part` per sample
(`0x240100a0` and `0x240102da` in `Instrument::process`, `0x24004bc6` in
`proc_deck_shell`). **No direction is claimed for that trade** — a call replaced
an inlined branch table, and this round measured no row that isolates it.

### 9.9 Item 1, reported separately

Items 2, 3 and 1 are three commits but one measured build, so **their effects are
not separable by this measurement** and none of the step from `dc17cdc` to
`86cf817` is attributed to any one of them. That step's own numbers are in §9.4:
four `Instrument` rows moving −1.00, −0.59, +0.96 and +1.11.

Item 1 therefore has **no measured value in this round, and none is folded into
the model's score**, per §5.2. What is on record is code-level and count-level
only, from `86cf817`'s commit message: `proc_deck_shell` keeps 207 instructions
but 113 → 95 of them are 32-bit encodings and its `ldr.w`/`str.w` count falls
33 → 14; `Instrument::process` emits 42 fewer 32-bit instructions at an unchanged
16-bit count. Counts are not a cost, and no direction is claimed from them. The
layout-assumption search §6.6 required was run over `engine/`, `src/`, `host/`,
`tests/` and `bench/` and found nothing; the commit message lists where it looked.

The excess of the final state's 0.80 per deck over §5.2's predictable 0.22–0.41
is **not attributed to item 1 as a fact.** It is consistent with item 1 being
worth something, which §5.2 allowed for explicitly. It is equally consistent with
item 4 alone being larger than the ISA argument allows — which the `dc17cdc` build
suggests independently, since item 4 by itself measured 1.05 per deck without item
1 present at all.

### 9.10 Item 3 was reverted, and the reason is code evidence rather than a delta

Item 3 was built (`ea32381`) and reverted (`cd639ec`). It was bit-exact: the
implementer re-verified against `engine/fx/fx_util.h` rather than against §4's
sentence, as §6.7 demanded, and `SoftSwitch::process` does return exactly `1.0f`
at hold — the `Stage::hold` case assigns `_out = 1.f` and the return is
`std::clamp(inverse ? 1.f - _out : _out, 0.f, 1.f)` with `inverse` defaulted false
and never passed by `Part` (`fx_util.h:82-106`, the hold case at `:94-98`, the
return at `:105`). The checksums confirm it.

**What did not survive review is §4 item 3's cost premise.** §4 called
`outL *= fade; outR *= fade;` "two wasted `vmul`", which reads as a removal. The
disassembly of `ea32381` shows a trade: the guard emits a `vmov.f32` of 1.0, a
`vcmp.f32`, a **`vmrs APSR_nzcv, fpscr`** — an FPU-to-core flag transfer — and a
`beq.n` in order to skip two `vldr`, two `vmul` and two `vstr`
(`proc_deck_shell`, `0x24004b66`–`0x24004b88`; the same guard appears at
`0x2401042a` and `0x24010672` in `Instrument::process`). **No direction is claimed
for that trade**, and none can be measured: the item's predicted value is 0.01–0.03
points against §9.4's 0.47–0.49 of control drift.

So the decision to drop it rests on the code and not on a delta. **The guard may
well be a net gain. This bench cannot show it**, and an unshowable change to the
audio path is not worth its 128 bytes of `.text`. Items 2 and 1 stayed on
different grounds: item 2 removes a provably dead call, and item 1 is a
declaration reordering with no statement changed.

**Two of Stage 1's four items produced no measurement at all** — items 2 and 3,
predicted 0.01–0.03 each. That is not a surprise; §5.2 predicted 0.02–0.06 for
the pair, which was always below this bench's floor. It is worth stating plainly
because it halves what the round could actually weigh.

### 9.11 The magnitude is not explained

§3.2 costs two prologue/epilogue pairs per sample — one per `Part`, since
`Instrument::process` interleaves the decks per sample — at 20–35 cycles each,
giving **0.40–0.70 points across the instrument**. Measured: **2.10** on the gate
for item 4 alone, and **1.38–2.71** on the four `Instrument` rows in the final
state. That is two to four times §3.2's upper bound, and §9.5's static test rules
out the explanation that the pair is still being emitted.

**The remainder is unexplained.** A candidate exists — that making the body
visible inside the caller's loop lets the compiler keep values in registers and
eliminate reloads across samples, on top of removing the save/restore — and it is
**not verified**. The attempt to read it out of the disassembly was abandoned: the
loop-detection used to bound the per-sample region kept pulling in cold and
init-path code reached from `Instrument::process` (`AmbientReverb::clear` at
`0x240109ca` and `0x24010b6e`, `Center::update` at `0x24010814`), which is the
same block-reordering effect §3.1 records for the baseline, and no clean region
boundary was established.

Per this project's rule and §5.3, that is recorded here as **an unexplained
residue with an unverified candidate, and it is not upgraded.** In particular
`86cf817`'s encoding counts are not offered as support: they are item 1's, not
item 4's. Whoever wants the mechanism should measure it, not read it.

### 9.12 The residue, and what Stage 1 recovered

Round 3 put `Part`-level code at **2.65 `pct_avg` / 4.00 `pct_max` per deck**
(`docs/bench/2026-07-30-ccd5f12-ablate.csv` run 2, quoted in §1). That figure
crosses a build boundary into this round, and it is safe to carry on `pct_avg` and
less safe on `pct_max`: this session's baseline rebuilt the same source and its
`pct_avg` moves by at most **0.04** on any row, while its `pct_max` on the gate
differs by 0.25.

Stated exactly, `ccd5f12` against `7272b27`, so that this paragraph agrees with
§9.4 rather than reverting to §6.9's pre-correction reading:

- **run 2, five rows move:** `instrument_init` −0.02, `wave_2x4` −0.01,
  `oliverb_solo_sram` −0.01, `instr_noverb` −0.01, `fx_flux_hot` +0.01. The
  largest is `instrument_init`, which is §9.4's correction to §6.9's table.
- **run 1, seven rows move:** `mod_plane_2x_center` +0.04, `deck_shell` +0.03,
  `synth_2x4` −0.01, `wave_2x4` −0.01, `instr_part_2` −0.01, `instr_noverb`
  −0.01, `fx_flux_hot` +0.01.

An earlier draft of this paragraph said "identical on every row but two
(`fx_flux_hot` 0.01, `deck_shell` run 1 0.03)". That is §6.9's picture, which
§9.4 had already corrected 250 lines earlier, and it undercounts both runs. **The
conclusion is unchanged and slightly better supported:** every movement across
the identical-source pair is ≤ 0.04, still an order of magnitude below §9.4's
0.5-point cross-build floor, so round 3's `pct_avg` figure carries into this
round safely.

This round recovered **0.80 per deck on `pct_avg`** (1.04 on `pct_max`), the gate's
1.59 / 2.07 halved — an average over the two decks, not each deck, and round 3
already noted deck B is about half a point dearer than deck A. That is **about
30 % of the `pct_avg` bucket and 26 % of the `pct_max` one**: roughly a quarter to
a third of `Part`-level code, against §5.2's registered expectation of 6–16 %.

**The residue is about 1.85 `pct_avg` / 2.96 `pct_max` per deck**, 3.7 / 5.9
across the instrument, against a remaining overrun of 4.91 / 8.69. So `Part`-level
code is still **68–75 % of the gap**, and still the largest named term, and still
carries no sonic cost. **No mechanism is named for it.** It sits in the 200
instructions of the per-sample body that §3.2 counted and in the 1444-byte control
tick, and this round measured no row that divides those two.

### 9.13 Should there be a round 5? Not another bit-exact one, not yet

The residue argues for continuing and the instrument argues against, and the
instrument wins for now.

**What the evidence supports.** The residue is real, large and free of sonic cost.
The one structural lever whose value could be predicted from the ISA has been
pulled, and `Part::process` no longer exists as an out-of-line symbol, so it
cannot be pulled again. Nothing in §3 identifies a second lever of comparable
size; §3.4's dead store was worth an unmeasurable 0.01–0.03 and §3.2's remaining
counts — 200 instructions, 22 object accesses at a large offset — describe work,
not overhead with an obvious removal.

**What the evidence forbids.** §9.4's floor: this bench cannot demonstrate a
change smaller than about 0.5 points on the gate, because every comparison it can
make is cross-build and cross-build layout drift is that size. **That bounds what
any future bit-exact round can show, whatever it actually saves.** Two of this
round's four items already fell below the floor, and the round's own headline
carries a ±0.5 tolerance from it. A round 5 of further micro-items inside `Part`
would spend builds and hardware sessions on changes it could not weigh, and would
be reduced to arguing from code — which is precisely the position item 3 ended in,
and why item 3 was reverted rather than kept on its reading.

**So the recommendation is: do not open another bit-exact `Part` round until the
bench can resolve below 0.5 points on the gate.** The most useful single
deliverable for whoever plans round 5 may be that instrument work rather than any
optimisation — a same-build A/B, a way to hold layout fixed across a change, or a
row set that measures a difference within one link. Nothing in this sequence has
attacked layout drift; four rounds have now worked around it, and round 3 §9.5
called it out before this round confirmed it a fifth time.

**The two candidates large enough for the bench as it stands** are §4's Stage 2
block entry point and round 2 §8.3's voice cut at ≈7.9 points. §7 forbade opening
Stage 2 on a prediction, and Stage 1 has now been measured, so the bar is cleared
— but **no size is claimed for Stage 2 here.** It was never predicted, §5.3
declines to predict it, and its hazard is unchanged and specific: it is not
bit-exact, because `Instrument::process` interleaves the decks per sample and
CHOKE reads `_parts[pri].gate()` at `:118` and `max_voice_env()` at `:120` each
sample (`engine/instrument.cpp`), so it goes to the owner's ear and not to a
checksum. The voice cut is measured and audible by construction. Choosing between
a listening decision and a listening decision is the owner's, not this document's.

**What this round settled, and it is not the 1.6 points.** The gate went 106.50 →
104.91 `pct_avg` and 110.76 → 108.69 `pct_max`, bit-for-bit identically, in the
first round of the sequence that could have changed the sound. That is worth
having. But the round's more durable outputs are findings about measurement:

1. **A call site that does not reproduce how the component will be called is not
   an instrument for a change to the call.** `deck_shell` isolates one `Part` and
   drops the caller (§9.2); the two hosts keep the caller and call
   `Instrument::process` with `n = 1`, dropping the block over which this round's
   saving was measured (§9.2 again). The second half of that was found by reading
   the hosts *after* the measurement, and it bounds what the change is worth to
   the code that exists today — a bound this round cannot put a number on. It
   does **not** bound the firmware: `app.cpp:117` sets `block_size = 96` and the
   callback passes it through, so on block size the bench and the product agree
   (§9.2). The lesson is to make that comparison *before* the measurement, not
   after; here it happened to come out well.
2. **This bench's cross-build floor is around half a point on the gate** (§9.4),
   which bounds what any future bit-exact round can show.
3. **Two of §5.1's four registered conditions were not tests.** One named no
   metric (§9.6) and one was self-referential (§9.5). Pre-registration is only
   worth what its conditions can fail on.

A fourth, negative, output: **the `Instrument`-versus-direct-call grouping this
section originally led with does not survive the final build** (§9.2). It held on
one build, was written up as a mechanism, and was refuted by the table printed
underneath it. The lesson that survives is the narrow one in item 1, not the
grouping.

All of these were bought with hardware sessions, and all will save the next round
more than 1.6 points would.
