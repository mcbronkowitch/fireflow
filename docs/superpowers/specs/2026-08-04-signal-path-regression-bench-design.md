# Signal-path regression bench — design

**Date:** 2026-08-04
**Status:** design, not yet planned or executed
**Kind:** measurement round. It changes `bench/`, and it does not change
`engine/`.

## 1. Why this round exists

The last hardware evidence on this desk is
`docs/bench/2026-08-01-19f7560-flux-tape.md`. In that session the decision
gate `instrument_worst_bbd_dtcm` read **97.91 % `pct_max` offline / 98.02 %
in the real callback** — below the 960,000-cycle block budget, with about two
points of margin and no more.

Since that commit, **seventeen commits have landed under `engine/` and none
under `bench/`** (72 commits total on the branch; the rest are VCV, docs and
release chores). Every one of the seventeen is in the per-sample path the
gate row executes:

| commit | what it changed |
|---|---|
| `a183852`, `28fda8d` | `BbdLine::Process` — crossfaded stage taps, and a write ring that now spans `max_cells_` |
| `10961a0`, `02134e3`, `25a2b80` | `AmbientReverb` — bloom rewrite, bounded return, reported return level |
| `25675db`, `0be9120`, `dcde0e3`, `e6f841b`, `701e3f5` | `Instrument::process` — the bloom duck on the dry bus |
| `a99af28`, `c37585b` | the master limiter's DRIVE shaper and its glided pre-gain |
| `653f49a` | `Comp` — the env ceiling's fade |
| `d67d5d0`, `3f53baa`, `47efb5e`, `bcb83df` | `SoftSwitch`, FLUX line flush/ramp, the reverb's fade-before-clear |

None of it is measured. Two points of margin is not enough margin to assume
it survived.

### 1.1 The three questions, and the row that answers each

1. **Does the instrument worst case still fit?**
   → `instrument_worst_bbd_dtcm` `pct_max` against 100 %.
2. **What did the signal-path round cost?**
   → the same row, in both trees, in one session (§3).
3. **What does the BBD crossfade cost, and what does the full-span write ring
   cost?**
   → the `bbd` family in both trees, plus one new row (§5).

### 1.2 What this round does not answer

It does not attribute points *within* the FX chain — reverb bloom versus
duck versus DRIVE shaper versus COMP ceiling. No row isolates any of those
today and this round does not build one. If the gate breaks, the **next**
round is an attribution round with its own spec; this one hands it the
number, not the cause.

It also does not re-price the `voice`, `mem`, `mod`, `abl`, `body` or
`sampler` families. Their last figures stand, with the dates they were taken.

## 2. The BBD engine, stated precisely

"The BBD engine has not been benched" is true in three specific senses and
false in one, and the round is scoped to the true ones.

**False:** the BBD *part engine* has been measured twice on this desk —
`2026-07-31-b9afe47-bbd-engine.md` (freeze off, 82.88 / 86.19) and
`2026-08-01-19f7560-flux-tape.md` (freeze held, 94.16 / 98.39–98.56).

**True, and this round's scope:**

- Neither session includes `a183852`. `BbdLine::Process` — the ITCM-hot
  per-sample kernel both rows execute — has changed since.
- **No `-O3` measurement of `inst_bbd_engine_worst` exists.** Both sessions
  linked `-O2`; the production root makefile ships `OPT = -O3`. `docs/roadmap.md`
  states this gap explicitly.
- The fx-level `bbd` family (`bbd_ceiling`, `bbd_line_only`, `bbd_line_tap`,
  `bbd_line_tap_half`, `bbd_walk_sdram`) was last measured **2026-07-28**,
  also before the crossfade.

### 2.1 The write ring is the finding that shaped this design

`a183852` did not only add a crossfade. It changed where the write index
wraps:

```c
// before
imem_ = (imem_ + 1 < cells_) ? imem_ + 1 : 0;
// after
write_index_ = (write_index_ + 1 < static_cast<int>(max_cells_))
                     ? write_index_ + 1 : 0;
```

Before, a short delay walked a short ring. Now **every** delay walks the full
`max_cells_` span. At `kMaxStages = 16384` that is `kCells = 8192` floats —
32 KB per line, in SDRAM; `BbdEngine` runs a stereo pair per deck, so
`inst_bbd_engine_worst` walks four of them, 128 KB. The shortest division is
exactly that row's operating point.

The consequence for the bench is that
`bench/workloads_bbd.cpp:145`'s comment — "The active window at 8192 stages
is 4096 cells = 16 KB per line" — now describes a state that no longer
exists. The comment is corrected as part of this round.

Whether the wider span costs measurable cycles is an open question this round
observes rather than predicts: the two `bbd_line_tap` rows already stand at
two clock rates in both trees, and the A/B will show it if it is there.

### 2.2 The crossfade is invisible to every existing row

`_stage_transition_active()` is true for `kStageXfadeReads = 16` read events
after a stage change and false otherwise. A row that holds its division fixed
never enters that branch and therefore never prices it. §5's new row exists
for exactly this reason, and its shape is dictated by the arithmetic: at the
ceiling clock a line performs ~0.67 read events per audio sample, so sixteen
reads is ~24 samples out of a 96-sample block. **One transition per block
would price the crossfade at a quarter and read as noise.**

## 3. The baseline is constructed, not checked out

The delta in question is a difference between two trees. `docs/bench/README`
and `2026-07-31-b9afe47-bbd-engine.md` §8.2 both record what this repo has
already been burned by: **composition and layout move the gate by points at
an unchanged checksum**, so a cross-image subtraction is not a measurement.

A same-session A/B is available here only because of an accident of history:
**no commit between `19f7560` and `HEAD` touched `bench/`.** Today's bench
code has, by construction, already compiled against the old engine.

So the baseline is a commit on a branch, not a checkout of `19f7560`:

```
branch bench/baseline-19f7560 = HEAD, with engine/ replaced by 19f7560's engine/
```

Checking out `19f7560` itself would not work: that tree has no `regress`
profile and its `run.py` row expectations do not contain the new row.

Both trees are committed. The bench refuses hardware evidence from a dirty
git tree, and that check is load-bearing here, not incidental.

The A/B therefore differs in `engine/` and nothing else: same bench code,
same row table, same profile, same desk, same session, same probe.

## 4. The measurement matrix

**Profile `regress` = `system` + `bbd`**, gates `{wave_acceptance}` (the
`system` family supplies `synth_2x4` and `wave_2x4`, which is what
`profiles.resolve()` validates). It is added alongside the existing profiles;
`bbd` and `system` are not modified or removed.

Two families in one image puts the gate rows and the BBD kernel rows in the
**same** binary, which is the comparison `2026-07-31-b9afe47-bbd-engine.md`
left open ("what would settle it is a same-build A/B"). `body`
(`system`+`body`) and `sweep` (`system`+`sweep`) are the precedents that a
two-family image links.

Family execution order is `system` then `bbd`, which is the order `full`
already declared, so the SDRAM-arena hazard documented in `bench/README.md`
is unchanged: `bbd` setups refill the arena they read.

| # | tree | profile | optimization | layout |
|---|---|---|---|---|
| 1 | `bench/baseline-19f7560` | `regress` | `o3` | `axi` |
| 2 | `bench/baseline-19f7560` | `regress` | `o3` | `itcm-hot` |
| 3 | `HEAD` | `regress` | `o3` | `axi` |
| 4 | `HEAD` | `regress` | `o3` | `itcm-hot` |

`-O3` is fixed because that is what ships. Layout is the variable because
the two answers differ: `axi` is what an M6 firmware would get **today**
(there is no ITCM loader yet), `itcm-hot` is the intended placement. LTO is
already rejected and is not measured.

Baseline first, both layouts, then `HEAD`. The comparison value is then
already on disk when the interesting number arrives, rather than after it.

Each cycle is the full `bench/README.md` sequence, and it does not shorten:

```
python run.py --profile regress --optimization o3 [--itcm-hot] --build-only
python run.py --profile regress --optimization o3 [--itcm-hot] --no-build --program-qspi --build-only
python run.py --profile regress --optimization o3 [--itcm-hot] --repeat 2
```

Any change under `engine/` invalidates the QSPI receipt even though the
bank's 65,024 bytes are untouched, and step 1 before step 3 is the documented
order whose violation costs a programming cycle and reads like a hardware
fault.

## 5. The new row: `bbd_line_stage_walk`

**Appended at the end of `kBbdWorkloads`.** Row order is execution state, not
presentation — `bench/README.md` records a checksum moving on an untouched
row because a row was inserted *before* it.

A `BbdLine` configured exactly as `bbd_line_tap`'s (ceiling clock, same
stages, same input), whose `SetStages` is driven between two values across
the measured block so that `_stage_transition_active()` holds for the large
majority of it. It prices the **sustained** transition, which is what a
plane-driven `LANE_SIZE` produces, rather than a single edge diluted into a
block average.

Read against `bbd_line_tap`, same build, same session: the difference **is**
the crossfade price.

The row is compiled in **both** trees. In the baseline it links against the
old `SetStages`, which returns early on an unchanged value and resets the
ring index on a changed one. Its A/B is therefore, directly, "click versus
crossfade, in cycles".

Files this touches: `bench/workloads_bbd.cpp` (the row and the stale-comment
correction), `bench/run.py` (`BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]`),
`bench/profiles.py` (the `regress` profile), `bench/README.md` (the profile
table). `families.cpp`, `main.cpp` and the `Makefile` need no change: the
family is registered and the runner iterates it generically.

## 6. Sequence

### 6.1 Offline preflight — nothing is plugged in yet

Everything that can fail here fails before a cable is touched.

1. Working tree clean; both trees committed.
2. `cmake --build build && ctest --test-dir build --output-on-failure` on the
   desktop host. The seventeen commits each passed tests; they have not been
   run together on this exact state.
3. One `host/render` scenario as a plausibility check. **No checksum gate** —
   renders are sanity checks in this repo, not byte-identity proofs.
4. **The link probe.** `--build-only` for all four matrix cells. Four builds,
   no hardware. `system`+`sweep` last linked with 3,296 bytes of `SRAM` free,
   so `regress` fitting is likely but not established. If it does not link,
   the round falls back to §8's plan B here, on purpose, rather than
   discovering it mid-session.
5. **Row-real check**, from `build/bench.map`, not from a memory table:
   `bbd_line_stage_walk`, `instrument_worst_bbd_dtcm`, `inst_bbd_engine_worst`
   and `bbd_line_tap` must each appear. The bench build can silently relink a
   stale object and still print a plausible figure for code that was never
   linked.

### 6.2 Hardware

Four cycles in matrix order. Monitors connected and **quiet** first: each
cycle produces two anchor bursts (eight in total), and the `instrument_worst`
segment inside each is underrun garbage on purpose, because that row is over
budget offline.

## 7. Gates, and what stops the round

`run.py` brings its own gates and they are not duplicated here: complete row
set with no missing/extra/duplicate rows, checksum equality across both
repeats, live QSPI digest against the receipt, MCU UID against the receipt,
reported layout and optimization against the request, WAVE acceptance, every
profile-derived anchor exactly once and numeric.

**Stops the round:**

- link probe fails → fall back to §8 plan B, and record why;
- ITCM placement preflight fails → run `axi` only, and name the omission;
- checksum drift between repeats;
- receipt / UID / layout / optimization mismatch;
- a dirty tree.

**Does not stop the round — it is the result:** the gate over 100 %. A valid
over-budget DTCM+BBD result is archived as rejected evidence rather than
treated as malformed, which is `run.py`'s existing behaviour and the correct
one. That outcome answers question 1; it does not fail to answer it.

## 8. Fallback

If `regress` does not link, the round runs the existing `system` and `bbd`
profiles separately: eight cycles instead of four, no link risk. The cost is
named rather than hidden — `bbd` carries no gate anchor, so `verdict()`
reports "undetermined" for those captures (this is documented in
`profiles.py` and is correct for a component profile), and gate-versus-kernel
becomes a cross-image comparison that §3 says is not a measurement. Under
plan B the BBD figures are reported as component prices only, and no
gate-versus-kernel arithmetic is performed.

## 9. Evidence

Eight raw captures (four cycles × `.md`/`.csv`) land in `docs/bench/`
automatically, named by date, git hash, profile, layout and optimization.

One hand-written document is added in the house style,
`docs/bench/2026-08-04-<hash>-signal-path-regression.md`:

- the row-real evidence, read from `bench.map`;
- the same-build tables, with `pct_max` bold, because `pct_max` is the
  decision value;
- the per-row A/B delta between the two trees, at each layout;
- a named section for what the round does not show — §1.2's list, plus
  anything the session itself leaves open.

`docs/roadmap.md`'s CPU-status paragraph is updated afterwards with the
result and its date.

## 10. Out of scope

This round changes nothing under `engine/`. If it finds a regression it ends
with the number and the row, not with a fix. The attribution round is next
and gets its own spec.
