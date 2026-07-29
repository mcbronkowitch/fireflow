# FLUX: control-rate work out of the per-sample path

**Date:** 2026-07-29
**Status:** design, approved by the owner
**Predecessor:** `docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md` §9
**Evidence:** `docs/bench/2026-07-29-cd6dafd-sweep.md`

---

## 1. What this round is

The cost-curves round measured where FLUX's cost lives and found no knee: FLUX
costs 16.87 points per deck at every setting, and the whole rate ladder spans
3.72 points. It split that 16.87 into **9.16 model** (the two `BbdEcho`
instances) and **7.71 wrapper** (everything in `Flux::process` and the
per-sample pushes into it from `PartFx::process`).

This round takes the wrapper. It is step one of the owner's stated order —
*first the wrapper, then measure, then mono* — and it is the only lever on the
whole list that costs nothing musically.

It touches `engine/`, which every task in the previous round was forbidden to
do.

## 2. The measured expectation, corrected

The previous round's summary said the wrapper was worth "up to 15.4 points"
(2 decks × 7.71). That figure is the wrapper's **whole** cost, and the wrapper
was never wholly removable. Reading it line by line:

**Genuinely per-sample, stays:** the `SoftSwitch` crossfade; the two
`fonepole` slews on `_dt_current` and `_stage_current` plus the stage snap;
the DRAG/THIN accumulator; the division in `bbd_clock_hz` (the slewed delay
time really does move every sample); the stage-change compare; the gate
branch; the mix additions.

**Control-rate work in the per-sample path, goes:** exactly two calls, both
made unguarded once per sample from `part_fx.cpp:38` and `:46` —
`Flux::set_feedback` and `Flux::set_time_mod`.

The wrapper is 771 cycles per sample per deck (7.71 % of the 960,000-cycle
block over 96 samples). The removable part is the `std::pow` inside
`bbd_drive_gain` (~198 cycles, per the repo's own `abl/micro_powf` row), one
division, and two cross-translation-unit calls that do not inline. Estimate:
**~250 of 771 cycles, ~2.5 points per deck, ~5 points total.** Not 15.4.

That estimate is a prediction, not a result. The re-measurement in §8 settles
it, and a materially smaller figure is itself a finding — it would mean the
un-inlined call overhead is cheaper than assumed and the remaining wrapper
cost sits somewhere this reading did not identify.

Against the 32.8 points the gate needs, ~5 is not decisive on its own. It is
banked, at zero musical cost, before the round that does change the sound.

## 3. The rule

A value that is a pure function of control-rate inputs is computed when those
inputs change, not once per sample.

Two mechanisms carry it, both already idiomatic in this class:

- a **cached derived value**, written by the setter that owns its inputs
  (`BbdEcho::SetDrive` already does this with `sat_in_`);
- an **unchanged-value guard**, an early return when the pushed value equals
  the stored one (`Flux::set_drive`, `set_stages`, `set_link` and
  `PartFx`'s GRIT push already do this).

No control-tick restructuring. The 2 ms smoothers in `PartFx::process` stay
per-sample, and `FXT_FLUX_TIME` genuinely needs that resolution — it is the
vibrato lane, and `part_fx.cpp:39-45` records why it must not ride the 30 ms
ladder slew instead.

## 4. Change 1 — cache DRIVE's contribution to the feedback coefficient

`Flux::apply_feedback` currently evaluates, once per sample:

```cpp
const float fb = _fb_norm * 1.2f / bbd_drive_gain(_drive_norm);
```

`bbd_drive_gain` is `std::pow(10.f, db * 0.05f)`. Its only argument is
`_drive_norm`, and `Flux::set_drive` is the sole writer of `_drive_norm` and
already carries an unchanged-value guard. So the whole quotient
`1.2f / bbd_drive_gain(_drive_norm)` is a control-rate constant.

**Design:** a new member `_fb_scale` holds it. `set_drive` writes it before
calling `apply_feedback()`. `apply_feedback` becomes two multiplications.

This removes the `pow` **unconditionally** — not merely at rest, but during a
FEEDBACK knob move as well. That is strictly better than the guard in §5,
and the two compose.

### 4.1 The init ordering, and why the reset is required anyway

`Flux::init` calls `set_feedback(0.45f)` at `flux.cpp:65` **before**
`set_drive(0.f)` at `flux.cpp:67`. Today that is safe: `apply_feedback` reads
`_drive_norm`, which is `-1.f` at that moment, and `bbd_drive_gain` clamps it
to 0, giving a gain of 1 — the same value `set_drive(0.f)` produces two lines
later. With the cache, `apply_feedback` reads `_fb_scale` instead, and on a
*re-init* of an already-running `Flux` (`Spotymod::reinit()`, reached from a
sample-rate change, a reset, or a fresh sampler add) that member still holds
the previous DRIVE's factor.

**This is not an observable defect, and the spec should not pretend it is.**
`init` sets `_drive_norm = -1.f` at line 57, so `set_drive(0.f)` at line 67
always passes its own guard and always rewrites `_fb_scale`. The stale window
is two lines wide, with no audio processed inside it, and `_fb_scale` is
therefore correct by the time `init()` returns — with or without an explicit
reset.

**The reset is still required**, for a different reason: without it the
correctness of a cached value rests entirely on that ordering, and nothing
protects the ordering. Reordering `init()`, or giving `set_drive` a reason to
return early, would turn a two-line window into a real one silently.

**Requirement:** `Flux::init` sets `_fb_scale` explicitly, alongside
`_drive_norm = -1.f`, to the value `set_drive(0.f)` will store — i.e.
`1.2f / bbd_drive_gain(0.f)`. Written as that expression, not as the literal
`1.2f`, so it cannot drift if `kDriveLoDb` ever moves. The accompanying
comment must say that the reset is defence against a future reordering, not a
fix for a reachable bug, or the next reader will delete it as dead code.

## 5. Change 2 — guard `set_feedback`

```cpp
void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _fb_norm) return;
    _fb_norm = n;
    apply_feedback();
}
```

The guard compares the **clamped** value, matching what `_fb_norm` stores
today, so two pushes that clamp to the same value are correctly one change.

### 5.1 Why an ordinary value is the right sentinel here

`_fb_norm` initialises to `0.45f`, a value a user can legitimately dial. The
sentinel idiom used by `_drive_norm` and `_stages_norm` — an unreachable
`-1.f`, so the first push after init always forwards — deliberately does
**not** apply, and the reason must be written down or a later reader will
"fix" it:

`init()` itself calls `set_feedback(0.45f)`. By the time any host pushes, the
state already matches. Swallowing a repeated push of 0.45 is therefore a no-op
on already-correct state, not a swallowed change. This is exactly the
reasoning `flux.cpp:30-46` gives for `_link = 0.f`, and the comment should say
so by pointing at it.

### 5.2 The guard fires at rest, and this is not an assumption

`v[FXT_FLUX_FB]` reaches `set_feedback` through `PartFx`'s `OnePole`
smoother. That smoother **snaps**: `onepole.h:29-31` assigns
`_value = target` and clears `_smoothing` once within 0.0005 of the target.
So at rest it returns the identical float sample after sample, and the guard
fires reliably.

This is worth stating because the same idea has nearly failed twice elsewhere
in this file for the opposite reason: `_gate` and `_stage_current` both ride
raw `fonepole` recurrences that stall short of their targets in float32, which
is why both carry explicit snaps of their own (`flux.cpp:306`,
`flux.cpp:347`). Had `OnePole` behaved that way, this guard would never fire
and the change would be worthless.

### 5.3 Order-independence survives

`set_drive` still calls `apply_feedback()` on every DRIVE change, so a DRIVE
move re-derives the coefficient from the stored knob position regardless of
whether FEEDBACK moved. The property promised at `flux.cpp:149-153` — that a
host may push the two in either order and get the same result — is preserved,
and §8 tests it directly.

## 6. Change 3 — guard `set_time_mod`

```cpp
void Flux::set_time_mod(float norm) {
    if (norm == _time_mod_norm) return;
    _time_mod_norm = norm;
    _time_mult = bbd_time_mult(norm);
}
```

A new member `_time_mod_norm` with sentinel `-1.f`, reset by `init()` for the
same re-init reason as §4.1. `bbd_time_mult` clamps its argument to 0..1, so
`-1.f` is unreachable and the first push after init always lands.

`set_time_mod` has no `_buf_ok` guard today. It does not gain one — this
round changes cost, not behaviour.

Note that `_time_mult` keeps its `1.f` default until the first real push, both
before and after this change: `init()` does not call `set_time_mod`. Behaviour
is unchanged.

## 7. A side effect that confirms the previous round

`part_fx.cpp:33` gates the FX block on `_grit.engaged() || _flux.engaged()`.
A deck running **GRIT alone** therefore pushes `set_feedback` into an idle
`Flux` once per sample and pays the full `pow` for nothing — `Flux::process`
early-returns at `flux.cpp:280`, two lines in.

That is the ~1.60 points of gated work the previous round measured at
`fx_grit` and attributed to the `_buf_ok` guard eliding this exact `pow`
(cost-curves spec §9.6). The guards in §4 and §5 remove it as a by-product.
The previous round's reading and this one agree, from opposite directions.

## 8. Verification

No bit-exactness or checksum-against-a-stored-file gate: this project does not
use them, and §9 explains why one would fail here anyway.

**This round is a behaviour-preserving refactor, so no test here fails first.**
Every test below passes on the code as it stands and must still pass after —
that is the entire point of writing them. Red-first is the wrong shape for
this work, and a review that expects it is applying the wrong rule. The tests
are a net under a change whose whole claim is that nothing observable moves,
and they should be written and committed *before* the change so the net exists
when it lands.

**Unit tests** (in `tests/test_flux.cpp`, already registered at
`CMakeLists.txt:87`):

- the feedback coefficient handed to both `BbdEcho`s equals
  `norm * 1.2f / bbd_drive_gain(drive)` to within **1e-6 relative**, for a
  grid of (drive, feedback) pairs pushed in **both** orders — the property of
  §5.3, which the cache is the thing that could break. The tolerance is a
  reassociation bound, not a slack allowance: the two spellings differ by
  roughly 1e-7 relative (§9), so 1e-6 passes the intended change and still
  fails any real drift in the law;
- pushing the same FEEDBACK value twice yields the same coefficient as
  pushing it once — the guard is a no-op, not a swallow;
- immediately after `init()`, and again after a **re-init** of an instance
  whose DRIVE was previously 1.0, the coefficient equals
  `0.45f * 1.2f / bbd_drive_gain(0.f)`. Per §4.1 this **cannot fail on either
  version** — it is a characterisation of the post-init state, and its job is
  to fail later, if someone reorders `init()` or gives `set_drive` an early
  return. Writing it as a bug-catcher would be a false claim;
- `set_time_mod`: the first push after init lands, a repeated push is a no-op,
  and `_time_mult` is `1.f` before any push.

**Desktop suite:** must stay at its **current** state — 832 passing, one
pre-existing failure (`tests/test_seed_audition_init.cpp`, red on `main` before
this round and not this round's to fix). The acceptance is "no new failure",
not "all green". Run with `source env.sh && cmake -S . -B build && cmake
--build build && ctest --test-dir build --output-on-failure`.

**Render A/B:** `host/render/scenarios/bbd_bloom.json`, rendered before and
after the change. It is already the FLUX listening scenario and it exercises
both changed paths in the worst direction the design allows: 30 seconds, FLUX
mix 0.8, FEEDBACK driven to 0.9 at t=21 s, DRIVE swept 0.15 → 0.85 → 0.15,
STAGES swept across its range. High feedback over a long tail is exactly where
§9's compounding gets its best chance to show, and the DRIVE sweep is what
re-derives the cached factor repeatedly.

Acceptance: RMS and peak within **0.1 dB**, spectral centroid within **1 %**.
These are "no audible difference" bounds, not identity bounds; a far tighter
result is expected, and a result outside them means the change is not what §9
claims and the round stops.

### 8.1 The two stored-hash gates are not affected

`tests/check_render_hash.cmake` compares a rendered WAV's SHA256 against a
stored expectation, for two ctest targets. §9's ULP change would move such a
hash if FLUX contributed to either render. **It does not:**
`ctrl_identity.json` never turns FLUX on, and `wave_formant_sweep.json`
explicitly turns FLUX *and* GRIT off (`"flag": false`). With `Flux` idle,
`Flux::process` returns at `flux.cpp:280` before the coefficient is ever read,
so a changed coefficient reaches no sample.

Neither hash may be re-baked in this round. If either moves, that is a real
regression and not an expected consequence — re-baking a reference hash to
make a test pass is precisely how a genuine regression would be hidden here.

**Hardware re-measurement:** `python run.py --profile sweep` — never without
`--profile`, which defaults to `full` and fails to link by design. The FLUX
rows carry the answer. Per-row checksums **will** move (see §9); that is
expected and is not a failure. The bench's own cross-run identity check — the
same firmware measured twice — must still hold exactly.

## 9. What "sound-neutral" means precisely

The guards of §5 and §6 are **exactly** neutral: the same value in produces
the same coefficient and the same state, so the audio is bit-identical.

The cache of §4 is not. `_fb_norm * (1.2f / g)` and `_fb_norm * 1.2f / g`
differ in the last ULP of the feedback coefficient, and that coefficient sits
inside a recursive loop — so the difference compounds over a long tail rather
than staying at one ULP in the output.

The honest claim is therefore **"no audible change", not "identical
samples"**: a relative error of order 1e-7 in loop gain, which no tail length
turns into something a listener can hear, but which will move a bench
checksum on the first repeat. Stating it the other way round would be a claim
this round cannot support.

## 10. Bench: two checksum gaps closed

The previous round deferred these deliberately — closing them moves per-row
checksums, which would have invalidated evidence that was then fresh. This
round moves those checksums anyway, so this is the only window in which the
fix is free.

- **`setup_flux_rate`** folds a stage count that merely echoes its own
  initialiser, and never folds the clock it exists to sweep. Fix: read the
  achieved clock once after the settle via `Flux::clock_hz()`, assert it is
  greater than zero, and fold it. This is the same fix Task 5 of the previous
  round applied to `sweep_flux_lines_2ch` after that row was caught measuring
  a stopped clock — the failure a folded-and-asserted clock is what catches.
- **`proc_sweep_room`** folds `reverb_asleep()` but not `active_voices()`.
  Fix: fold both.

Both land **before** the hardware run, so one run covers the engine change and
the bench fix together.

## 11. What deliberately does not change

- **`part_fx.cpp`** — untouched. The guards belong in the class that owns the
  value, which is how `set_drive`, `set_stages` and `set_link` already work.
- **`bbd.h`** — untouched, by the owner's scope decision. `BbdLine::SetClock`
  computes `2.f * hz / sr_` once per sample per channel against a `sr_` fixed
  at init: the same defect class, worth roughly 0.6 points, and left standing
  because it is model territory and the mono round restructures `BbdEcho`'s
  use anyway. Recorded here so it is not lost.
- **The GRIT guard's location.** `PartFx` guards `set_intensity` at the call
  site (`part_fx.cpp:34-37`) because `Grit::set_intensity` has none of its
  own — inconsistent with the idiom this round follows for FLUX. Observed,
  not fixed; changing it is not this round's scope.
- **No control-tick restructuring**, per §3.

## 12. Global constraints

- Work happens in the fork at `C:\Users\bernd\Documents\AI\Spotykach`, on a
  branch, never on `main` directly.
- Commit trailer is exactly
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
  with nothing after it.
- Files this round may modify: `engine/fx/flux.h`, `engine/fx/flux.cpp`,
  `tests/test_flux.cpp`, `bench/workloads_sweep.cpp`, and the documents this
  round produces. `engine/fx/bbd.h`, `engine/fx/part_fx.cpp` and every other engine
  file stay untouched (§11).
- No render scenario under `host/render/scenarios/` is modified, and neither
  stored render hash in `CMakeLists.txt` is re-baked (§8.1).
- `source env.sh` before any cmake or ctest invocation.
- Never run `python run.py` without `--profile` (`bench/README.md:34`).
- The bench refuses hardware evidence from a dirty git tree.
- The QSPI receipt binds the bank digest to the built ELF hashes; any engine
  change invalidates it. Rebind with
  `python run.py --profile sweep --no-build --program-qspi --build-only`
  before the measuring run.
- Build `host/vcv` only via `./build-local.sh` if it is touched at all (it is
  not, in this round).
- No bit-exactness or checksum-against-stored-file gates (§9).

## 13. Results

Evidence: `docs/bench/2026-07-29-cd6dafd-sweep.csv`/`.md` (before, engine at
`cd6dafd`) against `docs/bench/2026-07-29-4d1e929-sweep.csv`/`.md` (after, this
round's three engine commits applied). Both are two-run hardware sweeps on
the same Daisy Seed, same QSPI digest within each file, same device
fingerprint across both files. Figures below are `pct_max` from **run 2** of
each file, which is what the gate ledger anchors on; `avg_cyc` was checked
for agreement in direction on every row (noted where it does not).

### 13.1 The FLUX and GRIT rows, before/after

| row | before | after | Δ (pts) | avg_cyc direction | checksum |
|---|---:|---:|---:|---|---|
| `instrument_worst_bbd` (the gate) | 132.79 | 125.24 | **−7.55** | agrees (1229934→1157377) | unchanged `607bb1af` |
| `instrument_worst` | 120.14 | 115.83 | −4.31 | agrees (1121054→1079301) | unchanged `e8cb281f` |
| `fx_flux_sdram` (one deck, isolated) | 19.42 | 17.93 | −1.49 | agrees (185098→170929) | unchanged `962535c1` |
| `fx_grit` | 6.53 | 5.16 | −1.37 | agrees (62473→49407) | unchanged `74f9b9f5` |
| `sweep_flux_lines_2ch` (bare `BbdEcho`, no `Flux` wrapper) | 9.16 | 9.17 | ≈0 (+0.01) | flat, noise-level (87433→87531) | unchanged `45b6f7aa` |
| `sweep_grit_bare` (no Flux at all) | 1.53 | 1.53 | 0.00 | **disagrees at noise level**: avg ticked up 14689→14721 (32 cycles, 0.2%) while pct_max held at 1.53; below this bench's documented ~1700-cycle intra-run jitter, not a real signal | unchanged `f57bd5c9` |
| `sweep_grit_no_bbd_mem` | 4.93 | 4.61 | −0.32 | agrees (47250→44128) | unchanged `9ddc20e9` |
| `sweep_flux_rate_0` | 18.94 | 17.37 | −1.57 | agrees (180701→165657) | **moved** `0671aeeb`→`36125151` |
| `sweep_flux_rate_3` | 19.48 | 18.00 | −1.48 | agrees (185941→171461) | **moved** `f04d97b9`→`0f18c879` |
| `sweep_flux_rate_6` | 20.68 | 19.17 | −1.51 | agrees (196946→182326) | **moved** `5d31d4f1`→`f41e0f12` |
| `sweep_flux_rate_8` | 21.78 | 20.38 | −1.40 | agrees (207455→194130) | **moved** `f7f487be`→`d088a597` |
| `sweep_flux_rate_11` | 22.66 | 21.32 | −1.34 | agrees (216262→203354) | **moved** `217030e1`→`e1c20a65` |
| `sweep_stages_512` | 18.45 | 16.85 | −1.60 | agrees (176100→160774) | **moved** `f7188542`→`b2a7aaf3` |
| `sweep_stages_2048` | 18.63 | 17.05 | −1.58 | agrees (177929→162867) | **moved** `a4c5a8be`→`86fe0a22` |
| `sweep_stages_8192` | 19.48 | 17.98 | −1.50 | agrees (185925→171461) | **moved** `6b75d87a`→`e1fa1cc4` |
| `sweep_stages_16384` | 20.68 | 19.13 | −1.55 | agrees (196922→182394) | **moved** `18faea57`→`39ac3aed` |

Every figure above was read from `pct_max`, run 2, in both CSVs directly (not
carried forward from the brief that proposed them), and every `avg_cyc`
column was cross-checked for direction. The `sweep_flux_rate_*` and
`sweep_stages_*` rows move by roughly the same 1.3–1.6 points regardless of
where on the rate/stage ladder they sit — consistent with §3's design: the
removed work is control-rate overhead paid once per sample regardless of
FLUX's operating point, not something that scales with the clock or stage
count. The `sweep_room_*` rows also moved (checksums `d4b02ae8→a87305ad`,
`a308f4b3→a04d6d7c`, `3b05b839→6439c691`) for the same reason as
`instrument_worst`/`instrument_worst_bbd`: `sweep_room_*` runs the full
instrument, decks included.

### 13.2 The prediction missed — in both directions

§2 predicted **~2.5 points per deck, ~5 points total**. Neither figure this
round measured lands there.

**Isolated, it over-predicted.** `fx_flux_sdram` — one deck, nothing else
competing for cache or issue slots — saved **1.49 points**, not 2.5. At this
bench's own convention (1 point = 100 cycles/sample over a 96-sample block),
that is ~149 of the wrapper's 771 cycles/sample, against a ~250-cycle
prediction. The `std::pow` and the guards did less work, in isolation, than
§2 estimated.

**On the full instrument, it under-predicted.** The gate row,
`instrument_worst_bbd`, saved **7.55 points** — 51% more than the ~5-point
total prediction, and its implied per-deck saving (3.78 points, see §13.3) is
51% *above* the 2.5-point prediction, not below it.

This is a plain finding, not something to round toward the original number:
the wrapper's removable cost is not a fixed quantity independent of what else
the CPU is doing. §2's per-line cycle accounting (the `std::pow`, the
division, the two cross-TU calls) was reasoned from the source, not measured
under load, and the isolated result confirms that reasoning was in the right
neighbourhood but not exact even before load effects are considered. The part
of the wrapper this round deliberately left standing — the crossfade, the two
`fonepole` slews, the DRAG/THIN accumulator, `bbd_clock_hz`'s division, the
stage-change compare, the gate branch, the mix additions (§2, "genuinely
per-sample, stays") — is where the remaining ~622 of 771 cycles/sample sit in
the isolated case, and §13.3 is the reason that number is not simply "what's
left" on the gate row.

### 13.3 The saving grows with machine load — a hypothesis, not a measurement

This is the round's most interesting result. Two decks at the isolated
`fx_flux_sdram` rate of 1.49 points each would sum to 2.98. The gate row,
which runs both decks inside the full 8-voice, both-FX, high-diffusion,
max-echo instrument, saved 7.55 — more than double.

The per-deck saving implied by each row (Δ ÷ 2, since the instrument runs
two decks):

| row | Δ (total) | implied Δ/deck |
|---|---:|---:|
| `fx_flux_sdram` (1 deck measured directly, not halved) | −1.49 | 1.49 |
| `instrument_worst` | −4.31 | 2.16 |
| `instrument_worst_bbd` | −7.55 | 3.78 |

The measurement, stated plainly first: the same code change is worth more on
a row where the machine is already busy than on a row where it is nearly
idle. 1.49 in isolation, 2.16 under a full-but-unloaded instrument, 3.78
under the worst-case instrument with BBD lines running flat out.

**Now the hypothesis, explicitly labelled as such and not a measurement:** a
libm `powf`/`std::pow` call executed once per sample per deck is more
expensive on a machine whose instruction and data caches are already under
pressure from voices, both FX chains, and reverb, than it is in an otherwise
near-idle row. Removing it returns not just its own cycles but the icache
footprint and pipeline disruption it was imposing on everything else
competing for the same core — a cost that does not show up at all when it is
the only thing running.

**If the hypothesis holds**, it means isolated single-row benchmarks
systematically *understate* what a removal is worth once it lands on the
gate row — the opposite of the usual worry that isolated numbers overstate
real-world impact. That has a direct consequence for the next round: the
mono-FLUX estimate of ~9.2 points (§13.5) is itself an isolated figure, and if
this pattern repeats, the real saving on `instrument_worst_bbd` could be
larger than 9.2.

**This is a question the next round answers, not a number anyone may bank.**
This round has one data point on the load/saving relationship (three rows,
not a swept parameter), which is a pattern, not a proof. Nothing above should
be read as license to plan against a >9.2 mono saving until it is measured on
the gate row directly.

### 13.4 The checksums are the strongest evidence

Every row's checksum that moved between `cd6dafd` and `4d1e929` belongs to
one of exactly three families, all of which run through the changed engine
code with a fold added to the harness this round (§10): the five
`sweep_flux_rate_*` rows, the four `sweep_stages_*` rows, and the three
`sweep_room_*` rows. Every other checksum in both files — every `system` row,
`sweep_grit_bare`, `sweep_grit_no_bbd_mem`, and `sweep_flux_lines_2ch` — is
byte-identical across the two files. That split is exactly what a
behaviour-preserving refactor with two ULP-neutral guards and one
ULP-non-neutral cache (§9) predicts, and it is stronger evidence than any
percentage figure above because a checksum match or mismatch is not subject
to measurement jitter.

Two of those identical checksums are load-bearing confirmations:

- **`fx_grit`: 1.37 points cheaper at an identical checksum, `74f9b9f5`.**
  This is §7's prediction confirmed exactly. A deck running GRIT alone was
  computing the feedback coefficient every sample and discarding it, because
  `Flux::process` returns at its idle guard before ever reading it. The
  guards in §4/§5 remove that wasted computation; the checksum staying fixed
  proves the audio GRIT actually produces did not change at all — the saving
  is pure waste removed, not a behaviour change that happens to sound the
  same.
- **`sweep_flux_lines_2ch`: unchanged in both cost (9.16→9.17, noise-level)
  and checksum, `45b6f7aa`.** This row is a bare `BbdEcho` pair with no
  `Flux` wrapper around it at all. Its checksum and cost holding fixed
  confirms the change is confined to the wrapper exactly as §3/§11 designed
  — nothing in the BBD model itself moved.

Note also that `fx_flux_sdram` kept checksum `962535c1` despite the
coefficient reassociation of §9 (`_fb_norm * (1.2f / g)` vs
`_fb_norm * 1.2f / g`, a ~1e-7 relative difference in loop gain). That is
consistent with, not contradictory to, §9's own claim: the render A/B
(§13.5, Task 5) found only 8 of 2,880,000 samples differing by one 16-bit
LSB, and this row's checksum is a fold over 96 samples of a float
accumulator — it need not resolve a perturbation that small, and evidently
does not for this particular workload.

### 13.5 Render A/B (Task 5, verbatim)

```
rms      0.011795 -> 0.011795   (0.0000 dB)
peak     0.318848 -> 0.318848   (0.0000 dB)
centroid 1094.30 Hz -> 1094.30 Hz   (0.0000 %)
PASS
```

Honest caveat: "0.0000 dB" means the difference is below 0.00005 dB at the
metric's printed precision, not that it is zero. These metrics are computed
on 16-bit renders and cannot resolve below the LSB, so this result cannot by
itself distinguish "no change" from "a change smaller than the quantisation
floor." The load-bearing figure is the byte comparison done alongside it:
of 2,880,000 sixteen-bit samples (1,440,000 frames × 2 channels) in the
30-second `bbd_bloom` scenario, **8 samples differ, each by exactly ±1 LSB**,
and none differs by more. That is the actual size of the reassociation's
audible footprint, and it is consistent with a ~1e-7 relative change in loop
gain landing almost entirely inside the rounding step to 16 bits.

### 13.6 The gate row, and the arithmetic that remains

`instrument_worst_bbd` needs to reach 100% of the block budget. Before this
round it stood at 132.79% (32.79 points over). After this round it stands at
**125.24%** — **25.24 points remain**, down from 32.79. The round is
authorised, worked, measured, and moves the gate row in the right direction.

The next step in the owner's fixed order (roadmap, "FX cost curves") is
collapsing FLUX to one mono `BbdEcho` per deck, estimated at **~9.2 points**
in isolation (half of the two-line model's measured cost, not yet
re-measured). Stated plainly: **even with mono FLUX applied, 125.24 − 9.2 =
116.04% — the row does not reach 100% on isolated estimates.** Some further
saving, from the `kFiltOrder` lever or elsewhere, would still be needed on
paper.

That statement carries less weight than the same statement would have
carried before this round, and §13.3 is the reason: this round's own isolated
per-deck figure (1.49) undershot what the same change was actually worth on
this same gate row (3.78) by more than double. If mono FLUX follows the same
pattern — cheaper in isolation than under the load the gate row imposes — the
"does not reach 100%" conclusion above may not survive being measured
directly. It is not safe to plan around the isolated 9.2 as a floor or a
ceiling; it is a number the next round has to re-measure on
`instrument_worst_bbd` itself, not extrapolate from.

`engine/fx/bbd.h`'s `1/sr_` division in `BbdLine::SetClock` (§11), worth an
estimated ~0.6 points, remains deliberately untouched — model territory, left
for the mono round that restructures `BbdEcho`'s use.
