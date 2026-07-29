# FLUX Control-Rate Work Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take the control-rate work out of FLUX's per-sample path — a cached
DRIVE factor and two unchanged-value guards — then re-measure on hardware.

**Architecture:** `PartFx::process` pushes `Flux::set_feedback` and
`Flux::set_time_mod` once per sample, unguarded. `set_feedback` evaluates a
`std::pow` whose only input is DRIVE, which moves at control rate. Cache that
quotient where DRIVE is written, and guard both setters the way `set_drive`,
`set_stages` and `set_link` in the same class already are. No structural
change: the 2 ms smoothers stay per-sample, `part_fx.cpp` is not touched.

**Tech Stack:** C++17, doctest, CMake + Ninja + clang (desktop), ARM
cross-toolchain + Python (bench), Daisy Seed hardware.

**Design spec:** `docs/superpowers/specs/2026-07-29-flux-control-rate-design.md`.
Section references below (§4, §8, §10 …) point into it.

## READ THIS BEFORE TASK 1: no test in this plan fails first

This is a behaviour-preserving refactor. Every test written here **passes on
the code as it stands** and must still pass after the change — that is the
entire point of writing them. Red-first is the wrong shape for this work
(spec §8). Do not "fix" a test so that it fails first, and do not report a
passing new test as a problem.

## Global Constraints

- Branch `perf/flux-control-rate` (already created). Never commit on `main`.
- Commit trailer is exactly, and with nothing after it:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- Files this plan may modify: `engine/fx/flux.h`, `engine/fx/flux.cpp`,
  `tests/test_flux.cpp`, `bench/workloads_sweep.cpp`, `docs/roadmap.md`, the
  design spec, and new files under `docs/bench/`. **Every other file under
  `engine/` stays untouched** — in particular `engine/fx/bbd.h` and
  `engine/fx/part_fx.cpp` (spec §11). `git diff main -- engine/fx/bbd.h
  engine/fx/part_fx.cpp` must stay empty for the life of this branch.
- No render scenario under `host/render/scenarios/` is modified, and neither
  stored render hash in `CMakeLists.txt` is re-baked (spec §8.1).
- `source env.sh` before any cmake or ctest invocation.
- Desktop suite acceptance is **"no new failure", not "all green"**: 832
  passing plus one pre-existing failure, `tests/test_seed_audition_init.cpp`
  ("Seed audition shares the complete generated VCV parameter snapshot"),
  which is red on `main` and is not this plan's to fix.
- Never run `python run.py` without `--profile` — the default is `full`,
  which fails to link by design (`bench/README.md:34`).
- The bench refuses hardware evidence from a dirty git tree: commit before
  measuring.
- No bit-exactness or checksum-against-a-stored-file gates (spec §9).
- Scratch files go in `.superpowers/sdd/2026-07-29-flux-control-rate/`
  (git-ignored via `.gitignore:14`). Never `git add` anything from there.

---

## File Structure

| File | Responsibility in this plan |
|---|---|
| `engine/fx/flux.h` | Two new members (`_fb_scale`, `_time_mod_norm`) and two test observers for the feedback coefficient. |
| `engine/fx/flux.cpp` | `init` resets the new state; `set_drive` writes the cached factor; `apply_feedback` multiplies instead of dividing; guards on `set_feedback` and `set_time_mod`. |
| `tests/test_flux.cpp` | The numerical net under the feedback law, the guards, and the post-init state. Appended to; nothing existing is edited. |
| `bench/workloads_sweep.cpp` | Two checksum gaps: `SweepFxGroup` gains `clock_achieved`, `proc_sweep_room` folds `active_voices`. |
| `docs/bench/2026-07-29-<sha>-sweep.{md,csv}` | The new hardware evidence. |
| `docs/roadmap.md`, the design spec | The written verdict. |

Desktop build/test command, used in every task below:

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The render CLI on this toolchain is `build/render.exe`.

---

### Task 1: Test observers, the feedback-law net, and the A/B baseline

Nothing in this task changes behaviour. It builds the net that the next three
tasks land inside, and captures the "before" render while the engine is still
unmodified — after Task 2 that is no longer possible.

**Files:**
- Modify: `engine/fx/flux.h` (public observer section, after
  `float drive_norm_for_test() const` at line 71)
- Modify: `tests/test_flux.cpp` (append at end of file)
- Create (git-ignored, never committed):
  `.superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_before.wav`

**Interfaces:**
- Produces: `float Flux::feedback_coef_l_for_test() const` and
  `float Flux::feedback_coef_r_for_test() const` — the coefficient handed to
  each `BbdEcho`. Tasks 2–4 are verified through these.
- Produces: the baseline WAV path above, consumed by Task 5.

- [ ] **Step 1: Add the two observers to `engine/fx/flux.h`**

Insert directly after the existing `drive_norm_for_test()` line:

```cpp
    // The feedback coefficient actually handed to each BbdEcho. The law is a
    // function of BOTH knobs (see the comment on apply_feedback), and the
    // control-rate round assembles it from a cached factor rather than
    // evaluating it whole -- which makes it the one quantity in this class
    // whose correctness is numerical rather than audible. A 1e-6 drift in it
    // is inaudible and still a bug, and no behavioural test in this file can
    // see one.
    float feedback_coef_l_for_test() const { return _echo_l.Feedback(); }
    float feedback_coef_r_for_test() const { return _echo_r.Feedback(); }
```

- [ ] **Step 2: Append the three test cases to `tests/test_flux.cpp`**

```cpp
TEST_CASE("flux: the FEEDBACK coefficient is norm * 1.2 / the DRIVE gain, in either push order") {
    // apply_feedback() divides bbd_drive_gain() back out so that a FEEDBACK
    // knob position means one thing at every DRIVE -- see the long comment on
    // its definition. The control-rate round caches that quotient in
    // set_drive instead of evaluating it per call, which is a REASSOCIATION
    // of the same arithmetic. The tolerance is chosen for exactly that: tight
    // enough that a changed LAW fails, loose enough that the reassociation
    // (~1e-7 relative) passes.
    //
    // Both push orders, because a host may send them either way round and
    // set_drive is the thing that has to re-derive the coefficient when DRIVE
    // moves.
    const float drives[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
    const float fbs[]    = { 0.f, 0.2f, 0.45f, 0.8f, 1.f };
    for (float d : drives) {
        for (float fb : fbs) {
            const float want = fb * 1.2f / bbd_drive_gain(d);
            Flux a;
            a.init(48000.f, s_buf_l, s_buf_r);
            a.set_feedback(fb);
            a.set_drive(d);
            Flux b;
            b.init(48000.f, s_buf_l, s_buf_r);
            b.set_drive(d);
            b.set_feedback(fb);
            INFO("drive=" << d << " feedback=" << fb);
            CHECK(a.feedback_coef_l_for_test() == doctest::Approx(want).epsilon(1e-6));
            CHECK(a.feedback_coef_r_for_test() == doctest::Approx(want).epsilon(1e-6));
            CHECK(b.feedback_coef_l_for_test() == doctest::Approx(want).epsilon(1e-6));
            CHECK(b.feedback_coef_r_for_test() == doctest::Approx(want).epsilon(1e-6));
        }
    }
}

TEST_CASE("flux: a repeated FEEDBACK push changes nothing, and DRIVE still re-derives") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_drive(0.6f);
    f.set_feedback(0.3f);
    const float once = f.feedback_coef_l_for_test();
    f.set_feedback(0.3f);
    f.set_feedback(0.3f);
    CHECK(f.feedback_coef_l_for_test() == doctest::Approx(once).epsilon(1e-6));
    // A DRIVE move re-derives the coefficient from the STORED knob position,
    // not from the coefficient currently in force, so an unchanged-value
    // guard on FEEDBACK must not make it sticky across a DRIVE change.
    f.set_drive(0.f);
    CHECK(f.feedback_coef_l_for_test()
          == doctest::Approx(0.3f * 1.2f / bbd_drive_gain(0.f)).epsilon(1e-6));

    // 0.45 -- the value init() itself pushes -- is a REACHABLE knob position,
    // unlike the -1 sentinels _drive_norm and _stages_norm use. Pushing it
    // straight after init is swallowed by the guard, and that is correct:
    // init() already put the state there, so there is no change to swallow.
    Flux g;
    g.init(48000.f, s_buf_l, s_buf_r);
    g.set_feedback(0.45f);
    CHECK(g.feedback_coef_l_for_test()
          == doctest::Approx(0.45f * 1.2f / bbd_drive_gain(0.f)).epsilon(1e-6));
}

TEST_CASE("flux: init leaves the FEEDBACK coefficient at its boot value, even re-initialised over a hot DRIVE") {
    // A CHARACTERISATION, not a bug-catcher: it passes before and after the
    // control-rate round by construction. init() sets _drive_norm = -1 before
    // calling set_drive(0.f), so that call always passes its own guard and
    // always rewrites whatever DRIVE-derived state exists. This test exists to
    // fail LATER -- if someone reorders init(), or gives set_drive an early
    // return, a cached DRIVE factor would silently survive a re-init. See
    // section 4.1 of docs/superpowers/specs/2026-07-29-flux-control-rate-design.md.
    const float want = 0.45f * 1.2f / bbd_drive_gain(0.f);
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    CHECK(f.feedback_coef_l_for_test() == doctest::Approx(want).epsilon(1e-6));
    f.set_drive(1.f);                       // a hot DRIVE, then re-init over it
    f.init(48000.f, s_buf_l, s_buf_r);      // reproduces Spotymod::reinit()
    CHECK(f.feedback_coef_l_for_test() == doctest::Approx(want).epsilon(1e-6));
    CHECK(f.feedback_coef_r_for_test() == doctest::Approx(want).epsilon(1e-6));
}
```

`bbd_drive_gain` is already visible: `tests/test_flux.cpp` includes
`fx/flux.h`, which includes `fx/bbd.h`, and the file is `using namespace spky`.

- [ ] **Step 3: Build and run the suite — the new tests must PASS**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: the three new cases pass (see "no test fails first" above). Total
833 passing plus the one pre-existing failure `test_seed_audition_init`; if
any *other* test fails, stop and report — the observers cannot change
behaviour, so a new failure means something else is wrong.

- [ ] **Step 4: Capture the A/B baseline from the unmodified engine**

```bash
mkdir -p .superpowers/sdd/2026-07-29-flux-control-rate
./build/render.exe host/render/scenarios/bbd_bloom.json \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_before.wav \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_before.csv
```

Confirm the WAV exists and is non-empty (30 s of stereo audio). This is the
last moment it can be captured; Task 2 changes the engine.

- [ ] **Step 5: Commit**

```bash
git add engine/fx/flux.h tests/test_flux.cpp
git commit -m "$(cat <<'EOF'
test(flux): a numerical net under the feedback law

Two observers for the coefficient handed to each BbdEcho, and three
cases pinning the law, the push-order independence, and the post-init
state. All three pass on the current code: the control-rate round that
follows is behaviour-preserving, so the net has to exist before it, not
after.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

Do not `git add` anything under `.superpowers/`.

---

### Task 2: Cache DRIVE's contribution to the feedback coefficient

Removes the `std::pow` from the per-sample path unconditionally — not merely
at rest (spec §4).

**Files:**
- Modify: `engine/fx/flux.h` (private member section, after
  `float _fb_norm = 0.45f;`)
- Modify: `engine/fx/flux.cpp` — `init` (near `_drive_norm = -1.f;`),
  `apply_feedback`, `set_drive`

**Interfaces:**
- Consumes: `feedback_coef_l_for_test()` / `feedback_coef_r_for_test()` from
  Task 1.
- Produces: `float Flux::_fb_scale` — the cached `1.2f / bbd_drive_gain
  (_drive_norm)`. Task 3's guard sits in front of the code that reads it.

- [ ] **Step 1: Add the member to `engine/fx/flux.h`**

Insert after the `_fb_norm` declaration and its comment:

```cpp
    // FEEDBACK's law is _fb_norm * 1.2 / bbd_drive_gain(_drive_norm), and
    // that quotient depends on DRIVE alone. set_drive is the sole writer of
    // _drive_norm and already guards itself, so the quotient is a
    // control-rate constant -- cached here because apply_feedback used to
    // evaluate a std::pow for it once per SAMPLE: PartFx pushes set_feedback
    // unguarded from the per-sample path (part_fx.cpp), which made a ~198
    // cycle libm call part of the audio loop. init() writes this explicitly;
    // see the comment there for why the member default is not relied on.
    float _fb_scale = 1.2f;
```

- [ ] **Step 2: Reset it explicitly in `Flux::init`**

In `engine/fx/flux.cpp`, immediately after the existing
`_drive_norm = -1.f;` / `_stages_norm = -1.f;` pair (and before
`set_stages(kBootStagesNorm);`):

```cpp
    // apply_feedback() reads _fb_scale, and set_feedback(0.45f) below runs
    // BEFORE set_drive(0.f) -- so on a re-init of an instance whose DRIVE was
    // hot, the member still holds the old DRIVE's factor at that moment.
    //
    // That window is NOT reachable today: _drive_norm was just set to -1, so
    // set_drive(0.f) below always passes its own guard and always rewrites
    // this. The window is two lines wide with no audio processed inside it.
    // This reset is therefore defence against a future reordering of init(),
    // or against set_drive gaining an early return -- not a fix for a live
    // bug. Do not delete it as dead code; it is what stops the correctness of
    // a cached value from resting on the order of two calls.
    //
    // Written as the expression rather than the literal 1.2f so it cannot
    // drift if kDriveLoDb ever moves.
    _fb_scale = 1.2f / bbd_drive_gain(0.f);
```

- [ ] **Step 3: Make `apply_feedback` a multiplication**

Replace the body of `Flux::apply_feedback` (keep every existing comment above
the function — it carries the musical reasoning for the division, which is
still the law, only pre-computed):

```cpp
void Flux::apply_feedback() {
    // Up to ~120 %: self-oscillation stays reachable, documented behaviour of
    // the original. The bound now comes from saturation WITHIN the loop
    // (BbdEcho) rather than a fast_tanh on the read path.
    //
    // _fb_scale is 1.2 / bbd_drive_gain(_drive_norm), maintained by set_drive.
    // Same law as before, evaluated where DRIVE changes instead of here --
    // this function is reached once per sample per deck.
    const float fb = _fb_norm * _fb_scale;
    _echo_l.SetFeedback(fb);
    _echo_r.SetFeedback(fb);
}
```

- [ ] **Step 4: Maintain the cache in `set_drive`**

In `Flux::set_drive`, insert the assignment immediately before the existing
`apply_feedback();` call, keeping the comment that is already there:

```cpp
    _fb_scale = 1.2f / bbd_drive_gain(d);
    apply_feedback();
```

- [ ] **Step 5: Build and run the suite**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: Task 1's three cases still pass — that is the whole verification of
this task. Plus 832 others and the one pre-existing failure. If the law test
fails, the reassociation is not what it was believed to be; stop and report
the actual and expected values it prints.

- [ ] **Step 6: Confirm the forbidden files are untouched**

```bash
git diff main -- engine/fx/bbd.h engine/fx/part_fx.cpp
```

Expected: empty output.

- [ ] **Step 7: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp
git commit -m "$(cat <<'EOF'
perf(flux): cache DRIVE's factor instead of a per-sample std::pow

apply_feedback ran bbd_drive_gain -- a std::pow(10, x), ~198 cycles --
once per sample per deck, for a quotient whose only input is DRIVE.
set_drive is its sole writer and already guards itself, so the quotient
is cached there and apply_feedback becomes a multiply. Same law,
evaluated where it changes.

Not bit-exact: this reassociates the arithmetic and the coefficient
sits in a recursive loop, so tails differ below the 1e-6 the tests
allow. Inaudible, and this project keeps no bit-exactness gates.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: Guard `set_feedback`

**Files:**
- Modify: `engine/fx/flux.cpp` (`Flux::set_feedback`)

**Interfaces:**
- Consumes: `_fb_scale` from Task 2; Task 1's observers.
- Produces: nothing new. `_fb_norm` keeps its type and meaning.

- [ ] **Step 1: Add the unchanged-value guard**

Replace `Flux::set_feedback` in `engine/fx/flux.cpp` with:

```cpp
// PartFx pushes this once per SAMPLE, unguarded (part_fx.cpp), so the guard
// below is what keeps apply_feedback out of the audio loop while the knob
// stands still. It fires reliably because the value arrives through PartFx's
// OnePole, which SNAPS: onepole.h assigns _value = target and clears
// _smoothing once within 0.0005, so at rest the identical float arrives
// sample after sample. That is not true of every slew in this file -- _gate
// and _stage_current ride raw fonepole recurrences that stall short of their
// targets in float32, which is why both carry explicit snaps of their own.
//
// The comparison is on the CLAMPED value, matching what _fb_norm stores, so
// two pushes that clamp to the same value are correctly one change.
//
// _fb_norm's initial 0.45 is a REACHABLE knob position, so unlike
// _drive_norm and _stages_norm this guard uses no unreachable sentinel. That
// is deliberate and it is the same reasoning set_link's -- see the long
// comment in init() on why _link resets to 0 rather than to -1: init() itself
// calls set_feedback(0.45f), so by the time any host pushes, the state
// already matches. Swallowing a repeated push of 0.45 is a no-op on
// already-correct state, not a swallowed change.
void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _fb_norm) return;
    _fb_norm = n;
    apply_feedback();
}
```

- [ ] **Step 2: Build and run the suite**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all of Task 1's cases still pass — in particular "a repeated
FEEDBACK push changes nothing", whose last block pushes 0.45 straight after
init and asserts the coefficient is still right, and "DRIVE does not move
where FEEDBACK blooms" (pre-existing, line ~254), which pushes FEEDBACK then
DRIVE and would catch a guard that made FEEDBACK sticky.

- [ ] **Step 3: Commit**

```bash
git add engine/fx/flux.cpp
git commit -m "$(cat <<'EOF'
perf(flux): guard set_feedback against an unchanged push

PartFx pushes it once per sample from the audio loop. The guard fires
reliably because PartFx's OnePole snaps exactly at rest rather than
creeping in ULPs. _fb_norm's 0.45 default is a reachable knob value, so
no unreachable sentinel: init() already pushed it, so swallowing a
repeat is a no-op on correct state, not a lost change.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: Guard `set_time_mod`

**Files:**
- Modify: `engine/fx/flux.h` (private member section, next to `_time_mult`)
- Modify: `engine/fx/flux.cpp` (`Flux::init`, `Flux::set_time_mod`)
- Modify: `tests/test_flux.cpp` (append one test case)

**Interfaces:**
- Consumes: nothing from Tasks 2–3.
- Produces: `float Flux::_time_mod_norm` — the guard's stored value.

- [ ] **Step 1: Add the member to `engine/fx/flux.h`**

Insert immediately after `float _time_mult = 1.f;`:

```cpp
    // set_time_mod's unchanged-value guard. PartFx pushes FXT_FLUX_TIME once
    // per sample too, and bbd_time_mult is a clamp, a cast and a table lerp
    // for a value that usually stands still. -1 is unreachable for a norm
    // bbd_time_mult clamps to 0..1, so the first push after init always
    // lands -- the sentinel idiom _drive_norm and _stages_norm use, which is
    // right here and wrong for _fb_norm (see set_feedback).
    float _time_mod_norm = -1.f;
```

- [ ] **Step 2: Reset it in `Flux::init`**

In `engine/fx/flux.cpp`, on the line after the `_fb_scale` assignment added in
Task 2:

```cpp
    _time_mod_norm = -1.f;
```

`init` already sets `_time_mult = 1.f` near the top, so the pair stays
consistent: `_time_mult` holds its neutral value until the first real push,
before and after this change.

- [ ] **Step 3: Add the guard**

Replace `Flux::set_time_mod` in `engine/fx/flux.cpp` with:

```cpp
// No _buf_ok guard, deliberately: this function had none, and this round
// changes cost, not behaviour.
void Flux::set_time_mod(float norm) {
    if (norm == _time_mod_norm) return;
    _time_mod_norm = norm;
    _time_mult = bbd_time_mult(norm);
}
```

- [ ] **Step 4: Append the test case to `tests/test_flux.cpp`**

```cpp
TEST_CASE("flux: the FXT_FLUX_TIME guard lands the first push and swallows repeats") {
    // The clock is the only observable of _time_mult, so this asserts through
    // clock_hz(). Rate 3 is the boot "1/4" (flux.cpp), i.e. 0.5 s at 120 BPM,
    // and the boot stage count is 8192 -- so the base clock is
    // 8192 / (2 * 0.5) = 8192 Hz.
    //
    // The DEPTH is 0.75, not 1.0. bbd_time_mult maps 0.5 -> x1 and 1.0 -> x4,
    // and x4 on this base lands at 32768 Hz, above kClockMaxHz (32000) -- the
    // ceiling would clamp it and the test would be asserting the clamp rather
    // than the guard. 0.75 is x2, landing at 16384 Hz: comfortably under the
    // ceiling and unmistakably different from neutral.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_mix(1.f);
    f.set_feedback(0.f);

    auto run = [&f](int n) {
        for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
        return f.clock_hz();
    };

    const float neutral = run(48000);
    REQUIRE(neutral > 0.f);

    // First push after init lands, despite _time_mult already holding x1:
    // the sentinel is -1, which no clamped norm can equal.
    f.set_time_mod(0.75f);
    const float doubled = run(4800);
    CHECK(doubled == doctest::Approx(2.f * neutral).epsilon(0.02f));

    // A repeat is swallowed, and swallowing it changes nothing.
    f.set_time_mod(0.75f);
    f.set_time_mod(0.75f);
    CHECK(run(4800) == doctest::Approx(doubled).epsilon(0.02f));

    // And back down again -- the guard must not make the control sticky.
    f.set_time_mod(0.5f);
    CHECK(run(4800) == doctest::Approx(neutral).epsilon(0.02f));
}
```

- [ ] **Step 5: Build and run the suite**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: the new case passes, and so do the pre-existing FXT_FLUX_TIME cases
at lines ~94 and ~116 ("FXT_FLUX_TIME moves the clock", "the lane reaches the
clock through the FAST path") and ~133 ("the ceiling holds when ladder and
lane push together"). Those three are the real regression net for this guard.

If the new case's clock figures do not come out as written, report the actual
numbers rather than adjusting the tolerance — the base clock is
`stages / (2 * delay)` and is fully determined, so a mismatch means an
assumption above is wrong.

- [ ] **Step 6: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp tests/test_flux.cpp
git commit -m "$(cat <<'EOF'
perf(flux): guard set_time_mod against an unchanged push

The second of PartFx's two unguarded per-sample pushes. Sentinel -1
because bbd_time_mult clamps to 0..1, so the first push after init
always lands -- the opposite choice from set_feedback's, and for the
opposite reason.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: The render A/B verdict

Decides whether the engine change is inaudible, before any hardware time is
spent on it (spec §8).

**Files:**
- Create (git-ignored, never committed):
  `.superpowers/sdd/2026-07-29-flux-control-rate/render_ab.py`,
  `bbd_bloom_after.wav`

**Interfaces:**
- Consumes: `bbd_bloom_before.wav` from Task 1 Step 4.

- [ ] **Step 1: Render the "after" file**

```bash
source env.sh && cmake --build build --target render
./build/render.exe host/render/scenarios/bbd_bloom.json \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_after.wav \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_after.csv
```

- [ ] **Step 2: Write the comparison script**

Create `.superpowers/sdd/2026-07-29-flux-control-rate/render_ab.py`:

```python
"""Compare two renders on level and brightness. Not an identity check.

Acceptance (design spec section 8): RMS and peak within 0.1 dB, spectral
centroid within 1 %. numpy 2.4 is available; scipy is not.
"""
import sys, wave, math
import numpy as np


def read(path):
    with wave.open(path, "rb") as w:
        n, ch, width = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
        sr = w.getframerate()
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    else:
        raise SystemExit("unsupported sample width: %d bytes" % width)
    return x.reshape(-1, ch), sr


def centroid(x, sr):
    # One FFT over the whole file, magnitude-weighted mean frequency. The
    # scenario is 30 s of one continuous evolving sound, so a global centroid
    # is the right summary; a frame-wise mean would only add a windowing
    # choice to argue about.
    mono = x.mean(axis=1)
    mag = np.abs(np.fft.rfft(mono))
    freqs = np.fft.rfftfreq(mono.size, 1.0 / sr)
    total = mag.sum()
    return float((freqs * mag).sum() / total) if total > 0 else 0.0


def db(v):
    return 20.0 * math.log10(v) if v > 0 else float("-inf")


a, sr_a = read(sys.argv[1])
b, sr_b = read(sys.argv[2])
if sr_a != sr_b or a.shape != b.shape:
    raise SystemExit("renders differ in rate or length: %s/%s %s/%s"
                     % (sr_a, sr_b, a.shape, b.shape))

rms_a, rms_b = float(np.sqrt((a ** 2).mean())), float(np.sqrt((b ** 2).mean()))
pk_a, pk_b = float(np.abs(a).max()), float(np.abs(b).max())
c_a, c_b = centroid(a, sr_a), centroid(b, sr_b)

d_rms = abs(db(rms_b) - db(rms_a))
d_pk = abs(db(pk_b) - db(pk_a))
d_c = abs(c_b - c_a) / c_a * 100.0 if c_a > 0 else 0.0

print("rms      %.6f -> %.6f   (%.4f dB)" % (rms_a, rms_b, d_rms))
print("peak     %.6f -> %.6f   (%.4f dB)" % (pk_a, pk_b, d_pk))
print("centroid %.2f Hz -> %.2f Hz   (%.4f %%)" % (c_a, c_b, d_c))

fails = []
if d_rms > 0.1:
    fails.append("rms %.4f dB > 0.1 dB" % d_rms)
if d_pk > 0.1:
    fails.append("peak %.4f dB > 0.1 dB" % d_pk)
if d_c > 1.0:
    fails.append("centroid %.4f %% > 1 %%" % d_c)
if fails:
    raise SystemExit("FAIL: " + "; ".join(fails))
print("PASS")
```

- [ ] **Step 3: Run it**

```bash
python .superpowers/sdd/2026-07-29-flux-control-rate/render_ab.py \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_before.wav \
  .superpowers/sdd/2026-07-29-flux-control-rate/bbd_bloom_after.wav
```

Expected: `PASS`, with all three deltas far inside their bounds. **Record the
three printed lines verbatim in the task report** — they are the evidence for
the spec's "no audible change" claim and Task 9 quotes them.

If it fails, stop. Do not widen the tolerances: the bounds come from the spec
and a failure means the change is not what §9 claims.

- [ ] **Step 4: No commit**

This task produces only git-ignored artefacts and a number for the report.
Confirm `git status --short` is clean before moving on.

---

### Task 6: Bench — fold the achieved clock into the `SweepFxGroup` rows

The first of the two checksum gaps (spec §10). `setup_flux_rate` folds a stage
count that merely echoes its own initialiser and never folds the clock the row
exists to sweep — so a row whose clock silently stopped would still produce a
stable, matching checksum. That is the exact failure Task 9 of the previous
round found in `sweep_flux_lines_2ch`.

**Files:**
- Modify: `bench/workloads_sweep.cpp` — `struct SweepFxGroup` (~line 58),
  `setup_flux_rate` (~line 250), `setup_stages` (~line 346),
  `setup_grit_no_bbd_mem` (~line 463), `proc_sweep_fx` (~line 372)

**Interfaces:**
- Produces: `float SweepFxGroup::clock_achieved`. **Every** setup that
  `emplace<SweepFxGroup>()`s must set it — there are exactly three, all listed
  above. Miss one and it reads a stale arena value.

- [ ] **Step 1: Add the field to `struct SweepFxGroup`**

After the existing `int stages_achieved = 0;`:

```cpp
    // The clock Flux actually settled to, read once after each setup's settle
    // loop (flux().clock_hz(), engine/fx/flux.h) and folded into
    // proc_sweep_fx's accumulator once per call. stages_achieved alone is not
    // enough: for the five sweep_flux_rate_* rows it reads back the boot
    // default 8192 and is therefore IDENTICAL across all five, so it cannot
    // tell a rate row that clocked from one that silently did not. The clock
    // is the quantity those rows sweep, and folding it is what makes a
    // stopped clock move the checksum -- the failure that took a discarded
    // hardware run to find in sweep_flux_lines_2ch (docs/bench/
    // 2026-07-29-cd6dafd-sweep.md).
    //
    // setup_grit_no_bbd_mem legitimately leaves this at 0: its Flux has null
    // buffers, so Flux::process returns before _clock_hz is ever written. The
    // "must be > 0" assertion therefore lives in the setups that run a clock,
    // not in proc_sweep_fx.
    float clock_achieved = 0.f;
```

- [ ] **Step 2: Set and assert it in `setup_flux_rate`**

Replace the two closing lines of `setup_flux_rate` (the comment and
`group.stages_achieved = group.fx.flux().stages();`) with:

```cpp
    // This row never calls set_stages -- stages_achieved reads back whatever
    // the boot default (8192, kBootStagesNorm in flux.cpp) settled to.
    group.stages_achieved = group.fx.flux().stages();
    // The clock this row exists to sweep. Asserted non-zero here rather than
    // trusted: a zero reading means the settle loop never reached the code
    // that writes _clock_hz (flux.cpp, below two early returns), which is
    // precisely how a row measures an idle machine while still producing a
    // stable checksum.
    group.clock_achieved = group.fx.flux().clock_hz();
    assert(group.clock_achieved > 0.f);
```

If `<cassert>` is not already included by this file, add it to the includes.

- [ ] **Step 3: Set and assert it in `setup_stages`**

Immediately after that function's existing
`group.stages_achieved = group.fx.flux().stages();`:

```cpp
    group.clock_achieved = group.fx.flux().clock_hz();
    assert(group.clock_achieved > 0.f);
```

- [ ] **Step 4: Set it — without an assertion — in `setup_grit_no_bbd_mem`**

Immediately after that function's existing
`group.stages_achieved = group.fx.flux().stages();`:

```cpp
    // Null buffers: Flux::process returns at its _buf_ok guard, so _clock_hz
    // is never written and stays 0 for the life of this instance. Set
    // explicitly rather than left to the arena, because SerialArena overlays
    // its groups and emplace does not zero what a previous group wrote.
    group.clock_achieved = group.fx.flux().clock_hz();
```

- [ ] **Step 5: Fold it in `proc_sweep_fx`**

Immediately after the existing
`acc += static_cast<float>(group.stages_achieved);`:

```cpp
    // Folded once per call, like stages_achieved above -- not per sample --
    // so the added work is constant and negligible against the block.
    acc += group.clock_achieved;
```

- [ ] **Step 6: Verify the bench image still builds**

```bash
source env.sh && python bench/run.py --profile sweep --build-only
```

Expected: a successful build, no link error. Never omit `--profile`.

- [ ] **Step 7: Commit**

```bash
git add bench/workloads_sweep.cpp
git commit -m "$(cat <<'EOF'
bench(sweep): fold the achieved clock into the SweepFxGroup rows

stages_achieved is identical across all five sweep_flux_rate_* rows --
they never call set_stages -- so it cannot distinguish a rate row that
clocked from one that silently did not. The clock is what those rows
sweep. Asserted non-zero in the two setups that run one; explicitly 0
in setup_grit_no_bbd_mem, whose Flux has null buffers.

Deferred from the cost-curves round because it moves per-row checksums.
That round's evidence is committed and the control-rate change moves
them anyway, so this is the window in which it is free.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: Bench — fold `active_voices` into `proc_sweep_room`

The second checksum gap (spec §10). The row folds `reverb_asleep()` but not
the voice count, so a room row whose voices silently stopped still matches.

**Files:**
- Modify: `bench/workloads_sweep.cpp` — `proc_sweep_room` (~line 804)

**Interfaces:**
- Consumes: `int Instrument::active_voices(int p) const`
  (`engine/instrument.h:150`), with `PART_A` / `PART_B` from
  `engine/instrument.h:14`.

- [ ] **Step 1: Fold both parts' voice counts**

Immediately after the existing
`acc += group.instrument.reverb_asleep() ? 1.f : 0.f;` in `proc_sweep_room`:

```cpp
    // reverb_asleep() catches "the room stopped running"; this catches "the
    // voices feeding it stopped". The three room rows sweep the reverb at a
    // FIXED voice load, so a drifting voice count would change what the room
    // is being fed while the row's name still claimed a controlled
    // comparison. Same fold bench/workloads_abl.cpp already uses.
    acc += static_cast<float>(group.instrument.active_voices(PART_A)
                            + group.instrument.active_voices(PART_B));
```

- [ ] **Step 2: Verify the bench image still builds**

```bash
source env.sh && python bench/run.py --profile sweep --build-only
```

Expected: a successful build.

- [ ] **Step 3: Run the bench contract test**

```bash
python -m pytest bench/test_run_contract.py -q
```

Expected: pass. This is what holds `run.py`'s row expectations and the
firmware's row table in agreement; neither task changed the row list, so it
must be unaffected.

- [ ] **Step 4: Commit**

```bash
git add bench/workloads_sweep.cpp
git commit -m "$(cat <<'EOF'
bench(sweep): fold the voice count into proc_sweep_room's checksum

reverb_asleep() catches a stopped room; this catches stopped voices.
The three room rows compare the reverb at a fixed voice load, so a
drifting count would silently change what is being compared.

Second of the two gaps deferred from the cost-curves round.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 8: The hardware measurement

**This task needs the Daisy Seed physically attached.** Do not start it
without confirmation from the owner that the board is connected and ready.

**Files:**
- Create: `docs/bench/2026-07-29-<sha>-sweep.md` and `.csv`, where `<sha>` is
  the short hash of the commit measured (`git rev-parse --short HEAD`)

- [ ] **Step 1: Confirm the tree is clean**

```bash
git status --short
```

Expected: empty. The bench refuses hardware evidence from a dirty tree.

- [ ] **Step 2: Rebind the QSPI receipt**

The engine changed, which invalidates `build/qspi-verified.json` — that
receipt binds the QSPI bank digest to the hashes of the built ELFs, so any
engine change breaks the binding even though the bank's 65,024 bytes are
unchanged.

```bash
source env.sh && python bench/run.py --profile sweep --no-build --program-qspi --build-only
```

Expected: the receipt is rewritten. If the run in Step 3 still reports
`QSPI verification receipt does not match current payload (artifacts)`,
repeat this step — it is a stale binding, not a corrupt bank.

- [ ] **Step 3: Run the bench**

```bash
source env.sh && python bench/run.py --profile sweep
```

Expected: a completed run with a cross-run checksum comparison that matches.
Per-row checksums **will differ from the 2026-07-29 `cd6dafd` run** — the
feedback coefficient moved (spec §9) and Tasks 6–7 added folds. That is
expected and is not a failure. What must hold exactly is the bench's own
comparison of two runs of the *same* firmware.

- [ ] **Step 4: Save the evidence**

Copy the run's markdown and CSV output to
`docs/bench/2026-07-29-<sha>-sweep.md` / `.csv`, following the naming and
layout of the existing `docs/bench/2026-07-29-cd6dafd-sweep.md`.

- [ ] **Step 5: Commit**

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): sweep profile after the FLUX control-rate change

Per-row checksums differ from the cd6dafd run by design: the feedback
coefficient reassociated and two rows gained checksum folds.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 9: The written verdict

**Files:**
- Modify: `docs/superpowers/specs/2026-07-29-flux-control-rate-design.md`
  (append a Results section)
- Modify: `docs/roadmap.md`

- [ ] **Step 1: Append a Results section to the design spec**

Append `## 13. Results` covering, each with the actual figure:

- the FLUX rows' before/after readings from
  `docs/bench/2026-07-29-cd6dafd-sweep.md` and the new evidence file, as a
  table with the delta per row;
- the total saving in points, against §2's **~2.5 per deck / ~5 total**
  prediction — and if it differs materially, say so and say what that implies
  about where the remaining 771 cycles per sample actually sit. A prediction
  that missed is a finding, not something to round toward;
- the GRIT-only rows (`fx_grit`, `sweep_grit_bare`,
  `sweep_grit_no_bbd_mem`), against §7's claim that the guards remove ~1.60
  points of work that was being done for nothing;
- the three render A/B numbers from Task 5 Step 3, verbatim;
- the instrument gate row's new figure against the 32.8 points needed, stated
  plainly whether or not it is encouraging.

- [ ] **Step 2: Update `docs/roadmap.md`**

Add a subsection under the FX cost work recording: what this round did, the
measured saving, the branch name, and what remains — mono FLUX (~9.2 points)
as the next step in the owner's order, and the `bbd.h` `1/sr_` division
(~0.6 points) that §11 recorded and left standing.

- [ ] **Step 3: Confirm the branch's shape**

```bash
git diff --stat main..HEAD
git diff main -- engine/fx/bbd.h engine/fx/part_fx.cpp
```

Expected: the second command prints nothing. The first should show only the
files listed in this plan's Global Constraints.

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "$(cat <<'EOF'
docs: the FLUX control-rate round, measured

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

- [ ] **Step 5: Do not merge**

Do not merge `perf/flux-control-rate` into `main` without being asked. Present
the branch and the measured result; whether it lands before or after the mono
round is the owner's call.
