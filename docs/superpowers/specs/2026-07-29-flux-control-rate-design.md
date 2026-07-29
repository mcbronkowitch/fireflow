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

### 4.1 The init-order hazard

This is the one real hazard in the design and must be handled explicitly.

`Flux::init` calls `set_feedback(0.45f)` at `flux.cpp:65` **before**
`set_drive(0.f)` at `flux.cpp:67`. Today this is safe by accident:
`apply_feedback` reads `_drive_norm`, which is `-1.f` at that moment, and
`bbd_drive_gain` clamps it to 0, giving a gain of 1 — the same value
`set_drive(0.f)` will produce two lines later.

With the cache, `apply_feedback` reads `_fb_scale` instead. A member default
alone is **not** sufficient: on a *re-init* of an already-running `Flux`
(`Spotymod::reinit()`, reached from a sample-rate change, a reset, or a fresh
sampler add) the member retains its old value, and `set_feedback(0.45f)` at
line 65 would push a coefficient derived from the previous DRIVE setting
before line 67 corrects it.

**Requirement:** `Flux::init` sets `_fb_scale` explicitly, alongside
`_drive_norm = -1.f`, to the value `set_drive(0.f)` will store — i.e.
`1.2f / bbd_drive_gain(0.f)`. It must be written as that expression, not as
the literal `1.2f`, so it cannot drift if `kDriveLoDb` ever moves.

This is the same class of re-init defect the long comment at `flux.cpp:30-46`
documents for `_link`, and it deserves a comment of the same kind.

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
  whose DRIVE was previously non-zero, the coefficient equals the value the
  pre-change code produced — this is the §4.1 hazard, and it is the test that
  must exist even if every other one is cut;
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
