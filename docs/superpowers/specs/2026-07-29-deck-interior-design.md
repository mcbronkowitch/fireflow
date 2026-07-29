# Inside the deck: where the 7.73 unpriced points per deck go

**Date:** 2026-07-29
**Status:** design, approved by the owner
**Predecessor:** `docs/superpowers/specs/2026-07-29-instrument-ablation-design.md`
**Evidence:** `docs/bench/2026-07-29-930ec17-ablate.csv`

---

## 1. The question

The instrument-level ablation closed one level and opened the next. Its §8.3
result:

| where the 24.14 unaccounted points are | points |
|---|---:|
| **inside the two `Part`s, beyond their own block rows** | **+15.47** |
| contention between the decks | −0.54 |
| instrument-level glue | +4.04 |
| the reverb costing more in situ | +5.17 |

**7.73 points per deck** sit inside a `Part` and are priced by no block row.
That is the largest single unattributed quantity this project has, and the
gate needs 10.77 points to reach 100 %. This round splits it.

It is a diagnostic round, like its predecessor. It changes no engine code and
optimises nothing. Its product is knowing where to aim.

## 2. What reading already established

Three things were settled by reading `engine/parts/part.cpp` before this spec
was written. Each removes a candidate or names one precisely, and none needs
measuring.

### 2.1 The four engines are not a cost

A `Part` holds `_synth`, `_wave`, `_body` and `_sampler` simultaneously, and
every `set_voice_*` forward writes to all four (`engine/parts/part.h:138-148`).
That looks like a fourfold cost and is not: `_engine` is a single pointer and
`Part::process` calls `_engine->process()` once
(`engine/parts/part.cpp:483`). The engine switch is a **gain multiply**, not a
second engine —

```cpp
const float fade = _engine_fade.process();
...
outL *= fade;
outR *= fade;
```

— and at hold the multiplier is exactly 1.0, which is what keeps unswitched
runs bit-identical (the M1.6 bypass invariant, stated in that function's own
comment). The three inactive engines cost memory and nothing else.

**This candidate is closed. Do not re-investigate it.**

### 2.2 `process_in()` is unpriced work, not a wrong operating point

`Part::process` calls `_engine->process_in(inL, inR)` every sample
(`engine/parts/part.cpp:482`). `proc_engine_2x4` (`bench/engine_2x4.h`) calls
only `process()`. So `synth_2x4`'s 35.80 points contain **no** `process_in`
at all.

This distinction matters for what a fix would look like. An operating-point
error means the row is right and the setting is wrong; unpriced work means the
row is incomplete. §8.3 of the predecessor conflated exactly these two and had
to be corrected.

**Caveat, found in `deck_engine_hot`'s review (fix round 1):** on a SYNTH deck
— this round's only engine — the "unpriced work" this section names is
dispatch cost, not compute. `SynthEngineT` never overrides `process_in()`; it
inherits `IPartEngine`'s empty default body (`engine/parts/engine_iface.h:57-
59`, "Only the sampler implements it"). So for `deck_engine_hot`, the marginal
cost of the `process_in` call this section describes **is** the virtual-
dispatch cost §2.3 counts under "two virtual calls per sample" — not a
second, additive charge on top of it. The distinction this section draws is
real only on a SAMPLER deck: `SamplerEngine::process_in`
(`engine/sampler/sampler_engine.cpp:158`) actually records and monitors from
the call, so a sampler-engine row would price real internal work through it,
not just a dispatch. `deck_engine_hot` measures a SYNTH deck, so its
`process_in` difference from `synth_2x4` is dispatch-only, and the round must
not budget it as a second cost beyond §2.3's.

### 2.3 The engine is called through a virtual interface, twice per sample

`_engine` is an `IPartEngine*` (`engine/parts/part.h:209`), and every method
on that interface is virtual (`engine/parts/engine_iface.h:22-59`), including
both `process` and `process_in`. `Part::process` therefore makes **two virtual
calls per sample**, neither of which the compiler can inline.

`proc_engine_2x4` holds a concrete `SynthEngine&` and calls `process()`
directly, so the 35.80 points were measured with that call inlined. Dispatch
cost is real, per-sample, and appears in no row.

This one is structural rather than fixable: the four-engine design *is* the
interface. It is named here so the round attributes it correctly rather than
discovering it as an unexplained residue — and it is the reason
`deck_engine_hot` must call through an `IPartEngine*`, not through a concrete
reference (see §4).

### 2.4 The gate runs a drone; the voice rows run gated notes

`_step_on` initialises to `false` (`engine/parts/part.cpp:35`), `flow()` is
`!_step_on` (`part.h:98`), and `setup_inst_worst` never calls `set_step`. The
gate therefore runs **both decks in FLOW — a drone.**

`setup_engine_2x4` sets `set_flow(false)`, i.e. **STEP**
(`bench/engine_2x4.h`). It also sets `set_cycle(2.f)` as a constant, where
`Part::process` derives it from the modulation:

```cpp
const float hz = _mod.master_hz();
if (hz != _last_master_hz && hz > 0.f) {
    _last_master_hz = hz;
    _engine->set_cycle(1.f / hz);
}
```

RATE 0.8 alone sets that baseline: `master_hz() = free_hz(0.8) ≈ 6.95 Hz`
(`engine/mod/divisions.h`), not the 0.5 Hz `set_cycle(2.f)` encodes. DENSITY
and BPM do not enter it: DENSITY is inert in FLOW (see the caveat under §3's
row table), and the modulator runs in FREE mode — `_synced` defaults false,
so `_update_rate()` never reads `_bpm` either (`engine/mod/super_modulator.cpp:28-29`).

And for this exact configuration **it does not move**. `master_hz() =
_base_hz * _pitch_scale`, and `_pitch_scale` changes only through
`set_rate_scale()`, called only from `Center::update`
(`engine/center/center.cpp:166-167,229-230`). `Center::init` sets `_couple =
0.f` and `_drift = 0.f` (`engine/center/center.cpp:57-61`), and neither
`setup_inst_common`, `setup_inst_worst`, nor `setup_inst_worst_bbd` ever calls
`set_couple`, `set_drift`, or `set_sync` — so in `Center::update`'s FREE-WORLD
branch (`_sync` stays false), `conv_a`/`conv_b` collapse to `pow(x, 0) = 1`,
`corr = 0 * … = 0`, and `rate_drift_a`/`rate_drift_b = pow(2, … * 0) = 1`
exactly (the source's own words: "exactly 0 while drift is 0",
`engine/center/center.cpp:133`). `set_rate_scale(1.f, 1.f)` runs every
control tick, always with the same arguments, so `_pitch_scale` is pinned at
exactly 1.0 and `master_hz()` never changes for the gate either. See §3.2 for
the consequence.

**These are not two settings a knob apart. They are two modes.** Whether a
mode difference costs anything is precisely what the round measures — it may
well be nothing, and that is a result.

## 3. The ladder

Per deck, the block rows price 38.505 points:

| block | source | per deck |
|---|---|---:|
| voices | `synth_2x4` / 2 | 17.90 |
| FX shell | `fx_none` | 2.54 |
| GRIT | `fx_grit` − `fx_none` | 3.07 |
| FLUX | `fx_flux_sdram` − `fx_none` | 10.59 |
| COMP | `fx_comp` − `fx_none` | 0.72 |
| modulation | `mod_plane_2x_center` / 2 | 3.685 |
| **sum** | | **38.505** |

`instr_part_1` measured **46.24**. The excess is **7.73**.

Two new rows correct the two block rows whose operating point provably
differs from the gate's. Everything else is then a named remainder.

| row | what it is |
|---|---|
| **`deck_engine_hot`** | one `SynthEngine`, driven as `Part::process` drives it: FLOW, cycle from a real modulator's `master_hz()`, `set_decay(1.0)`, and `process_in()` called every sample |
| **`deck_mod_hot`** | one `SuperModulator` at the gate's RATE 0.8 / DENSITY 1.0, **without** a `Center` |

DENSITY 1.0 is set for faithfulness to the gate, not because it costs
anything: DENSITY is inert in FLOW, and `deck_mod_hot`, `mod_plane_2x_center`,
and the gate itself all run in FLOW — `setup_inst_worst` never calls
`set_step` (§2.4), and neither does `mod_plane_2x_center`'s setup. The
mechanism is narrow: `set_density()` writes only `ModLane::_density`
(`lane.h:23`), read solely by `_groove_k()` (`lane.cpp:422`), which
`_effective_gate()` reaches only when `_step_mode` is true (`lane.cpp:449`).
With `_step_mode` false on all three configurations, `_on_boundary()` hardcodes
`gated = true` regardless of DENSITY. This is a finding of the round, not a
caveat on the method: a knob the gate sets has no cost implication at the
gate's own operating point.

Three differences follow:

- **`deck_engine_hot` − `synth_2x4` / 2** — what the voices really cost at the
  gate's operating point, including `process_in`. Answers the round's
  originating question: are the 35.80 points underpriced, and by how much?
- **`deck_mod_hot` − `mod_plane_2x_center` / 2** — the same for the modulation.
  This difference also removes a known defect: `mod_plane_2x_center` includes
  the instrument-level `Center`, which no bare `Part` runs, so charging each
  deck half of it double-counts against the glue term that already contains
  `Center::update`. The predecessor's §8.3 flagged this and could not fix it.
- **`instr_part_1` − `deck_engine_hot` − `deck_mod_hot` − (FX shell + GRIT +
  FLUX + COMP)** — the remainder: the chord builder, the quantizer,
  `_control_tick`, the fade multiply, and the per-sample loop itself.

### 3.1 Why the remainder gets no row of its own

The owner chose the three-rung shape deliberately. A row for the remainder
would mean instantiating a `Part` with its engine and FX suppressed, which the
`Part` interface does not offer — it would have to be faked, and faked
structure is what the predecessor round was built to avoid (its §3.1). The
remainder is therefore **stated as a subtraction and labelled as such**, with
its constituents named so a later round can price any of them directly.

### 3.2 How `deck_engine_hot` gets its cycle

Hardcoding a cycle constant would reintroduce the exact defect this round
exists to correct. Instead the row builds a real `SuperModulator`, configures
it as the gate does, runs it through the settle, reads `master_hz()`, and uses
`1 / master_hz()` as the engine's cycle. The modulator is then **not** driven
inside the measured loop — its cost belongs to `deck_mod_hot`, and paying it
twice would corrupt both rows.

The read-back value is asserted non-zero, asserted different from 0.5 Hz (the
`set_cycle(2.f)` the old row uses), and folded into the checksum. A row that
silently fell back on the old operating point fails loudly instead of
returning a plausible number.

**The naive worry — that deriving the cycle once and holding it undermeasures
against a gate whose cycle keeps changing — does not apply to this round's
gate.** `Part::process` only re-pushes `set_cycle` when `master_hz()` actually
changes (`if (hz != _last_master_hz && hz > 0.f) { … _engine->set_cycle(1.f /
hz); }`, `engine/parts/part.cpp:399-402`, quoted in §2.4) — and for
`instrument_worst_bbd`'s exact configuration, as §2.4 establishes, it never
does: `_couple` and `_drift` both default to 0 and nothing in this bench path
ever sets them, so `Center::update`'s `set_rate_scale` calls always pass
`(1.f, 1.f)`, and `_pitch_scale` — hence `master_hz()` — is pinned for the
whole run, on the gate exactly as much as on `deck_engine_hot`.

**The bias is therefore zero, not merely small, for this round's rows.**
`Part::process`'s guard (`hz != _last_master_hz`) fires `set_cycle` once, at
the first control tick, on both sides, and never again — the derive-once
cycle is exact here, not an approximation accepted for simplicity. This does
not generalise past this configuration: a row that enabled COUPLE or DRIFT
would make the gate's `set_rate_scale` arguments genuinely vary from tick to
tick and reintroduce a real re-push cost that a derive-once row would still
not pay. That is not the operating point `setup_inst_worst` configures, so it
is out of scope for this round, not a bias this round carries.

## 4. The risk, and what bounds it

Same shape as the predecessor's §4: **`deck_engine_hot` must drive its engine
exactly as `Part::process` drives it**, or every difference measures the
divergence instead of what it names.

This is a shorter checklist than last round's, because the call sites are all
in one function. From `Part::init` and `Part::process`:

```
set_seed(seed_base ^ 0x5eedC0DE)   init(sample_rate)
set_flow(true)                     (boot: FLOW, and set_step is never called)
set_decay(1.0)                     (set_voice_decay(1.0), part.h:139)
set_cycle(1 / master_hz())         (derived, see 3.2)
process_in(inL, inR)               every sample, before process()
process(outL, outR)                every sample
```

**Both per-sample calls go through an `IPartEngine*`, not through a concrete
`SynthEngine&`** (§2.3). The row holds a `SynthEngine` and drives it through a
base pointer to it, so the measured loop pays the same virtual dispatch
`Part::process` pays. Calling the concrete type instead would let the compiler
inline both calls and would push the dispatch cost silently into the
remainder — the single easiest way to get this row wrong.

Three things the row deliberately does not get, because they are `Part`-level
and belong in the remainder: `_chord`/`_quantizer` (fed by COLOR, which never
reaches the engine — `set_color` goes to `_chord.set_color`,
`engine/parts/part.cpp:275`), `_control_tick`'s target pushes, and the
`_engine_fade` multiply.

**The bound on being wrong:** both new rows are compared against rows measured
in the *same* run, and the remainder is a subtraction within that run. No
cross-build figure enters any difference. The predecessor established that
cross-build comparison in this project carries about ±2 `pct_max` points on the
gate; that bound applies to none of the numbers this round computes.

## 5. What the round answers, and what it does not

It answers: **how much of the 7.73 points per deck is the voices at the
wrong operating point, how much is the modulation, and how much is `Part`'s
own structure.**

It does **not** identify a line worth changing. It does not decide whether to
cut a voice, and it does not touch `engine/`.

**A null result is a result.** If both corrections come back near zero, then
the 7.73 points are `Part` structure — the chord builder, the quantizer, the
control tick — and the voice rows were right all along. Write that as plainly
as the other outcome. The predecessor round committed to this in advance and
kept it; so does this one.

One outcome is worth naming ahead of time because it would be actionable
immediately: if `deck_engine_hot − synth_2x4 / 2` is large, the voices cost
more than 17.90 per deck, and the per-voice figure of 4.48 that the "cut a
voice" option rests on is **understated** — cutting one would save more than
8.95 points, not less.

## 6. Verification

- New rows join the existing **`instr`** family and the **`ablate`** profile.
  No new family, no new profile, no Makefile change.
- **Four registration points must agree** or the whole run fails: the
  `kInstrWorkloads[]` table, `bench/run.py`'s
  `BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"]`, and the contract test. Note that
  `run.py` compares the row **set**, not its order
  (`bench/run.py:280-286`) — the predecessor's plan claimed otherwise and was
  wrong.
- Confirm with `cd bench && python -m unittest test_run_contract` — unittest,
  not pytest.
- **The image must link.** SRAM sits at 97.83 %. `SerialArena` overlays its
  groups, so two more groups cost only what they exceed the largest existing
  one by. If it stops linking, that is a finding, not something to squeeze
  past.
- **Hardware:** `python bench/run.py --profile ablate` — never without
  `--profile`. Build, then rebind the QSPI receipt, then measure, in that
  order (`bench/README.md`).
- No engine file is modified, so **no existing row's checksum may move.** The
  predecessor proved this bench's costs drift with layout while checksums hold;
  a moved checksum is therefore a real finding, and drift in cost is expected
  and recorded separately.

## 7. Global constraints

- Work in the fork at `C:\Users\bernd\Documents\AI\Spotykach`, on branch
  `perf/deck-interior-ablation`, never on `main`.
- Commit trailer is exactly, with nothing after it:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **`engine/` is off limits.** `git diff main -- engine/` must stay empty. If a
  measurement appears to need an engine change, that is a finding to report,
  not a change to make.
- Do not add, remove or rename any existing bench row.
- `source ./env.sh` before any cmake or ctest invocation, in the same shell
  command.
- The bench refuses hardware evidence from a dirty git tree.
- No bit-exactness or checksum-against-stored-file gates; the bench's own
  cross-run comparison is a different thing and is required.

---

## 8. Results

**Evidence:** `docs/bench/2026-07-29-c4ae8db-ablate.{md,csv}`, run 2, `pct_max`
unless stated. Every figure below comes from that single run; no baseline
number enters any difference.

### 8.1 The answer: the operating points were not the problem

**Of the 7.645 unpriced points per deck, 6.85 — nearly 90 % — is `Part`
structure.** The two corrections this round was built to measure come to
0.795 between them.

| | `pct_max` | `pct_avg` |
|---|---:|---:|
| voices: `deck_engine_hot` − `synth_2x4` / 2 | **+0.70** | +0.29 |
| modulation: `deck_mod_hot` − `mod_plane_2x_center` / 2 | **+0.10** | −0.05 |
| **remainder: `Part` structure** | **+6.85** | +5.77 |
| total = the per-deck excess | 7.645 | 6.015 |

This is a null result for the hypothesis §1 was written to test, and §5
committed in advance to writing it as plainly as the other kind. The four
differences §2.2–§2.4 established by reading — two virtual dispatches per
sample, FLOW instead of STEP, a cycle 14× shorter than the old row's, and
`process_in` called where the old row never calls it — **cost 0.70 points per
deck in total.** All four were real; none of them was expensive.

The remainder is what is left when the engine, the modulation and the four FX
blocks are all subtracted from a whole deck: the chord builder, the quantizer,
`_control_tick`'s target pushes, `_adjust_surface` running every tick in FLOW,
the `_engine_fade` multiply, and `Part::process`'s own per-sample loop.

**Both forms of the remainder agree**, which is where an arithmetic slip would
have hidden:

```
46.00 − 18.61 − 3.81 − (2.54 + 2.89 + 10.58 + 0.72)                    = 6.85
46.00 − 18.61 − 3.81 − (5.43 + 13.12 + 3.26 − 2 × 2.54)               = 6.85
```

and the per-deck block sum reconstructs the excess exactly:
`17.915 + 16.73 + 3.71 = 38.355`, against `instr_part_1` at `46.00`, leaving
`7.645 = 0.695 + 0.10 + 6.85`.

### 8.2 The fix to `deck_engine_hot` was falsified, not merely confirmed

The row's first version let voice occupancy collapse from 4 to 1 partway
through the measured window: the derived cycle (0.1439 s) gives
`decay_s = 8 × cycle = 1.151 s` and an `Env` Idle threshold at 1.535 s, which
lands 0.94 s inside a 2.0 s window, while `synth_2x4` holds four voices for
its whole run. The review that caught it predicted the symptom: unfixed, the
row would return `pct_avg / pct_max ≈ 0.55–0.65`, an outlier no other row in
this bench shows.

After the fix — a `trigger_chord` cadence every 6908 samples, mirroring the
`master_hz` rate at which a real deck's PITCH lane fires — the row returned
**17.94 / 18.61 = 0.964**, inside the predicted 0.96–0.99. The spread of 3.7 %
relative sits slightly above the predicted 1–3 % and well below the 5 % that
would have meant the cadence itself needed pricing out.

The predicted floor was the sharper test: the row **cannot** legitimately cost
less than half of `synth_2x4` (17.915), since it runs the same four voices and
additionally pays two un-inlined dispatches, `_adjust_surface` every control
tick, and ~14 chord fires. It came back at 18.61 — above the floor by 0.695,
which *is* the voices correction. A figure below ~17.5 would have meant the
occupancy was still wrong and the `voices == 4` assert was passing on a state
that does not persist.

### 8.3 Are the 35.80 points underpriced? No — and the per-voice figure needs a
different correction than expected

`synth_2x4` prices eight voices at 35.83, i.e. **4.479 per voice**, and
`deck_engine_hot` prices four at 18.61, i.e. **4.65 per voice** at the gate's
own operating point. The rows were right.

**But that average is the wrong number for the "cut a voice" decision, and it
is too generous.** The existing ladder prices polyphony directly:

| row | `pct_max` | increment |
|---|---:|---|
| `synth_1_voice` | 5.65 | — |
| `synth_2_voices` | 9.91 | +4.26 |
| `synth_4_voices` | 17.85 | +7.94, i.e. **+3.97 per voice** |

So roughly 1.39 points per engine are fixed overhead that removing a voice
does not reclaim, and the **marginal** voice costs about **3.97**, not 4.48.
Going from four voices to three per deck therefore saves approximately
**7.9 points**, not the 9.3 that the average implies.

The gate stands at **110.10** in this run, so 10.10 points are needed. One
voice per deck is about **78 % of the gap** — a large, cheap, structural
saving, but not sufficient on its own. This figure is derived by differencing
`synth_N_voices`, which run at the *old* operating point; the marginal cost at
the gate's operating point is not measured, and a row for it would be one
cheap row, not a ladder.

### 8.4 Two findings from reading, which no row produced

Recorded because a later round would otherwise spend a ladder rediscovering
them:

- **The four engines a `Part` holds cost memory and no CPU.** `_engine` is one
  pointer, `Part::process` calls through it once, and the switch is a gain
  multiply that is exactly 1.0 at hold (§2.1). **This candidate is closed.**
- **The two virtual dispatches per sample are structural.** The interface *is*
  the four-engine design (§2.3). They are inside the 0.70-point voices
  correction, so their ceiling is now known and small — but they are not
  deletable without changing that design.

A third, narrower finding came out of the round's own fix rounds: **DENSITY is
inert in FLOW**, in `deck_mod_hot`, in `mod_plane_2x_center` and in the gate
itself, because `_density` is read only by `_groove_k()`, reached only from
`_effective_gate()`, consulted only when `_step_mode` is true — and none of the
three ever calls `set_step` (§3). A knob the gate sets to 1.0 has no cost
implication at the gate's own operating point. `set_tempo_bpm` is inert for the
same class of reason in FREE mode (§3).

### 8.5 The layout drift is now reproducible, and `fx_grit` is its clearest case

Every one of the 18 rows shared with `2026-07-29-930ec17-ablate.csv` returned
an **identical checksum**, and the row count matches on both sides, so the
comparison is not vacuously clean. The costs moved anyway:

| row | 930ec17 | c4ae8db | Δ |
|---|---:|---:|---:|
| `instrument_worst_bbd` (the gate) | 110.77 | 110.10 | −0.67 |
| `fx_grit` | 5.61 | 5.43 | −0.18 |
| `oliverb_solo_sram` | 9.62 | 9.66 | +0.04 |

`fx_grit` is the row the mono round first suspected of layout sensitivity and
could not prove. This is the third consecutive run in which it moves at an
unchanged checksum, and the gate has now moved in both transitions
(112.79/112.88 → 110.78 → 110.10). The predecessor's ±2-point bound on
cross-build comparison holds and is not tightened by this round.

### 8.6 What is worth measuring next

Unlike the predecessor round, **the finer ladder now has a subject.** The
remainder is 6.85 points per deck — **13.70 across the instrument** — which is
larger than the 10.10 the gate needs. Last round's recommendation against a
finer split rested on the glue being only 4.04 points, too small to repay a
round even if deleted entirely. That argument does not apply here.

Two cheap rows would split it, in this order:

1. **A `Part` with its chord surface held at one note** — i.e. the difference
   `_adjust_surface` and the chord/quantizer path make. This is the largest
   named constituent and the only one whose exclusion this round had to
   document as a consequence rather than measure.
2. **The marginal voice at the gate's operating point** — one row at three
   voices instead of four, so the 7.9-point estimate in §8.3 stops being a
   difference of rows measured at the old operating point.

The second is the one that changes a decision. If the marginal voice at the
faithful operating point is nearer 4.65 than 3.97, cutting one closes 93 % of
the gap instead of 78 %, and the panel question ("four voices or three")
becomes the cheapest route to 100 % that this project has.
