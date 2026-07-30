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

*To be written after the measurement. Every figure must cite the CSV; every
causal claim must cite the source. §5's predictions are to be reported as held or
falsified, including the ones that were wrong, and §5 is not to be edited.*
