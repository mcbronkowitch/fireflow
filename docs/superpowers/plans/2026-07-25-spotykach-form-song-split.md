# Spotykach FORM / SONG Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the phrase engine and A/B arrangement into independent FORM and
SONG controls, add the approved fixed and dynamic song sequences, remove the VCV
TRIG button, migrate existing patches, and install the verified local plugin.

**Architecture:** Keep the existing two persistent `MelodyPattern` snapshots and
turnaround derivation. Replace the mixed `FormMode` controller with a direct
`Principle` phrase-engine selection plus a new `SongMode`; one pure lookup maps a
monotonic phrase index to A or B. The portable engine owns musical state, while
the VCV host maps stable parameter IDs, performs legacy JSON migration, and
generates the faceplate from `gen_panel.py`.

**Tech Stack:** C++17 portable engine, doctest, CMake + Clang + Ninja, JSON render
scenarios, Python panel generator/guard, VCV Rack SDK 2.6.6, MSYS2 make, and
WinLibs GCC.

## Global Constraints

- The approved design is
  `docs/superpowers/specs/2026-07-25-spotykach-form-song-split-design.md`.
- Work on branch `codex/song-pattern-chaining` in
  `.worktrees/song-pattern-chaining`; baseline commit is `6c6a215`.
- FORM has exactly five values in this order: `TWO MOTIFS`, `ONE + VAR`,
  `HIERARCHICAL`, `CALL / RESPONSE`, `OSTINATO`.
- SONG has exactly seven values in this order: `AAAB`, `ABAB`, `ABBB`, `BUILD`,
  `ROTATE`, `MIRROR`, `OFF`.
- Factory defaults are `FORM = HIERARCHICAL` and `SONG = AAAB` for both Parts.
- SONG only selects persistent A/B snapshots; it never mutates musical content
  or consumes random draws.
- FORM, SONG, NEW, and effective STEPS changes become audible only at a melodic
  STEP phrase boundary; the final write before the boundary wins.
- SONG-only changes preserve A/B and reset the phrase index. FORM, NEW, and
  effective STEPS changes rebuild A/B and reset the phrase index.
- NEW always queues a fresh A/B pair. On a Sampler Part it additionally punches
  the sampler immediately.
- FLOW pauses A/B evolution and SONG position. Non-melodic lanes do not run SONG.
- Keep exactly two pattern identities. Do not add C, probability-based modes, or
  a SONG variation amount.
- All portable state stays fixed-size and allocation-free. All randomness stays
  in the lane `Rng`.
- Keep the whole-instrument ARM/Daisy `ModLane` increase below 5 KiB from the
  recorded 412-byte pre-song baseline: `(sizeof(ModLane) - 412) * 10 < 5120`.
- Keep `PART_STRIDE == 23`, `NUM_PARAMS`, and every parameter's numeric index
  stable. Rename fixed enum slots; never insert or append a new strided control.
- `PRINCIPLE_A/B` numeric slots become FORM, `TRIGGER_A/B` numeric slots become
  SONG, and `NEWPHRASE_A/B` keep their numeric slots.
- Generate `host/vcv/src/generated_panel.hpp` and
  `host/vcv/res/Spotymod.svg` from `host/vcv/res/gen_panel.py`; never edit either
  generated artifact by hand.
- Do not serialize live A/B notes, grooves, or the runtime SONG phrase index.
- Preserve the legacy JSON readers for both released `principle` and beta
  `lastBasis`; new saves write `formSongVersion = 1`.
- Do not bump the plugin version as part of this change; release versioning is a
  separate decision.

---

## File Structure

- `engine/mod/song_form.h` — FORM/SONG enums, clamping, pure symbol lookup, two
  snapshots, turnaround, and pattern-groove helpers.
- `engine/mod/lane.h`, `engine/mod/lane.cpp` — boundary-safe FORM/SONG lifecycle,
  outgoing evolution, A/B selection, FLOW pause, and shared process/tick path.
- `engine/mod/super_modulator.h`, `engine/instrument.h` — portable FORM/SONG API
  and test observability.
- `host/render/scenario.cpp` — dispatch `set_form`, `set_song`, and legacy
  `set_principle`.
- `host/render/scenarios/demo_song_aaab.json` — keep the focused AAAB/GROW/RENEW
  audition using the split API.
- `host/render/scenarios/demo_song_modes.json` — audition all seven SONG modes.
- `tests/test_song_form.cpp` — pure sequence, clamping, turnaround, storage, and
  determinism tests.
- `tests/test_song_lane.cpp`, `tests/test_lane_tick.cpp` — lane boundary,
  persistence, reset, evolution, FLOW, and process/tick tests.
- `tests/test_instrument.cpp`, `tests/test_scenario.cpp` — portable public API and
  renderer dispatch/scenario coverage.
- `host/vcv/res/gen_panel.py` — sole source for parameter names, stable ordering,
  mirrored geometry, labels, and generated artifacts.
- `host/vcv/res/test_panel.py` — static panel/host/migration contract guard.
- `host/vcv/src/init_patch.hpp` — stable-index factory parameter defaults.
- `host/vcv/src/Spotymod.cpp` — Rack parameter configuration, engine forwarding,
  NEW behavior, reset, and JSON migration.
- `host/vcv/README.md` — user-visible FORM, SONG, NEW, and removed-TRIG behavior.

---

### Task 1: Add SONG Modes and Pure Symbol Lookup

**Files:**
- Modify: `engine/mod/song_form.h`
- Modify: `tests/test_song_form.cpp`

**Interfaces:**
- Consumes: existing `MelodyPattern`, `SongForm`, and turnaround helpers.
- Produces:
  - `enum class SongMode : uint8_t`
  - `SongMode clamp_song(int value)`
  - `uint8_t song_symbol_at(SongMode mode, uint32_t phrase_index)`

- [ ] **Step 1: Replace the fixed-AAAB helper test with table-driven failing tests**

In `tests/test_song_form.cpp`, add this helper beside the existing anonymous
namespace helpers:

```cpp
std::string song_text(SongMode mode, uint32_t count) {
    std::string result;
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        result.push_back(song_symbol_at(mode, i) == 0 ? 'A' : 'B');
    return result;
}
```

Replace `TEST_CASE("song form is exactly AAAB")` with:

```cpp
TEST_CASE("song modes clamp and produce the approved sequences") {
    CHECK(clamp_song(-7) == SongMode::AAAB);
    CHECK(clamp_song(99) == SongMode::Off);

    CHECK(song_text(SongMode::AAAB, 8) == "AAABAAAB");
    CHECK(song_text(SongMode::ABAB, 8) == "ABABABAB");
    CHECK(song_text(SongMode::ABBB, 8) == "ABBBABBB");
    CHECK(song_text(SongMode::Build, 32) ==
          "AAABAABBABBBAABBAAABAABBABBBAABB");
    CHECK(song_text(SongMode::Rotate, 32) ==
          "AAABAABAABAABAAAAAABAABAABAABAAA");
    CHECK(song_text(SongMode::Off, 32) ==
          "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
}

TEST_CASE("MIRROR is the deterministic Thue-Morse stream") {
    CHECK(song_text(SongMode::Mirror, 64) ==
          "ABBABAABBAABABBABAABABBAABBABAAB"
          "BAABABBAABBABAABABBABAABBAABABBA");

    const uint32_t indices[] = {
        0u, 1u, 2u, 3u, 31u, 255u, 0x12345678u, 0xffffffffu
    };
    for (const uint32_t index : indices) {
        uint32_t bits = index;
        uint8_t parity = 0;
        while (bits != 0u) {
            parity ^= static_cast<uint8_t>(bits & 1u);
            bits >>= 1u;
        }
        CHECK(song_symbol_at(SongMode::Mirror, index) == parity);
    }
}
```

Add `#include <string>` at the top.

- [ ] **Step 2: Run the helper tests and confirm they fail**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song modes*,MIRROR*"
```

Expected: compilation fails because `SongMode`, `clamp_song`, and the new lookup
signature do not exist.

- [ ] **Step 3: Add the dedicated enum and lookup without removing `FormMode` yet**

In `engine/mod/song_form.h`, keep the current mixed `FormMode` temporarily so
the lane remains buildable during this task. Add:

```cpp
enum class SongMode : uint8_t {
    AAAB = 0,
    ABAB,
    ABBB,
    Build,
    Rotate,
    Mirror,
    Off,
    kCount
};

inline SongMode clamp_song(int value) {
    if (value < 0) value = 0;
    const int last = static_cast<int>(SongMode::kCount) - 1;
    if (value > last) value = last;
    return static_cast<SongMode>(value);
}
```

Replace the one-argument `song_symbol_at` with:

```cpp
inline uint8_t song_symbol_at(SongMode mode, uint32_t phrase_index) {
    static constexpr uint8_t fixed[3][4] = {
        {0, 0, 0, 1},
        {0, 1, 0, 1},
        {0, 1, 1, 1}
    };
    static constexpr uint8_t build[16] = {
        0,0,0,1, 0,0,1,1, 0,1,1,1, 0,0,1,1
    };
    static constexpr uint8_t rotate[16] = {
        0,0,0,1, 0,0,1,0, 0,1,0,0, 1,0,0,0
    };

    const SongMode clamped = clamp_song(static_cast<int>(mode));
    const int value = static_cast<int>(clamped);
    if (value <= static_cast<int>(SongMode::ABBB))
        return fixed[value][phrase_index & 3u];
    if (clamped == SongMode::Build)
        return build[phrase_index & 15u];
    if (clamped == SongMode::Rotate)
        return rotate[phrase_index & 15u];
    if (clamped == SongMode::Off)
        return 0u;

    uint8_t parity = 0;
    while (phrase_index != 0u) {
        parity ^= static_cast<uint8_t>(phrase_index & 1u);
        phrase_index >>= 1u;
    }
    return parity;
}
```

- [ ] **Step 4: Run all song-form helper tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song modes*,MIRROR*,song zones*,cell groove*,song turnaround*,song cadence*,pattern groove*"
```

Expected: all selected cases pass.

- [ ] **Step 5: Commit the independently usable SONG mapping**

```powershell
git add engine/mod/song_form.h tests/test_song_form.cpp
git commit -m "feat(mod): add fixed and dynamic song arrangements"
```

---

### Task 2: Split FORM and SONG in the Lane Lifecycle

**Files:**
- Modify: `engine/mod/song_form.h`
- Modify: `engine/mod/lane.h`
- Modify: `engine/mod/lane.cpp`
- Modify: `tests/test_song_form.cpp`
- Modify: `tests/test_song_lane.cpp`
- Modify: `tests/test_lane_tick.cpp`

**Interfaces:**
- Consumes:
  - `SongMode`
  - `clamp_song(int)`
  - `song_symbol_at(SongMode, uint32_t)`
  - existing `generate_phrase`, `derive_turnaround`, and cadence helpers.
- Produces:
  - `void ModLane::set_form(Principle form)`
  - `Principle ModLane::form() const`
  - `void ModLane::set_song(SongMode song)`
  - `SongMode ModLane::song() const`
  - `uint32_t ModLane::song_position() const`
  - always-persistent melodic STEP snapshots A/B.

- [ ] **Step 1: Rewrite the lane test fixture for split controls**

In `tests/test_song_lane.cpp`, replace the `set_last_basis` and mixed FORM setup
inside `make_song_lane` with:

```cpp
lane.set_form(Principle::Hierarchical);
lane.set_song(SongMode::AAAB);
```

Change all stored song-position variables and comparisons from `uint8_t` to
`uint32_t`. Update the first LOOP case to retain its exact AAAB assertion.

- [ ] **Step 2: Add a failing SONG-only boundary and snapshot-preservation case**

Append:

```cpp
TEST_CASE("SONG-only change is boundary-safe and preserves both snapshots") {
    ModLane lane = make_song_lane(0x50A6u);
    drive_to_step(lane, 3);
    const MelodyPattern before_a = lane.pattern_for_test(0);
    const MelodyPattern before_b = lane.pattern_for_test(1);

    lane.set_song(SongMode::ABAB);
    CHECK(lane.song() == SongMode::AAAB);
    CHECK(same_pattern(lane.pattern_for_test(0), before_a));
    CHECK(same_pattern(lane.pattern_for_test(1), before_b));

    drive_to_wrap(lane);
    CHECK(lane.song() == SongMode::ABAB);
    CHECK(lane.song_position() == 0u);
    CHECK(lane.active_pattern() == 0u);
    CHECK(same_pattern(lane.pattern_for_test(0), before_a));
    CHECK(same_pattern(lane.pattern_for_test(1), before_b));

    drive_to_wrap(lane);
    CHECK(lane.song_position() == 1u);
    CHECK(lane.active_pattern() == 1u);
}
```

- [ ] **Step 3: Add failing lifecycle cases for FORM, NEW, STEPS, and final writes**

Replace the old normal-to-SONG/leaving-SONG cases with tests that establish:

```cpp
TEST_CASE("FORM rebuilds A and B at the wrap and restarts SONG") {
    ModLane lane = make_song_lane(0xF04Du);
    lane.set_song(SongMode::Rotate);
    drive_to_wrap(lane); // apply Rotate at index 0
    drive_to_wrap(lane); // index 1
    const MelodyPattern old_a = lane.pattern_for_test(0);
    const MelodyPattern old_b = lane.pattern_for_test(1);

    lane.set_form(Principle::Ostinato);
    CHECK(lane.form() == Principle::Hierarchical);
    drive_to_wrap(lane);

    CHECK(lane.form() == Principle::Ostinato);
    CHECK(lane.song() == SongMode::Rotate);
    CHECK(lane.song_position() == 0u);
    CHECK(lane.active_pattern() == 0u);
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0), old_a));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(1), old_b));
}
```

Keep and adapt the existing NEW and effective-length tests so both require
`song_position() == 0u`. In the combined-write case, write intermediate and
final FORM/SONG values to one lane and only the final values to its equal-seed
twin; after the wrap require byte-identical snapshots and equal active symbols.

- [ ] **Step 4: Add failing exact lane-stream cases for all seven SONG modes**

Add a helper:

```cpp
std::vector<uint8_t> collect_symbols(ModLane lane, int count) {
    std::vector<uint8_t> symbols;
    symbols.push_back(lane.active_pattern());
    while (static_cast<int>(symbols.size()) < count) {
        drive_to_wrap(lane);
        symbols.push_back(lane.active_pattern());
    }
    return symbols;
}
```

Add a table that checks 32 symbols for AAAB, ABAB, ABBB, BUILD, ROTATE, and OFF
against `song_symbol_at`, and 64 symbols for MIRROR. OFF must select only A while
both stored snapshots remain byte-identical at LOOP. Configure variation to LOOP
so snapshot evolution cannot obscure arrangement behavior.

- [ ] **Step 5: Run the lane-focused tests and confirm the split API fails**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="SONG-only*,FORM rebuilds*,song LOOP*,song streams*,song pending*,FLOW pauses*,pre-roll FORM*"
```

Expected: compilation fails on the new `set_form(Principle)`, `set_song`, and
`song` interfaces.

- [ ] **Step 6: Replace mixed FORM state with independent fields**

In `engine/mod/song_form.h`, remove `FormMode`, `clamp_form`,
`form_basis`, and `form_for_principle`. Change the controller fields to:

```cpp
struct SongForm {
    MelodyPattern patterns[2] = {};
    uint32_t phrase_index = 0;
    uint8_t active_pattern = 0;
    Principle selected_form = Principle::Hierarchical;
    Principle pending_form = Principle::Hierarchical;
    SongMode selected_song = SongMode::AAAB;
    SongMode pending_song = SongMode::AAAB;
    bool form_pending = false;
    bool song_pending = false;
    bool new_pending = false;
    bool length_pending = false;
    uint8_t cadence_slot = 0;
    float bound_a_opening = 0.f;
};
```

Keep the two patterns first so the existing POD snapshot assumptions remain
easy to audit.

- [ ] **Step 7: Expose split lane setters and observability**

In `engine/mod/lane.h`, replace the mixed methods with:

```cpp
void set_form(Principle form);
Principle form() const { return _song.selected_form; }
void set_song(SongMode song);
SongMode song() const { return _song.selected_song; }
uint32_t song_position() const { return _song.phrase_index; }
uint8_t active_pattern() const { return _song.active_pattern; }
void set_principle(Principle p) { set_form(p); }
```

Remove `last_basis` and `set_last_basis`. Rename private lifecycle helpers:

```cpp
void _apply_pending_song_work();
void _advance_song();
```

Remove `_capture_active_as_a`, `_generate_normal_pattern`, and `_song_active`;
melodic STEP playback now always uses the persistent A/B controller.

- [ ] **Step 8: Make initialization always construct the selected A/B pair**

In `ModLane::init`, apply pending FORM and SONG selections before generation,
reset `phrase_index` and `active_pattern`, and for melodic lanes:

```cpp
_generate_pattern_a();
_derive_pattern_b();
```

`_generate_pattern_a` must pass `_song.selected_form` directly to
`generate_phrase`. Non-melodic initialization retains `_fill_walk`.

Clear all four pending flags at the end of initialization. If an effective
STEPS value is supplied after initialization but before the first sample,
pre-roll work rebuilds at that length before step 0 fires.

- [ ] **Step 9: Implement independent setters**

In `engine/mod/lane.cpp`:

```cpp
void ModLane::set_form(Principle form) {
    int value = static_cast<int>(form);
    if (value < 0) value = 0;
    const int last = static_cast<int>(Principle::kCount) - 1;
    if (value > last) value = last;
    _song.pending_form = static_cast<Principle>(value);
    _song.form_pending = _song.pending_form != _song.selected_form;
}

void ModLane::set_song(SongMode song) {
    _song.pending_song = clamp_song(static_cast<int>(song));
    _song.song_pending = _song.pending_song != _song.selected_song;
}
```

- [ ] **Step 10: Implement the coalesced boundary transition**

Replace `_apply_pending_form_work` with `_apply_pending_song_work`. It must:

1. commit pending FORM and SONG values;
2. compute `rebuild = form_pending || new_pending || length_pending ||
   patterns[1].pattern_groove.len == 0`;
3. when rebuilding, call `_generate_pattern_a`, `_derive_pattern_b`, and
   `_clear_fresh_phrase_state`;
4. reset `phrase_index = 0`;
5. set `active_pattern = song_symbol_at(selected_song, 0)`;
6. bind cadence if that opening symbol is B;
7. clear all pending flags.

A `song_pending` transition with `rebuild == false` must not invoke generation,
derivation, mutation, or RNG.

- [ ] **Step 11: Advance with the pure SONG lookup**

Implement:

```cpp
void ModLane::_advance_song() {
    ++_song.phrase_index;
    const uint8_t incoming =
        song_symbol_at(_song.selected_song, _song.phrase_index);
    if (incoming == 1u) {
        bind_song_cadence(
            _song.patterns[0], _song.patterns[1],
            _song.cadence_slot, _song.bound_a_opening);
    }
    _song.active_pattern = incoming;
}
```

Unsigned overflow is intentional and deterministic.

- [ ] **Step 12: Make every melodic STEP phrase use pattern-long groove**

At `_effective_gate`, `_groove_k`, `_start_note`, `_renew_units`, and
`_mutate_groove`, remove the normal/SONG split. When `_melodic && _step_mode`,
use `_active_pattern().pattern_groove`; `_renew_units` uses
`_song.selected_form` as its principle basis.

Non-melodic and FLOW behavior retain their current cell/walk paths.

- [ ] **Step 13: Simplify the shared wrap lifecycle**

Implement `_wrap_events` in this order:

```cpp
const bool pending =
    _song.form_pending || _song.song_pending ||
    _song.new_pending || _song.length_pending;

if (_melodic && !_step_mode)
    return;

_evolve_outgoing_pattern();
if (_melodic && _step_mode) {
    if (pending) _apply_pending_song_work();
    else         _advance_song();
}
```

Keep the existing non-melodic evolution path after the melodic branch. Update
`_apply_preroll_work` so FORM, SONG, NEW, or length work is applied before the
first audible STEP.

- [ ] **Step 14: Reframe the obsolete normal-mode golden test**

In `tests/test_song_form.cpp`, delete the fixed hash
`0x68396762664c16f8ull`, because standalone normal playback no longer exists.
Replace it with equal-seed pairs for every `Principle`, steps
`{1, 8, 16, 32}`, and variations `{-1, 0, 1}`. Drive 1,600 samples and require
bit-identical output, target, fired, wrapped, gate, song position, active
pattern, and both pattern snapshots between each pair.

Keep the storage-budget test and change only its wording from migration to split.

- [ ] **Step 15: Adapt the process/tick SONG fixture**

In `tests/test_lane_tick.cpp`, replace:

```cpp
lane.set_last_basis(Principle::Hierarchical);
lane.set_form(FormMode::SongAAAB);
```

with:

```cpp
lane.set_form(Principle::Hierarchical);
lane.set_song(SongMode::AAAB);
```

Add one MIRROR timeline to the existing process/tick comparison and compare
`song_position()` as `uint32_t`.

- [ ] **Step 16: Run the complete lane and song suite**

Run twice:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song*,SONG*,FORM*,FLOW pauses*,pre-roll*,steps changes*,tick:*"
.\build\spky_tests.exe --test-case="song*,SONG*,FORM*,FLOW pauses*,pre-roll*,steps changes*,tick:*"
```

Expected: both runs pass with identical case/assertion counts.

- [ ] **Step 17: Commit the lifecycle split**

```powershell
git add engine/mod/song_form.h engine/mod/lane.h engine/mod/lane.cpp tests/test_song_form.cpp tests/test_song_lane.cpp tests/test_lane_tick.cpp
git commit -m "feat(mod): split phrase form from song lifecycle"
```

---

### Task 3: Expose FORM / SONG Through Instrument and Render Hosts

**Files:**
- Modify: `engine/mod/super_modulator.h`
- Modify: `engine/instrument.h`
- Modify: `host/render/scenario.cpp`
- Modify: `tests/test_instrument.cpp`
- Modify: `tests/test_scenario.cpp`
- Modify: `host/render/scenarios/demo_song_aaab.json`
- Create: `host/render/scenarios/demo_song_modes.json`

**Interfaces:**
- Consumes: split `ModLane` FORM/SONG API from Task 2.
- Produces:
  - `Instrument::set_form(int part, int form)`
  - `Instrument::set_song(int part, int song)`
  - `Instrument::form(int part) const`
  - `Instrument::song(int part) const`
  - renderer action `set_song`.

- [ ] **Step 1: Add failing public-API tests**

Replace the current `"instrument FORM API..."` case with a split test that:

```cpp
Instrument inst;
inst.init(48000.f);
for (int part = 0; part < PART_COUNT; ++part) {
    inst.set_form(part, static_cast<int>(Principle::CallResponse));
    inst.set_song(part, static_cast<int>(SongMode::Rotate));
    inst.set_step(part, true, 8);
}
float l = 0.f, r = 0.f;
inst.process(nullptr, nullptr, &l, &r, 1);
CHECK(inst.form(PART_A) == static_cast<int>(Principle::CallResponse));
CHECK(inst.form(PART_B) == static_cast<int>(Principle::CallResponse));
CHECK(inst.song(PART_A) == static_cast<int>(SongMode::Rotate));
CHECK(inst.song(PART_B) == static_cast<int>(SongMode::Rotate));
```

Add a second instance with FORM values `-7` and `99` and SONG values `-7` and
`99`; require `TwoMotif`, `Ostinato`, `AAAB`, and `Off` respectively.

- [ ] **Step 2: Add a failing renderer dispatch test**

Replace the current song-form scenario action test with:

```cpp
Event form;
form.action = "set_form";
form.part = PART_A;
form.ivalue = static_cast<int>(Principle::Ostinato);

Event song;
song.action = "set_song";
song.part = PART_A;
song.ivalue = static_cast<int>(SongMode::Mirror);
```

Apply both, enable STEP, process one sample, and require the corresponding
getters. Retain a `set_principle` event and require it to behave as a FORM
compatibility wrapper.

- [ ] **Step 3: Run the API tests and confirm they fail**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="instrument FORM / SONG*,scenario: FORM / SONG*"
```

Expected: compilation fails because the public SONG methods and action do not
exist.

- [ ] **Step 4: Wire SuperModulator**

In `engine/mod/super_modulator.h`, expose:

```cpp
void set_form(Principle form) { _lanes[LANE_PITCH].set_form(form); }
void set_song(SongMode song) { _lanes[LANE_PITCH].set_song(song); }
Principle form() const { return _lanes[LANE_PITCH].form(); }
SongMode song() const { return _lanes[LANE_PITCH].song(); }
void set_principle(Principle p) { set_form(p); }
```

Remove `set_last_basis` and `last_basis`. Return `uint32_t` from the testing-only
song-position accessor.

- [ ] **Step 5: Wire Instrument with explicit clamping**

In `engine/instrument.h`, implement:

```cpp
void set_form(int p, int form) {
    if (form < 0) form = 0;
    const int last = static_cast<int>(Principle::kCount) - 1;
    if (form > last) form = last;
    _parts[p].mod().set_form(static_cast<Principle>(form));
}

void set_song(int p, int song) {
    _parts[p].mod().set_song(clamp_song(song));
}

int form(int p) const {
    return static_cast<int>(_parts[p].mod().form());
}

int song(int p) const {
    return static_cast<int>(_parts[p].mod().song());
}
```

Make `set_principle` delegate to `set_form`. Remove beta-only `set_last_basis`
and `last_basis`.

- [ ] **Step 6: Add renderer SONG dispatch**

In `host/render/scenario.cpp`, keep:

```cpp
else if (a == "set_form")      inst.set_form(e.part, e.ivalue);
else if (a == "set_principle") inst.set_principle(e.part, e.ivalue);
```

Add between them:

```cpp
else if (a == "set_song") inst.set_song(e.part, e.ivalue);
```

Remove `set_last_basis`.

- [ ] **Step 7: Update the focused AAAB scenario**

In `host/render/scenarios/demo_song_aaab.json`:

- remove `set_last_basis`;
- change `set_form` to `ivalue: 2` for HIERARCHICAL;
- add `{"action": "set_song", "part": 0, "ivalue": 0}`;
- update the comment so FORM is the phrase engine and SONG is AAAB.

Update its test in `tests/test_scenario.cpp` to require FORM 2 and SONG 0.

- [ ] **Step 8: Add an all-modes audition scenario**

Create `host/render/scenarios/demo_song_modes.json` with 48 kHz, 240 BPM,
16 STEPS, HIERARCHICAL FORM, LOOP variation, and a silent Part B. Schedule:

```text
t=0:   SONG AAAB
t=16:  SONG ABAB
t=32:  SONG ABBB
t=48:  SONG BUILD
t=112: SONG ROTATE
t=176: SONG MIRROR
t=240: SONG OFF
```

Set `duration_s` to 256 so each fixed mode and OFF get one four-phrase cycle and
each dynamic mode gets one complete 16-phrase listening window. Use the same
Part A synth and Part B silencing setup as `demo_song_aaab.json`.

Add a scenario test that loads the file, requires `duration_s == 256`, requires
FORM 2, STEP 16, LOOP variation, and sees each `set_song` integer `0..6` exactly
once across init plus timeline.

- [ ] **Step 9: Run API and scenario tests**

Run:

```powershell
cmake --build build --target spky_tests render
.\build\spky_tests.exe --test-case="instrument FORM / SONG*,scenario: FORM / SONG*,AAAB listening*,SONG modes listening*"
```

Expected: all selected cases pass and `render.exe` links.

- [ ] **Step 10: Commit the portable/renderer boundary**

```powershell
git add engine/mod/super_modulator.h engine/instrument.h host/render/scenario.cpp tests/test_instrument.cpp tests/test_scenario.cpp host/render/scenarios/demo_song_aaab.json host/render/scenarios/demo_song_modes.json
git commit -m "feat(engine): expose phrase form and song controls"
```

---

### Task 4: Replace VCV TRIG With the SONG Knob

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/res/gen_panel.py`
- Regenerate: `host/vcv/src/generated_panel.hpp`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Modify: `host/vcv/src/init_patch.hpp`
- Modify: `host/vcv/src/Spotymod.cpp`

**Interfaces:**
- Consumes: `Instrument::set_form` and `Instrument::set_song`.
- Produces: stable-index VCV PLAY row `STEP · FORM · SONG · NEW`, correct factory
  defaults, and unified NEW behavior.

- [ ] **Step 1: Change the panel contract tests first**

In `host/vcv/res/test_panel.py`:

- rename fixed-order `PRINCIPLE_A/B` expectations to `FORM_A/B`;
- rename fixed-order `TRIGGER_A/B` expectations to `SONG_A/B`;
- keep their list positions unchanged;
- set Part A geometry to:

```python
'FORM_A': (56.50, 103.60),
'SONG_A': (67.00, 103.60),
'NEWPHRASE_A': (77.50, 103.60),
```

- require mirrored Part B geometry;
- require FORM kind `KNOBI`, label/tip `FORM`;
- require SONG kind `KNOBI`, label/tip `SONG`;
- require NEW kind `SMBTN`, label/tip `NEW`;
- require `PART_STRIDE == 23`.

Replace the old six-value FORM source fragment with exact guards for:

```cpp
configSwitch(c.id, 0.f, 4.f, init, "Form",
             {"TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
              "CALL / RESPONSE", "OSTINATO"});

configSwitch(c.id, 0.f, 6.f, init, "Song",
             {"AAAB", "ABAB", "ABBB", "BUILD", "ROTATE", "MIRROR", "OFF"});
```

- [ ] **Step 2: Add failing runtime guards**

Require these normalized fragments:

```cpp
inst.set_form(p, form);
inst.set_song(p, song);
```

Require NEW to be:

```cpp
if (newPhraseTrig[p].process(ppb(NEWPHRASE_A, p))) {
    inst.new_phrase(p);
    if (samplerPart) inst.sampler_punch(p);
}
```

Reject `triggerTrig`, `TRIGGER_A`, `TRIGGER_B`, and
`inst.trigger_manual(p)` in `Spotymod.cpp`. Update the existing sampler-routing
mutation guard so removing `inst.new_phrase(p)` or the sampler-only condition
causes the Python suite to fail.

Remove the old guard requirements for `kInitLastBasis`, the `lastBasis` member,
its reinit/reset forwarding, and writing or reading `lastBasis` as the current
format. Task 5 will add explicit legacy-read-only migration guards.

- [ ] **Step 3: Run the panel guard and confirm it fails**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: failures report old enum names, old geometry, six-state FORM, absent
SONG configuration, and old TRIG processing.

- [ ] **Step 4: Preserve parameter indices while changing generator names and geometry**

In `host/vcv/res/gen_panel.py`, keep the `part_controls()` return order at 23
entries. Replace the three final records with:

```python
out.append(Ctl("FORM", KNOBI, fx(PAD_X[3]), PLAY_Y, "FORM"))
out.append(Ctl("NEWPHRASE", SMBTN, fx(PAD_X[5]), PLAY_Y, "NEW"))
out.append(Ctl("SONG", KNOBI, fx(PAD_X[4]), PLAY_Y, "SONG"))
```

This deliberate list order preserves the old numeric slots:

```text
old PRINCIPLE -> FORM
old NEWPHRASE -> NEWPHRASE
old TRIGGER   -> SONG
```

Update `PAD_X` comments and tooltip lists. Do not sort these three records by
their screen x-coordinate.

- [ ] **Step 5: Regenerate the panel artifacts**

Run from the VCV directory:

```powershell
Set-Location host/vcv
python res/gen_panel.py
Set-Location ../..
```

Expected: only `generated_panel.hpp` and `Spotymod.svg` change as generated
outputs, with both mirrored rows reading `STEP FORM SONG NEW`.

- [ ] **Step 6: Update indexed factory defaults**

In `host/vcv/src/init_patch.hpp`, rename the two comments per Part and set:

```cpp
2.000000000f, // FORM_A / FORM_B: HIERARCHICAL
0.000000000f, // NEWPHRASE_A / NEWPHRASE_B
0.000000000f, // SONG_A / SONG_B: AAAB
```

Remove `kInitLastBasis`; FORM now stores that selection directly.

- [ ] **Step 7: Configure both snapped switches**

In `Spotymod::configControls`, branch on `FORM_A/B` and `SONG_A/B` using the
exact `configSwitch` calls guarded in Step 1. Keep `snapEnabled = true`.
Update the Part-stride static assertion from TRIGGER to SONG.

- [ ] **Step 8: Forward FORM and SONG every parameter push**

Replace the `lastBasis` block in `pushParams` with:

```cpp
const int form =
    static_cast<int>(std::lround(pp(FORM_A, p)));
const int song =
    static_cast<int>(std::lround(pp(SONG_A, p)));
inst.set_form(p, form);
inst.set_song(p, song);
```

Remove the `lastBasis` member and all reinit/reset forwarding for it.

- [ ] **Step 9: Remove TRIG and unify NEW**

Remove `triggerTrig[2]` and the complete manual-trigger block. Change NEW to:

```cpp
if (newPhraseTrig[p].process(ppb(NEWPHRASE_A, p))) {
    inst.new_phrase(p);
    if (samplerPart) inst.sampler_punch(p);
}
```

Do not remove the portable `Instrument::trigger_manual`; only the VCV surface
and host path are out of scope.

- [ ] **Step 10: Run generator idempotence and panel guards**

Run:

```powershell
Set-Location host/vcv
$before = Get-FileHash src/generated_panel.hpp,res/Spotymod.svg -Algorithm SHA256
python res/gen_panel.py
$after = Get-FileHash src/generated_panel.hpp,res/Spotymod.svg -Algorithm SHA256
for ($i = 0; $i -lt $before.Count; ++$i) {
    if ($before[$i].Hash -ne $after[$i].Hash) {
        throw "panel generation is not idempotent: $($before[$i].Path)"
    }
}
python res/test_panel.py
Set-Location ../..
```

Expected: regeneration produces no second diff and every panel guard passes
except migration/documentation assertions deliberately deferred to Task 5.

- [ ] **Step 11: Build the portable suite to catch renamed-ID leakage**

Run:

```powershell
cmake --build build --target spky_tests render
ctest --test-dir build --output-on-failure
```

Expected: configure state remains valid and all CTest cases pass.

- [ ] **Step 12: Commit the surface and runtime split**

```powershell
git add host/vcv/res/test_panel.py host/vcv/res/gen_panel.py host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg host/vcv/src/init_patch.hpp host/vcv/src/Spotymod.cpp
git commit -m "feat(vcv): add independent song control"
```

---

### Task 5: Migrate Legacy Patches and Document the New Surface

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/src/Spotymod.cpp`
- Modify: `host/vcv/README.md`

**Interfaces:**
- Consumes: stable FORM/SONG param IDs from Task 4.
- Produces: `formSongVersion = 1` persistence marker, legacy migration, and
  user-facing behavior documentation.

- [ ] **Step 1: Add failing persistence source guards**

In `test_panel.py`, require `dataToJson` to contain:

```cpp
json_object_set_new(root, "formSongVersion", json_integer(1));
```

Require `dataFromJson` to:

- read `"formSongVersion"`;
- read beta `"lastBasis"` when the marker is absent;
- otherwise read released `"principle"`;
- clamp migrated FORM to `0..4`;
- use `2` when neither legacy array supplies a value;
- set `params[FORM_A + p * PART_STRIDE]` to the migrated FORM;
- set `params[SONG_A + p * PART_STRIDE]` to `0.f`;
- not write `lastBasis` or `principle` in `dataToJson`.

The test must reject migration that runs when `formSongVersion >= 1`.

- [ ] **Step 2: Add failing README guards**

Have `test_panel.py` read `host/vcv/README.md` and require:

- the row text `STEP · FORM · SONG · NEW`;
- the five FORM display names;
- the seven SONG display names;
- a statement that NEW always rebuilds A/B;
- a statement that Sampler NEW also spawns immediately;
- no description of a visible TRIG button.

- [ ] **Step 3: Run the VCV guard and confirm migration/docs failures**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: failures identify the absent version marker, legacy migration, and
updated documentation.

- [ ] **Step 4: Write only the new format marker**

At the start of `dataToJson`, after creating `root`, add:

```cpp
json_object_set_new(root, "formSongVersion", json_integer(1));
```

Remove the old `lastBasis` serialization. Leave sampler state serialization
unchanged.

- [ ] **Step 5: Implement one-time migration**

At the start of `dataFromJson`, before sampler restoration:

```cpp
json_t* version = json_object_get(root, "formSongVersion");
const bool modern =
    version && json_is_integer(version) &&
    json_integer_value(version) >= 1;

if (!modern) {
    json_t* legacy = json_object_get(root, "lastBasis");
    if (!legacy)
        legacy = json_object_get(root, "principle");

    for (int p = 0; p < spky::PART_COUNT; ++p) {
        int form = 2;
        if (legacy && json_is_array(legacy)) {
            if (json_t* value = json_array_get(legacy, p))
                if (json_is_integer(value))
                    form = static_cast<int>(json_integer_value(value));
        }
        if (form < 0) form = 0;
        if (form > 4) form = 4;
        params[FORM_A + p * PART_STRIDE].setValue(
            static_cast<float>(form));
        params[SONG_A + p * PART_STRIDE].setValue(0.f);
    }
}
```

Do not serialize or restore runtime A/B content or phrase position. On a
live-preset load, the next `pushParams` call forwards the migrated parameters;
on a fresh module load, factory initialization and the first push do the same.

- [ ] **Step 6: Rewrite the VCV README control section**

Document:

```text
FORM: TWO MOTIFS / ONE + VAR / HIERARCHICAL / CALL / RESPONSE / OSTINATO
SONG: AAAB / ABAB / ABBB / BUILD / ROTATE / MIRROR / OFF
PLAY row: STEP · FORM · SONG · NEW
```

Explain BUILD as `AAAB → AABB → ABBB → AABB`, ROTATE as
`AAAB → AABA → ABAA → BAAA`, and MIRROR as deterministic
`ABBA · BAAB · BAAB · ABBA …`. Explain OFF as `AAAA …`: MELODY continues to
evolve A while B remains stored for a seamless return to an active arrangement.
State that NEW rebuilds A/B and restarts SONG; Sampler NEW additionally returns
to ORGANIZE and spawns immediately. Remove the old visible-TRIG description.

- [ ] **Step 7: Run panel, source, migration, and documentation guards**

Run:

```powershell
python host/vcv/res/test_panel.py
git diff --check
```

Expected: panel suite passes and the diff check emits nothing.

- [ ] **Step 8: Commit migration and docs**

```powershell
git add host/vcv/res/test_panel.py host/vcv/src/Spotymod.cpp host/vcv/README.md
git commit -m "fix(vcv): migrate phrase form and song patches"
```

---

### Task 6: Verify, Render, Build, and Install the Local Plugin

**Files:**
- Modify only if a verification failure identifies a concrete defect in files
  already listed by Tasks 1–5.
- Generated render outputs under `renders/` remain ignored and uncommitted.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: clean portable build, deterministic auditions, passing panel guard,
  verified VCV package, and installed live plugin.

- [ ] **Step 1: Configure a fresh canonical Release build**

Run from the worktree root:

```powershell
cmake --fresh -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang++.exe' '-DCMAKE_RC_COMPILER=C:/Program Files/LLVM/bin/llvm-rc.exe' '-DCMAKE_MAKE_PROGRAM=C:/Users/bernd/AppData/Roaming/Python/Python314/Scripts/ninja.exe'
cmake --build build --target spky_tests render
ctest --test-dir build --output-on-failure
```

Expected: configure and build succeed; CTest reports 100% pass.

- [ ] **Step 2: Run the focused deterministic suite twice**

Run:

```powershell
.\build\spky_tests.exe --test-case="song*,SONG*,FORM*,MIRROR*,tick:*,instrument FORM / SONG*,*listening scenario*"
.\build\spky_tests.exe --test-case="song*,SONG*,FORM*,MIRROR*,tick:*,instrument FORM / SONG*,*listening scenario*"
```

Expected: both runs pass with identical case and assertion counts.

- [ ] **Step 3: Reconfirm the recorded ARM/Daisy storage budget**

Run the compiled storage case, which retains the recorded ARM/Daisy baseline of
412 bytes and applies the ten-lane budget formula:

```powershell
.\build\spky_tests.exe --test-case="song storage budget"
```

Expected: the compiled test reports a total delta below 5,120 bytes. Record its
reported `sizeof(ModLane)` and remaining margin in the completion note.

- [ ] **Step 4: Verify panel generation and guards**

Run:

```powershell
Set-Location host/vcv
python res/gen_panel.py
git diff --exit-code -- src/generated_panel.hpp res/Spotymod.svg
python res/test_panel.py
Set-Location ../..
```

Expected: generation is idempotent and the guard reports PASS.

- [ ] **Step 5: Render both auditions twice and compare hashes**

Run:

```powershell
New-Item -ItemType Directory -Force renders | Out-Null
.\build\render.exe host/render/scenarios/demo_song_aaab.json renders/demo_song_aaab.wav renders/demo_song_aaab.csv
.\build\render.exe host/render/scenarios/demo_song_modes.json renders/demo_song_modes.wav renders/demo_song_modes.csv
$first = Get-FileHash renders/demo_song_aaab.wav,renders/demo_song_aaab.csv,renders/demo_song_modes.wav,renders/demo_song_modes.csv -Algorithm SHA256
.\build\render.exe host/render/scenarios/demo_song_aaab.json renders/demo_song_aaab.wav renders/demo_song_aaab.csv
.\build\render.exe host/render/scenarios/demo_song_modes.json renders/demo_song_modes.wav renders/demo_song_modes.csv
$second = Get-FileHash renders/demo_song_aaab.wav,renders/demo_song_aaab.csv,renders/demo_song_modes.wav,renders/demo_song_modes.csv -Algorithm SHA256
for ($i = 0; $i -lt $first.Count; ++$i) {
    if ($first[$i].Hash -ne $second[$i].Hash) {
        throw "nondeterministic render: $($first[$i].Path)"
    }
}
$second | Format-Table Path,Hash
@'
import array
import math
import wave

for path in ("renders/demo_song_aaab.wav", "renders/demo_song_modes.wav"):
    with wave.open(path, "rb") as wav:
        samples = array.array("h", wav.readframes(wav.getnframes()))
    energy = sum(float(sample) * float(sample) for sample in samples)
    rms = math.sqrt(energy / len(samples))
    print(f"{path}: RMS={rms:.3f}")
    if rms <= 1.0:
        raise SystemExit(f"silent render: {path}")
'@ | python -
```

Expected: all four hashes match across renders and each WAV is non-silent.

- [ ] **Step 6: Build the VCV plugin with the verified local toolchain**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd "/c/Users/bernd/Documents/AI/Spotykach/.worktrees/song-pattern-chaining/host/vcv" && export PATH="/c/Users/bernd/Documents/AI/mingw64/bin:/c/msys64/usr/bin:$PATH" && /c/msys64/usr/bin/make RACK_DIR=/c/Users/bernd/Documents/AI/Rack-SDK CC=gcc CXX=g++ TMP="C:/Users/bernd/AppData/Local/Temp" TEMP="C:/Users/bernd/AppData/Local/Temp" SHELL=/usr/bin/bash -j4'
```

Expected: `host/vcv/plugin.dll` builds without errors.

- [ ] **Step 7: Install the package and live directory**

First verify Rack is not running:

```powershell
if (Get-Process Rack -ErrorAction SilentlyContinue) {
    throw "Rack is running; close it before replacing plugin.dll"
}
```

Then run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd "/c/Users/bernd/Documents/AI/Spotykach/.worktrees/song-pattern-chaining/host/vcv" && export PATH="/c/Users/bernd/Documents/AI/mingw64/bin:/c/msys64/usr/bin:$PATH" && /c/msys64/usr/bin/make install RACK_DIR=/c/Users/bernd/Documents/AI/Rack-SDK RACK_USER_DIR=/c/Users/bernd/AppData/Local/Rack2 CC=gcc CXX=g++ TMP="C:/Users/bernd/AppData/Local/Temp" TEMP="C:/Users/bernd/AppData/Local/Temp" SHELL=/usr/bin/bash -j4 && mkdir -p "/c/Users/bernd/AppData/Local/Rack2/plugins-win-x64/Spotymod" && cp -r dist/Spotymod/. "/c/Users/bernd/AppData/Local/Rack2/plugins-win-x64/Spotymod/"'
```

Expected: the `.vcvplugin` archive and expanded live plugin are updated.

- [ ] **Step 8: Verify the installed DLL byte-for-byte**

Run:

```powershell
$built = 'host/vcv/dist/Spotymod/plugin.dll'
$live = Join-Path $env:LOCALAPPDATA 'Rack2/plugins-win-x64/Spotymod/plugin.dll'
$hashes = Get-FileHash $built,$live -Algorithm SHA256
$hashes | Format-Table Path,Hash
if ($hashes[0].Hash -ne $hashes[1].Hash) {
    throw "installed plugin.dll does not match the built distribution"
}
```

Expected: both SHA-256 hashes match.

- [ ] **Step 9: Inspect the final diff and history**

Run:

```powershell
git diff 6c6a215 --check
git status --short
git log --oneline 6c6a215..HEAD
```

Expected: diff check emits nothing, status is clean, and history shows the five
focused implementation commits. Ignored build and render outputs do not appear.

- [ ] **Step 10: Commit only a concrete verification correction**

If Steps 1–9 exposed and fixed a defect, stage only that correction:

```powershell
git add -p
git commit -m "fix(song): satisfy cross-layer verification"
```

If no correction was required, do not create an empty commit.

---

## Execution Notes

- The worktree and branch already exist and contain the approved AAAB
  implementation plus this split design. Do not create another worktree.
- Preserve unrelated user changes if the worktree becomes dirty during
  execution.
- Do not tune turnaround probabilities while implementing the control split.
  The approved A/B derivation remains unchanged.
- At each reviewer checkpoint, compare the implementation to this invariant:
  evolve outgoing snapshot → apply pending work or advance SONG → cadence-bind
  before B → select incoming snapshot before step 0.
- After Task 5, the plugin surface is behaviorally complete. Task 6 is evidence
  and local deployment, not a new feature phase.
