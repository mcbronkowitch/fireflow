# Deck-Interior Ablation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the 7.73 points per deck that sit inside a `Part` and are
priced by no block row, into the voices at the gate's operating point, the
modulation at the gate's operating point, and a named remainder.

**Architecture:** Two new rows in the existing `instr` family, carried by the
existing `ablate` profile. One drives a `SynthEngine` exactly as
`Part::process` drives it — through a virtual `IPartEngine*`, in FLOW, with a
cycle derived from a real modulator, and with `process_in()` called. The other
runs one `SuperModulator` at the gate's RATE/DENSITY without a `Center`. No
engine change; no new family, profile or Makefile entry.

**Tech Stack:** C++17, ARM cross-toolchain + Python (bench), Daisy Seed.

**Design spec:** `docs/superpowers/specs/2026-07-29-deck-interior-design.md`.
Section references (§2.3, §4 …) point into it.

## READ THIS BEFORE TASK 1

**A null result is a result.** If both corrections come back near zero, the
7.73 points are `Part` structure and the voice rows were right all along.
Write that outcome as plainly as the other one (spec §5).

**`engine/` is off limits.** This round measures; it does not change the
engine. If a measurement appears to need an engine change, that is a finding
to report, not a change to make.

**The single easiest way to get this round wrong** is to call the engine
through a concrete `SynthEngine&` instead of an `IPartEngine*`. The compiler
inlines the concrete call; `Part::process` cannot. That would push two virtual
dispatches per sample silently into the remainder (spec §2.3).

## Global Constraints

- Branch `perf/deck-interior-ablation` (already created). Never commit on `main`.
- Commit trailer is exactly, and with nothing after it:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **No file under `engine/` may be modified.** `git diff main -- engine/` must
  stay empty for the life of this branch.
- Do not add, remove or rename any **existing** bench row. Existing rows'
  checksums must not move; if one does, that is a real finding (spec §6).
- Never run `python bench/run.py` without `--profile` — the default is `full`,
  which fails to link by design (`bench/README.md`).
- Build, then rebind the QSPI receipt, then measure, in that order
  (`bench/README.md`).
- The bench refuses hardware evidence from a dirty git tree.
- `source ./env.sh` before any cmake/ctest invocation, in the same shell
  command. The Bash tool's working directory persists between calls — if you
  `cd bench`, `cd` back before sourcing.
- `bench/run.py` compares the row **set**, not its order
  (`bench/run.py:280-286`). Keep the table and the tuple in the same order
  anyway for readability, but a reordering is not what would fail a run.

---

## File Structure

| File | Responsibility |
|---|---|
| `bench/workloads_instr.cpp` | **Modify.** Two new groups, two setups, two proc functions, two table entries. |
| `bench/run.py` | Two entries in `BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"]`. |
| `bench/test_run_contract.py` | Assertions for the two new rows in `AblateProfileTest`. |
| `docs/bench/2026-07-29-<sha>-ablate.{md,csv}` | The evidence. |
| The spec, `docs/roadmap.md` | The written verdict. |

No new family, no new profile, no `bench/Makefile` change, no
`bench/families.cpp` change — the `instr` family and the `ablate` profile
already exist and already carry `system`.

---

### Task 1: `deck_mod_hot`

The simpler of the two rows, first, so the four registration points are
re-proven with a low-risk change before the row that carries the round's real
risk.

**Files:**
- Modify: `bench/workloads_instr.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `g_instr_arena` and `kInstrSettleBlocks` from the existing file.
- Produces: row `deck_mod_hot`; the arena gains a `DeckModGroup` template
  argument. Task 2 adds a third.

- [ ] **Step 1: Read the file you are extending**

Open `bench/workloads_instr.cpp` and read it end to end. It already contains
`InstrNoVerbGroup`, `InstrPartGroup`, `SerialArena<InstrNoVerbGroup, InstrPartGroup> g_instr_arena;`,
`constexpr int kInstrSettleBlocks = 200;`, three setups, three proc functions
and a three-entry `kInstrWorkloads[]`. Match its style; it was reviewed twice.

- [ ] **Step 2: Add the include and the group**

`super_modulator.h` may not be included yet — check before adding. Add
`#include "mod/super_modulator.h"` if it is missing.

Insert into the anonymous namespace, before the workload table:

```cpp
// One SuperModulator at the gate's operating point, with no Center.
//
// The row it corrects is mod_plane_2x_center, which runs two modulators at
// RATE 0.5 and 0.6 and DENSITY 0.7 (bench/workloads_system.cpp:75-76), never
// calls set_tempo_bpm, and does no settle. setup_inst_worst runs RATE 0.8 and
// DENSITY 1.0 on both decks, and Instrument::set_tempo_bpm pushes 120 BPM
// into every part's modulator (engine/instrument.cpp:70). All three
// differences are deliberate here and all three are part of what the
// subtraction measures -- see the design spec section 3.
//
// The Center is deliberately absent. mod_plane_2x_center includes it, so
// charging each deck half of that row double-counts an instrument-level
// object that no bare Part runs and that the measured 4.04-point glue term
// already contains.
struct DeckModGroup {
    SuperModulator mod;
};
```

Add `DeckModGroup` to the arena's template arguments so it reads
`SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup> g_instr_arena;`.

- [ ] **Step 3: Add the setup**

```cpp
void setup_deck_mod_hot()
{
    auto& g = g_instr_arena.emplace<DeckModGroup>();
    // PART_A's seed base, as Part::init passes it: _mod.init(sr, seed_base)
    // with seed_base = 0x1234abcd for PART_A (engine/parts/part.cpp:16,
    // engine/instrument.cpp:22).
    g.mod.init(kSampleRate, 0x1234abcdu);
    g.mod.set_tempo_bpm(120.f);
    g.mod.set_rate(0.8f);
    g.mod.set_density(1.f);

    // Settle to the same depth the Instrument rows settle to, so the row is
    // measured in the state the gate is measured in. mod_plane_2x_center has
    // no settle at all; that difference is part of what this row corrects.
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) g.mod.process();
}
```

- [ ] **Step 4: Add the proc function**

```cpp
float proc_deck_mod_hot()
{
    auto& g = g_instr_arena.get<DeckModGroup>();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        g.mod.process();
        acc += g.mod.lane_output(LANE_PITCH);
    }
    return acc;
}
```

Same accumulate shape as `proc_mod` (`bench/workloads_system.cpp:78-94`) with
one modulator instead of two, and without the `Center::update` call that row
makes once per block.

- [ ] **Step 5: Add the table entry and the row expectation**

In `kInstrWorkloads[]`, after the three existing rows:

```cpp
    { "instr", "deck_mod_hot",  setup_deck_mod_hot,  proc_deck_mod_hot  },
```

and in `bench/run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"]`:

```python
        "deck_mod_hot",
```

- [ ] **Step 6: Extend the contract test**

In `AblateProfileTest` in `bench/test_run_contract.py`, add:

```python
    def test_deck_mod_row_is_expected(self):
        self.assertIn("deck_mod_hot", run.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
```

Read a neighbouring test in the same class first and match its style.

- [ ] **Step 7: Run the contract test**

```bash
cd bench && python -m unittest test_run_contract && cd ..
```

Expected: all pass, count up by one.

- [ ] **Step 8: Build the ARM image**

```bash
source ./env.sh && python bench/run.py --profile ablate --build-only
```

Expected: a clean link. **Report the SRAM and SRAM_EXEC lines.** SRAM sat at
97.83 % after the previous round. `SerialArena` overlays its groups, so
`DeckModGroup` — one `SuperModulator` — should cost nothing, being far smaller
than `InstrNoVerbGroup`. If the image does not link, **stop and report it.**

- [ ] **Step 9: Commit**

```bash
git add bench/
git commit -m "$(cat <<'EOF'
bench(instr): the modulation at the gate's operating point

mod_plane_2x_center runs RATE 0.5/0.6 and DENSITY 0.7, never sets a
tempo, and does no settle. The gate runs RATE 0.8, DENSITY 1.0 and
120 BPM on both decks. This row runs one modulator at the gate's
settings, without the Center -- which mod_plane_2x_center includes and
which no bare Part runs, so halving that row double-counts it against
the glue term that already contains Center::update.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: `deck_engine_hot`

The row that carries the round. Read spec §2.3, §2.4 and §4 before starting.

**Files:**
- Modify: `bench/workloads_instr.cpp`, `bench/run.py`, `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `g_instr_arena`, `kInstrSettleBlocks`, `DeckModGroup` from Task 1.
- Produces: row `deck_engine_hot`.

- [ ] **Step 1: Add the includes and the group**

Add `#include "synth/synth_engine.h"` and `#include "parts/engine_iface.h"`
if they are not already present.

```cpp
// One SynthEngine, driven exactly as Part::process drives it.
//
// Four differences from synth_2x4 (bench/engine_2x4.h), each deliberate and
// each part of what this row corrects:
//
//   1. Called through an IPartEngine*, not a concrete SynthEngine&. Part
//      holds `IPartEngine* _engine` (engine/parts/part.h:231) and every
//      method on that interface is virtual (engine/parts/engine_iface.h),
//      so Part pays two virtual dispatches per sample and can inline
//      neither. proc_engine_2x4 holds a concrete reference and the compiler
//      inlines both calls. Calling the concrete type here would push that
//      dispatch cost silently into the round's remainder -- the one way to
//      get this row wrong (design spec section 2.3).
//   2. FLOW, not STEP. _step_on initialises to false (part.cpp:35), flow()
//      is !_step_on (part.h:98), and setup_inst_worst never calls set_step,
//      so the gate runs both decks as a drone. setup_engine_2x4 calls
//      set_flow(false).
//   3. The cycle comes from a real modulator's master_hz(), as
//      Part::process derives it (part.cpp:403-407), not from the constant
//      set_cycle(2.f) the old row uses.
//   4. process_in() is called every sample, before process()
//      (part.cpp:482). proc_engine_2x4 never calls it, so the 35.80 points
//      contain none of it.
//
// Not included, by design: the chord builder, the quantizer, _control_tick's
// target pushes and the _engine_fade multiply. Those are Part-level and stay
// in the round's remainder (design spec section 4).
struct DeckEngineGroup {
    SynthEngine    synth;
    SuperModulator mod;          // setup only -- see setup_deck_engine_hot
    IPartEngine*   engine = nullptr;
    float          master_hz = 0.f;
    int            voices = 0;
};
```

Extend the arena to
`SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup, DeckEngineGroup> g_instr_arena;`.

- [ ] **Step 2: Add the setup**

```cpp
// Four fixed pitches, the same set setup_engine_2x4 uses
// (bench/engine_2x4.h, kEngine2x4Pitches), so this row and synth_2x4 hold
// the same voice occupancy and the difference between them cannot be a
// voice count. The chord builder that Part::trigger_manual would normally
// run (part.cpp:149-162) is Part-level and belongs in the remainder; what
// this row needs from it is only the number of voices it lands.
constexpr float kDeckEnginePitches[] = { 0.25f, 0.35f, 0.45f, 0.55f };

void setup_deck_engine_hot()
{
    auto& g = g_instr_arena.emplace<DeckEngineGroup>();

    // Mirrors Part::init for PART_A (engine/parts/part.cpp:16-45).
    g.mod.init(kSampleRate, 0x1234abcdu);
    g.mod.set_tempo_bpm(120.f);
    g.mod.set_rate(0.8f);
    g.mod.set_density(1.f);
    g.synth.set_seed(0x1234abcdu ^ 0x5eedC0DEu);
    g.synth.init(kSampleRate);

    // From here on the engine is reached ONLY through the base pointer.
    g.engine = &g.synth;
    g.engine->set_flow(true);        // boot: lanes boot in FLOW -> drone
    g.synth.set_decay(1.f);          // Part::set_voice_decay(1.0), part.h:139

    // Derive the cycle the way Part::process does: run the modulator to a
    // settled state, read master_hz(), push 1/hz. The modulator is then left
    // alone -- driving it inside the measured loop would pay deck_mod_hot's
    // cost a second time and corrupt both rows (design spec section 3.2).
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) g.mod.process();
    g.master_hz = g.mod.master_hz();
    assert(g.master_hz > 0.f);
    // 0.5 Hz is exactly what set_cycle(2.f) encodes, i.e. the operating point
    // this row exists to move away from. Landing on it would mean the
    // modulator never came up and the row silently measures the old
    // configuration. Banded rather than compared for equality: a float
    // equality assert would be brittle and would read as an oversight.
    assert(std::fabs(g.master_hz - 0.5f) > 1e-3f);
    g.engine->set_cycle(1.f / g.master_hz);

    for (float p : kDeckEnginePitches) g.engine->trigger(p);

    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) {
            float ol, orr;
            g.engine->process_in(in[i], in[i]);
            g.engine->process(ol, orr);
        }

    // A drone that has stopped sounding would make this row measure silence
    // and produce a plausible, meaningless number. SynthEngine::kVoices is 4
    // (engine/synth/synth_engine.h:35), so 4 is both the count triggered and
    // the ceiling.
    g.voices = g.synth.active_voices();
    assert(g.voices == 4);
}
```

`<cassert>` is already the file's first include. Add `<cmath>` for
`std::fabs` if it is not present.

`SynthEngine` is a type alias for `SynthEngineT<VoiceT<MorphOsc>>`
(`engine/synth/synth_engine.h:167`) — use the alias, as
`bench/workloads_system.cpp` does.

- [ ] **Step 3: Add the proc function**

```cpp
float proc_deck_engine_hot()
{
    auto& g = g_instr_arena.get<DeckEngineGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr;
        // Both calls through the base pointer, in Part::process's order:
        // process_in first, then process (part.cpp:482-483).
        g.engine->process_in(in[i], in[i]);
        g.engine->process(ol, orr);
        acc += ol + orr;
    }
    acc += static_cast<float>(g.synth.active_voices());
    acc += g.master_hz + static_cast<float>(g.voices);
    return acc;
}
```

- [ ] **Step 4: Add the table entry and the row expectation**

```cpp
    { "instr", "deck_engine_hot", setup_deck_engine_hot, proc_deck_engine_hot },
```

and in `bench/run.py`, in the same relative position:

```python
        "deck_engine_hot",
```

Add a matching assertion to `AblateProfileTest`.

- [ ] **Step 5: Verify the mirror against `Part`, line by line**

Before building, open `engine/parts/part.cpp` (`init`, lines 14-45, and
`process`, lines 376-486) beside `setup_deck_engine_hot` and
`proc_deck_engine_hot`. Check each call has its counterpart with the same
value and in the same relative order, and check the four deliberate
differences from `synth_2x4` are all present. **List every line and its
counterpart in your report.** This is the round's single point of failure
(spec §4).

Confirm explicitly, in your report, that **both** per-sample calls go through
`g.engine`, the `IPartEngine*`, and neither through `g.synth`.

- [ ] **Step 6: Build and run the contract test**

```bash
cd bench && python -m unittest test_run_contract && cd ..
source ./env.sh && python bench/run.py --profile ablate --build-only
```

Expected: contract tests pass; the image links. Report the SRAM and SRAM_EXEC
lines. `DeckEngineGroup` holds a `SynthEngine` and a `SuperModulator` and may
now be the arena's largest group. **If the image does not link, stop and
report it** — do not shrink a row to squeeze past.

- [ ] **Step 7: Commit**

```bash
git add bench/
git commit -m "$(cat <<'EOF'
bench(instr): the voices as the Part actually drives them

Four differences from synth_2x4, each measured rather than assumed: the
calls go through IPartEngine* so the two virtual dispatches per sample
are paid and not inlined away; FLOW rather than STEP, which is what the
gate runs; the cycle derived from a real modulator's master_hz instead
of the constant set_cycle(2.f); and process_in() called every sample,
which the old row never calls at all.

The row asserts its settled voice count and its derived clock and folds
both into the checksum, so a row that fell back on the old operating
point fails loudly instead of returning a plausible number.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: The hardware measurement

**The Daisy Seed must be attached.**

**Files:**
- Create: `docs/bench/2026-07-29-<sha>-ablate.md` and `.csv`, `<sha>` being
  `git rev-parse --short HEAD`

- [ ] **Step 1: Confirm the tree is clean and `engine/` is untouched**

```bash
git status --short
git diff main -- engine/
```

Expected: both empty.

- [ ] **Step 2: Build, then rebind, then measure — in that order**

`--no-build --program-qspi` binds the QSPI receipt to whatever `bench.elf` is
on disk; if the measuring run then relinks anything, the hashes differ and the
run aborts with `QSPI verification receipt does not match current payload
(artifacts)` **after** reprogramming the bank for nothing. That message names
the artifacts, not the bank — a stale binding, not corrupt flash.

```bash
source ./env.sh
python bench/run.py --profile ablate --build-only
python bench/run.py --profile ablate --no-build --program-qspi --build-only
python bench/run.py --profile ablate
```

- [ ] **Step 3: Check the rows that must NOT have moved**

This round changes no engine file, so every `system` row and all three
existing `instr` rows should return the checksums they returned in
`docs/bench/2026-07-29-930ec17-ablate.csv`. Compare them row by row, and
**confirm the row count on both sides** — a row silently absent from one file
produces a vacuously clean comparison.

**A moved checksum is a real finding, not noise.** Report any that moved, with
both values. Costs *may* move even where checksums do not; that is the layout
effect the predecessor round proved, and it is expected. Record it separately
and do not confuse the two.

- [ ] **Step 4: Save the evidence and commit**

Copy the run's markdown and CSV to `docs/bench/2026-07-29-<sha>-ablate.md` /
`.csv`, following the layout of `docs/bench/2026-07-29-930ec17-ablate.md`.

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): deck-interior ablation

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: The written verdict

**Files:**
- Modify: `docs/superpowers/specs/2026-07-29-deck-interior-design.md`
  (append `## 8. Results`)
- Modify: `docs/roadmap.md`

- [ ] **Step 1: Compute the two corrections and the remainder**

Using `pct_max` from **run 2**, every figure from this run, and checking
`avg_cyc` agrees in direction:

- **`deck_engine_hot` − `synth_2x4` / 2** — what the voices really cost at the
  gate's operating point. This is the round's headline number. It bundles four
  causes that the ladder does **not** separate: virtual dispatch, FLOW vs STEP,
  the derived cycle, and `process_in`. Say so explicitly rather than
  attributing the total to any one of them.
- **`deck_mod_hot` − `mod_plane_2x_center` / 2** — the same for the
  modulation. This one also bundles the removal of the `Center` with the
  RATE/DENSITY/tempo/settle differences, and the `Center`'s removal pushes it
  *down* while the hotter settings push it *up*. Name both directions.
- **The remainder** = `instr_part_1` − `deck_engine_hot` − `deck_mod_hot`
  − (`fx_none` + (`fx_grit` − `fx_none`) + (`fx_flux_sdram` − `fx_none`)
  + (`fx_comp` − `fx_none`)), which simplifies to
  `instr_part_1 − deck_engine_hot − deck_mod_hot − (fx_grit + fx_flux_sdram
  + fx_comp − 2 × fx_none)`. **State both forms and confirm they agree
  numerically** — the simplification is where an arithmetic slip would hide.
  Name its constituents: the chord builder, the quantizer, `_control_tick`,
  the `_engine_fade` multiply, and `Part::process`'s own per-sample loop.

- [ ] **Step 2: Answer the round's question directly**

State, in one paragraph near the top, how the 7.73 points per deck divide
between voices, modulation and `Part` structure. §5 of the spec commits in
advance to writing a null result as plainly as a positive one.

Then answer the question that started this: **are the 35.80 points
underpriced, and does that change the per-voice figure of 4.48?** If
`deck_engine_hot` is materially above `synth_2x4` / 2, then cutting one voice
per deck saves *more* than the 8.95 points that figure implies. Give the
corrected per-voice number, and say plainly that it is derived by division and
not measured — `synth_1_voice` / `synth_2_voices` / `synth_4_voices` exist and
none of them runs at this operating point.

- [ ] **Step 3: Record the two findings that came from reading, not measuring**

They belong in the results even though no row produced them, because a later
round would otherwise spend a ladder rediscovering them:

- The four engines a `Part` holds cost memory and no CPU — `_engine` is one
  pointer, the fade is a multiply, and at hold it is exactly 1.0 (spec §2.1).
  **This candidate is closed.**
- Two virtual dispatches per sample are structural: the interface *is* the
  four-engine design. Whatever share of `deck_engine_hot`'s excess they are,
  they are not deletable without changing that design (spec §2.3).

- [ ] **Step 4: Say what is worth measuring next, if anything**

The remainder's constituents are named but not separated. Recommend whether
splitting them is worth a round, with the number behind the recommendation.
If the answer is no, say so and say where the 10.77 points should be sought
instead.

- [ ] **Step 5: Update `docs/roadmap.md`**

Record what the round did, the two corrections, the remainder, the answer, the
branch, and what remains: the gap to 100 %, the open panning decision, and the
`bbd.h` `1/sr_` division (~0.6 points, still standing).

- [ ] **Step 6: Confirm the branch's shape and commit**

```bash
git diff --stat main..HEAD
git diff main -- engine/
```

Expected: the second is empty; the first shows only `bench/` and `docs/`.

```bash
git add docs/
git commit -m "$(cat <<'EOF'
docs: the deck interior, measured

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

- [ ] **Step 7: Do not merge**

Do not merge `perf/deck-interior-ablation` into `main` without being asked.
