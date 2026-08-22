# PULL — chord gravity between the decks: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One deck's melody is drawn onto the other deck's sounding chord, by a
bipolar centre knob whose magnitude is the per-note probability of being pulled.

**Architecture:** The leader publishes the pitch classes of its current chord as
a 12-bit absolute mask. `Instrument` — the only scope that sees both decks, as
with CHOKE and the excitation bus — reads PULL at the control raster, picks
leader and follower from the sign, and pushes `(mask, probability)` into the
follower. The follower draws once per PITCH-lane fire; a bound note switches its
`Quantizer` from the root-relative scale mask onto the absolute gravity mask.
Nothing new runs per sample: the quantizer already runs per control tick, and the
gravity path only swaps which mask it consults.

**Tech Stack:** C++17, doctest (vendored in `third_party/`), clang + Ninja,
Python 3 for the two panel generators.

**Spec:** `docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md`

---

## Global Constraints

- **Build (engine + tests + render host)** — clang + Ninja, never MSVC:
  ```bash
  source env.sh
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ctest --test-dir build --output-on-failure
  ```
  `-DCMAKE_BUILD_TYPE=Release` is **not optional**: a Debug configure makes
  `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- **VCV build** — always `host/vcv/build-local.sh`, never a hand-rolled `g++`
  (the system `g++` on this machine is the ARM cross-compiler).
- **Panel generators** — both panels are generated, never hand-edited:
  `python res/gen_panel.py` and `python res/gen_hw_panel.py`, each run from
  `host/vcv/`, each guarded by `res/test_panel.py` / `res/test_hw_panel.py`
  (plain scripts — pytest is not installed here).
- **Shell etiquette (binding, Windows/Git Bash auto-approval):**
  1. Never modify a file through the shell — no `sed -i`, `>`, `>>`, `tee`.
     Use the Edit tool for existing files, Write for new ones.
  2. Never use `cd`, not even before a read-only command. A command that
     genuinely needs a working directory goes into a script file in the
     session scratchpad.
  3. Never chain a write (`rm`, `mv`, `git add`, `git commit`) behind `&&`
     or `;` — each shell write is its own call.
  4. Repo-relative paths in write calls.
- **Everything written into the repo is English** — code, comments, tests,
  commit messages, docs.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **The probe rule:** no runtime claim enters a review reply, a commit message
  or a doc until a probe has printed it. Recipe: `docs/engine-map.md` §6.
- **No bit-exactness gates in new tests.** Renders are sanity checks. The two
  *existing* byte-identity gates (`ctrl_identity`, `wave_formant_sweep`) are a
  different matter — see the identity requirement below, which they enforce.
- **Identity requirement, and it is the load-bearing one:** with PULL at 0 the
  engine must be byte-identical to today. Every task that touches
  `Quantizer::process`, `Part::_control_tick` or `Instrument::process` has to
  leave `ctrl_identity` and `wave_formant_sweep` green without regenerating a
  hash. If a hash moves, the change is wrong — do not re-bake it.

---

## What was measured before this plan was written

The spec is from 2026-07-19. BODY, BBD, FEED, the melody rework, the 60 HP
plate and the MOD latch layer all landed after it. Everything below was
printed by a probe on 2026-08-22 against `main` @ `224d2ac`, desktop, 48 kHz,
`clang++ -std=c++17 -O2`. **Do not re-derive these by reading; they are the
plan's premises.**

1. **The quantizer reaches the sounding pitch on five of the six engines, and
   not on the sixth.** Probe: one `Part` per engine, `LANE_PITCH` base 0.5,
   depth 0, scale swapped Dorian → whole-tone (17.0 st → 18.0 st), pitch read
   back from `target_value(LANE_PITCH)` after 20 000 samples each:

   | engine | FLOW | STEP |
   |---|---|---|
   | SYNTH (1) | quantized | quantized |
   | SAMPLER (2) | **bypass** | **bypass** |
   | WAVE (3) | quantized | quantized |
   | BODY (4) | quantized | quantized |
   | BBD (5) | **bypass** | quantized |
   | FEED (6) | quantized | quantized |

   So PULL's **follower** side is inert on a SAMPLER deck and on a BBD deck in
   FLOW. That falls out of the existing bypass in `Part::_control_tick`
   (`part.cpp:282`) — no new gate is needed, but it must be documented, and the
   tests must not be written against a sampler deck.

2. **A sounding SYNTH note follows a mask change; it is not latched at
   trigger.** A/B render, identical runs except that run B swaps the scale
   100 ms into a `trigger_manual()` note: **24 000 of 24 000 samples differ,
   max |d| 0.544**. The spec's "bound notes follow the chord live — no special
   machinery" is confirmed. (`SynthEngineT::_adjust_surface` only adds and
   drops chord slots; the pitch tracking is the voice reading the target.)

3. **The change slew is 40.0 ms**, i.e. exactly 20 calls at
   `SynthEngine::kCtrlInterval` = 96 samples, 48 kHz. Probe: settle on Dorian
   at input 0.5 (17.0 st), swap to whole-tone (18.0 st), count calls until the
   output stops moving → 20. The spec's "~40 ms" is right.

4. **`Quantizer::process` returns the input untouched in FREE** and never
   looks at a mask there (`quantizer.h:96`). The spec's "FREE mode is not
   exempt" therefore needs its own branch — it is not a mask swap.

5. **The chord is already absolute, and COLOR 0 really is one pitch class.**
   `ChordBuilder::build(0.5, Dorian, root 0, …)`, pitch classes of the built
   semitones:

   | COLOR | n | semis | mask | pitch classes |
   |---|---|---|---|---|
   | 0.00 | 1 | 18.0 | `0x040` | 1 |
   | 0.25 | 2 | 18.0 13.0 | `0x042` | 2 |
   | 0.50 | 3 | 18.0 13.0 22.0 | `0x442` | 3 |
   | 0.75 | 4 | 18.0 13.0 22.0 28.0 | `0x452` | 4 |
   | 1.00 | 4 | 18.0 32.0 22.0 28.0 | `0x550` | 4 |

   The spec's "COLOR 0 = unison follow, emergent from the mask size" holds.

## Four places where the spec is out of date, and the decision taken

- **D1 — "Read-only; no new state" is wrong.** `Part::_control_tick` builds the
  chord into a *local* array (`part.cpp:396`) and pushes it; nothing keeps it.
  The leader mask needs one cached `uint16_t` member. Taken: cache it, one
  member, written at the same site.
- **D2 — the leader side is gated to note decks.** The spec predates SAMPLER
  being flattened to one note (`Part::_flatten_for_sampler`) and predates BBD
  and FEED entirely. On a SAMPLER or BBD deck the PITCH lane is not a note —
  that is the same reasoning `set_flow_melody` (`part.cpp:47`) and the
  quantizer bypass already use. Taken: a SAMPLER or BBD leader publishes mask
  0, i.e. PULL is simply off in that direction. FEED, BODY and WAVE lead
  normally. **This is the one design call this plan makes that the spec does
  not; it is flagged in the handover.**
- **D3 — the panel tail moved.** `PARAMS = PANEL_PARAMS + HIDDEN_PARAMS +
  APPENDED_PANEL_PARAMS + MOD_LAYER_PARAMS`, and `MOD_LAYER_PARAMS` is 49 ids
  since 2.21.7. "Appended LAST" now means *last in `APPENDED_PANEL_PARAMS`*,
  which shifts every MOD-layer id by one. Acceptable under the dev-alpha rule
  (saved patches may break freely) but it must be said out loud in the commit,
  and the init snapshot has to be re-baked.
- **D4 — "the `init.vcvm` snapshot needs a refresh" is stale.** The init state
  is baked from `INIT_DEFAULTS` in `gen_panel.py` (from `FM-INIT.vcvm`), and
  re-baking is a six-file job. PULL boots at 0.0, which is off, so the job is
  small — but `res/test_panel.py`'s `approved` dict is an independent
  transcription and must be retyped, not copied.

## File structure

| File | Responsibility |
|---|---|
| `engine/pitch/quantizer.h` | the gravity mask: an absolute second mask that replaces the scale mask while on, in every mode including FREE |
| `engine/pitch/chord.h` | `pc_mask12()` — the pitch-layer helper that turns built chord tones into a 12-bit absolute mask |
| `engine/parts/part.h` / `part.cpp` | leader: publish `chord_pc_mask()`. follower: hold `(mask, probability)`, draw per fire, push the override into its own quantizer |
| `engine/instrument.h` / `instrument.cpp` | `set_pull()`, the dead zone, leader/follower selection at the control raster |
| `engine/param_table.h` | `P_PULL` and its `apply_param` case |
| `host/render/scenario.cpp` | the `set_pull` scenario action |
| `host/vcv/res/gen_panel.py` | the `PULL` control + its `INIT_DEFAULTS` entry |
| `host/vcv/src/Fireflow.cpp` | `pushParams` → `inst.set_pull(...)` |
| `host/vcv/res/gen_hw_panel.py` | PULL's slot, size class and 4-character plate word |
| `tests/test_pull.cpp` | the feature's own gates (new file) |
| `tests/test_quantizer.cpp` | the gravity mask's gates, next to the mask code they test |
| `tests/test_param_table.cpp` | the inventory marker, bumped |

---

### Task 1: The gravity mask in `Quantizer`

**Files:**
- Modify: `engine/pitch/quantizer.h:86-140`
- Test: `tests/test_quantizer.cpp` (append)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `void Quantizer::set_gravity(uint16_t abs_pc_mask, bool on)` and
  `bool Quantizer::gravity_on() const`. `abs_pc_mask` is checked mod 12 with no
  root shift; `on` with a zero mask means off.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_quantizer.cpp`:

```cpp
// --- PULL, spec 2026-07-19 pull-chord-gravity ---

TEST_CASE("gravity: off is the old quantizer, value for value") {
    // The identity that ctrl_identity and wave_formant_sweep enforce at the
    // render level, asserted here where a failure names the input.
    Quantizer a, b;
    a.init(48000.f, 96); b.init(48000.f, 96);
    a.set_scale(SCALE_MASKS[SCALE_DORIAN]); b.set_scale(SCALE_MASKS[SCALE_DORIAN]);
    a.set_root(3);                          b.set_root(3);
    b.set_gravity(0x0000, true);            // a zero mask is not gravity
    for (int i = 0; i <= 100; ++i) {
        const float x = i / 100.f;
        CHECK(a.process(x) == b.process(x));   // exact, not Approx
    }
    CHECK_FALSE(b.gravity_on());
}

TEST_CASE("gravity: a bound note lands on a pitch class of the mask") {
    Quantizer q; q.init(48000.f, 96);
    q.set_scale(SCALE_MASKS[SCALE_DORIAN]);
    q.set_root(0);
    q.set_gravity(0x0044, true);            // pitch classes 2 and 6 only
    float v = 0.f;
    for (int i = 0; i < 200; ++i) v = q.process(0.5f);   // ride the slew out
    const int semis = static_cast<int>(v * Quantizer::SPAN_SEMIS + 0.5f);
    CHECK(((1u << (semis % 12)) & 0x0044u) != 0u);
}

TEST_CASE("gravity: the mask is absolute -- the root does not shift it") {
    // The scale mask is root-relative; this one is not, because it comes from
    // the sibling deck's sounding chord, which is already absolute.
    Quantizer a, b;
    a.init(48000.f, 96); b.init(48000.f, 96);
    a.set_root(0);  b.set_root(5);
    a.set_gravity(0x0044, true); b.set_gravity(0x0044, true);
    float va = 0.f, vb = 0.f;
    for (int i = 0; i < 200; ++i) { va = a.process(0.5f); vb = b.process(0.5f); }
    CHECK(va == vb);
}

TEST_CASE("gravity: FREE is not exempt while bound, and passthrough returns") {
    Quantizer q; q.init(48000.f, 96);
    q.set_mode(QuantMode::Free);
    q.set_scale(SCALE_MASKS[SCALE_DORIAN]);
    CHECK(q.process(0.3141f) == 0.3141f);         // free: raw
    q.set_gravity(0x0044, true);
    float v = 0.f;
    for (int i = 0; i < 200; ++i) v = q.process(0.3141f);
    CHECK(v != 0.3141f);                          // bound: quantized anyway
    const int semis = static_cast<int>(v * Quantizer::SPAN_SEMIS + 0.5f);
    CHECK(((1u << (semis % 12)) & 0x0044u) != 0u);
    q.set_gravity(0x0044, false);
    CHECK(q.process(0.3141f) == 0.3141f);         // released: raw again
}

TEST_CASE("gravity: a mask change slews over 20 calls (40 ms at 48k/96)") {
    // Measured 2026-08-22: the existing scale-change slew is exactly 20 calls
    // at kCtrlInterval 96. Gravity rides the same on_change() path.
    Quantizer q; q.init(48000.f, 96);
    q.set_root(0);
    q.set_gravity(0x0044, true);                  // classes 2, 6
    float v = 0.f;
    for (int i = 0; i < 200; ++i) v = q.process(0.5f);
    q.set_gravity(0x0088, true);                  // classes 3, 7
    int last_move = 0; float prev = v;
    for (int i = 1; i <= 200; ++i) {
        const float x = q.process(0.5f);
        if (x != prev) last_move = i;
        prev = x;
    }
    CHECK(last_move == 20);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh
cmake --build build --target spky_tests
./build/spky_tests -tc="gravity*"
```
Expected: compile error — `set_gravity` is not a member of `Quantizer`.

- [ ] **Step 3: Implement the gravity mask**

In `engine/pitch/quantizer.h`, add the setter next to the other three, replace
`process()`'s FREE guard and mask pick, and thread an explicit root through the
two mask helpers so the gravity path can pass 0:

```cpp
    void set_root(int semis)        { if (semis != _root)   { _root = semis;   on_change(); } }

    // PULL (spec 2026-07-19 pull-chord-gravity): a SECOND mask, absolute.
    // Bit i = pitch class i with no root shift -- the scale mask stays
    // root-relative, this one does not, because it comes from the sibling
    // deck's sounding chord and that is already absolute. While it is on it
    // REPLACES the scale/chrom mask in every mode, FREE included: binding a
    // note to the neighbour's harmony is the whole point of the feature, so a
    // free-running deck is not exempt. A zero mask is not gravity.
    void set_gravity(uint16_t abs_pc_mask, bool on) {
        const bool want = on && abs_pc_mask != 0;
        if (want == _grav_on && (!want || abs_pc_mask == _grav_mask)) return;
        _grav_mask = abs_pc_mask;
        _grav_on   = want;
        on_change();
    }
    bool gravity_on() const { return _grav_on; }
```

```cpp
    float process(float norm) {
        if (!_quantizing()) {                  // FREE, and no note bound
            _last_out = norm;
            _have_out = true;
            _have_note = false;
            return norm;
        }
        const uint16_t mask = _grav_on ? _grav_mask
                            : (_mode == QuantMode::Chrom ? CHROM_MASK : _scale);
        const int root = _grav_on ? 0 : _root;
        const float semis = clampf(norm, 0.f, 1.f) * SPAN_SEMIS;
        int note = nearest_note(semis, mask, root);
        if (_have_note && note != _last_note && allowed(_last_note, mask, root)) {
            const float d_last = std::fabs(semis - static_cast<float>(_last_note));
            const float d_note = std::fabs(semis - static_cast<float>(note));
            if (d_last - d_note < HYST_SEMIS) note = _last_note;   // hold
        }
        _last_note = note;
        _have_note = true;

        float out = static_cast<float>(note) / SPAN_SEMIS;
        if (_slew_ctr > 0) {
            --_slew_ctr;
            const float t = 1.f - static_cast<float>(_slew_ctr) / static_cast<float>(_slew_len);
            out = lerpf(_slew_from, out, t);
        }
        _last_out = out;
        _have_out = true;
        return out;
    }

private:
    // "Is the next process() call going to snap?" -- FREE alone no longer
    // answers it, because a bound note quantizes in FREE too.
    bool _quantizing() const { return _grav_on || _mode != QuantMode::Free; }

    void on_change() {
        _have_note = false;                       // re-pick without hysteresis
        if (_have_out && _quantizing()) {
            _slew_from = _last_out;               // soften the jump (~40 ms)
            _slew_ctr = _slew_len;
        } else {
            _slew_ctr = 0;                        // into passthrough: instant
        }
    }

    bool allowed(int k, uint16_t mask, int root) const {
        int deg = (k - root) % 12;
        if (deg < 0) deg += 12;
        return (mask >> deg) & 1;
    }
```

`nearest_note` takes the same extra parameter and forwards it:

```cpp
    int nearest_note(float semis, uint16_t mask, int root) const {
        const int center = static_cast<int>(semis + 0.5f);
        for (int d = 0; d <= 12; ++d) {
            const int lo = center - d, hi = center + d;
            const bool lo_ok = lo >= 0 && allowed(lo, mask, root);
            const bool hi_ok = hi <= 36 && allowed(hi, mask, root);
            if (lo_ok && hi_ok && lo != hi)
                return std::fabs(semis - static_cast<float>(hi))
                     < std::fabs(semis - static_cast<float>(lo)) ? hi : lo;
            if (lo_ok) return lo;
            if (hi_ok) return hi;
        }
        return center;  // unreachable with a non-empty mask
    }
```

New members beside the existing ones:

```cpp
    uint16_t  _grav_mask = 0;
    bool      _grav_on   = false;
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="gravity*"
```
Expected: all five cases PASS.

- [ ] **Step 5: Prove the identity, not just the tests**

```bash
ctest --test-dir build --output-on-failure
```
Expected: the whole suite green, **including `ctrl_identity` and
`wave_formant_sweep`**. Those two are byte-identity gates; gravity is off
everywhere in this task, so they must not move. If either goes red, the
`_quantizing()` / root threading changed behaviour at PULL 0 — fix that, do not
re-bake a hash.

- [ ] **Step 6: Prove one gate can go red**

Temporarily change `const int root = _grav_on ? 0 : _root;` to
`const int root = _root;`, rebuild, run
`./build/spky_tests -tc="gravity: the mask is absolute*"`.
Expected: FAIL. Revert the mutation, rebuild, confirm green again.

- [ ] **Step 7: Commit**

```bash
git add engine/pitch/quantizer.h tests/test_quantizer.cpp
git commit -m "feat(pitch): the quantizer grows an absolute gravity mask"
```

---

### Task 2: The leader publishes its chord as a pitch-class mask

**Files:**
- Modify: `engine/pitch/chord.h` (append the helper after the class)
- Modify: `engine/parts/part.h`, `engine/parts/part.cpp:396-405`
- Create: `tests/test_pull.cpp`
- Modify: `CMakeLists.txt` (register the new test file)

**Interfaces:**
- Consumes: `Quantizer::SPAN_SEMIS` from Task 1's file (unchanged).
- Produces: `uint16_t spky::pc_mask12(const float* norm, int n)` and
  `uint16_t Part::chord_pc_mask() const` — 0 means "this deck publishes no
  harmony".

- [ ] **Step 1: Write the failing test**

Create `tests/test_pull.cpp`:

```cpp
// PULL -- chord gravity between the decks.
// Spec: docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md
// Plan: docs/superpowers/plans/2026-08-22-pull-chord-gravity.md
#include <doctest/doctest.h>
#include <vector>
#include "parts/part.h"
#include "pitch/chord.h"
using namespace spky;

namespace {
int popcount12(uint16_t m) { int k = 0; while (m) { k += m & 1; m >>= 1; } return k; }

// Settle a part on a fixed pitch with the quantizer in scale mode.
void settle(Part& p, EngineId e, float color) {
    p.init(48000.f, 11);
    p.set_engine(e);
    p.set_step(false, 8);
    p.set_target_active(LANE_PITCH, true);
    p.set_target_base(LANE_PITCH, 0.5f);
    p.set_target_depth(LANE_PITCH, 0.f);
    p.set_color(color);
    p.quant().set_mode(QuantMode::Scale);
    p.quant().set_scale(SCALE_MASKS[SCALE_DORIAN]);
    float l, r;
    for (int i = 0; i < 20000; ++i) p.process(l, r);
}
} // namespace

TEST_CASE("pc_mask12: absolute pitch classes, nearest semitone") {
    const float notes[3] = { 12.f / 36.f, 18.f / 36.f, 25.f / 36.f };
    CHECK(pc_mask12(notes, 3) == (uint16_t)((1u << 0) | (1u << 6) | (1u << 1)));
    CHECK(pc_mask12(notes, 0) == 0u);
}

TEST_CASE("leader: COLOR sets how many pitch classes the deck publishes") {
    // Measured 2026-08-22 on ChordBuilder directly: 1/2/3/4 classes at
    // COLOR 0 / .25 / .5 / .75. The Part path has to reproduce that.
    const float colors[4] = { 0.f, 0.25f, 0.5f, 0.75f };
    for (int i = 0; i < 4; ++i) {
        Part p; settle(p, ENGINE_SYNTH, colors[i]);
        CAPTURE(colors[i]);
        CHECK(popcount12(p.chord_pc_mask()) == i + 1);
    }
}

TEST_CASE("leader: a sampler or BBD deck publishes no harmony") {
    // On those two the PITCH lane is a read position and a clock bend, not a
    // note -- the same two engines set_flow_melody and the quantizer bypass
    // name. Measured 2026-08-22: the quantizer never reaches the sounding
    // pitch on SAMPLER, and on BBD only in STEP.
    Part s; settle(s, ENGINE_SAMPLER, 0.75f);
    CHECK(s.chord_pc_mask() == 0u);
    Part b; settle(b, ENGINE_BBD, 0.75f);
    CHECK(b.chord_pc_mask() == 0u);
    Part f; settle(f, ENGINE_FEED, 0.75f);
    CHECK(f.chord_pc_mask() != 0u);      // FEED is a note deck and does lead
}
```

Register it in `CMakeLists.txt`, in the `add_executable(spky_tests ...)` list,
directly after `tests/test_chord.cpp`:

```cmake
    tests/test_pull.cpp
```

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh
cmake --build build --target spky_tests
./build/spky_tests -tc="pc_mask12*,leader:*"
```
Expected: compile error — `pc_mask12` and `chord_pc_mask` do not exist.

- [ ] **Step 3: Implement the helper and the publication**

At the end of `engine/pitch/chord.h`, inside `namespace spky`, after the
`ChordBuilder` class:

```cpp
// Built chord tones (0..1 = 36 semitones) -> absolute 12-bit pitch-class mask,
// rounded to the nearest semitone. Absolute on purpose: PULL's follower checks
// this mask mod 12 with no root shift (spec 2026-07-19 pull-chord-gravity).
// The rounding is exact for a quantized root and is the only sane reading of
// an unquantized one.
inline uint16_t pc_mask12(const float* norm, int n) {
    uint16_t m = 0;
    for (int i = 0; i < n; ++i) {
        int s = static_cast<int>(norm[i] * Quantizer::SPAN_SEMIS + 0.5f) % 12;
        if (s < 0) s += 12;
        m |= static_cast<uint16_t>(1u << s);
    }
    return m;
}
```

In `engine/parts/part.h`, public, next to `chord_size()`:

```cpp
    // PULL, leader side (spec 2026-07-19 pull-chord-gravity): the pitch
    // classes this deck is currently sounding, as an absolute 12-bit mask.
    // 0 = this deck publishes no harmony, which is what a SAMPLER or BBD deck
    // does: there the PITCH lane is a read position and a clock bend, not a
    // note. Instrument is the only reader -- a Part never sees its sibling.
    uint16_t chord_pc_mask() const { return _chord_pc; }
```

private, beside `_chord`:

```cpp
    uint16_t _chord_pc = 0;      // PULL: what chord_pc_mask() hands out
    // The two engines on which the PITCH lane is not a note. Same set as
    // set_flow_melody's (part.cpp) and as the quantizer bypass's, and named
    // separately for the same reason that one is: they share a cause, not a
    // definition.
    bool _note_deck() const {
        return _engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD;
    }
```

In `engine/parts/part.cpp::_control_tick()`, immediately after the
`_chord.apply(...)` call and **before** `_flatten_for_sampler`:

```cpp
    int nch = _chord.apply(_tg[LANE_PITCH], _chord_mask(),
                           _quant.root_semis(), chord);
    // PULL, leader side. Taken here rather than after the flatten because the
    // flatten is a sampler-only collapse and a sampler publishes nothing
    // anyway -- reading before it keeps the two concerns separate.
    _chord_pc = _note_deck() ? pc_mask12(chord, nch) : 0;
    nch = _flatten_for_sampler(chord, nch);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="pc_mask12*,leader:*"
```
Expected: all three cases PASS.

- [ ] **Step 5: Prove the suite is still identical**

```bash
ctest --test-dir build --output-on-failure
```
Expected: green, `ctrl_identity` and `wave_formant_sweep` included — this task
only *reads* state, so a hash move would mean the read changed the write.

- [ ] **Step 6: Commit**

```bash
git add engine/pitch/chord.h engine/parts/part.h engine/parts/part.cpp tests/test_pull.cpp CMakeLists.txt
git commit -m "feat(pitch): a note deck publishes its chord as a pitch-class mask"
```

---

### Task 3: The follower binds a note, once per fire

**Files:**
- Modify: `engine/parts/part.h` (setter, members, the `fired` branch at
  `part.h:320`), `engine/parts/part.cpp` (seed in `init`, push in
  `_control_tick`)
- Test: `tests/test_pull.cpp` (append)

**Interfaces:**
- Consumes: `Quantizer::set_gravity(uint16_t, bool)` (Task 1),
  `Part::chord_pc_mask()` (Task 2).
- Produces: `void Part::set_gravity(uint16_t abs_pc_mask, float p)` — `p` is
  the per-note bind probability in 0..1; a zero mask or `p == 0` is off and
  releases a bound note. Test-visible state: `bool Part::gravity_bound() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_pull.cpp`:

```cpp
namespace {
// A stepped note deck that fires often, for counting draws.
void stepped(Part& p, uint32_t seed) {
    p.init(48000.f, seed);
    p.set_engine(ENGINE_SYNTH);
    p.set_step(true, 8);
    p.set_target_active(LANE_PITCH, true);
    p.set_target_base(LANE_PITCH, 0.5f);
    p.set_target_depth(LANE_PITCH, 1.f);
    p.set_depth(1.f);
    p.mod().set_range(1.f);
    p.quant().set_mode(QuantMode::Scale);
    p.quant().set_scale(SCALE_MASKS[SCALE_DORIAN]);
}
// Count fires and how many of them were bound, over `sec` seconds.
void count(Part& p, float sec, int& fires, int& bound) {
    fires = 0; bound = 0;
    bool prev = false; float l, r;
    const int n = static_cast<int>(48000.f * sec);
    for (int i = 0; i < n; ++i) {
        p.process(l, r);
        const bool f = p.mod().lane_fired(LANE_PITCH);
        if (f && !prev) { ++fires; if (p.gravity_bound()) ++bound; }
        prev = f;
    }
}
} // namespace

TEST_CASE("follower: probability 0 binds nothing, probability 1 binds every note") {
    int fires = 0, bound = 0;
    { Part p; stepped(p, 21); p.set_gravity(0x0044, 0.f);
      count(p, 8.f, fires, bound);
      CHECK(fires > 20); CHECK(bound == 0); }
    { Part p; stepped(p, 21); p.set_gravity(0x0044, 1.f);
      count(p, 8.f, fires, bound);
      CHECK(fires > 20); CHECK(bound == fires); }
}

TEST_CASE("follower: half probability binds roughly half, reproducibly") {
    // Deterministic seed -> the count is exact and repeatable, so this is a
    // regression gate on the stream as well as a statistics check.
    int f1 = 0, b1 = 0, f2 = 0, b2 = 0;
    Part a; stepped(a, 21); a.set_gravity(0x0044, 0.5f); count(a, 30.f, f1, b1);
    Part b; stepped(b, 21); b.set_gravity(0x0044, 0.5f); count(b, 30.f, f2, b2);
    CHECK(f1 == f2);
    CHECK(b1 == b2);                       // same seed, same stream
    CHECK(b1 > f1 / 4);                    // not stuck off
    CHECK(b1 < (3 * f1) / 4);              // not stuck on
}

TEST_CASE("follower: a bound note sounds a pitch class of the mask") {
    Part p; stepped(p, 21);
    p.set_gravity(0x0044, 1.f);            // classes 2 and 6 only
    float l, r;
    bool prev = false; int checked = 0;
    for (int i = 0; i < 48000 * 20 && checked < 8; ++i) {
        p.process(l, r);
        const bool f = p.mod().lane_fired(LANE_PITCH);
        if (f && !prev) {
            // Ride the 40 ms change slew out before reading the pitch: the
            // glide is the feature, so the value at the fire sample is not
            // the destination (measured 2026-08-22: 20 control ticks).
            for (int k = 0; k < 4000; ++k) p.process(l, r);
            const int semis =
                static_cast<int>(p.pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
            CAPTURE(semis);
            CHECK(((1u << (semis % 12)) & 0x0044u) != 0u);
            ++checked;
        }
        prev = f;
    }
    CHECK(checked == 8);
}

TEST_CASE("follower: gravity off releases a bound note") {
    Part p; stepped(p, 21);
    p.set_gravity(0x0044, 1.f);
    float l, r;
    for (int i = 0; i < 48000 * 4; ++i) p.process(l, r);
    CHECK(p.gravity_bound());
    p.set_gravity(0, 0.f);
    CHECK_FALSE(p.gravity_bound());
    for (int i = 0; i < 48000; ++i) p.process(l, r);
    CHECK_FALSE(p.quant().gravity_on());
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="follower:*"
```
Expected: compile error — `set_gravity` / `gravity_bound` are not members of
`Part`.

- [ ] **Step 3: Implement the follower side**

`engine/parts/part.h`, public, right after `chord_pc_mask()`:

```cpp
    // PULL, follower side. Instrument pushes the LEADER's mask plus the
    // per-note bind probability at the control raster; a zero mask or a zero
    // probability is off, and off releases whatever is bound (the spec's
    // zero-crossing release). The draw itself happens on the PITCH fire, in
    // process() -- see the comment there for why it cannot happen here.
    void set_gravity(uint16_t abs_pc_mask, float p) {
        _grav_mask = abs_pc_mask;
        _grav_prob = clampf(p, 0.f, 1.f);
        if (_grav_mask == 0 || _grav_prob == 0.f) _grav_bound = false;
    }
    bool gravity_bound() const { return _grav_bound; }
```

private members, beside `_chord_pc`:

```cpp
    uint16_t _grav_mask  = 0;    // PULL: the leader's pitch classes
    float    _grav_prob  = 0.f;  // PULL: per-note bind probability
    bool     _grav_bound = false;// PULL: is the note now sounding bound?
    Rng      _pull_rng;          // PULL: own stream, seeded off _seed_base
```

`part.h` already reaches `Rng` through `mod/super_modulator.h`; add the direct
include anyway so the dependency is visible:

```cpp
#include "mod/rng.h"
```

In `process()`, the `fired` branch (`part.h:320`) grows the draw:

```cpp
        const bool fired = _mod.lane_fired(LANE_PITCH);
        if (fired) {
            _note_suppressed = _inhibit;
            if (!_inhibit) _gate_ctr = _gate_len;
            // PULL: bind-or-free for the note that is about to fire. Drawn
            // HERE, in front of the fire's own _control_tick() below, so the
            // quantizer call in that tick already carries this note's
            // decision -- a draw inside _fire_trigger() would land one tick
            // (up to 96 samples) late and the note would be struck free and
            // only then glide. Drawn even when the note is suppressed, so the
            // stream does not depend on CHOKE: same reproducibility rule the
            // lane seeds follow.
            _grav_bound = (_grav_mask != 0 && _grav_prob > 0.f)
                        && _pull_rng.next_unipolar() < _grav_prob;
        }
```

In `engine/parts/part.cpp::init()`, beside the other seeded state:

```cpp
    _pull_rng.seed(seed_base ^ 0x50554C4Cu);   // 'PULL'
```

In `_control_tick()`, immediately before the `_quant.process(pitch_raw)` call:

```cpp
    // PULL: the override is pushed every tick, not only on a fire, because a
    // bound note follows the leader's chord LIVE -- when the leader
    // re-triggers, this deck glides onto the nearest tone of the new mask over
    // the quantizer's own 40 ms change slew. Measured 2026-08-22: a sounding
    // SYNTH note does follow a mask change (24000/24000 samples differ,
    // max |d| 0.544), so no re-trigger machinery is needed here.
    _quant.set_gravity(_grav_mask, _grav_bound);
    const float pitch_quantized = _quant.process(pitch_raw);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="follower:*"
```
Expected: all four cases PASS.

- [ ] **Step 5: Prove the identity again, and prove a gate can go red**

```bash
ctest --test-dir build --output-on-failure
```
Expected: green including the two hash gates — nothing above runs while
`_grav_mask` is 0, which is every existing scenario.

Then mutate: change the draw to `_grav_bound = false;`, rebuild, run
`./build/spky_tests -tc="follower: probability 0 binds nothing*"`.
Expected: FAIL on the `bound == fires` half. Revert, rebuild, green.

- [ ] **Step 6: Commit**

```bash
git add engine/parts/part.h engine/parts/part.cpp tests/test_pull.cpp
git commit -m "feat(parts): a follower deck binds a note to the sibling's chord"
```

---

### Task 4: `Instrument::set_pull` — direction, dead zone, routing

**Files:**
- Modify: `engine/instrument.h` (setter + member, beside `set_choke`/`_choke`),
  `engine/instrument.cpp:239-278` (the control-rate block)
- Test: `tests/test_pull.cpp` (append)

**Interfaces:**
- Consumes: `Part::chord_pc_mask()` (Task 2), `Part::set_gravity()` (Task 3).
- Produces: `void Instrument::set_pull(float)`, `-1..+1`, and
  `Instrument::kPullDead = 0.03f`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_pull.cpp`:

```cpp
#include "instrument.h"

namespace {
// Two note decks, both stepped, A voiced as a chord.
void two_decks(Instrument& in) {
    in.init(48000.f);
    for (int d = 0; d < 2; ++d) {
        in.set_engine(d, ENGINE_SYNTH);
        in.set_step(d, true, 8);
        in.set_target_active(d, LANE_PITCH, true);
        in.set_target_base(d, LANE_PITCH, 0.5f);
        in.set_target_depth(d, LANE_PITCH, 1.f);
        in.set_quant_mode(d, QuantMode::Scale);
    }
    in.set_scale(SCALE_DORIAN);
    in.set_color(PART_A, 0.75f);      // A leads with four tones
    in.set_color(PART_B, 0.f);
}
void run(Instrument& in, float sec) {
    const int n = static_cast<int>(48000.f * sec);
    std::vector<float> l(64), r(64);
    for (int i = 0; i < n; i += 64) in.process(nullptr, nullptr, l.data(), r.data(), 64);
}
} // namespace

TEST_CASE("pull: the sign picks the leader, CHOKE's convention") {
    { Instrument in; two_decks(in); in.set_pull(-1.f); run(in, 4.f);
      CHECK(in.part(PART_B).gravity_bound());        // A leads, B is pulled
      CHECK_FALSE(in.part(PART_A).gravity_bound()); }
    { Instrument in; two_decks(in); in.set_pull(+1.f); run(in, 4.f);
      CHECK(in.part(PART_A).gravity_bound());
      CHECK_FALSE(in.part(PART_B).gravity_bound()); }
}

TEST_CASE("pull: the dead zone makes noon reliably off") {
    Instrument in; two_decks(in);
    in.set_pull(0.02f);                              // inside kPullDead
    run(in, 4.f);
    CHECK_FALSE(in.part(PART_A).gravity_bound());
    CHECK_FALSE(in.part(PART_B).gravity_bound());
}

TEST_CASE("pull: at full deflection every follower note is a leader chord tone") {
    Instrument in; two_decks(in);
    in.set_pull(-1.f);
    run(in, 2.f);
    int checked = 0;
    for (int k = 0; k < 12 && checked < 6; ++k) {
        run(in, 0.5f);
        const uint16_t lead = in.part(PART_A).chord_pc_mask();
        if (!lead) continue;
        const int semis = static_cast<int>(
            in.part(PART_B).pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
        CAPTURE(lead); CAPTURE(semis);
        CHECK(((1u << (semis % 12)) & lead) != 0u);
        ++checked;
    }
    CHECK(checked == 6);
}

TEST_CASE("pull: sweeping through centre releases the old follower") {
    Instrument in; two_decks(in);
    in.set_pull(-1.f); run(in, 4.f);
    CHECK(in.part(PART_B).gravity_bound());
    in.set_pull(0.f);  run(in, 0.1f);
    CHECK_FALSE(in.part(PART_B).gravity_bound());
    CHECK_FALSE(in.part(PART_B).quant().gravity_on());
}

TEST_CASE("pull: a sampler leader means the knob does nothing") {
    // D2: the PITCH lane is not a note on SAMPLER or BBD, so those decks
    // publish mask 0 and gravity in that direction is simply off.
    Instrument in; two_decks(in);
    in.set_engine(PART_A, ENGINE_SAMPLER);
    in.set_pull(-1.f);
    run(in, 4.f);
    CHECK_FALSE(in.part(PART_B).gravity_bound());
}
```

`Instrument` has no part accessor today. Add one, test-only, in the same idiom
as the other `SPKY_TESTING` windows in `engine/instrument.h`:

```cpp
#ifdef SPKY_TESTING
    // A window for the PULL gates (tests/test_pull.cpp): they have to see
    // which deck ended up bound, which is per-Part state with no host-facing
    // reason to exist. Same idiom as accent_for_test() in synth_engine.h --
    // compiled only for the tests target, so render and the firmware never
    // see it.
    const Part& part(int p) const { return _parts[p]; }
#endif
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="pull:*"
```
Expected: compile error — `set_pull` is not a member of `Instrument`.

- [ ] **Step 3: Implement the knob and the routing**

`engine/instrument.h`, next to `set_choke`:

```cpp
    // PULL (spec 2026-07-19 pull-chord-gravity): bipolar chord gravity between
    // the decks, CHOKE's sign convention -- negative = A leads and B's notes
    // are pulled onto A's chord, positive mirrored, 0 = off and structurally
    // bypassed. |PULL| is the per-note probability of a note being bound,
    // rescaled off the dead zone so full deflection is exactly 1. The dead
    // zone exists so 12 o'clock on a real pot is reliably off.
    void set_pull(float p) { _pull = clampf(p, -1.f, 1.f); }
    static constexpr float kPullDead = 0.03f;
```

member, beside `_choke`:

```cpp
    float _pull = 0.f;         // -1..+1 chord-gravity knob (dead zone at noon)
```

`engine/instrument.cpp`, inside the `if (_ctrl_ctr == 0)` block, directly after
the FLUX rhythm publication (it belongs with the other cross-deck hand-overs and
must run before the Parts' own control ticks consume it):

```cpp
            // PULL: chord gravity, leader -> follower (spec 2026-07-19
            // pull-chord-gravity). Cross-deck knowledge stays here, exactly as
            // it does for CHOKE, the excitation bus and the FLUX rhythm: the
            // Parts never see each other. A leader with no harmony to offer --
            // a SAMPLER or BBD deck -- publishes mask 0, and Part::set_gravity
            // turns that into "off", so the knob is simply inert in that
            // direction rather than special-cased here.
            {
                const float amt = _pull < 0.f ? -_pull : _pull;
                if (amt <= kPullDead) {
                    _parts[PART_A].set_gravity(0, 0.f);
                    _parts[PART_B].set_gravity(0, 0.f);
                } else {
                    const int lead = _pull < 0.f ? PART_A : PART_B;
                    const int foll = lead == PART_A ? PART_B : PART_A;
                    const float prob = (amt - kPullDead) / (1.f - kPullDead);
                    _parts[foll].set_gravity(_parts[lead].chord_pc_mask(), prob);
                    _parts[lead].set_gravity(0, 0.f);
                }
            }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="pull:*"
```
Expected: all five cases PASS.

- [ ] **Step 5: Prove the bypass**

```bash
ctest --test-dir build --output-on-failure
```
Expected: green. `ctrl_identity` and `wave_formant_sweep` are the gate that PULL
0 costs nothing: at `_pull == 0` the block above calls `set_gravity(0, 0.f)`
twice per control tick, `Part` clears a bool, `Quantizer::set_gravity` returns
early, and the mask never changes. If a hash moves, that chain is broken.

- [ ] **Step 6: Prove the direction gate can go red**

Temporarily flip `const int lead = _pull < 0.f ? PART_B : PART_A;`, rebuild, run
`./build/spky_tests -tc="pull: the sign picks the leader*"`.
Expected: FAIL. Revert, rebuild, green.

- [ ] **Step 7: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_pull.cpp
git commit -m "feat(engine): PULL routes chord gravity from leader to follower"
```

---

### Task 5: `P_PULL`, `apply_param`, and the render host action

**Files:**
- Modify: `engine/param_table.h:113-115` (append after `P_PACE`), and its
  `apply_param()` switch
- Modify: `host/render/scenario.cpp:151`
- Modify: `tests/test_param_table.cpp:60-63` (the inventory marker)

**Interfaces:**
- Consumes: `Instrument::set_pull()` (Task 4).
- Produces: `spky::P_PULL`, range `-1..+1`, `steps 0`, appended last so
  `P_PULL == P_COUNT - 1`; the scenario action name `set_pull`.

- [ ] **Step 1: Write the failing test**

Replace the inventory marker in `tests/test_param_table.cpp`. Keep the whole
explanatory comment block above it — only the three `CHECK`s and one added
sentence change:

```cpp
    // 62 -> 63 on 2026-08-22: P_PULL was APPENDED after P_PACE (plan
    // 2026-08-22-pull-chord-gravity, task 5), so P_MODE and P_PACE keep their
    // indices and tests/param_impact_points.h's frozen vectors keep their
    // meaning -- only the trailing marker moves. That is what an append is
    // supposed to cost.
    CHECK(P_MODE == 62);
    CHECK(P_PACE == P_MODE + 1);
    CHECK(P_PULL == P_COUNT - 1);      // inventory marker: bump on append
}
```

And append a case to the range test in the same file:

```cpp
TEST_CASE("param table: PULL is bipolar and continuous") {
    CHECK(kParams[P_PULL].steps == 0);
    CHECK(kParams[P_PULL].lo == doctest::Approx(-1.f));
    CHECK(kParams[P_PULL].hi == doctest::Approx(1.f));
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="param table*"
```
Expected: compile error — `P_PULL` is not declared.

- [ ] **Step 3: Add the parameter**

In `engine/param_table.h`, append to the X-macro list, after the `P_PACE` row
(and after its comment), keeping the trailing backslash discipline of the
lines above:

```c
  X(P_PACE,       0.f, 1.f, 0) \
  /* PULL: bipolar chord gravity between the decks (spec 2026-07-19
     pull-chord-gravity). Sign = direction on CHOKE's convention, magnitude =
     per-note bind probability, 0 = off with a +-0.03 dead zone in the
     engine. Appended LAST: tests/param_impact_points.h's frozen vectors are
     positional, so an append is free and an insertion is not. */ \
  X(P_PULL,      -1.f, 1.f, 0)
```

In `apply_param()`, beside the `P_CHOKE` case:

```cpp
    case P_PULL:       in.set_pull(v); break;
```

In `host/render/scenario.cpp`, beside the `set_choke` action:

```cpp
    else if (a == "set_pull")             inst.set_pull(e.value);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="param table*"
ctest --test-dir build --output-on-failure
```
Expected: green.

- [ ] **Step 5: Find out what the parameter-impact gate says, and report it**

```bash
./build/spky_tests -tc="*param impact*" -s
```
`tests/test_param_impact.cpp` runs every `ParamId` against four frozen
operating points and compares the audible/dead verdict to an expected set.
PULL needs a *chord-voiced leader and a melodic follower* to move audio, which
those four points do not obviously provide.

- If PULL comes back audible: nothing to do, note the result in the commit.
- If PULL comes back dead: add `P_PULL` to `expected_untraced` (**not** to
  `expected_proven` — proven means somebody proved it dead by construction),
  with a comment naming the reason: the four frozen points do not put a chord
  on one deck and a melody on the other, so PULL has nothing to bind at any of
  them. Quote the gate's own printed line in the comment.

Do not guess which branch applies. Run it, read the printed
`dead now: / dead expected:` lines, then edit.

- [ ] **Step 6: Commit**

```bash
git add engine/param_table.h host/render/scenario.cpp tests/test_param_table.cpp tests/test_param_impact.cpp
git commit -m "feat(params): P_PULL, appended last, reaches both hosts"
```

---

### Task 6: The knob on the VCV panel, and the init re-bake

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`APPENDED_PANEL_PARAMS` tail,
  `INIT_DEFAULTS`)
- Modify: `host/vcv/src/Fireflow.cpp` (`pushParams`, near line 1169)
- Modify: `host/vcv/res/test_panel.py` (the `approved` dict)
- Check: `bench/audition/init_patch.cpp`

**Interfaces:**
- Consumes: `Instrument::set_pull()` (Task 4).
- Produces: the `PULL` param id at the end of `APPENDED_PANEL_PARAMS`, boot
  value `0.0`.

- [ ] **Step 1: Add the control**

In `host/vcv/res/gen_panel.py`, append to `APPENDED_PANEL_PARAMS` — as the
last entry, after `SHUFFLE`:

```python
    # PULL: bipolar chord gravity between the decks (spec 2026-07-19
    # pull-chord-gravity). Appended LAST like CHOKE/FILT/TIDE, which now means
    # last in APPENDED_PANEL_PARAMS -- the 49 MOD_LAYER_PARAMS ids behind it
    # all shift by one. Accepted: this is a dev alpha and saved patches may
    # break (memory fireflow-dev-alpha-no-patch-compat). The slot is the empty
    # ROW_DUO2 directly under CHOKE, the centre's other cross-deck knob.
    Ctl("PULL", SMKNOB, CX, ROW_DUO2, "PULL"),
```

and to `INIT_DEFAULTS`:

```python
    # Centre = off, the same boot value CHOKE has and for the same reason:
    # a cross-deck control that is on at boot is a surprise, not a feature.
    "PULL": 0.000000000,
```

- [ ] **Step 2: Regenerate and run the guard**

```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
```
(Both must run **from `host/vcv/`** — put the two lines in a scratchpad script
rather than prefixing a `cd`.)

Expected: the generator prints its param count and writes
`res/Fireflow.svg`, `src/generated_panel.hpp`, `src/init_patch.hpp`;
`test_panel.py` FAILS on the `approved` dict, which does not know `PULL`.

- [ ] **Step 3: Retype the approved entry**

In `host/vcv/res/test_panel.py`, add `"PULL": 0.0` to the `approved` dict.
**Retype the number, do not copy it from `INIT_DEFAULTS`** — the dict is a
deliberate second, independent transcription, and copying defeats its purpose.

- [ ] **Step 4: Run the guard again**

```bash
python host/vcv/res/test_panel.py
```
Expected: PASS.

- [ ] **Step 5: Wire it in the module**

In `host/vcv/src/Fireflow.cpp::pushParams`, beside the CHOKE line:

```cpp
        inst.set_pull(params[PULL].getValue());     // continuous -1..+1, engine holds the dead zone
```

Check the `configParam` block for how `CHOKE` is declared and give `PULL` the
same bipolar form (`-1.f, 1.f, 0.f`) with the tooltip text
`"Chord gravity: left = A leads, right = B leads"`.

- [ ] **Step 6: Check the bench mirror**

Read `bench/audition/init_patch.cpp`. It mirrors Rack's `pushParams`; a new
control that boots at 0 needs no arm there, but per the init re-bake checklist
this file goes silently stale, so confirm by eye that nothing per-engine
changed and note the check in the commit message. Do not edit it unless the
snapshot changed which ENGINE boots (it did not).

- [ ] **Step 7: Build the plugin**

```bash
host/vcv/build-local.sh
```
Expected: builds clean. Never invoke `g++` by hand here.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/Fireflow.cpp host/vcv/res/Fireflow.svg host/vcv/src/generated_panel.hpp host/vcv/src/init_patch.hpp
git commit -m "feat(vcv): PULL joins the centre under CHOKE"
```

---

### Task 7: PULL on the 60 HP plate

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` (`HW_CLASS`, `HW_CAPTION`,
  `CENTER_POS`)
- Run: `host/vcv/res/test_hw_panel.py`

**Interfaces:**
- Consumes: the `PULL` control from Task 6 — `test_hw_panel.py` asserts that
  `HW_PARAMS` mirrors `gp.RUNTIME_PANEL_PARAMS` name for name and order, so
  this task is not optional once Task 6 lands.
- Produces: a placed `PULL` knob, size class `S`, plate word `PULL`.

- [ ] **Step 1: Add the three table entries**

In `host/vcv/res/gen_hw_panel.py`:

```python
    "MORPH": "G", "TIDE": "S", "CHOKE": "S", "PACE": "S", "PULL": "S",
```

`HW_CAPTION` needs no entry — `PULL` is already four characters and the
generator falls back to the enum name. Add it anyway only if the guard
complains.

In `CENTER_POS`, the free left slot of the ROOM band's lower row:

```python
    "REV_TONE": (152.40, 97.00), "PULL": (136.40, 97.00),
```

Measured 2026-08-22 from the generator's own tables: the centre column runs
x = 136.40 / 152.40 / 168.40 with the lower ROOM row at y = 97.00 holding only
`REV_TONE`; `KEEP_BOT` is 119.5 and an `S` knob's radius is 6.0, so the slot
clears both rails. **The slot is a placement proposal, not a settled one** —
the guard proves it fits, not that it belongs there; flag it for Bastian in
the handover.

- [ ] **Step 2: Regenerate and guard**

```bash
python host/vcv/res/gen_hw_panel.py
python host/vcv/res/test_hw_panel.py
```
(from `host/vcv/`, via a scratchpad script)

Expected: the generator prints `params=… inputs=12 outputs=6 lights=19
panel=60HP` with the param count one higher than before, and the guard passes
`test_same_runtime_params_same_order`, `test_hardware_footprints`,
`test_rail_keepout` and `test_no_overlap_with_hw_radii`.

If `test_no_overlap_with_hw_radii` or `test_rail_keepout` fires, move the slot
— the guard is the authority on geometry, and no number in this plan overrides
what it prints.

- [ ] **Step 3: Build the plugin**

```bash
host/vcv/build-local.sh
```
Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -m "feat(hw-panel): PULL takes the free ROOM-row slot"
```

---

### Task 8: The spec's five acceptance checks, as engine gates

**Files:**
- Modify: `tests/test_pull.cpp` (append)

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: nothing new; this task closes the spec's "Testing" section.

Spec checks 1, 2 and 3 are already covered by Tasks 1, 3 and 4. The two that
are not are check 4 (a chord change under a sounding bound note settles within
~40 ms) and check 5 (leader COLOR 0 + full PULL = unison/octaves).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("pull: a chord change under a bound note settles within 40 ms") {
    // Spec check 4. The leader's mask is swapped by hand rather than by
    // waiting for its next trigger, so the test measures the settle and not
    // the sequencer.
    Part p; stepped(p, 21);
    p.set_gravity(0x0044, 1.f);                    // classes 2, 6
    float l, r;
    for (int i = 0; i < 48000 * 4; ++i) p.process(l, r);
    REQUIRE(p.gravity_bound());
    p.set_gravity(0x0088, 1.f);                    // classes 3, 7
    // 40 ms = 1920 samples at 48k. Give it that and one control tick's slack.
    for (int i = 0; i < 1920 + 96; ++i) p.process(l, r);
    const int semis = static_cast<int>(p.pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
    CAPTURE(semis);
    CHECK(((1u << (semis % 12)) & 0x0088u) != 0u);
}

TEST_CASE("pull: leader COLOR 0 plus full PULL is a unison follow") {
    // Spec check 5, and it is emergent, not coded: at COLOR 0 the leader's
    // "chord" is one pitch class (measured 2026-08-22), so the follower can
    // only land on that class, in whatever octave its own register puts it.
    Instrument in; two_decks(in);
    in.set_color(PART_A, 0.f);
    in.set_pull(-1.f);
    run(in, 4.f);
    const uint16_t lead = in.part(PART_A).chord_pc_mask();
    REQUIRE(lead != 0u);
    CHECK(popcount12(lead) == 1);
    const int semis = static_cast<int>(
        in.part(PART_B).pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
    CAPTURE(semis); CAPTURE(lead);
    CHECK(((1u << (semis % 12)) & lead) != 0u);
}
```

- [ ] **Step 2: Run them**

```bash
cmake --build build --target spky_tests
./build/spky_tests -tc="pull: a chord change*,pull: leader COLOR 0*"
```
Expected: PASS (the implementation is complete by now — these two document the
spec's acceptance criteria rather than driving new code). If either fails, that
is a real finding: fix the engine, not the test.

- [ ] **Step 3: Prove both can go red**

Mutate `_quant.set_gravity(_grav_mask, _grav_bound);` in `_control_tick` to
`_quant.set_gravity(_grav_mask, false);`, rebuild, run both cases.
Expected: both FAIL. Revert, rebuild, green.

- [ ] **Step 4: Full suite**

```bash
ctest --test-dir build --output-on-failure
```
Expected: green, all of it.

- [ ] **Step 5: Commit**

```bash
git add tests/test_pull.cpp
git commit -m "test(pull): the spec's two remaining acceptance checks"
```

---

### Task 9: Say what shipped

**Files:**
- Modify: `docs/roadmap.md` (the M5l row, and the milestone-order paragraph
  under the table)
- Modify: `docs/engine-map.md` (a new subsection under §7, where the melody's
  reachability lives)
- Modify: `docs/by-ear-decisions.md`
- Modify: `host/vcv/plugin.json`, `docs/release-notes.md`

- [ ] **Step 1: Roadmap**

Change the M5l row's status from `⬜ **planned** (spec ready; not implemented)`
to a done row in the house style: what it is, which spec and plan, which
commit, which release, and — honestly — what is still open. At minimum the two
open items are: PULL has had **no listening pass**, and `kPullDead = 0.03` is a
first-try number. Also update the paragraph under the table that lists "the
engine-level milestones still completable without the target hardware" — it
names AIR, M5k and M5l.

- [ ] **Step 2: Engine map**

Add a subsection to `docs/engine-map.md` §7 recording the two measurements this
plan rests on, because the next session should not have to re-measure them:

- the six-engine × two-mode quantizer-reach table from "What was measured"
  above (which is also the answer to "where is PULL inert?");
- the live-follow A/B result (24 000/24 000 samples differ, max |d| 0.544) with
  its setup: `Part`, SYNTH, FLOW, `trigger_manual()`, mask swapped 100 ms in,
  24 000 samples compared.

State seed, rate, duration and construction order beside every number, as §6
requires.

- [ ] **Step 3: By-ear list**

Add `kPullDead` (0.03) and the probability curve (linear in |PULL| after the
dead-zone rescale) to `docs/by-ear-decisions.md` as **untuned first-try
values** — the file's job is to stop a later session from "finishing" a tuned
number, and it is equally useful as a record of which numbers were never
heard at all. Say plainly that no listening pass has happened.

- [ ] **Step 4: Release**

Bump `host/vcv/plugin.json` to the next patch version and rewrite
`docs/release-notes.md` for *this* release (it is the release body, not a
changelog). Then:

```bash
git add docs/roadmap.md docs/engine-map.md docs/by-ear-decisions.md host/vcv/plugin.json docs/release-notes.md
git commit -m "release: PULL, chord gravity between the decks"
```

Tagging is Bastian's call, not the executor's — do not push a tag.

---

## Self-review

**Spec coverage.** Every section of the spec maps to a task: the Decision
paragraph → Tasks 4 (sign, magnitude, dead zone) and 1 (structural off); "The
pull is a quantizer mask override" → Task 1; per-note decision at fire time →
Task 3; bound notes follow live → Task 3's per-tick push, with the measurement
that proves it possible; any octave → falls out of a pitch-class mask, asserted
in Task 3; FREE not exempt → Task 1; COLOR 0 unison → Task 8; Wiring 1/2/3 →
Tasks 2/4/3; Edge cases → leader choked (the mask is cached and does not
expire, Task 2), scale/root changes (re-derived every tick, Task 2), sweep
through centre (Task 4), STEP sustain untouched (nothing in this plan touches
timing); Host/panel → Tasks 6 and 7; Testing 1–5 → Tasks 1, 3, 4 and 8.

**Two spec statements this plan deliberately does not implement as written:**
"Read-only; no new state" (D1 — one cached `uint16_t`) and the implicit
assumption that every deck can lead (D2 — SAMPLER and BBD publish nothing).
Both are recorded above with their reasons.

**Placeholder scan.** No step says "add error handling", "similar to task N",
or "write tests for the above"; every code step carries the code. The one
deliberately open branch is Task 5 step 5, where the correct edit depends on
what the parameter-impact gate prints — and that step says to run it and read
it rather than to guess.

**Type consistency.** `set_gravity(uint16_t, bool)` on `Quantizer` and
`set_gravity(uint16_t, float)` on `Part` are different signatures on different
types, deliberately: the quantizer takes a decision, the part takes a
probability. `chord_pc_mask()`, `gravity_bound()`, `gravity_on()`, `pc_mask12()`
and `kPullDead` are spelled the same in every task that uses them.
