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
