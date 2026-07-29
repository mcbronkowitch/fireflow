# FX Cost Curves Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `sweep` bench family that measures CPU cost across each suspect control's travel, so the settings that cost disproportionately become visible as settings rather than as totals.

**Architecture:** A new firmware translation unit `bench/workloads_sweep.cpp` contributes one bench row per sample point, in the established `{family, name, setup_fn, proc_fn}` table form. A new `sweep` profile carries `system` alongside it so the `instrument_worst` anchor appears in the same report. The host side — `run.py`'s row expectations and `profiles.py` — is covered by real unit tests; the firmware side is proved by linking and then by one hardware run.

**Tech Stack:** C++17 (arm-none-eabi-g++, Cortex-M7, `-ffast-math`), Python 3 with `unittest` for the host-side bench controller, OpenOCD + ST-Link V3 for the measurement.

**Spec:** `docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md`

## Global Constraints

- **Branch.** All work happens on `perf/fx-cost-curves`, branched from `main`. Never commit to `main` directly.
- **Commit trailer.** Every commit ends with `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`. Nothing else.
- **`engine/` is off limits**, with exactly one exception: the test-only clock accessor in Task 5. No other production file may change in this plan. If a task seems to need one, stop and report rather than editing.
- **Never run `python run.py` without `--profile`.** The default is `full`, which does not link by design (`bench/README.md:34`).
- **Hardware evidence is refused from a dirty git tree.** Commit before measuring.
- **Desktop suite stays green.** `source env.sh` then the project's normal ctest invocation, after every task that touches anything outside `bench/`.
- **Do not build `host/vcv` in this plan.** It is untouched here, and building it wrongly is a known trap (it requires `./build-local.sh`).
- Python tests run from `bench/` as `python -m unittest test_run_contract -v`.
- Firmware link check runs from `bench/` as `python run.py --profile sweep --build-only`.

## File Structure

| File | Responsibility |
|---|---|
| `bench/workloads_sweep.cpp` | **Create.** All sweep rows and their arena. One TU so the family can be dropped whole by `--gc-sections`. |
| `bench/workload.h` | **Modify.** Declare `kSweepWorkloads` / `kSweepCount` alongside the existing family tables. |
| `bench/families.cpp` | **Modify.** One `#if BENCH_FAMILY_SWEEP` registry entry. |
| `bench/Makefile` | **Modify.** `FAMILY_SOURCE_sweep` and `FAMILY_DEFINE_sweep` entries. |
| `bench/profiles.py` | **Modify.** The `sweep` profile. |
| `bench/run.py` | **Modify.** The `sweep` entry in `BENCH_PROTOCOL_ROWS_BY_FAMILY`. |
| `bench/test_run_contract.py` | **Modify.** Tests for the new profile and its row expectations. |
| `engine/fx/flux.h` | **Modify, Task 5 only.** A test-only clock accessor. |
| `docs/bench/2026-07-29-*-sweep.{md,csv}` | **Create, Task 9.** Accepted hardware evidence. |
| `docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md` | **Modify, Task 10.** The written reading, appended as a results section. |
| `docs/roadmap.md` | **Modify, Task 10.** The outcome. |

**Row-count bookkeeping.** Every task that adds firmware rows must add the same names to `run.py`'s expectation in the same commit. `run.py` fails the run if the measured row set differs by one row. This is intended behaviour, not an obstacle to work around.

---

### Task 1: Scaffold the family and prove it links

The spec calls this out as the risk to retire first: `workloads_sweep.cpp` needs its own `SerialArena` (the `system` one is in an anonymous namespace and unreachable), and the instrument-level sweeps set its size. Find out now, with one trivial row, whether the image still links and how much SRAM it costs.

**Files:**
- Create: `bench/workloads_sweep.cpp`
- Modify: `bench/workload.h`, `bench/families.cpp`, `bench/Makefile`, `bench/profiles.py`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: nothing.
- Produces: the `sweep` family and profile; `bench::kSweepWorkloads` / `bench::kSweepCount`; the row name `sweep_probe`.

- [ ] **Step 1: Create the branch**

```bash
cd /c/Users/bernd/Documents/AI/Spotykach
git checkout main && git pull --ff-only
git checkout -b perf/fx-cost-curves
```

- [ ] **Step 2: Write the failing host-side test**

Add to `bench/test_run_contract.py`. Put it next to the other profile tests; the file already imports `Profile, WAVE_ACCEPTANCE, resolve` from `profiles`.

```python
class SweepProfileTest(unittest.TestCase):
    def test_sweep_profile_resolves_and_carries_system(self):
        profile = resolve("sweep", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertEqual(profile.families, ("system", "sweep"))
        self.assertIn(WAVE_ACCEPTANCE, profile.gates)

    def test_sweep_family_has_row_expectations(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertIn("sweep_probe", rows)
        self.assertEqual(len(rows), len(set(rows)), "duplicate row names")
```

- [ ] **Step 3: Run it to confirm it fails**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: FAIL — `KeyError: 'sweep'` from `resolve`, because `PROFILES` has no such entry.

- [ ] **Step 4: Add the profile**

In `bench/profiles.py`, add to `PROFILES`, after the `bbd` entry:

```python
    # The cost-curve round (spec 2026-07-29-fx-cost-curves-design). Carries
    # `system` deliberately, unlike `bbd`: without it verdict() has no
    # instrument_worst anchor and reports "undetermined", which is exactly
    # how the BBD numbers came to stand for two days without a system
    # verdict. `body` (system + body) is the precedent that a two-family
    # image links.
    "sweep": Profile(
        families=("system", "sweep"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
```

- [ ] **Step 5: Add the row expectation**

In `bench/run.py`, add to `BENCH_PROTOCOL_ROWS_BY_FAMILY`, after the `"system"` entry:

```python
    "sweep": (
        "sweep_probe",
    ),
```

- [ ] **Step 6: Run the host test to verify it passes**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: PASS, both tests.

- [ ] **Step 7: Create the firmware translation unit**

Create `bench/workloads_sweep.cpp`:

```cpp
#include "workload.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "fx/bbd.h"
#include "fx/flux.h"
#include "fx/grit.h"
#include "fx/part_fx.h"

namespace bench {
namespace {

using namespace spky;

// This family's own arena. workloads_system.cpp's g_system_arena is a static
// in an ANONYMOUS namespace -- unreachable from here by design, and exporting
// it would mean editing workloads_system.cpp, which perturbs exactly the code
// layout this round is investigating (spec S4.6).
//
// SerialArena overlays its groups: `capacity` is the max sizeof, not the sum.
// So the cost of this second arena is one more max-sized .bss block, and
// SweepInstrumentGroup is what sets that maximum. Task 1 measured the delta
// before the rows were written -- see the plan.
struct SweepInstrumentGroup {
    Instrument instrument;
    float out_l[kBlock], out_r[kBlock];
};

SerialArena<SweepInstrumentGroup> g_sweep_arena;

// A trivial row whose only job is to prove the family links and registers.
// It is replaced by real rows in later tasks and must not survive to the
// hardware run.
void setup_sweep_probe()
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    group.instrument.init(kSampleRate);
}

float proc_sweep_probe()
{
    auto& group = g_sweep_arena.get<SweepInstrumentGroup>();
    group.instrument.process(nullptr, group.out_l, group.out_r, kBlock);
    return group.out_l[0] + group.out_r[0];
}

} // namespace

const Workload kSweepWorkloads[] = {
    { "sweep", "sweep_probe", setup_sweep_probe, proc_sweep_probe },
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
```

**Note for the implementer:** `Instrument::process`'s exact signature is the one `workloads_system.cpp`'s `proc_inst` already uses. Read that function and match it — do not trust the line above if it disagrees. Same for `init`.

- [ ] **Step 8: Declare the table in `bench/workload.h`**

After the `kBbdWorkloads` declaration:

```cpp
extern const Workload kSweepWorkloads[];
extern const int      kSweepCount;
```

- [ ] **Step 9: Register the family in `bench/families.cpp`**

After the `BENCH_FAMILY_BBD` block and before `BENCH_FAMILY_BODY`:

```cpp
#if BENCH_FAMILY_SWEEP
    { "sweep",   kSweepWorkloads,   kSweepCount   },
#endif
```

- [ ] **Step 10: Wire the Makefile**

In `bench/Makefile`, add alongside the existing entries:

```make
FAMILY_SOURCE_sweep   = workloads_sweep.cpp
FAMILY_DEFINE_sweep   = BENCH_FAMILY_SWEEP
```

Do **not** add `sweep` to the `BENCH_FAMILIES ?=` default line: that line is the `full` profile's family list, and `full` already fails to link on purpose. Adding to it would change what "full fails" means.

- [ ] **Step 11: Link it and record the SRAM cost**

```bash
cd bench && python run.py --profile sweep --build-only
```

Expected: builds and links. The link step prints a memory table; **record the `SRAM` and `SRAM_EXEC` lines**. The `system`-only baseline from 2026-07-29 is `SRAM_EXEC 182728 / 262880 (69.51 %)` and `SRAM 205408 / 261408 (78.58 %)`.

**If it fails to link with a region overflow:** stop and report. Do not start deleting rows — the spec's §8 fallback (drop sweeps C and D together, never `system`) is a decision for the owner, not for the implementer, and it is worth knowing the exact overflow figure before taking it.

- [ ] **Step 12: Commit**

```bash
git add bench/workloads_sweep.cpp bench/workload.h bench/families.cpp bench/Makefile bench/profiles.py bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): scaffold the cost-curve family and prove it links

One trivial row, all four registration points wired, so the memory
question is answered before nineteen rows are written. The family needs
its own SerialArena -- workloads_system.cpp's is in an anonymous
namespace, and exporting it would edit the very file whose code layout
S4.6 is investigating.

SRAM after: <fill in from step 11>
SRAM_EXEC after: <fill in from step 11>

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

Replace the two `<fill in>` lines with the real figures from step 11 before committing.

---

### Task 2: Sweep A — the FLUX clock, 5 rows

The most likely steep knee: the clock drives the tick loop linearly until `kClockMaxHz` clamps it, and this is most of what separates `instrument_worst_bbd` from `instrument_worst`.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: the `sweep` family from Task 1.
- Produces: rows `sweep_flux_rate_0`, `sweep_flux_rate_3`, `sweep_flux_rate_6`, `sweep_flux_rate_9`, `sweep_flux_rate_11`.

- [ ] **Step 1: Write the failing host-side test**

Add to `SweepProfileTest` in `bench/test_run_contract.py`:

```python
    def test_flux_rate_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for index in (0, 3, 6, 9, 11):
            self.assertIn("sweep_flux_rate_%d" % index, rows)
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: FAIL — `'sweep_flux_rate_0' not found in ('sweep_probe',)`.

- [ ] **Step 3: Add the row names to `run.py`**

Replace the `"sweep"` tuple in `BENCH_PROTOCOL_ROWS_BY_FAMILY` with:

```python
    "sweep": (
        "sweep_probe",
        "sweep_flux_rate_0",
        "sweep_flux_rate_3",
        "sweep_flux_rate_6",
        "sweep_flux_rate_9",
        "sweep_flux_rate_11",
    ),
```

- [ ] **Step 4: Run the host test to verify it passes**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: PASS.

- [ ] **Step 5: Add the firmware rows**

First read `bench/workloads_system.cpp`'s `setup_fx` and `proc_fx` and mirror their shape — the same `PartFx` + `values[FXT_COUNT]` pairing, the same `set_fx_on(..., true)` with `immediate = true`. Then add to `workloads_sweep.cpp`, inside the anonymous namespace:

```cpp
// --- Sweep A: cost against the FLUX division ladder --------------------------
// engine/mod/divisions.h: kFluxRateCount == 12. Index 11 is the shortest
// division, which drives the clock onto kClockMaxHz -- 1.33 ticks per audio
// sample at 32 kHz. Five points, so the curve between the ends is visible and
// not merely interpolated.
struct SweepFxGroup {
    PartFx fx;
    float  values[FXT_COUNT];
};

void setup_flux_rate(int rate_index)
{
    auto& group = g_sweep_arena.emplace<SweepFxGroup>();
    const FxMem& m = fx_mem();
    group.fx.init(kSampleRate, m.echo[0][0], m.echo[0][1]);
    group.fx.set_fx_on(FxBlock::Grit, false, true);
    group.fx.set_fx_on(FxBlock::Flux, true,  true);
    group.fx.set_comp(0.f);
    for (int t = 0; t < FXT_COUNT; ++t) group.values[t] = 0.f;
    group.fx.set_flux_rate(rate_index);

    // Settle OUTSIDE the measured window: an unsettled BBD line measures an
    // empty machine, consistently across both runs, so the checksum gate
    // cannot catch it. setup_bbd_ceiling's precedent is four line-fills.
    const float* in = test_input();
    for (int i = 0; i < 49152; ++i) {
        float l = in[i % kBlock], r = l * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
    }
}

void setup_flux_rate_0()  { setup_flux_rate(0);  }
void setup_flux_rate_3()  { setup_flux_rate(3);  }
void setup_flux_rate_6()  { setup_flux_rate(6);  }
void setup_flux_rate_9()  { setup_flux_rate(9);  }
void setup_flux_rate_11() { setup_flux_rate(11); }

float proc_sweep_fx()
{
    auto& group = g_sweep_arena.get<SweepFxGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
        acc += l + r + sl + sr;
    }
    return acc;
}
```

Add `SweepFxGroup` to the arena's type list:

```cpp
SerialArena<SweepInstrumentGroup, SweepFxGroup> g_sweep_arena;
```

**Two things the implementer must verify against the real headers rather than trusting this listing:** the exact name and signature of `PartFx`'s FLUX-rate setter (it may be `set_flux_rate(int)` or take a normalized float — `engine/mod/divisions.h` has `kFluxRateCount` and a norm-to-index helper), and whether `set_comp` or the `values` array needs anything else to keep GRIT and COMP genuinely out of the measurement. `fx_none` costing 2.56 % is the reference: this row minus a rate-0 baseline should be plausible against it.

- [ ] **Step 6: Add the rows to the table**

```cpp
const Workload kSweepWorkloads[] = {
    { "sweep", "sweep_probe",       setup_sweep_probe,  proc_sweep_probe },
    { "sweep", "sweep_flux_rate_0",  setup_flux_rate_0,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_3",  setup_flux_rate_3,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_6",  setup_flux_rate_6,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_9",  setup_flux_rate_9,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_11", setup_flux_rate_11, proc_sweep_fx },
};
```

- [ ] **Step 7: Link**

```bash
cd bench && python run.py --profile sweep --build-only
```

Expected: builds and links. Record the SRAM figures again and note the delta against Task 1.

- [ ] **Step 8: Commit**

```bash
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): cost against the FLUX division ladder, five points

The clock drives the tick loop linearly until kClockMaxHz clamps it, so
this is where the knee is most likely and it is most of what separates
instrument_worst_bbd from instrument_worst. Each setup settles 49152
samples outside the measured window -- an unsettled line measures an
empty machine consistently across both runs, which the checksum gate
cannot catch.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: Sweep B — STAGES, 4 rows

The suspicion here is memory, not arithmetic: stage count does not change the tick rate at a fixed clock, but four lines at 16384 stages hold 128 KB of SDRAM against a 16 KB D-cache.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `SweepFxGroup`, `proc_sweep_fx` from Task 2.
- Produces: rows `sweep_stages_512`, `sweep_stages_2048`, `sweep_stages_8192`, `sweep_stages_16384`.

- [ ] **Step 1: Write the failing host-side test**

```python
    def test_stages_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for stages in (512, 2048, 8192, 16384):
            self.assertIn("sweep_stages_%d" % stages, rows)
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: FAIL on `sweep_stages_512`.

- [ ] **Step 3: Add the row names to `run.py`**

Append to the `"sweep"` tuple:

```python
        "sweep_stages_512",
        "sweep_stages_2048",
        "sweep_stages_8192",
        "sweep_stages_16384",
```

- [ ] **Step 4: Run the host test to verify it passes**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: PASS.

- [ ] **Step 5: Add the firmware rows**

`Flux::set_stages` is geometric — `512 * 32^n` — so the four norms are the solutions of `512 * 32^n = S`:

| stages | norm |
|---|---|
| 512 | 0.0 |
| 2048 | 0.4 |
| 8192 | 0.8 |
| 16384 | 1.0 |

```cpp
// --- Sweep B: cost against STAGES --------------------------------------------
// Flux::set_stages is geometric, 512 * 32^n, so these norms are exact:
// 32^0.4 == 4 and 32^0.8 == 16. The hypothesis under test is CACHE, not
// arithmetic -- stage count does not change the tick rate at a fixed clock,
// but it does change how much SDRAM the line walks against a 16 KB D-cache.
// The clock is held at one division for all four rows so stages is the only
// variable; index 6 is mid-ladder, away from both the ceiling clamp and the
// longest division.
void setup_stages(float norm)
{
    auto& group = g_sweep_arena.emplace<SweepFxGroup>();
    const FxMem& m = fx_mem();
    group.fx.init(kSampleRate, m.echo[0][0], m.echo[0][1]);
    group.fx.set_fx_on(FxBlock::Grit, false, true);
    group.fx.set_fx_on(FxBlock::Flux, true,  true);
    group.fx.set_comp(0.f);
    for (int t = 0; t < FXT_COUNT; ++t) group.values[t] = 0.f;
    group.fx.set_flux_rate(6);
    group.fx.set_stages(norm);

    const float* in = test_input();
    for (int i = 0; i < 49152; ++i) {
        float l = in[i % kBlock], r = l * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
    }
}

void setup_stages_512()   { setup_stages(0.0f); }
void setup_stages_2048()  { setup_stages(0.4f); }
void setup_stages_8192()  { setup_stages(0.8f); }
void setup_stages_16384() { setup_stages(1.0f); }
```

**Verify before trusting:** that `PartFx` exposes `set_stages` with that name and a normalized argument — `engine/fx/part_fx.h` is the header to read, and `Flux::set_stages(float norm)` at `engine/fx/flux.cpp:156` is what it should reach. Also confirm the norm-to-stage mapping empirically if a stage-count accessor exists; if the mapping is off, the row names lie about what they measure, which is worse than a missing row.

- [ ] **Step 6: Add the four rows to `kSweepWorkloads`, each with `proc_sweep_fx`**

- [ ] **Step 7: Link**

```bash
cd bench && python run.py --profile sweep --build-only
```

Expected: builds and links.

- [ ] **Step 8: Commit**

```bash
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): cost against STAGES, four points

The hypothesis is cache, not arithmetic: stage count does not change the
tick rate at a fixed clock, but four lines at 16384 stages walk 128 KB of
SDRAM against a 16 KB D-cache. If the knee is here, halving kMaxStages is
not a sound compromise at all -- it is the fix.

Clock held at division 6 for all four rows so stages is the only variable.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: Ablation F — the `fx_grit` anomaly, 2 rows

`fx_grit` rose from 4.78 % to 7.70 % max between `518f639` and `1f7671d` with an **identical checksum**, no commits to `engine/fx/grit.{cpp,h}` in that range, FLUX provably not leaking in, and `fx_none` unmoved. Three candidate causes; two rows separate them.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `SweepFxGroup`, `proc_sweep_fx` from Task 2.
- Produces: rows `sweep_grit_bare`, `sweep_grit_no_bbd_mem` (the second is conditional — see Step 5).

- [ ] **Step 1: Write the failing host-side test**

```python
    def test_grit_ablation_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertIn("sweep_grit_bare", rows)
```

Note this asserts only the unconditional row. If Step 5 establishes that the second row is buildable, extend this test in the same commit.

- [ ] **Step 2: Run it to confirm it fails**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

Expected: FAIL on `sweep_grit_bare`.

- [ ] **Step 3: Add `sweep_grit_bare` to `run.py`'s tuple**

- [ ] **Step 4: Run the host test to verify it passes**

- [ ] **Step 5: Establish whether the second row is buildable**

Read `engine/fx/flux.cpp`'s `init` and find `_buf_ok`. `Flux::process` returns at `flux.cpp:278` (`if (!_buf_ok) return;`) before doing anything.

The question: does `PartFx::init(kSampleRate, nullptr, nullptr)` leave `_buf_ok` false and construct without dereferencing? Read `PartFx::init` (`engine/fx/part_fx.cpp:10`) and `Flux::init`, and follow through to `BbdLine::Init` — whose own comment in `engine/fx/bbd.h` says `Init(nullptr, 0, sr)` leaves `cells_` at its floor of 1.

- If it is safe, build `sweep_grit_no_bbd_mem`.
- If it is not, **do not change production code to make it safe.** Drop the row, and say so in the commit message and in the plan's Task 10 reading.

- [ ] **Step 6: Add the firmware rows**

```cpp
// --- Ablation F: where did fx_grit's 2.9 points come from? -------------------
// fx_grit rose 4.78 -> 7.70 % max between 518f639 and 1f7671d at an IDENTICAL
// checksum, with no commits to engine/fx/grit.{cpp,h} in range and fx_none
// unmoved. Three candidates: GRIT itself, the PartFx shell, or cache pressure
// from the BBD buffers merely being resident.
//
// fx_grit - fx_none is 5.14 today against a historical 2.22.
//   sweep_grit_bare ~ 5.14                 -> GRIT costs that; the old figure
//                                             was a smaller image, i.e. layout
//   sweep_grit_bare ~ 2.2, no_bbd_mem ~5.14 -> the shell is the suspect
//   sweep_grit_no_bbd_mem ~ 2.2             -> cache pressure from the BBD
//                                             buffers; nothing in FX is wrong,
//                                             and sweep B should show it too
struct SweepGritGroup {
    Grit grit;
};

void setup_grit_bare()
{
    auto& group = g_sweep_arena.emplace<SweepGritGroup>();
    group.grit.init(kSampleRate);
    // Match setup_fx(SEL_GRIT)'s settings exactly -- read workloads_system.cpp
    // and copy them rather than inventing values, or this row answers a
    // different question than the one asked.
}

float proc_grit_bare()
{
    auto& group = g_sweep_arena.get<SweepGritGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f;
        group.grit.process(l, r);
        acc += l + r;
    }
    return acc;
}
```

Add `SweepGritGroup` to the arena's type list. **Read `engine/fx/grit.h` for the real `init` and `process` signatures** — the listing above is the shape, not a promise.

For `sweep_grit_no_bbd_mem`, reuse `SweepFxGroup` and `proc_sweep_fx`, with a setup identical to `setup_fx(SEL_GRIT)` except that `init` receives null echo memory.

- [ ] **Step 7: Link**

```bash
cd bench && python run.py --profile sweep --build-only
```

- [ ] **Step 8: Commit**

```bash
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): two rows to locate fx_grit's unexplained 2.9 points

The rise happened at an identical checksum with no source change, so the
cause is GRIT itself, the shell around it, or cache pressure from the BBD
buffers being resident. Bare GRIT and GRIT-without-BBD-memory separate
the three.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: Ablation E — the Flux wrapper, 1 row

Today the wrapper's cost is an inference from two rows measured in different contexts (~5.7 points per deck). This makes it a measurement. It matters because the wrapper's per-sample work runs every sample although its inputs move only on the 96-sample control tick — and unlike everything else here, fixing that changes the sound at no setting.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`, `engine/fx/flux.h`

**Interfaces:**
- Consumes: the arena from Task 1.
- Produces: row `sweep_flux_lines_2ch`; `spky::Flux::clock_hz_for_test()`.

- [ ] **Step 1: Write the failing host-side test**

```python
    def test_flux_wrapper_ablation_row_is_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertIn("sweep_flux_lines_2ch", rows)
```

- [ ] **Step 2: Run it to confirm it fails, then add the name to `run.py` and re-run to pass**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
```

- [ ] **Step 3: Add the test-only accessor**

This is the plan's **only** permitted `engine/` change. In `engine/fx/flux.h`, next to the other public accessors:

```cpp
    // Observer for tests and the bench only: the clock the ladder and the
    // lane arrived at this sample. Flux already stores it as _clock_hz; the
    // accessor exists so a bench row can drive two bare BbdEcho at exactly
    // the clock a real Flux would give them, which is what makes
    // "fx_flux_sdram - sweep_flux_lines_2ch - fx_none" the wrapper's own
    // cost rather than an estimate. Compander::env_comp()/env_exp() in
    // engine/fx/bbd.h are the existing precedent for this shape.
    float clock_hz_for_test() const { return _clock_hz; }
```

Confirm `_clock_hz` is the member's real name (`engine/fx/flux.cpp:319` assigns it) and that this compiles for both the desktop and the ARM builds.

- [ ] **Step 4: Run the desktop suite**

```bash
cd /c/Users/bernd/Documents/AI/Spotykach
source env.sh
# then the project's normal ctest invocation
```

Expected: green. An accessor cannot break a test, but this is the one task that touches `engine/`, so it is verified rather than assumed.

- [ ] **Step 5: Add the firmware row**

```cpp
// --- Ablation E: the Flux wrapper's own cost ---------------------------------
// Two bare BbdEcho at the stage count and clock a default-initialised Flux
// computes, with no Flux around them. Then
//   fx_flux_sdram - sweep_flux_lines_2ch - fx_none
// is the wrapper's own per-sample work: two fonepole slews, two std::fabs
// snaps, a clampf and bbd_clock_hz's division -- all of which run every
// sample although their inputs only move on the 96-sample control tick.
struct SweepLineGroup {
    BbdEcho l, r;
    float   clock_hz;
};

void setup_flux_lines_2ch()
{
    // Derive the clock from a real Flux rather than hard-coding it, so this
    // row cannot silently drift away from what fx_flux_sdram measures.
    static Flux probe;
    const FxMem& m = fx_mem();
    probe.init(kSampleRate, m.echo[1][0], m.echo[1][1]);
    {
        float pl = 0.f, pr = 0.f;
        probe.process(pl, pr);
    }
    const float hz = probe.clock_hz_for_test();

    auto& group = g_sweep_arena.emplace<SweepLineGroup>();
    group.clock_hz = hz;
    group.l.Init(kSampleRate, m.echo[0][0], Flux::kMaxSamples);
    group.r.Init(kSampleRate, m.echo[0][1], Flux::kMaxSamples);

    const float* in = test_input();
    for (int i = 0; i < 49152; ++i) {
        const float x = in[i % kBlock];
        group.l.Process(x, hz);
        group.r.Process(x * 0.9f, hz);
    }
}

float proc_flux_lines_2ch()
{
    auto& group = g_sweep_arena.get<SweepLineGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        acc += group.l.Process(in[i], group.clock_hz);
        acc += group.r.Process(in[i] * 0.9f, group.clock_hz);
    }
    return acc;
}
```

Add `SweepLineGroup` to the arena's type list.

**Four things to verify against the headers, because this listing is the shape and not a promise:** `BbdEcho::Init`'s exact parameter order (`workloads_bbd.cpp`'s `setup_bbd_ceiling` calls `g_echo.Init(kSampleRate, sdram_arena(), Flux::kMaxSamples)` — match that); that `Flux::kMaxSamples` is the right capacity constant; that `m.echo[1][*]` is a genuinely separate buffer pair from `m.echo[0][*]` so the probe does not disturb the measured lines; and that the probe `Flux` does not need `set_bpm` or a control tick before its clock is meaningful.

- [ ] **Step 6: Add the row to `kSweepWorkloads`**

- [ ] **Step 7: Link**

```bash
cd bench && python run.py --profile sweep --build-only
```

- [ ] **Step 8: Commit**

```bash
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py engine/fx/flux.h
git commit -m "$(cat <<'EOF'
bench(sweep): measure the Flux wrapper instead of inferring it

Two bare BbdEcho at the clock a real Flux computes, so
fx_flux_sdram - sweep_flux_lines_2ch - fx_none is the wrapper's own cost
rather than a comparison of rows measured in different contexts. The
wrapper runs two fonepole slews, two fabs snaps, a clamp and a division
every sample although the inputs move on the 96-sample control tick --
and unlike every other lever in this round, fixing that changes the sound
at no setting.

Adds Flux::clock_hz_for_test(), this plan's only engine/ change.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: Sweep C — voice count, 4 rows

35.8 points for eight voices is the single largest item. The question is whether cost grows linearly or whether the last voice costs more than the first.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `SweepInstrumentGroup` from Task 1.
- Produces: rows `sweep_voices_1` … `sweep_voices_4`.

- [ ] **Step 1: Write the failing host-side test, add the four names to `run.py`, re-run to pass**

```python
    def test_voice_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for n in (1, 2, 3, 4):
            self.assertIn("sweep_voices_%d" % n, rows)
```

- [ ] **Step 2: Find the real COLOR-to-chord-size mapping**

**Do not assume 0.0 / 0.33 / 0.67 / 1.0.** Read the chord layer (`engine/` — start from `Instrument::set_color` and follow it) and find the norm values that actually yield 1, 2, 3 and 4 notes. Write the four values into a comment in the row, with the file and function they came from.

A row that believes it is measuring three voices while four are sounding puts a knee in the curve that is not there — which is the exact failure this whole round exists to avoid.

- [ ] **Step 3: Add the firmware rows**

Base the setup on `workloads_system.cpp`'s `setup_inst_worst`, changed in exactly one respect: COLOR. Everything else — density, depth, rate, FX, comp, reverb, master drive — stays at the worst-case values, so the curve is measured on the instrument the gate is set on.

```cpp
// --- Sweep C: cost against voice count ---------------------------------------
// setup_inst_worst with COLOR as the only variable. Everything else stays at
// its worst-case value so this curve is measured on the same instrument the
// gate is set on.
//
// COLOR norms for 1..4 notes, from <file>::<function>:
//   1 note  -> <norm>
//   2 notes -> <norm>
//   3 notes -> <norm>
//   4 notes -> <norm>
void setup_voices(float color_norm)
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    // ... mirror setup_inst_worst exactly, with set_color(p, color_norm) ...
}
```

Fill in the norms found in Step 2 and mirror `setup_inst_worst` line by line.

- [ ] **Step 4: Confirm each row got the voice count it intended**

If the engine exposes an active-voice count reachable from the bench (`active_voices()` exists on the engines — `proc_engine_2x4` in `workloads_system.cpp` already folds it into its checksum), fold it into `proc`'s return value the same way. Then the checksum itself carries the voice count, and a wrong mapping shows up as an unexpected checksum rather than as a plausible wrong number.

If it is not reachable, say so in the commit message — an unverified mapping is a caveat the reading in Task 10 must carry.

- [ ] **Step 5: Link, then commit**

```bash
cd bench && python run.py --profile sweep --build-only
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): cost against voice count, four points

setup_inst_worst with COLOR as the only variable, so the curve is
measured on the same instrument the gate is set on. Eight voices are
35.8 points, the largest single item; this says whether the last voice
costs more than the first.

The COLOR norms are read from the chord layer's own mapping, not assumed
to be evenly spaced -- a row that believes it has three voices while four
sound would invent a knee.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: Sweep D — reverb, 3 rows

`setup_inst_worst` puts DIFF, SMEAR and MOD near maximum, and in practice they are one gesture.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `SweepInstrumentGroup` from Task 1, the `setup_inst_worst` mirror from Task 6.
- Produces: rows `sweep_room_lo`, `sweep_room_mid`, `sweep_room_hi`.

- [ ] **Step 1: Write the failing host-side test, add the three names to `run.py`, re-run to pass**

```python
    def test_room_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for name in ("sweep_room_lo", "sweep_room_mid", "sweep_room_hi"):
            self.assertIn(name, rows)
```

- [ ] **Step 2: Add the firmware rows**

Same `setup_inst_worst` mirror, with DIFF / SMEAR / MOD swept together and everything else — including COLOR at its worst-case value — held:

```cpp
// --- Sweep D: cost against the room controls ---------------------------------
// DIFF, SMEAR and MOD move together: setup_inst_worst puts all three near
// maximum and in practice they are one gesture. lo = 0.0, mid = 0.45,
// hi = 0.9, matching setup_inst_worst's own diffusion value at the top.
void setup_room(float amount)
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    // ... mirror setup_inst_worst, then: ...
    // group.instrument.set_reverb_diffusion(amount);
    // group.instrument.set_reverb_smear(amount / 0.9f);
    // group.instrument.set_reverb_mod(amount / 0.9f);
}
```

Read `setup_inst_worst` for the real setter names and the exact top values (diffusion 0.9, smear 1.0, mod 1.0) and scale the three consistently, so `sweep_room_hi` reproduces `setup_inst_worst`'s room exactly.

- [ ] **Step 3: Settle the reverb outside the measured window**

The reverb is stateful and its tail is long. Run the instrument for at least the reverb's decay time before measuring — `setup_inst_worst`'s own warm-up is the reference; if it relies on the runner's fixed 100-block warm-up, check whether that is enough at decay 0.95 and extend it here if not.

- [ ] **Step 4: Link, then commit**

```bash
cd bench && python run.py --profile sweep --build-only
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): cost against the room controls, three points

DIFF, SMEAR and MOD swept together because setup_inst_worst puts all
three near maximum and they are one gesture in practice. sweep_room_hi
reproduces that room exactly, so it doubles as a cross-check that this
family's instrument setup matches the system family's.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 8: Remove the probe row

`sweep_probe` did its job in Task 1 and must not reach the hardware run: it measures nothing and would sit in the evidence as a row nobody can interpret.

**Files:**
- Modify: `bench/workloads_sweep.cpp`, `bench/run.py`, `bench/test_run_contract.py`

- [ ] **Step 1: Update the host-side test**

Replace `test_sweep_family_has_row_expectations`'s `assertIn("sweep_probe", rows)` with an assertion that it is gone, and add a count check:

```python
    def test_sweep_family_has_no_probe_row_and_expected_count(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertNotIn("sweep_probe", rows)
        self.assertEqual(len(rows), len(set(rows)), "duplicate row names")
        self.assertEqual(len(rows), 18)   # 17 if Task 4's second row was dropped
```

Set the number to what the family actually has. If Task 4 dropped `sweep_grit_no_bbd_mem`, it is 17.

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAIL — `'sweep_probe' unexpectedly found`.

- [ ] **Step 3: Delete the probe from `run.py`'s tuple, from `kSweepWorkloads`, and delete `setup_sweep_probe` / `proc_sweep_probe`**

Keep `SweepInstrumentGroup` — Tasks 6 and 7 use it.

- [ ] **Step 4: Run the host test to verify it passes, then link**

```bash
cd bench && python -m unittest test_run_contract.SweepProfileTest -v
python run.py --profile sweep --build-only
```

- [ ] **Step 5: Commit**

```bash
git add bench/workloads_sweep.cpp bench/run.py bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(sweep): drop the scaffolding probe row

It proved the family links and registers, which was its whole purpose.
Leaving it in would put a row in the evidence that measures nothing and
that nobody reading the report later could interpret.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 9: The hardware run

**Files:**
- Create: `docs/bench/2026-07-29-<githash>-sweep.md`, `docs/bench/2026-07-29-<githash>-sweep.csv`

**Preconditions:** the Seed is connected via ST-Link on SWD and powered; the git tree is clean (the bench refuses evidence otherwise); monitors, if connected to the Seed's audio out, are turned down — the anchor sweep plays audio.

- [ ] **Step 1: Confirm a clean tree**

```bash
cd /c/Users/bernd/Documents/AI/Spotykach
git status --short
```

Expected: no output.

- [ ] **Step 2: Rebind the QSPI receipt**

The receipt binds the QSPI bank's digest to the *hashes of the built ELFs*. `engine/fx/flux.h` changed in Task 5 and a whole TU was added, so `bench.elf` differs and the guard will reject the stale binding with `ERROR: QSPI verification receipt does not match current payload (artifacts)`. The QSPI bytes themselves are unchanged; only the binding is stale.

```bash
cd bench
python run.py --profile sweep --build-only
python run.py --profile sweep --no-build --program-qspi --build-only
```

Verify `bench/build/qspi-verified.json` now has a fresh `artifacts.elf_sha256` and that `qspi_sha256` still reads `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`.

- [ ] **Step 3: Measure**

```bash
cd bench && python run.py --profile sweep --repeat 2
```

Expected: exit code 0. Two runs, identical unique row sets and per-row checksums, identical QSPI digest and device fingerprint, `wave_acceptance` passed.

**If a row's checksum drifts between the two runs,** that row did not settle — go back to its task, extend the settle loop, and re-run. Do not widen any tolerance.

- [ ] **Step 4: Sanity-check before committing the evidence**

Read the generated report and check three things:

1. `instrument_worst` is present as the anchor. Compare its max % against 120.55 **in writing**. It is not required to match — this is a different image and layout shifts are documented — but a difference of more than a few points is itself a finding and belongs in Task 10's reading.
2. Each sweep's most expensive point is plausible against the corresponding existing binary row (`sweep_flux_rate_11` against `fx_flux_sdram`, `sweep_room_hi` against `setup_inst_worst`'s room). A wildly low figure means an unsettled row, not a discovery.
3. No row reports zero or a suspiciously round number.

- [ ] **Step 5: Commit the evidence**

```bash
git add docs/bench/2026-07-29-*-sweep.md docs/bench/2026-07-29-*-sweep.csv
git commit -m "$(cat <<'EOF'
bench(sweep): accepted hardware evidence for the cost curves

Two runs, all gates passed, instrument_worst carried as the anchor.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 10: The reading

Numbers without a reading are not the deliverable. This task turns the curves into a disposition per knee, per the spec's §5.

**Files:**
- Modify: `docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md` (append a results section), `docs/roadmap.md`

- [ ] **Step 1: Append a results section to the spec**

For each of the four sweeps: the measured curve as a table, and one of the three dispositions from §5 — *leave it*, *reshape the range*, or *throttle from predicted cost* — with the reason. "Leave it" must be stated as explicitly as the other two; a knee that is not there is a result.

For each of the two ablations: an answer, not a hypothesis. §4.6 names three possible outcomes for `fx_grit` and what each implies; say which one the numbers show.

Then the arithmetic that matters: **how many of the 34 points are now accounted for, and how many are not.** If the answer is short of 34, say so plainly and name what is left on the table — the spec's §3 already records that the refused levers return as a decision in that case.

- [ ] **Step 2: Update `docs/roadmap.md`**

The header's ⚠ block currently says an optimization round comes before ZAP, PULL and M6. Extend it with what this round found and what the next one will do. Keep it short — the detail lives in the spec.

- [ ] **Step 3: Run the desktop suite one last time**

```bash
cd /c/Users/bernd/Documents/AI/Spotykach
source env.sh
# then the project's normal ctest invocation
```

Expected: green.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md docs/roadmap.md
git commit -m "$(cat <<'EOF'
docs(sweep): read the cost curves and name a disposition per knee

<one line per sweep: what the curve showed and what happens to it>

Accounted for: <N> of the 34 points needed. <what is left, and what that
means for the refused levers>

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

- [ ] **Step 5: Report, do not merge**

Summarise for the owner: the curves, the dispositions, the two ablation answers, and the points accounted for against 34. **Do not merge `perf/fx-cost-curves` into `main` without being asked** — the branch is evidence and a reading, and whether it merges before or after the fixes it motivates is the owner's call.

---

## Self-Review

**Spec coverage.** §1 → Task 10's arithmetic. §2 scope → the plan adds no rows outside the four sweeps and two ablations. §3 constraints → Global Constraints (engine/ off limits, one exception) and Task 10 (the refused levers return as a decision). §4.1 → Task 2. §4.2 → Task 3. §4.3 → Task 6, including the "do not assume evenly spaced norms" requirement. §4.4 → Task 7. §4.5 → Task 5. §4.6 → Task 4, including the conditional second row. §5 decision rule → Task 10 Step 1. §6 mechanics → Task 1 wires all four places; determinism appears as an explicit settle step in Tasks 2, 3, 5 and 7. §6 memory correction → Task 1's purpose. §7 verification → Task 9 (criteria 1–3) and Task 10 (criteria 4–5). §8 risks → Task 1 Step 11 (link failure escalates rather than improvising), Task 9 Step 4 (the empty-machine failure mode).

**Placeholder scan.** Three deliberate fill-ins remain, each with an explicit instruction to replace it: the SRAM figures in Task 1's commit message, the COLOR norms in Task 6 (which cannot be written here — they must be read from the engine), and the row count in Task 8's assertion (which depends on Task 4's outcome). No "TBD", no "handle edge cases", no "similar to Task N".

**Type consistency.** `SweepFxGroup` is introduced in Task 2 and reused by name in Tasks 3 and 4. `SweepInstrumentGroup` is introduced in Task 1 and reused in Tasks 6 and 7, which is why Task 8 keeps it while deleting the probe. `proc_sweep_fx` is introduced in Task 2 and reused in Tasks 3 and 4. `SweepLineGroup` and `SweepGritGroup` are local to Tasks 5 and 4. `clock_hz_for_test()` is defined in Task 5 Step 3 and used in Task 5 Step 5.

**One honest caveat about this plan's code.** The bench and engine headers were read while writing it, but the C++ listings are the *shape* of each row, not a promise about exact signatures. Every task that contains one says which header to check and what to match. An implementer who copies a listing without reading the header it mirrors will produce something that does not compile — which is the safe failure — or, worse, a row that measures a different thing than its name claims. The listings that carry the most risk of the second kind are Task 2's FLUX-rate setter, Task 3's stage-norm mapping and Task 6's COLOR norms; all three have an explicit verification step.
