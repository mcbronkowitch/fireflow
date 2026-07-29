# Where the unmeasured budget goes: an instrument-level ablation

**Date:** 2026-07-29
**Status:** design, approved by the owner
**Predecessor:** `docs/superpowers/specs/2026-07-29-flux-mono-design.md`
**Evidence:** `docs/bench/2026-07-29-1ba3f18-sweep.md`

---

## 1. The question, and where it came from

Adding up every block this project has a bench row for — voices, the FX shell,
GRIT, FLUX, COMP, the modulation plane, the reverb — reaches roughly **90 %**
of the block budget. `instrument_worst_bbd` measures **112.88 %**. Around
**23 points, a fifth of the budget, belongs to nothing anyone has measured.**

The owner asked the right question about that number: *do we actually know
where it goes?* The honest answer was no. The FX chain was ablated properly in
the cost-curves round; the instrument level never has been.

This matters beyond bookkeeping. **12.88 points remain to reach 100 %, and the
unattributed region is roughly twice that.** Every optimisation round so far
has searched inside the blocks that have rows — which is searching where the
light is.

## 2. What reading already eliminated

Two suspects from the first pass are gone, and saying so keeps the round from
re-investigating them:

- **CHOKE is not a suspect.** `setup_inst_worst` never calls `set_choke`, so
  `_choke` is 0, `amt` is 0, and the expensive window logic — including
  `max_voice_env()` — is skipped entirely in the gate row.
- **The modulation plane is already covered.** `mod_plane_2x_center` builds
  two `SuperModulator`s and a `Center` and processes them
  (`bench/workloads_system.cpp`, `setup_mod`/`proc_mod`), so its 7.41 points
  are a real measurement of that work, not a gap.

What remains genuinely unmeasured, per sample, is modest: the MORPH blend,
four `OnePole` smoothers on the reverb's per-deck dry/wet gains, the send and
return mixing, the instrument-level limiter behind `set_master_drive`, and
inside `Part::process` the engine-fade multiply and `process_in()`. At control
rate: `Center::update` and two `set_rhythm` calls with `derive_intervals`.

**That list does not look like 23 points.** Which produces the round's real
hypothesis, and the reason a ladder is the right instrument:

> A large part of the gap may not be unmeasured *work* at all, but the same
> work costing **more inside the instrument** than it does in an isolated row —
> cache pressure and SDRAM contention. If so there is nothing there to delete,
> and the round's value is knowing that rather than continuing to look.

## 3. The ladder

Three new rows. The fourth rung already exists.

Three new rows in a new **`instr`** family, carried by a new **`ablate`**
profile alongside `system` (which supplies the fourth rung and the
`instrument_worst` anchor `verdict()` needs — without it the run reports
"undetermined", which is how the BBD numbers once stood for two days).

| row | what it is |
|---|---|
| **`instr_part_1`** | one bare `Part`, configured exactly as `Instrument` configures each of its decks in `setup_inst_worst_bbd` |
| **`instr_part_2`** | two bare `Part`s, same configuration, no `Instrument` |
| **`instr_noverb`** | the full instrument at the `worst_bbd` configuration, with `FxMem::reverb = nullptr` |
| *(existing, from `system`)* `instrument_worst_bbd` | the gate |

Three differences, each isolating one thing:

- **`instr_part_2` − 2 × `instr_part_1`** — what it costs to run two decks at
  the same time. No extra code runs; this is memory and cache contention alone.
- **`instr_noverb` − `instr_part_2`** — the instrument-level glue:
  `Center::update`, the CHOKE framing, the MORPH blend, the dry taps, the
  cross-deck rhythm exchange, the limiter, and the per-sample loop itself.
- **`instrument_worst_bbd` − `instr_noverb`** — the reverb *in situ*,
  including its four gain smoothers and the send/return mixing, not just the
  algorithm `oliverb_solo_sram` prices in isolation.

### 3.1 Why the null reverb rather than a rebuilt one

`Instrument::process` gates the entire reverb section behind `if (_reverb)`,
and `FxMem::reverb` is already a host-supplied pointer. Passing `nullptr`
therefore removes the whole block — algorithm, smoothers and mixing — with no
engine change and, more importantly, **without reimplementing any instrument
logic in the bench.** Rebuilt logic drifts from the original, and a drifted
copy silently measuring the wrong thing is exactly the failure class this
round exists to detect. The MORPH blend still runs on the null path
(`l = al*ga + bl*gb` is unconditional), so it correctly stays on the glue side
of the subtraction rather than moving with the reverb.

## 4. The one risk that decides whether any of this means anything

**`instr_part_1` must configure its `Part` identically to how `Instrument`
configures a deck.** One divergence and the differences above measure the
divergence instead of the thing they name.

This is less dangerous than it sounds, because every relevant `Instrument`
setter is a one-line forward (`engine/instrument.h`): `set_color(p,n)` is
literally `_parts[p].set_color(n)`, `set_rate(p,n)` is
`_parts[p].mod().set_rate(n)`, and so on. Mirroring is a checklist, not a
judgement. The full list, from `setup_inst_worst` plus `setup_inst_worst_bbd`:

```
mod().set_tempo_bpm(120)        fx().set_bpm(120)
set_color(1.0)                  mod().set_density(1.0)
set_depth(1.0)                  mod().set_rate(0.8)
fx().set_fx_on(Grit, true)      fx().set_fx_on(Flux, true)
fx().set_grit_mix(1.0)          fx().set_flux_mix(1.0)
fx().set_comp(1.0)              set_voice_decay(1.0)
trigger_manual()                fx().set_stages(1.0)
fx().set_drive(0.85)            fx().set_flux_rate(kFluxRateCount - 1)
set_fx_target_base(FXT_FLUX_FB, 0.9)
```

Seeds must match too: `Instrument::init` uses `0x1234abcdu` for PART_A and
`0x9e3779b9u` for PART_B (`engine/instrument.cpp`). `instr_part_1` uses
PART_A's; `instr_part_2` uses both, in order.

**The rows carry a self-check rather than trusting the checklist.** After the
settle, `instr_part_1` and `instr_part_2` assert that each part's FLUX reports
`stages() == 16384` and a `clock_hz()` at `bbd_tuning::kClockMaxHz`, and fold
both into the returned checksum. That catches the FLUX half hard. It does not
cover the voice half, so the voice count is folded as well — the same guard
`proc_inst` already uses.

### 4.1 Three things rows A and B deliberately do not get

They land in the glue bucket, correctly, and must be named so nobody
"corrects" them later:

- `set_master_drive` — instrument-wide, reaching `_limiter`, with no `Part`
  equivalent.
- `set_other_deck_tap` — supplied by `Instrument` at control rate.
- `fx().set_rhythm(...)` — likewise. This one is harmless as well as correct:
  `setup_inst_worst_bbd` never touches LINK, so `_link` stays 0 and both DRAG
  and THIN are inert on either side of the comparison.

## 5. What the round answers, and what it does not

It answers **whether the gap is glue or contention, and in what proportion.**

It does **not** identify a line of glue worth deleting. That needs the finer
ladder — reverb smoothers, MORPH, `process_in`, `derive_intervals` measured
separately — and that round is only worth running if this one shows real work
sitting there. Deciding otherwise now would be committing to the more
expensive round before knowing whether it has a subject.

**A null result is a result.** If the gap turns out to be mostly contention,
the round has established that no instrument-level cut exists to be found, and
the remaining 12.88 points must come from inside the blocks after all. Write
that outcome as plainly as the other one.

## 6. Verification

`bench/workloads_sweep.cpp` is the wrong home — these are not sweep rows. They
go in a new `bench/workloads_instr.cpp` under the new `instr` family, so the
`sweep` family's row list and its committed evidence stay untouched. The name
avoids colliding with the existing `bench/workloads_abl.cpp`, which belongs to
an earlier round and is not touched here.

- **The row-table agreement is the gate before hardware.** `bench/run.py`
  keeps a hand-maintained expectation of row names; a mismatch fails the whole
  run. Four registration points must agree — the workload file, `families.cpp`
  plus the Makefile, `run.py`, and `profiles.py`. Confirm with
  `cd bench && python -m unittest test_run_contract` (unittest, not pytest —
  pytest is not installed here).
- **The image must link.** SRAM sits at 97.83 % and SRAM_EXEC at 73.66 %;
  three new rows that each hold a `Part` or an `Instrument` are not free.
  `SerialArena` overlays its groups, so the cost is one more max-sized block
  rather than the sum — but if the image stops linking, that is a finding, not
  something to squeeze past.
- **Desktop suite:** no new failure. `tests/test_seed_audition_init.cpp` is red
  on `main` already and is not this round's to fix.
- **Hardware:** `python run.py --profile ablate` — never without `--profile`.
  Build, then rebind the QSPI receipt, then measure, in that order
  (`bench/README.md`).

No engine file is modified, so no existing row's checksum may move. **If one
does, that is a real finding** — it would mean a new translation unit changed
the layout enough to matter, which is precisely the effect the mono round
observed on `fx_grit` and could not prove.

## 7. Global constraints

- Work in the fork at `C:\Users\bernd\Documents\AI\Spotykach`, on a branch,
  never on `main` directly.
- Commit trailer is exactly
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
  with nothing after it.
- **`engine/` is off limits.** This round measures; it does not change the
  instrument. If a measurement turns out to need an engine change, that is a
  finding to report, not a change to make.
- `source env.sh` before any cmake or ctest invocation.
- Never run `python run.py` without `--profile` (`bench/README.md`).
- Build, then rebind the QSPI receipt, then measure (`bench/README.md`).
- The bench refuses hardware evidence from a dirty git tree.
- Do not add, remove or rename any existing bench row.
- No bit-exactness or checksum-against-stored-file gates; the bench's own
  cross-run comparison is a different thing and is required.

---

## 8. Results

Measured 2026-07-29 on a Daisy Seed (STM32H750), branch
`perf/instrument-ablation` at `930ec17`, profile `ablate`, two runs. Evidence:
`docs/bench/2026-07-29-930ec17-ablate.csv` and `.md`. Every row returned an
identical checksum in both runs; the QSPI digest and device fingerprint were
identical too. Figures below are `pct_max` from **run 2**, with run 1 given
where the spread matters, and `avg_cyc` checked to agree in direction on every
difference. **Every difference is computed from within this one run** — §8.1 is
why.

*(One warning for anyone reading the evidence file directly: the "Verdict"
prose in `2026-07-29-930ec17-ablate.md` is `run.py` boilerplate, templated off
`instrument_worst`. It is the same template as the baseline file's, sentence
for sentence, with the numbers refilled from this run — the baseline reads
108 %, this one 107 %. It is not a verdict computed for this round. This
section is.)*

### 8.1 The anchor moved — and that is the round's first finding

§6 said that if a checksum moved, that would be a real finding. None did: all
15 `system` rows returned exactly the checksum they returned in the baseline.
The **cost** moved anyway, and it moved on the gate.

`instrument_worst_bbd` reads **110.78 / 110.77** (runs 1 / 2) here, against
**112.79 / 112.88** in `docs/bench/2026-07-29-1ba3f18-sweep.csv` — at checksum
`483e8e82` on both sides. Identical bytes out, roughly 2.0 points cheaper in.

The perturbation between those two images is a **family swap, not a minimal
addition**, and the finding has to be stated that way. The baseline was
captured under profile `sweep` = `system` + `sweep` (`bench/profiles.py`,
`docs/bench/2026-07-29-1ba3f18-sweep.md`); this run is `ablate` = `system` +
`instr`. So the ~900-line `bench/workloads_sweep.cpp`, with its fifteen rows
and their arena groups, is **absent** from this image, and the much smaller
three-row `bench/workloads_instr.cpp` is present in its place. One bench
translation unit left and a different one arrived — this is not a minimal
addition to an otherwise identical image.

What did *not* change is the instrument. The only `engine/` edit anywhere in
the compared interval is a single comment line in `engine/fx/bbd.h` ("shared
by all four lines" → "shared by both lines", `9c82da0`), which cannot reach
codegen — so "no engine file was touched" is not literally true of the
interval, but nothing that generates an instruction moved. This is the
code-layout effect the mono round observed on `fx_grit` and could not prove.
**Here it is proven**, because the checksum is provably identical: the same
computation, at a different price.

It is not confined to the gate. Comparing run 1 of each build (full table in
`.superpowers/sdd/2026-07-29-instrument-ablation/task-3-report.md`; these are
relative changes in **`avg_cyc`**, not `pct_max` points — the "roughly 2.0"
above is `pct_max` points): `instrument_init` **+4.27 %**, `fx_grit`
**+2.24 %**, `oliverb_solo_sram` **+1.73 %** upward; `instrument_worst_bbd`
**−1.77 %**, `mod_plane_2x_center` −1.24 %, `instrument_worst` −1.12 %,
`fx_flux_sdram` −1.12 % downward. Every one at an unchanged checksum, so none
of it is new work.

Two consequences, and the second outlives this round:

1. **Every ladder difference below is computed inside a single run.** The
   quantities being measured are 0.5 to 15 points; a cross-build subtraction
   would inject up to 2 points of layout noise into them. Where a baseline
   figure is cited at all (§8.3), it is cited as an order-of-magnitude check,
   never inside a difference.
2. **Cross-build comparison in this project is worth about ±2 `pct_max`
   points on the gate and about ±2 % of `avg_cyc` on a 5-point row, before
   any real effect.** Any future round claiming a saving smaller than that,
   across a build change of comparable size, has measured its own build
   layout. The mono round's 12.36-point cut clears this comfortably; a
   one-point claim would not clear it at all.

   **What that bound is and is not.** It was calibrated across a *family
   swap* — a ~900-line translation unit out, a small one in — not across the
   smallest possible build change, so it is not a demonstrated floor for a
   one-file
   edit; a smaller perturbation may well move less, and nothing here measures
   how much less. What it does establish is that the perturbation is real,
   is not zero, and is not bounded by anything this project has measured — so
   a round wanting a sub-2-point *cross-build* claim has to measure its own
   layout rather than assume it away. Differences taken **within one run**
   are not subject to this bound at all; that is exactly why §8.2's ladder is
   built the way it is.

The round's own motivating arithmetic has to be restated as a result. §1
quoted the gate at 112.88 and derived from it "around **23 points**... belongs
to nothing anyone has measured" and "**12.88 points** remain to reach 100 %".
Both were stated against a number that has since moved with no change to the
instrument. In this build the gate is 110.77, so **the overshoot is 10.77
points, not 12.88** — 2.11 points of the target disappeared without anyone
optimising anything. The unaccounted region is re-derived from scratch in §8.3
rather than patched.

### 8.2 The three differences

The ladder, run 2:

| rung | avg cyc | `pct_max` |
|---|---:|---:|
| `instr_part_1` | 423095 | 46.24 |
| `instr_part_2` | 840959 | 91.94 |
| `instr_noverb` | 883797 | 95.98 |
| `instrument_worst_bbd` | 1024664 | 110.77 |

| difference | what it isolates | `pct_max` | avg cyc | run 1 |
|---|---|---:|---:|---:|
| `instr_part_2` − 2 × `instr_part_1` | contention *between* decks (± deck A/B asymmetry — see below) | **−0.54** | −5231 | −0.64 |
| `instr_noverb` − `instr_part_2` | instrument glue | **+4.04** | +42838 | +4.36 |
| `instrument_worst_bbd` − `instr_noverb` | reverb *in situ* | **+14.79** | +140867 | +14.58 |

`avg_cyc` agrees in direction and in rough proportion on all three. Run-to-run
spread is 0.10, 0.32 and 0.21 points respectively — small against the
quantities themselves except on the glue figure, where it is 8 % of the value.
The partition is exhaustive by construction:

```
2 × 46.24  −  0.54  +  4.04  +  14.79  =  110.77
  decks     contention  glue    reverb     gate
```

**Contention *between the decks* is nil.** Two decks running together cost
**0.54 points less** than two decks priced one at a time — a *negative*
contention term. The hypothesis in §2, that a large part of the gap is the
same work costing more inside the instrument through cache pressure and SDRAM
contention, is **refuted for the deck pair**: whatever the two decks do to
each other's D-cache lines and to the SDRAM bus, this ladder bounds it at a
few tenths of a point. That is a statement about decks running *alongside*
each other. It says nothing about the blocks running *inside* one deck, which
this row cannot see and which §8.3 keeps open.

**Two biases in that figure, named rather than left to be found.** The first
is small. The per-sample accumulate loop is common to both bare-`Part` rows,
but it appears **once** in `proc_instr_part_2` and **twice** in `2 ×
instr_part_1`, so the difference is biased low — too negative — by one loop's
scaffolding. The accumulates themselves match (two `acc +=` per iteration on
both sides) and `test_input()` is hoisted out of the loop on both sides, so
the biased quantity is 96 iterations of an increment, a compare and a branch
on a 480 MHz M7 — of order 10² cycles. `empty_callback` bounds the harness
floor at 2 avg / 11 max cycles, **0.0011 % of the block**, and the loop cannot
exceed a few hundredths of a point above that. Correcting for it moves −0.54
to about −0.50.

The second is larger, and it is the one that sets the real bound. The
difference is not `contention + loop bias`; it is `(cost B − cost A) +
contention + loop bias`. **`instr_part_1` measures deck A alone** — seed
`0x1234abcd` — and there is no deck-B-alone row to pair with it. The two decks
are configured identically but seeded differently (`0x1234abcd` and
`0x9e3779b9`, matching `Instrument::init`; `setup_instr_part_common` in
`bench/workloads_instr.cpp`),
so their modulation streams differ, and with them trigger timing and voice
occupancy inside the measured window. A deck-to-deck asymmetry of 0.5 points —
**1.1 % of a 46.24-point row** — is enough to flip the sign of the whole
difference on its own.

So: the **magnitude** conclusion is safe, the **sign** is not. What this rung
supports is *"inter-deck contention is at most a few tenths of a point, in
either direction"* — and that is the finding, because §2 predicted a large
positive penalty and there is no room for one. Reading −0.54 as a proven
negative term, or as proof that running two decks together is cheaper than
running them apart, would be reading past the bound. A passage whose stated
virtue is naming its bias has to name the bigger one too.

### 8.3 The answer: barely glue, not inter-deck contention — the missing points are inside the decks, and more than one mechanism put them there

§1's gap has to be rebuilt inside this run before it can be attributed. Every
block row this project has, priced from `2026-07-29-930ec17-ablate.csv`, run 2,
with the FX shell counted once per deck so the blocks do not double-charge for
it:

| block | source | per deck | points |
|---|---|---:|---:|
| 8 voices | `synth_2x4` | — | 35.80 |
| FX shell | `fx_none` | 2.54 | 5.08 |
| GRIT | `fx_grit` − `fx_none` | 3.07 | 6.14 |
| FLUX | `fx_flux_sdram` − `fx_none` | 10.59 | 21.18 |
| COMP | `fx_comp` − `fx_none` | 0.72 | 1.44 |
| modulation plane | `mod_plane_2x_center` | — | 7.37 |
| reverb | `oliverb_solo_sram` | — | 9.62 |
| **sum** | | | **86.63** |

Against the gate's 110.77 that leaves **24.14 unaccounted points** in this
build — the same region §1 estimated at ~23 against the old gate value. The
ladder splits it exhaustively:

| where the 24.14 points are | points | share |
|---|---:|---:|
| inside the two `Part`s, beyond their own block rows (92.48 − 77.01) — **7.73 per deck** | **+15.47** | 64 % |
| contention *between* the decks | **−0.54** | −2 % |
| instrument-level glue | **+4.04** | 17 % |
| the reverb costing more *in situ* than `oliverb_solo_sram` prices it | **+5.17** | 21 % |
| **total** | **24.14** | |

Two things about that first row, so nobody carries them silently. It is a
**two-deck** figure — 92.48 is `2 × instr_part_1` and 77.01 is `2 × 38.51`;
per deck the excess is **7.73 points**, not 15.47. And the 38.51 per-deck
block sum charges each deck **half of `mod_plane_2x_center`**, which is not a
per-deck quantity: that row runs two modulators *and* the instrument-level
`Center`, and no bare `Part` runs a `Center` at all — while `Center::update`
also sits inside the measured 4.04-point glue term, where it really runs. So
the `Center` is charged twice in this table. The **total is unaffected** —
24.14 is `110.77 − 86.63` and the identity holds whatever split of 77.01 is
written down — but the first row is **understated** by whatever
`Center::update` costs, since that cost is charged against the decks in 77.01
while the glue row already contains it. It is a fraction of a point, which is
why the halving is kept and flagged rather than invented around.

**The answer to §1's question is that the gap is mostly neither of the two
things the round offered.** It is not **inter-deck** contention: that term is
−0.54, and §8.2 bounds it at a few tenths of a point either way. It is only
17 % glue. **Nearly two thirds of it — 15.47 points — is inside the two decks,
which the block rows were supposed to have priced already.** One bare `Part`,
configured exactly as the gate configures a deck, costs **46.24 points**; the
block rows for that same deck sum to 38.51.

**At least two mechanisms put those points there, and neither swallows the
other.** Some block rows are **mis-set** — right work, wrong operating point
(FLUX, below: about 4.58 points across the instrument). The rest is work that
**no row prices at all**: on that part the rows are not cheap, they are
*incomplete*, which is a different defect with a different fix. And a third
mechanism is not excluded by anything measured this round: **contention
between the blocks inside one deck.** Every block row was measured *in
isolation*; the 15.47 is a whole `Part` minus a sum of such rows. When those
blocks run together inside one `Part` they share the D-cache and the SDRAM
bus, and §2's hypothesis — the same work costing more inside the instrument —
is **fully consistent with part of this bucket and is not separated from it by
this ladder**. §8.2 refuted that hypothesis *between* decks, at deck
granularity. Within a deck it remains a live candidate, and the next round
should not assume the 15.47 is all deletable work.

FLUX is the clearest measured case of the mis-set mechanism, and it is the
round's own suspicion now carrying a size. `fx_flux_sdram` never calls
`set_stages` or `set_flux_rate`, so it prices FLUX at STAGES 8192 and rate
index 3, while the gate runs STAGES 16384 with the clock on `kClockMaxHz`. The
`sweep` family priced each axis alone: 8192 → 16384 costs +0.63, rate 3 → 11
costs +1.66 (`2026-07-29-1ba3f18-sweep.csv`, run 2 — a *different build*,
quoted here only for scale and subject to §8.1's ±2 %, and the two axes
together were never measured). If they add, FLUX hot is worth **+2.29 points
per deck, +4.58 across the instrument** over what the block sum charged it.
**That accounts for about 30 % of the 15.47.**

The remaining **~10.9 points** across two decks is `Part`-internal cost that
no block row charges. One component of it is checked and is the same class of
error as FLUX: the deck's own modulation. The gate runs it at rate 0.8 /
density 1.0, while `mod_plane_2x_center` prices it at rate 0.5 / density 0.7
(`bench/workloads_system.cpp:75-76`) — a different operating point,
confirmed by reading. The other candidates named here — `process_in()`, the
engine-fade multiply, the voice-to-FX routing — are **suspects, not
measurements**: they are real `Part`-internal work that no row prices, but
this round did not size any of them, and in-deck block contention (above) is
not separated from them either. This round does not decompose that further,
and §8.5 explains why it should not be asked to.

**This is a null result, and §5 committed in advance to writing it as plainly
as the other kind.** No instrument-level cut exists to be found. The entire
glue bucket — `Center::update`, the CHOKE framing, the MORPH blend, the dry
taps, the cross-deck rhythm exchange, the limiter and the per-sample loop, all
of it together — is **4.04 points**, and most of that is features rather than
overhead. Deleting the whole of it, which is not possible, would leave 6.73 of
the 10.77 points still outstanding. **The remaining 10.77 points must come
from inside the decks after all** — from the blocks, from their operating
points, or from what they cost each other in there. The light was in the right
place; the map of it was wrong.

### 8.4 The reverb in situ, against its isolated row

`instrument_worst_bbd` − `instr_noverb` = **14.79** points. `oliverb_solo_sram`
in the *same run* = **9.62**. The gap is **+5.17 points, 54 % more than the
isolated row**, and run 1 agrees at +4.95.

That gap is not a memory-region artefact: `bench/mem.cpp` hands `FxMem::reverb`
the very same `g_rev_sram` object that `reverb_sram()` returns, so both figures
measure one `AmbientReverb` in SRAM. Three things make it up, and this round
can rank them but not separate them:

- **A hotter operating point — and a second parameter that does not match
  either.** `setup_reverb` sets SIZE 0.9; `setup_inst_worst` sets
  `set_reverb_size(1.f)`. DECAY, DIFFUSION and both mod depths match.
  **TONE does not:** `setup_reverb` sets 0.8
  (`bench/workloads_system.cpp:234`), while `setup_inst_worst` never calls
  `set_reverb_tone` at all, so the in-situ reverb runs at
  `AmbientReverb::init`'s boot default of **0.5** (`engine/fx/reverb.cpp:34`).
  Its cost contribution is nil — `set_tone` only recomputes a one-pole
  coefficient, and the per-sample work is identical at either value — so the
  ranking here is unaffected and the 5.17 stands. It matters for §8.5's
  follow-up, which has to copy it. SIZE remains the live suspect: on a
  Doppler-SIZE reverb that is not a free parameter.
- **The four `OnePole` smoothers** on the per-deck dry/wet gains, which §2
  listed as genuinely unmeasured.
- **The send and return mixing** — the per-deck sends into the shared bus and
  the equal-power join back — which the isolated row never included by
  construction.

So the round's smallest finding is still a real one: **`oliverb_solo_sram`
under-prices the reverb as the instrument actually runs it, by about 5 points —
more than half again its own value.** Anyone budgeting from that row should
use the in-situ figure instead.

### 8.5 Is the finer round worth running? No.

§5 made this the round's decision and made it conditional: the finer ladder —
reverb smoothers, MORPH, `process_in`, `derive_intervals` measured separately —
is worth running **only if this one shows real work sitting in the glue.**

It does not. **The glue is 4.04 points in total**, run-to-run spread 0.32, and
that one number already covers every item the finer ladder would have split
apart at the instrument level. `derive_intervals` and MORPH live inside it;
splitting 4.04 points four ways yields four sub-point quantities.

Those quantities are **resolvable**, and this section should not pretend
otherwise: a finer ladder would subtract *within one run*, exactly as this
round did, and the within-run spreads here were 0.10 / 0.32 / 0.21 points.
§8.1's ±2-point bound is a **cross-build** bound and does not apply to that.
The argument against the finer round is a value judgement — four sub-point
numbers inside a 4.04-point bucket are too small to repay a hardware round —
resting on two measured facts, not on resolution: deleting the *entire* glue
bucket would still leave 6.73 of the 10.77 points outstanding, and
`process_in`, the one named target with real weight behind it, is not in the
glue at all.

The reverb smoothers live in the 5.17-point in-situ excess, whose largest
component is probably an operating-point difference rather than overhead.
`process_in` is the one named target that is *not* in the glue — it is inside
the 15.47 — and the ladder §5 described would not have measured it.

**Recommendation: do not run it.** The two bare decks are **92.48 points,
83.5 % of the gate.** That is where the remaining 10.77 points have to come
from, and the existing rows already say which lines are large enough to
matter: FLUX hot at roughly 13 points per deck (~26 across the instrument, a
quarter of the budget) and the eight voices at 35.80. Nothing else in the
instrument is big enough to close a 10.77-point gap even if it were deleted
outright.

The one cheap follow-up worth having is **one row, not a ladder**: the reverb
alone at the instrument's *actual* operating point — **SIZE 1.0 and TONE
0.5**, not SIZE 1.0 alone. `setup_reverb` differs from the instrument on both
(§8.4), so a row that copies only SIZE would carry the TONE difference into
the answer and hand the next round the same confound this one had to find.
That row splits §8.4's 5.17 points into "operating point" and "smoothers plus
mixing" for the cost of a single workload. If it lands mostly on the operating
point, the reverb has no overhead worth attacking either, and the search
narrows to FLUX and the voices with nothing else outstanding.
