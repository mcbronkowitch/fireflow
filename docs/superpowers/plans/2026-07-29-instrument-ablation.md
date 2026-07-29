# Instrument-Level Ablation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Find out whether the ~23 points of block budget that no bench row
accounts for are unmeasured instrument-level work, or the same work costing
more inside the instrument than in isolation.

**Architecture:** Three new bench rows form a ladder whose fourth rung already
exists. One bare `Part`, two bare `Part`s, the full instrument with a null
reverb pointer, and `instrument_worst_bbd`. Three differences isolate deck
contention, instrument glue, and the reverb in situ. No engine change: `Part`
can be instantiated alone, and the reverb is already behind a host-supplied
pointer.

**Tech Stack:** C++17, ARM cross-toolchain + Python (bench), Daisy Seed,
CMake + Ninja + clang (desktop, for the contract test only).

**Design spec:** `docs/superpowers/specs/2026-07-29-instrument-ablation-design.md`.
Section references (§3, §4 …) point into it.

## READ THIS BEFORE TASK 1

**A null result is a result.** If the gap turns out to be mostly contention,
this round has established that no instrument-level cut exists to be found.
Write that outcome as plainly as the other one — do not go looking for a more
interesting story (spec §5).

**`engine/` is off limits.** This round measures; it does not change the
instrument. If a measurement appears to need an engine change, that is a
finding to report, not a change to make.

## Global Constraints

- Branch `perf/instrument-ablation` (already created). Never commit on `main`.
- Commit trailer is exactly, and with nothing after it:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **No file under `engine/` may be modified.** `git diff main -- engine/` must
  stay empty for the life of this branch.
- Do not add, remove or rename any **existing** bench row. Existing rows'
  checksums must not move; if one does, that is a real finding (spec §6).
- Never run `python bench/run.py` without `--profile` — the default is `full`,
  which fails to link by design (`bench/README.md`).
- Build, then rebind the QSPI receipt, then measure, in that order
  (`bench/README.md`). Doing it the other way round costs a programming cycle
  and reads like a hardware fault.
- The bench refuses hardware evidence from a dirty git tree.
- `source ./env.sh` before any cmake/ctest invocation, in the same shell
  command. The Bash tool's working directory persists between calls — if you
  `cd bench`, `cd` back before sourcing.
- Desktop suite acceptance is "no new failure": `tests/test_seed_audition_init.cpp`
  is red on `main` already and is not this round's to fix.

---

## File Structure

| File | Responsibility |
|---|---|
| `bench/workloads_instr.cpp` | **New.** The three rows, their groups, the shared config mirror, the `kInstrWorkloads` table. |
| `bench/workload.h` | `extern` declarations for `kInstrWorkloads` / `kInstrCount`. |
| `bench/families.cpp` | The `#if BENCH_FAMILY_INSTR` registry entry. |
| `bench/Makefile` | `FAMILY_SOURCE_instr` and the default family list. |
| `bench/run.py` | `BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"]`. |
| `bench/profiles.py` | The `ablate` profile. |
| `bench/test_run_contract.py` | Contract coverage for the new family and profile. |
| `docs/bench/2026-07-29-<sha>-ablate.{md,csv}` | The evidence. |
| The spec, `docs/roadmap.md` | The written verdict. |

The name `workloads_instr.cpp` deliberately avoids `bench/workloads_abl.cpp`,
which belongs to an earlier round and is not touched here.

---

### Task 1: The `instr` family, the `ablate` profile, and the null-reverb row

Gets one row working end to end through all four registration points. The two
bare-`Part` rows follow in Task 2, once the plumbing is proven.

**Files:**
- Create: `bench/workloads_instr.cpp`
- Modify: `bench/workload.h`, `bench/families.cpp`, `bench/Makefile`,
  `bench/run.py`, `bench/profiles.py`, `bench/test_run_contract.py`

**Interfaces:**
- Produces: family `instr`, profile `ablate` (families `("system", "instr")`),
  row `instr_noverb`, and `bench/workloads_instr.cpp`'s anonymous namespace
  with `g_instr_arena`. Task 2 adds two rows to the same file and table.

- [ ] **Step 1: Create `bench/workloads_instr.cpp` with the null-reverb row**

```cpp
#include "workload.h"
#include "families.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "parts/part.h"

using namespace spky;

namespace bench {
namespace {

// Mirrors setup_inst_worst_bbd's own settle (bench/workloads_system.cpp):
// fill both BBD lines and let every envelope and slew arrive before the
// runner's measured window opens.
constexpr int kInstrSettleBlocks = 200;

// The full instrument at the gate row's configuration, with the reverb
// removed. Instrument::process gates its whole reverb section behind
// `if (_reverb)`, and FxMem::reverb is a host-supplied pointer, so a null
// pointer removes the algorithm, its four per-deck gain smoothers AND the
// send/return mixing in one move -- without reimplementing any instrument
// logic here. Rebuilt logic drifts from the original, and a drifted copy
// silently measuring the wrong thing is the exact failure class this round
// exists to detect (design spec section 3.1).
//
// The MORPH blend is NOT removed with it: `l = al*ga + bl*gb` runs
// unconditionally above the guard, so it correctly stays on the glue side of
// instrument_worst_bbd - instr_noverb.
struct InstrNoVerbGroup {
    Instrument instrument;
    float out_l[kBlock], out_r[kBlock];
    int   counter = 0;
};

SerialArena<InstrNoVerbGroup> g_instr_arena;

void setup_instr_noverb()
{
    auto& group = g_instr_arena.emplace<InstrNoVerbGroup>();
    auto& inst = group.instrument;

    // fx_mem() hands out echo and sampler storage with the SRAM reverb
    // attached; copy it and drop only the reverb. Everything else must stay
    // identical to what instrument_worst_bbd gets, or the subtraction
    // measures the difference in memory rather than the reverb.
    FxMem mem = fx_mem();
    mem.reverb = nullptr;
    inst.init(kSampleRate, mem);
    inst.set_tempo_bpm(120.f);
    group.counter = 0;

    // Mirrors setup_inst_worst + setup_inst_worst_bbd exactly, minus the
    // reverb calls, which have nothing to act on here.
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_color(p, 1.f);
        inst.set_density(p, 1.f);
        inst.set_depth(p, 1.f);
        inst.set_rate(p, 0.8f);
        inst.set_fx_on(p, FxBlock::Grit, true);
        inst.set_fx_on(p, FxBlock::Flux, true);
        inst.set_grit_mix(p, 1.f);
        inst.set_flux_mix(p, 1.f);
        inst.set_comp(p, 1.f);
        inst.set_voice_decay(p, 1.f);
        inst.trigger_manual(p);
    }
    inst.set_master_drive(1.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_stages(p, 1.f);
        inst.set_drive(p, 0.85f);
        inst.set_flux_rate(p, kFluxRateCount - 1);
        inst.set_fx_target_base(p, FXT_FLUX_FB, 0.9f);
    }

    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        inst.process(in, in, group.out_l, group.out_r, kBlock);
}

float proc_instr_noverb()
{
    auto& group = g_instr_arena.get<InstrNoVerbGroup>();
    auto& inst = group.instrument;
    const float* in = test_input();
    inst.process(in, in, group.out_l, group.out_r, kBlock);
    // Same retrigger cadence as proc_inst (bench/workloads_system.cpp): voice
    // occupancy has to match the row this one is subtracted from, or the
    // difference measures voices instead of the reverb.
    if (++group.counter >= 250) {
        group.counter = 0;
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
    }
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.out_l[i] + group.out_r[i];
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

} // namespace

const Workload kInstrWorkloads[] = {
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
};
const int kInstrCount = sizeof(kInstrWorkloads) / sizeof(kInstrWorkloads[0]);

} // namespace bench
```

- [ ] **Step 2: Declare the table in `bench/workload.h`**

Beside the existing `kSweepWorkloads` / `kSweepCount` pair:

```cpp
extern const Workload kInstrWorkloads[];
extern const int      kInstrCount;
```

- [ ] **Step 3: Register the family in `bench/families.cpp`**

Insert after the `sweep` block and before `body` — registry order is execution
order, and `sampler` must stay last (see the header comment in
`bench/families.h`):

```cpp
#if BENCH_FAMILY_INSTR
    { "instr",   kInstrWorkloads,   kInstrCount   },
#endif
```

The file's own comment warns that `families_csv()`'s `buf[128]` and
`report.cpp`'s shared 256-byte `g_buf` both bound the family-name string.
Adding `instr` takes the longest possible list from 41 characters to 47,
against roughly 128 available — no change needed, but **say in your report
that you checked**, because the comment specifically asks whoever adds a
family to look.

- [ ] **Step 4: Add the source mapping in `bench/Makefile`**

Beside the other `FAMILY_SOURCE_*` lines:

```make
FAMILY_SOURCE_instr   = workloads_instr.cpp
```

Leave the `BENCH_FAMILIES ?=` default list alone. It is the `full` profile's
set, which already fails to link by design; adding a family to it changes
nothing useful and makes the failure larger.

- [ ] **Step 5: Add the row expectation in `bench/run.py`**

In `BENCH_PROTOCOL_ROWS_BY_FAMILY`, after the `sweep` entry:

```python
    "instr": (
        "instr_noverb",
    ),
```

Task 2 extends this tuple. The names here must match `kInstrWorkloads[]`
exactly; the order does not have to. `run.py` folds this table into a
frozenset of `(family, name)` pairs (`protocol_rowset`, `bench/run.py:281-286`,
and the gate-supply check at `:689-693`), so what is enforced is the set, not
the sequence.

- [ ] **Step 6: Add the profile in `bench/profiles.py`**

```python
    # The instrument-level ablation (spec 2026-07-29-instrument-ablation).
    # Carries `system` for two reasons, not one: it supplies the ladder's
    # fourth rung (instrument_worst_bbd, which the whole round is measured
    # against), and without an instrument_worst anchor verdict() reports
    # "undetermined" -- which is how the BBD figures stood for two days with
    # no verdict attached.
    "ablate": Profile(
        families=("system", "instr"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
```

`system` supplies `synth_2x4` and `wave_2x4`, so declaring the WAVE gate is
valid — `resolve()` validates exactly that and would raise otherwise.

- [ ] **Step 7: Extend `bench/test_run_contract.py`**

Add a test class mirroring `SweepProfileTest`:

```python
class AblateProfileTest(unittest.TestCase):
    def test_ablate_profile_resolves_and_carries_system(self):
        p = profiles.resolve("ablate", run.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertIn("system", p.families)
        self.assertIn("instr", p.families)

    def test_instr_family_has_row_expectations(self):
        self.assertIn("instr", run.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertTrue(run.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_noverb_row_is_expected(self):
        self.assertIn("instr_noverb", run.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
```

Match the file's existing import names and style; read a neighbouring class
before writing rather than assuming.

- [ ] **Step 8: Run the contract test**

```bash
cd bench && python -m unittest test_run_contract && cd ..
```

Expected: all tests pass, count up by three. This suite is `unittest`-based;
pytest is not installed here and is not needed.

- [ ] **Step 9: Build the ARM image**

```bash
source ./env.sh && python bench/run.py --profile ablate --build-only
```

Expected: a clean link. **Report the SRAM and SRAM_EXEC lines from the memory
report.** SRAM sat at 97.83 % before this round and a second `SerialArena`
costs one more max-sized `.bss` block. If the image does not link, **stop and
report it** — that is a finding about how little headroom is left, not
something to squeeze past by shrinking a row.

- [ ] **Step 10: Commit**

```bash
git add bench/
git commit -m "$(cat <<'EOF'
bench(instr): a new family for instrument-level ablation

One row to start: the gate row's configuration with a null reverb
pointer. Instrument::process gates its whole reverb section behind
`if (_reverb)`, so this removes the algorithm, its four gain smoothers
and the send/return mixing without reimplementing instrument logic in
the bench -- rebuilt logic drifts, and a drifted copy measuring the
wrong thing is what this round exists to catch.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The two bare-`Part` rows

**Files:**
- Modify: `bench/workloads_instr.cpp`, `bench/run.py`,
  `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `g_instr_arena` and `kInstrSettleBlocks` from Task 1.
- Produces: rows `instr_part_1` and `instr_part_2`.

- [ ] **Step 1: Add the group and the configuration mirror**

Insert into the anonymous namespace, before the workload table, and add
`InstrPartGroup` to the arena's template arguments so it reads
`SerialArena<InstrNoVerbGroup, InstrPartGroup> g_instr_arena;`:

```cpp
// Two bare Parts, driven directly. The point of the row is that NOTHING
// wraps them -- no Instrument, so no Center, no CHOKE framing, no MORPH, no
// dry taps, no cross-deck rhythm exchange, no limiter. Whatever
// instr_noverb costs above this row is exactly that glue.
struct InstrPartGroup {
    Part  a, b;
    int   counter = 0;
    // Read back after the settle and folded into the checksum: a row that
    // silently configured its Parts differently from the Instrument would
    // otherwise produce a plausible number that means nothing.
    float clock_a = 0.f, clock_b = 0.f;
    int   stages_a = 0, stages_b = 0;
};

// Mirrors setup_inst_worst + setup_inst_worst_bbd, deck for deck.
//
// This is a CHECKLIST, not a reinterpretation: every Instrument setter used
// there is a one-line forward declared in engine/instrument.h -- e.g.
// `set_color(p,n)` IS `_parts[p].set_color(n)` and `set_rate(p,n)` IS
// `_parts[p].mod().set_rate(n)`. Verify each line against
// bench/workloads_system.cpp's setup_inst_worst/setup_inst_worst_bbd and
// against engine/instrument.h's forward, in that order.
//
// Three things Instrument does that this deliberately does NOT (design spec
// section 4.1), because they have no Part equivalent and therefore belong on
// the glue side of the subtraction:
//   - set_master_drive, which reaches Instrument's own _limiter;
//   - set_other_deck_tap, supplied at control rate by Instrument;
//   - fx().set_rhythm(), likewise -- and harmless as well as correct, since
//     setup_inst_worst_bbd never touches LINK, so _link stays 0 and both
//     DRAG and THIN are inert on either side of the comparison.
void configure_worst_bbd(Part& part)
{
    part.mod().set_tempo_bpm(120.f);
    part.fx().set_bpm(120.f);
    part.set_color(1.f);
    part.mod().set_density(1.f);
    part.set_depth(1.f);
    part.mod().set_rate(0.8f);
    part.fx().set_fx_on(FxBlock::Grit, true);
    part.fx().set_fx_on(FxBlock::Flux, true);
    part.fx().set_grit_mix(1.f);
    part.fx().set_flux_mix(1.f);
    part.fx().set_comp(1.f);
    part.set_voice_decay(1.f);
    part.trigger_manual();
    part.fx().set_stages(1.f);
    part.fx().set_drive(0.85f);
    part.fx().set_flux_rate(kFluxRateCount - 1);
    part.set_fx_target_base(FXT_FLUX_FB, 0.9f);
}
```

- [ ] **Step 2: Add the two setups**

```cpp
void setup_instr_part_common(InstrPartGroup& g, int n_parts)
{
    // Seeds must match Instrument::init's, or the modulation streams differ
    // and so does voice timing: PART_A 0x1234abcd, PART_B 0x9e3779b9
    // (engine/instrument.cpp).
    // Draw every buffer from the same FxMem the Instrument rows get, so the
    // subtraction cannot be measuring different memory. Going through fx_mem()
    // rather than sampler_arena()/kSamplerFrames directly keeps this row on
    // the one accessor whose contents are guaranteed to match.
    const FxMem& mem = fx_mem();
    g.a.init(kSampleRate, 0x1234abcdu, mem.echo[PART_A],
             mem.sampler_buf[PART_A], mem.sampler_frames);
    configure_worst_bbd(g.a);
    if (n_parts == 2) {
        g.b.init(kSampleRate, 0x9e3779b9u, mem.echo[PART_B],
                 mem.sampler_buf[PART_B], mem.sampler_frames);
        configure_worst_bbd(g.b);
    }
    g.counter = 0;

    for (int b = 0; b < kInstrSettleBlocks; ++b) {
        const float* in = test_input();
        for (size_t i = 0; i < kBlock; ++i) {
            float ol, orr, sl, sr;
            g.a.process(in[i], in[i], ol, orr, sl, sr);
            if (n_parts == 2) g.b.process(in[i], in[i], ol, orr, sl, sr);
        }
    }

    // The self-check. STAGES at 1.0 must have settled to kMaxStages, and the
    // "1/32" rate at 120 BPM must have driven the clock onto its ceiling
    // (16384 / (2 * 0.0625) = 131072 Hz, clamped to kClockMaxHz). A row that
    // mirrored the configuration wrongly fails here instead of returning a
    // plausible number.
    g.stages_a = g.a.fx().flux().stages();
    g.clock_a  = g.a.fx().flux().clock_hz();
    assert(g.stages_a == bbd_tuning::kMaxStages);
    assert(g.clock_a >= bbd_tuning::kClockMaxHz);
    if (n_parts == 2) {
        g.stages_b = g.b.fx().flux().stages();
        g.clock_b  = g.b.fx().flux().clock_hz();
        assert(g.stages_b == bbd_tuning::kMaxStages);
        assert(g.clock_b >= bbd_tuning::kClockMaxHz);
    }
}

void setup_instr_part_1()
{
    setup_instr_part_common(g_instr_arena.emplace<InstrPartGroup>(), 1);
}

void setup_instr_part_2()
{
    setup_instr_part_common(g_instr_arena.emplace<InstrPartGroup>(), 2);
}
```

Add `#include <cassert>` at the top of the file if it is not already there.

- [ ] **Step 3: Add the two proc functions**

Two functions rather than one with a branch: a per-sample `if (n == 2)` would
put a branch inside the measured loop of *both* rows, and the whole value of
this pair is the difference between them.

```cpp
float proc_instr_part_1()
{
    auto& g = g_instr_arena.get<InstrPartGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr, sl, sr;
        g.a.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
    }
    if (++g.counter >= 250) { g.counter = 0; g.a.trigger_manual(); }
    acc += static_cast<float>(g.a.active_voices());
    acc += g.clock_a + static_cast<float>(g.stages_a);
    return acc;
}

float proc_instr_part_2()
{
    auto& g = g_instr_arena.get<InstrPartGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr, sl, sr;
        g.a.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
        g.b.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
    }
    if (++g.counter >= 250) {
        g.counter = 0;
        g.a.trigger_manual();
        g.b.trigger_manual();
    }
    acc += static_cast<float>(g.a.active_voices());
    acc += static_cast<float>(g.b.active_voices());
    acc += g.clock_a + static_cast<float>(g.stages_a);
    acc += g.clock_b + static_cast<float>(g.stages_b);
    return acc;
}
```

- [ ] **Step 4: Extend the workload table and the row expectation**

```cpp
const Workload kInstrWorkloads[] = {
    { "instr", "instr_part_1", setup_instr_part_1, proc_instr_part_1 },
    { "instr", "instr_part_2", setup_instr_part_2, proc_instr_part_2 },
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
};
```

and in `bench/run.py`, matching that order exactly:

```python
    "instr": (
        "instr_part_1",
        "instr_part_2",
        "instr_noverb",
    ),
```

Add matching assertions to `AblateProfileTest`.

- [ ] **Step 5: Verify the configuration mirror line by line**

Before building, open `bench/workloads_system.cpp`'s `setup_inst_worst` and
`setup_inst_worst_bbd` beside `configure_worst_bbd`, and check each call has
its counterpart with the same value and in the same relative order. **List
every line and its counterpart in your report.** This is the round's single
point of failure (spec §4): a divergence here makes all three differences
measure the divergence instead of what they name.

- [ ] **Step 6: Build and run the contract test**

```bash
cd bench && python -m unittest test_run_contract && cd ..
source ./env.sh && python bench/run.py --profile ablate --build-only
```

Expected: contract tests pass; the image links. Report the SRAM and SRAM_EXEC
lines again — `InstrPartGroup` may now be the arena's largest group.

- [ ] **Step 7: Commit**

```bash
git add bench/
git commit -m "$(cat <<'EOF'
bench(instr): the two bare-Part rungs

One Part and two Parts, configured deck-for-deck as Instrument
configures its own, with nothing wrapping them. The difference against
instr_noverb is the instrument-level glue; the difference between the
two is what running a second deck costs in contention alone.

Both rows read back FLUX's settled stage count and clock and assert
them, so a row that mirrored the configuration wrongly fails loudly
instead of returning a plausible number.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: The hardware measurement

**The Daisy Seed must be attached.** The owner confirmed it is.

**Files:**
- Create: `docs/bench/2026-07-29-<sha>-ablate.md` and `.csv`, `<sha>` being
  `git rev-parse --short HEAD`

- [ ] **Step 1: Confirm the tree is clean and `engine/` is untouched**

```bash
git status --short
git diff main -- engine/
```

Expected: both empty. The bench refuses hardware evidence from a dirty tree,
and an engine diff would break this round's central promise.

- [ ] **Step 2: Build, then rebind, then measure — in that order**

`--no-build --program-qspi` binds the QSPI receipt to whatever `bench.elf` is
on disk; if the measuring run then relinks anything, the hashes differ and the
run aborts with `QSPI verification receipt does not match current payload
(artifacts)` **after** reprogramming the bank for nothing. That message names
the artifacts, not the bank — it means a stale binding, not corrupt flash.

```bash
source ./env.sh
python bench/run.py --profile ablate --build-only
python bench/run.py --profile ablate --no-build --program-qspi --build-only
python bench/run.py --profile ablate
```

- [ ] **Step 3: Check the rows that must NOT have moved**

This round changes no engine file, so every `system` row should return the
checksum it returned in `docs/bench/2026-07-29-1ba3f18-sweep.csv`. Compare
them.

**A moved checksum is a real finding, not noise** — it would mean a new
translation unit shifted the layout enough to matter, which is exactly the
effect the mono round observed on `fx_grit` and could not prove. Report any
that moved, with both values. Do not proceed to the write-up assuming it is
nothing.

Costs *may* move even where checksums do not; that is the layout effect and is
expected. Record it.

- [ ] **Step 4: Save the evidence and commit**

Copy the run's markdown and CSV to `docs/bench/2026-07-29-<sha>-ablate.md` /
`.csv`, following the layout of `docs/bench/2026-07-29-1ba3f18-sweep.md`.

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): instrument-level ablation ladder

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: The written verdict

**Files:**
- Modify: `docs/superpowers/specs/2026-07-29-instrument-ablation-design.md`
  (append `## 8. Results`)
- Modify: `docs/roadmap.md`

- [ ] **Step 1: Compute the three differences and say what they mean**

Using `pct_max` from **run 2**, and checking `avg_cyc` agrees in direction:

- **`instr_part_2` − 2 × `instr_part_1`** — deck contention.
  **Name the bias:** the per-sample accumulate loop is common to both rows and
  appears once in `instr_part_2` but twice in `2 × instr_part_1`, so this
  difference is biased low by one loop's worth. `empty_callback` bounds that
  loop at effectively 0.00 points, so the bias is negligible — but state it
  rather than leaving a reader to find it.
- **`instr_noverb` − `instr_part_2`** — the instrument glue.
- **`instrument_worst_bbd` − `instr_noverb`** — the reverb in situ. Compare
  this against `oliverb_solo_sram`'s isolated 9.47: if the in-situ figure is
  materially larger, the difference is the mixing and smoothing the isolated
  row never included, which is itself a small finding.

- [ ] **Step 2: Answer the round's question directly**

State, in one paragraph near the top, whether the ~23-point gap is **glue** or
**contention**, and in what proportion. §5 of the spec commits in advance to
writing a null result as plainly as a positive one: if it is mostly
contention, say that no instrument-level cut exists to be found and that the
remaining 12.88 points must come from inside the blocks.

Also reconcile with the arithmetic that motivated the round: the ~23 figure
came from summing rows measured at *different* operating points — in
particular FLUX was priced at its boot configuration while the gate runs it
hot. Say what the ladder makes of that.

- [ ] **Step 3: Say whether a finer round is worth running**

The spec's §5 makes this the round's decision: the finer ladder — reverb
smoothers, MORPH, `process_in`, `derive_intervals` measured separately — is
only worth running if this one shows real work sitting in the glue. Give a
recommendation with the number behind it.

- [ ] **Step 4: Update `docs/roadmap.md`**

Record what the round did, the three differences, the answer, the branch, and
what remains: the 12.88-point gap, the open panning decision from the mono
round, and the `bbd.h` `1/sr_` division (~0.6 points, still standing).

- [ ] **Step 5: Confirm the branch's shape and commit**

```bash
git diff --stat main..HEAD
git diff main -- engine/
```

Expected: the second is empty; the first shows only `bench/` and `docs/`.

```bash
git add docs/
git commit -m "$(cat <<'EOF'
docs: the instrument-level ablation, measured

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

- [ ] **Step 6: Do not merge**

Do not merge `perf/instrument-ablation` into `main` without being asked.
