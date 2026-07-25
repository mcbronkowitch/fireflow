# Spotykach Song-Form Pattern Chaining Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent melodic `AAAB` song form whose B pattern keeps A's opening character, develops into a distinct turnaround, and evolves independently under the existing MELODY control.

**Architecture:** Add allocation-free pattern/form helpers beside the phrase generator, then let the melodic `ModLane` own two `MelodyPattern` snapshots and switch them only in the shared wrap path used by `process()` and `tick()`. Keep normal principles on their existing cell groove, route SONG through a full-pattern groove, and expose the six-value form selection to VCV as a snapped parameter while persisting only `last_basis` as associated JSON state.

**Tech Stack:** C++17 portable engine, doctest/CMake tests, VCV Rack C++ API, Python panel generator and guard tests, JSON Rack module state.

## Global Constraints

- The approved design is `docs/superpowers/specs/2026-07-24-spotykach-song-pattern-chaining-design.md`.
- SONG and MELODY remain independent: form selects `A,A,A,B`; RENEW/LOOP/GROW evolves the pattern that just completed after every `STEPS` cycle.
- Pattern A and Pattern B are persistent fixed-size POD snapshots; B is derived once and then evolves independently.
- SONG uses a full-pattern groove; normal forms keep the existing motif-length `GrooveCell` behavior.
- FORM, NEW, and effective STEPS changes become audible only at the next melodic STEP wrap; the last FORM value wins and combined changes coalesce into one rebuild.
- SONG is active only on the melodic PITCH lane in STEP. FLOW pauses form state, and non-melodic lanes never execute SONG behavior.
- VCV STEPS remains snapped to `2..16`; the portable engine must be safe for effective lengths `1..32`.
- Both factory Parts default to `SONG · AAAB`, with `HIERARCHICAL` as `last_basis`.
- Live generated A/B notes are not serialized. FORM is a Rack parameter; `last_basis` is JSON state.
- No heap allocation, virtual dispatch, or libDaisy dependency may enter the generator or sample-critical form path.
- All random draws use the lane `Rng`; form playback and cadence binding consume no random draws.
- Measure `sizeof(ModLane)` before and after. Across ten uniform lanes (five lanes × two Parts), added storage must be less than 5120 bytes.
- Preset compatibility is not required during beta, so replacing the old PRINCIPLE parameter slot is allowed.

---

## File Map

- Create `engine/mod/song_form.h`: form enum/mapping, POD pattern state, pattern-groove expansion/mutation, turnaround derivation, cadence binding, and fixed `AAAB` symbol lookup.
- Modify `engine/mod/lane.h`: replace parallel phrase fields with `SongForm`, expose FORM selection and minimal read-only form observability, and declare wrap helpers.
- Modify `engine/mod/lane.cpp`: route active pattern access, queue boundary-safe changes, implement SONG lifecycle, and preserve the existing non-SONG/FLOW behavior.
- Modify `engine/mod/super_modulator.h` and `engine/instrument.h`: carry FORM and `last_basis` through the portable public API.
- Create `tests/test_song_form.cpp`: deterministic helper, groove, zone, turnaround, cadence, and storage-budget tests.
- Create `tests/test_song_lane.cpp`: AAAB transport, persistence, variation, pending-change, FLOW, determinism, and process/tick tests.
- Modify `CMakeLists.txt`: compile both new doctest files.
- Modify `host/vcv/res/gen_panel.py`: replace PRINCIPLE's small button presentation with a compact integer FORM knob in the same PLAY-row position.
- Regenerate `host/vcv/src/generated_panel.hpp` and `host/vcv/res/Spotymod.svg`: checked-in generated control metadata and faceplate.
- Modify `host/vcv/src/init_patch.hpp`: default both FORM parameters to SONG and both `last_basis` values to Hierarchical.
- Modify `host/vcv/src/Spotymod.cpp`: configure FORM names, push FORM rather than cycling principles, reset/persist `last_basis`, and remove obsolete trigger/index state.
- Modify `host/vcv/res/test_panel.py`: guard FORM geometry/type/names/defaults/persistence and retain STEPS `2..16`.
- Create `host/render/scenarios/demo_song_aaab.json`: a deterministic 16-step HIERARCHICAL/LOOP listening scenario with a later GROW/RENEW section.

---

### Task 1: Allocation-Free Song-Form Helpers

**Files:**
- Create: `engine/mod/song_form.h`
- Create: `tests/test_song_form.cpp`
- Modify: `CMakeLists.txt:36-110`

**Interfaces:**
- Consumes: `spky::Principle`, `spky::PhraseLayout`, `spky::GrooveCell`, `spky::Rng`, `generate_phrase()`, and groove mutation helpers from `engine/mod/phrase_gen.h`.
- Produces:
  - `enum class FormMode : uint8_t { SongAAAB, TwoMotifs, OnePlusVar, Hierarchical, CallResponse, Ostinato, kCount };`
  - `FormMode clamp_form(int value)`
  - `Principle form_basis(FormMode form, Principle fallback)`
  - `FormMode form_for_principle(Principle principle)`
  - `struct PatternGroove { uint8_t rank_of_slot[32]; uint8_t note_len[32]; uint8_t len; };`
  - `struct MelodyPattern { float pitch[32]; bool gate[32]; uint8_t motif_id[32]; PhraseLayout layout; GrooveCell cell_groove; PatternGroove pattern_groove; };`
  - `struct TurnaroundZones { int related_end; int turn_start; int length; };`
  - `struct SongForm` containing two patterns, form/active indices, selected/pending form, `last_basis`, pending flags, cadence slot, and bound A-opening pitch.
  - `TurnaroundZones song_zones(int steps)`
  - `uint8_t song_symbol_at(uint8_t form_position)`
  - `void expand_pattern_groove(const GrooveCell& cell, int steps, PatternGroove& out)`
  - `void mutate_pattern_groove(Rng& rng, PatternGroove& groove, float variation, bool renew_side)`
  - `void derive_turnaround(const MelodyPattern& a, int steps, Rng& rng, MelodyPattern& b, uint8_t& cadence_slot, float& bound_a_opening)`
  - `void bind_song_cadence(const MelodyPattern& a, MelodyPattern& b, uint8_t cadence_slot, float& bound_a_opening)`

- [ ] **Step 1: Add failing form, zone, and groove tests**

Add `tests/test_song_form.cpp` with table-driven checks:

```cpp
#include <doctest/doctest.h>
#include "mod/song_form.h"
#include <cmath>
#include <cstring>

using namespace spky;

TEST_CASE("song form is exactly AAAB") {
    const uint8_t expected[] = {0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 8; ++i)
        CHECK(song_symbol_at(static_cast<uint8_t>(i)) == expected[i]);
}

TEST_CASE("song zones scale safely from 1 through 32 steps") {
    struct Case { int n, related, turn; };
    const Case cases[] = {
        {1, 1, 1}, {2, 1, 1}, {3, 1, 2},
        {8, 4, 6}, {12, 6, 9}, {16, 8, 12}, {32, 16, 24}
    };
    for (const auto& c : cases) {
        const auto z = song_zones(c.n);
        CHECK(z.length == c.n);
        CHECK(z.related_end == c.related);
        CHECK(z.turn_start == c.turn);
        CHECK(z.related_end >= 1);
        CHECK(z.related_end <= z.turn_start);
        CHECK(z.turn_start <= z.length);
    }
}

TEST_CASE("cell groove expands to unique absolute ranks at every supported length") {
    GrooveCell cell{};
    cell.len = 4;
    cell.rank_of_slot[0] = 0; cell.rank_of_slot[1] = 2;
    cell.rank_of_slot[2] = 1; cell.rank_of_slot[3] = 3;
    cell.note_len[0] = 1; cell.note_len[1] = 2;
    cell.note_len[2] = 3; cell.note_len[3] = 4;
    for (int n = 1; n <= 32; ++n) {
        PatternGroove groove{};
        expand_pattern_groove(cell, n, groove);
        bool seen[32] = {};
        REQUIRE(groove.len == n);
        CHECK(groove.rank_of_slot[0] == 0);
        for (int i = 0; i < n; ++i) {
            REQUIRE(groove.rank_of_slot[i] < n);
            CHECK_FALSE(seen[groove.rank_of_slot[i]]);
            seen[groove.rank_of_slot[i]] = true;
            CHECK(groove.note_len[i] >= 1);
            CHECK(groove.note_len[i] <= 4);
        }
    }
}
```

Register `tests/test_song_form.cpp` immediately after `tests/test_phrase_gen.cpp` in `spky_tests`.

- [ ] **Step 2: Run the focused tests and confirm the missing header failure**

Run:

```powershell
cmake --build build --target spky_tests
```

Expected: compilation fails because `mod/song_form.h` does not exist.

- [ ] **Step 3: Implement the form model, mappings, zones, and groove expansion**

Create `engine/mod/song_form.h`. Clamp effective length to `1..32`; map normal FORM values bijectively to the five existing principles; use `{0,0,0,1}` for the symbol sequence. For groove expansion, build absolute slots ordered stably by `(cell.rank_of_slot[i % cell.len], i / cell.len)`, assign ranks `0..n-1`, copy note lengths from the tiled cell, and explicitly preserve absolute slot 0 at rank 0.

The state definition must use values rather than pointers:

```cpp
struct SongForm {
    MelodyPattern patterns[2] = {};
    uint8_t form_position = 0;
    uint8_t active_pattern = 0;
    FormMode selected_form = FormMode::SongAAAB;
    FormMode pending_form = FormMode::SongAAAB;
    Principle last_basis = Principle::Hierarchical;
    bool form_pending = false;
    bool new_pending = false;
    bool length_pending = false;
    uint8_t cadence_slot = 0;
    float bound_a_opening = 0.f;
};
```

- [ ] **Step 4: Add failing turnaround, cadence, and mutation tests**

Extend `tests/test_song_form.cpp` to construct a deterministic A using `generate_phrase(Principle::Hierarchical, ...)`, expand its cell groove, derive B twice from equal seeds, and check byte-equal outputs. Add fixed-seed corpus checks over `2,3,8,12,16,32`:

```cpp
CHECK(turnaround_difference(a, b, z.turn_start, z.length) > 0);
CHECK(zone_distance(a, b, 0, z.related_end)
      < zone_distance(a, b, z.turn_start, z.length));
CHECK(b.pattern_groove.rank_of_slot[0] == 0);
CHECK(b.pattern_groove.rank_of_slot[cadence] == 1);
```

Define `zone_distance` and `turnaround_difference` as test-local functions comparing pitch epsilon, ranks, and note lengths. Add a cadence test that copies B before binding, changes only `a.pitch[0]`, calls `bind_song_cadence`, and checks:

```cpp
CHECK(b.pitch[cadence] == doctest::Approx(
    0.5f * (before.pitch[cadence] + a.pitch[0])));
for (int i = 0; i < 32; ++i)
    if (i != cadence) CHECK(b.pitch[i] == before.pitch[i]);
CHECK(bound == a.pitch[0]);
```

Prove the no-op path by calling binding again and comparing B byte-for-byte. Prove no RNG is involved by showing that the next draw from two equally seeded `Rng` instances remains equal when only one side performs cadence binding.

- [ ] **Step 5: Implement deterministic B derivation and full-pattern groove mutation**

Implement a fixed draw order per active slot: pitch decision, pitch amount, rank decision, length decision. Copy A first, then apply:

- Related `[0, related_end)`: pitch probability `0.20`, maximum nudge `0.12`; at most one adjacent rank swap in the zone; note-length nudge probability `0.15`.
- Departure `[related_end, turn_start)`: pitch probability `0.60`, maximum nudge `0.35`; rank-swap probability `0.45`; note-length nudge probability `0.40`.
- Turnaround `[turn_start, n)`: regenerate a contour from the preceding B pitch using `pg_contour_walk`, rerank those absolute slots, and regenerate note lengths in `[1,4]`.

After regeneration, force a difference from A at the cadence slot if the whole turnaround still matches, assign slot 0 rank 0 and cadence rank 1 by swapping ranks rather than duplicating them, then softly bias the cadence pitch once and store `bound_a_opening`. Keep pitch normalized to `[-1,1]`.

For `mutate_pattern_groove`, mirror the existing groove-variation threshold and probability policy but operate on `groove.len` absolute slots, always restoring slot 0 rank 0 and keeping `note_len` in `[1,4]`.

- [ ] **Step 6: Run helper tests and the complete portable suite**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="*song*"
ctest --test-dir build --output-on-failure
```

Expected: all song helper tests and the complete `spky_tests` target pass.

- [ ] **Step 7: Commit the helper layer**

```powershell
git add engine/mod/song_form.h tests/test_song_form.cpp CMakeLists.txt
git commit -m "feat(mod): add persistent song form helpers"
```

---

### Task 2: Migrate ModLane to Pattern Snapshots Without Changing Normal Modes

**Files:**
- Modify: `engine/mod/lane.h:1-165`
- Modify: `engine/mod/lane.cpp:38-304`
- Modify: `tests/test_song_form.cpp`

**Interfaces:**
- Consumes: `MelodyPattern`, `SongForm`, and form mapping helpers from Task 1.
- Produces:
  - `void ModLane::set_form(FormMode form)`
  - `FormMode ModLane::form() const`
  - `Principle ModLane::last_basis() const`
  - `void ModLane::set_last_basis(Principle basis)`
  - `uint8_t ModLane::song_position() const`
  - `uint8_t ModLane::active_pattern() const`
  - Private `MelodyPattern& _active_pattern()` and const overload.
  - Existing `set_principle(Principle)` remains as a compatibility wrapper that queues the corresponding normal FORM.

- [ ] **Step 1: Confirm the recorded pre-change lane size**

The ARM/Daisy ABI class-layout dump at baseline commit `c3c3c70` reports:

```text
Class spky::ModLane
   size=412 align=4
   base size=412 base align=4
```

Reconfirm it from the baseline worktree with the Daisy compiler's
`-fdump-lang-class` option before modifying `lane.h`.

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song storage budget" --no-skip
```

Expected: the class dump reports `size=412 align=4`.

- [ ] **Step 2: Add a failing normal-mode regression matrix**

In `tests/test_song_form.cpp`, create pairs of lanes for every `Principle`, seeds `{1, 0xBEEF}`, steps `{1,8,16,32}`, and variations `{-1,0,1}`. Drive identical control timelines and check bit-identical outputs, fires, target, and gate state. Also assert `set_principle(p)` reports `form_for_principle(p)` only after the next STEP wrap.

This locks down the behavior before replacing `_seq`, `_gate`, `_motif_id`, `_layout`, and `_groove`.

- [ ] **Step 3: Run the regression and verify the new FORM assertion fails**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="normal forms survive pattern snapshot migration"
```

Expected: compilation fails because the FORM accessors are not defined.

- [ ] **Step 4: Replace parallel phrase fields and route all normal accesses**

Include `mod/song_form.h` from `lane.h`, replace the five parallel phrase members plus `_principle` and `_regen_pending` with one `SongForm _song`, and add active-pattern accessors. In `lane.cpp`, bind local references at the point of use:

```cpp
MelodyPattern& p = _active_pattern();
generate_phrase(_song.last_basis, _rng, _steps,
                p.pitch, p.gate, p.motif_id, p.layout);
pg_gen_groove(_rng, p.layout.motif_len, p.cell_groove);
```

Route `_compute_raw`, `_effective_gate`, `_start_note`, `_mutate_slot`, `_renew_units`, and `_mutate_groove` through `p`. Normal modes must continue using `cell_groove`, and all array indexing must use `min(_steps, 32)`.

Implement `set_principle()` as:

```cpp
void set_principle(Principle p) { set_form(form_for_principle(p)); }
```

At this task boundary, normal FORM transitions can reuse the existing regeneration flag semantics; do not enable AAAB advancement yet.

- [ ] **Step 5: Add and enforce the storage budget**

After migration, add:

```cpp
TEST_CASE("song storage budget") {
    constexpr size_t kLaneBytesBefore = 412u; // ARM/Daisy ABI at c3c3c70
    constexpr size_t kAddedAcrossTwoParts =
        (sizeof(ModLane) - kLaneBytesBefore) * 10u;
    CHECK(kAddedAcrossTwoParts < 5u * 1024u);
}
```

If the check fails, stop: move `SongForm` ownership to only the two PITCH lanes in `SuperModulator` before continuing rather than weakening the bound.

- [ ] **Step 6: Run all existing modulation tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="*phrase*,*groove*,*variation*,*new_phrase*,*lane*"
ctest --test-dir build --output-on-failure
```

Expected: the new regression, storage budget, and all existing tests pass.

- [ ] **Step 7: Commit the state migration**

```powershell
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_song_form.cpp
git commit -m "refactor(mod): store melodies as pattern snapshots"
```

---

### Task 3: Implement AAAB Boundary Lifecycle and Independent Variation

**Files:**
- Create: `tests/test_song_lane.cpp`
- Modify: `CMakeLists.txt:36-110`
- Modify: `engine/mod/lane.h:97-165`
- Modify: `engine/mod/lane.cpp:82-370`

**Interfaces:**
- Consumes: Task 2 FORM accessors and Task 1 derivation/binding helpers.
- Produces private lane helpers:
  - `int _effective_length() const`
  - `bool _song_active() const`
  - `void _generate_pattern_a()`
  - `void _derive_pattern_b()`
  - `void _capture_active_as_a()`
  - `void _apply_pending_form_work()`
  - `void _advance_song_form()`
  - `void _evolve_outgoing_pattern()`
  - `void _clear_fresh_phrase_state()`

- [ ] **Step 1: Add failing LOOP transport and persistence tests**

Create `tests/test_song_lane.cpp` with a helper that configures melodic STEP before `init()`, selects SONG, uses shape `1`, density `1`, and collects one record per wrap containing form position, active pattern, fired targets, fired slots, and note holds. Assert:

```cpp
CHECK(symbols == std::vector<uint8_t>({0, 0, 0, 1, 0, 0, 0, 1}));
CHECK(cycles[0] == cycles[1]);
CHECK(cycles[1] == cycles[2]);
CHECK(cycles[0] != cycles[3]);
CHECK(cycles[0] == cycles[4]);
CHECK(cycles[3] == cycles[7]);
```

Collect two full supercycles at LOOP and compare pitch, fired-step set, and note sustain duration exactly.

- [ ] **Step 2: Run and confirm the AAAB sequence fails**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song LOOP*"
```

Expected: the lane repeats one pattern because SONG advancement is not implemented.

- [ ] **Step 3: Implement initialization, capture, rebuild, and AAAB advancement**

When a melodic lane is initialized with `selected_form == SongAAAB`, generate A from `last_basis`, expand its cell groove, derive B, and start at form position 0/A. In `_wrap_events()` preserve this exact order:

```cpp
_evolve_outgoing_pattern();
if (_song.form_pending || _song.new_pending || _song.length_pending)
    _apply_pending_form_work();
else if (_song_active())
    _advance_song_form();
```

Before selecting B, call `bind_song_cadence`; it must be a no-op if A slot 0 did not move. Load the incoming pattern index before `_enter_step(0)` fires. Do not copy A for its three positions: all three point to `patterns[0]`.

Queue `set_form`, `new_phrase`, and effective length changes rather than touching active buffers. Apply FORM first, then NEW. Entering SONG without NEW captures the outgoing normal pattern as A; entering with NEW generates A from `last_basis`. Leaving SONG or changing normal forms generates a fresh normal pattern at the boundary.

Treat configuration before the first audible STEP as a pre-roll boundary: apply
the final pending FORM/NEW state, including the requested length when that work
builds an uninitialized SONG snapshot, before step 0 fires. This keeps restored
normal-FORM patches from emitting one unintended SONG cycle while preserving
the boundary-only rule once playback has begun. A normal mode's length-only
change retains the existing first-wrap regeneration timing so normal transport
and RNG behavior remain bit-identical.

- [ ] **Step 4: Add failing pending-change, FLOW, and short-length tests**

Add cases that change FORM, NEW, and STEPS halfway through a pattern and confirm no target/form change before wrap, exactly one rebuild at wrap, and restart at A with the final settings. Exercise lengths `{1,2,3,12,32}` at the engine level. Add:

- normal → SONG capture;
- SONG → normal regeneration;
- normal → normal update of `last_basis`;
- last FORM write wins;
- NEW in SONG uses `last_basis`;
- FLOW leaves `song_position()` and both snapshots unchanged and STEP resumes that stored position.

- [ ] **Step 5: Implement coalesced pending changes and FLOW pause**

In `set_step()`, set `length_pending` only when `min(old_steps,32) != min(new_steps,32)`. Preserve the existing live phase rescaling, but rebuild snapshots only at wrap. FLOW must skip `_wrap_events()` for SONG pattern work; normal non-melodic FLOW behavior remains unchanged. `_clear_fresh_phrase_state()` resets note age/hold and `_ev_phase/_ev_shape/_ev_rate` once per rebuild.

- [ ] **Step 6: Add failing GROW/RENEW ownership and cadence tests**

At fixed seeds, snapshot both patterns through read-only test observability and prove:

- GROW/RENEW alters only the outgoing snapshot;
- A gets three evolution opportunities for B's one;
- B is not re-derived after RENEW;
- A/B remain distinct after many supercycles;
- changing A slot 0 causes exactly one halfway cadence move before B;
- LOOP consumes no playback RNG, demonstrated by equal future mutation results after equal control timelines.

Prefer narrow read-only helpers returning `const MelodyPattern& pattern_for_test(uint8_t)` only under `#ifdef SPKY_TESTING`; add `SPKY_TESTING=1` to the `spky_tests` target rather than exposing mutable production state.

- [ ] **Step 7: Implement snapshot-local RENEW/GROW**

Make `_evolve_outgoing_pattern()` run before form advancement. Existing pitch/EVOLVE behavior stays unchanged, but `_renew_units()` receives the outgoing snapshot and its principle basis, while SONG groove mutations call `mutate_pattern_groove`. Never derive B during ordinary RENEW. Preserve slot 0 rank 0 after every pattern-groove mutation.

- [ ] **Step 8: Add and satisfy process/tick equivalence**

Extend `tests/test_lane_tick.cpp` with SONG at LOOP, GROW, RENEW, a pending NEW, and a pending STEPS change. After each tick window compare target, output, fired/wrapped flags, form position, active pattern, and cadence observable, using the existing isolated tick-edge skew allowance.

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="song*,tick: SONG*"
ctest --test-dir build --output-on-failure
```

Expected: all SONG lifecycle tests and the complete portable suite pass.

- [ ] **Step 9: Commit SONG playback**

```powershell
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_song_lane.cpp tests/test_lane_tick.cpp CMakeLists.txt
git commit -m "feat(mod): play persistent AAAB song form"
```

---

### Task 4: Wire FORM Through the Portable Instrument API

**Files:**
- Modify: `engine/mod/super_modulator.h:20-38`
- Modify: `engine/instrument.h:35-50`
- Modify: `tests/test_instrument.cpp`

**Interfaces:**
- Consumes: `ModLane::set_form`, `set_last_basis`, `form`, and `last_basis`.
- Produces:
  - `void SuperModulator::set_form(FormMode form)`
  - `void SuperModulator::set_last_basis(Principle basis)`
  - `FormMode SuperModulator::form() const`
  - `Principle SuperModulator::last_basis() const`
  - `void Instrument::set_form(int part, int form)`
  - `void Instrument::set_last_basis(int part, int principle)`
  - `int Instrument::form(int part) const`
  - `int Instrument::last_basis(int part) const`

- [ ] **Step 1: Add failing public-API tests**

In `tests/test_instrument.cpp`, initialize an instrument, set both Parts to SONG with Hierarchical basis, drive through at least five wraps, and verify each Part reports SONG while its PITCH lane follows AAAB independently. Pass invalid integers `-7` and `99` and check they clamp to `SongAAAB` and `Ostinato`.

- [ ] **Step 2: Run and confirm missing API failures**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="instrument FORM API*"
```

Expected: compilation fails because the FORM methods are absent.

- [ ] **Step 3: Implement the thin API forwarding**

Replace `Instrument::set_principle` use at the host boundary with clamped FORM:

```cpp
void set_form(int p, int form) {
    _parts[p].mod().set_form(clamp_form(form));
}
void set_last_basis(int p, int principle) {
    const int clamped = principle < 0 ? 0 :
        (principle >= static_cast<int>(Principle::kCount)
            ? static_cast<int>(Principle::kCount) - 1 : principle);
    _parts[p].mod().set_last_basis(static_cast<Principle>(clamped));
}
```

Keep `set_principle` temporarily as a source-compatible wrapper for render scenarios and existing tests, but route new VCV work through FORM.

- [ ] **Step 4: Run instrument and full tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="instrument FORM API*"
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit the portable API**

```powershell
git add engine/mod/super_modulator.h engine/instrument.h tests/test_instrument.cpp
git commit -m "feat(engine): expose song form controls"
```

---

### Task 5: Replace the VCV PRIN Button With the FORM Knob

**Files:**
- Modify: `host/vcv/res/gen_panel.py:205-252`
- Modify: `host/vcv/res/test_panel.py`
- Regenerate: `host/vcv/src/generated_panel.hpp`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Modify: `host/vcv/src/init_patch.hpp`
- Modify: `host/vcv/src/Spotymod.cpp:164-245,475-510,600-690`

**Interfaces:**
- Consumes: Task 4 `Instrument::set_form`, `set_last_basis`, `form`, and `last_basis`.
- Produces: a snapped six-position Rack parameter in the existing `PRINCIPLE_A/B` enum slots, visually labeled `FORM`, with display values `SONG · AAAB`, `TWO MOTIFS`, `ONE + VAR`, `HIERARCHICAL`, `CALL / RESPONSE`, `OSTINATO`; host state `int lastBasis[2] = {2,2}` mirrors the remembered normal principle without reading mutable audio-thread state during JSON save.

**Rebase note (2026-07-25):** Current `main` includes the SOURCE knob/detune
context-menu work added after this plan was written. Preserve its appended
`DETUNE_A/B` parameter IDs, SOURCE defaults/captions, generated metadata, and
panel guards while regenerating the FORM artifacts.

- [ ] **Step 1: Change panel guard expectations first**

In `host/vcv/res/test_panel.py`, keep the enum IDs `PRINCIPLE_A/B` but require:

```python
check(ctl('PRINCIPLE_A').kind == g.KNOBI, "FORM A must be an integer knob")
check(ctl('PRINCIPLE_B').kind == g.KNOBI, "FORM B must be an integer knob")
check(ctl('PRINCIPLE_A').label == "FORM", "FORM A faceplate label")
check(ctl('PRINCIPLE_B').label == "FORM", "FORM B faceplate label")
```

Update `PARAM_TIPS` from `PRIN` to `FORM`, require both factory parameter values at those indices to be `0.0`, require `kInitLastBasis[] = {2, 2}`, and scan `Spotymod.cpp` for all six exact display strings plus `snapEnabled = true`. Retain the existing assertion that STEPS is configured `2.f, 16.f` and snapped.

- [ ] **Step 2: Run the panel guard and confirm failure**

Run:

```powershell
Set-Location host/vcv
python res/test_panel.py
Set-Location ../..
```

Expected: failures report the old button kind/PRIN label and old `[2,0]` principle defaults.

- [ ] **Step 3: Change the generator and regenerate checked-in artifacts**

In `part_controls()`, remove PRINCIPLE from the `pads` button list, add:

```python
out.append(Ctl("PRINCIPLE", KNOBI, fx(PAD_X[3]), PLAY_Y, "FORM"))
```

Keep NEW at `PAD_X[4]`; do not move STEPS or TRIGGER. Run the repository's generator from `host/vcv` using its documented entry point:

```powershell
Set-Location host/vcv
python res/gen_panel.py
Set-Location ../..
```

Confirm `generated_panel.hpp` reports `WK_KNOBI` and `Spotymod.svg` renders `FORM` at both former PRIN positions.

- [ ] **Step 4: Configure FORM and remove button-cycling state**

Remove `principleTrig[2]` and `principleIdx[2]`, and add
`int lastBasis[2] = {kInitLastBasis[0], kInitLastBasis[1]};`. In
`configControls()`, special-case `PRINCIPLE_A/B` under `WK_KNOBI`:

```cpp
configSwitch(c.id, 0.f, 5.f, init, "Form",
    {"SONG · AAAB", "TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
     "CALL / RESPONSE", "OSTINATO"});
getParamQuantity(c.id)->snapEnabled = true;
```

Delete the audio-thread button-cycle block. On each control push use:

```cpp
inst.set_form(p, static_cast<int>(std::lround(pp(PRINCIPLE_A, p))));
```

When the snapped FORM value is `1..5`, set `lastBasis[p] = form - 1` before
forwarding it. Set the snapshot values for `PRINCIPLE_A/B` to `0.0f`, add
`kInitLastBasis[] = {2,2}`, and forward `lastBasis[p]` after every `inst.init()`
and in `onReset()`.

- [ ] **Step 5: Replace principle JSON with `lastBasis` JSON**

`dataToJson()` writes:

```cpp
json_t* bases = json_array();
for (int p = 0; p < spky::PART_COUNT; ++p)
    json_array_append_new(bases, json_integer(lastBasis[p]));
json_object_set_new(root, "lastBasis", bases);
```

`dataFromJson()` reads into `lastBasis[p]`, clamps to
`0..static_cast<int>(Principle::kCount)-1`, and forwards the two values. When
absent, use `kInitLastBasis[p]`. Do not serialize FORM manually because Rack
already serializes parameters. Since beta compatibility is explicitly not
required, remove the old `"principle"` compatibility path instead of
maintaining dual state.

- [ ] **Step 6: Run panel tests and build VCV**

Run:

```powershell
Set-Location host/vcv
python res/test_panel.py
make -j2
Set-Location ../..
```

Expected: panel guard prints its success summary and the Rack plugin compiles.

- [ ] **Step 7: Commit the VCV control**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg host/vcv/src/init_patch.hpp host/vcv/src/Spotymod.cpp
git commit -m "feat(vcv): make AAAB song form the default"
```

---

### Task 6: Add a Deterministic Listening Scenario

**Files:**
- Create: `host/render/scenarios/demo_song_aaab.json`
- Modify: `tests/test_scenario.cpp`

**Interfaces:**
- Consumes: existing render scenario actions plus the public FORM API.
- Produces: a checked-in scenario that auditions 16-step SONG at LOOP, then moderate GROW and RENEW without changing form.

- [ ] **Step 1: Add a failing scenario parse test**

Extend `tests/test_scenario.cpp` to load `host/render/scenarios/demo_song_aaab.json`, assert its duration covers at least two 64-step supercycles, and assert the event timeline contains FORM=SONG, STEP on, STEPS=16, MELODY values `0`, positive, and negative.

- [ ] **Step 2: Run and confirm the missing-file failure**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="AAAB listening scenario*"
```

Expected: test fails because the scenario file does not exist.

- [ ] **Step 3: Add FORM and basis scenario parsing**

Add these exact actions to `host/render/scenario.cpp`:

```cpp
else if (action == "set_form")
    inst.set_form(part, static_cast<int>(value));
else if (action == "set_last_basis")
    inst.set_last_basis(part, static_cast<int>(value));
```

Cover it in the existing scenario action tests. Keep the older `principle` action as an alias for normal forms used by older listening scenarios.

Use the existing dispatcher variables (`a`, `e.part`, and `e.ivalue`) in the
actual implementation. Keep the existing `set_principle` action as the
compatibility alias; the bare `principle` spelling does not exist in current
scenarios.

- [ ] **Step 4: Create the listening scenario**

Base the file on `demo_step_melody.json`. Configure Part A as synth, FORM `0`, Hierarchical `last_basis`, STEP on, STEPS `16`, density around `0.6`, pure S&H shape, and MELODY `0` for two supercycles. Then schedule moderate GROW (`+0.55`) for several supercycles and moderate RENEW (`-0.55`) for several supercycles. Silence Part B so the form is unambiguous.

- [ ] **Step 5: Render and inspect artifacts**

Run:

```powershell
cmake --build build --target render
.\build\render.exe host/render/scenarios/demo_song_aaab.json renders/demo_song_aaab.wav renders/demo_song_aaab.csv
```

Expected: command exits 0 and writes non-empty WAV/CSV files. Listen for three recognizable A passes, a related B opening, a different melodic/rhythmic last quarter, a convincing B→A return, and no click or skipped boundary. Do not commit generated WAV/CSV artifacts.

- [ ] **Step 6: Commit the scenario**

```powershell
git add host/render/scenarios/demo_song_aaab.json host/render/scenario.cpp tests/test_scenario.cpp
git commit -m "test(render): add AAAB song form audition"
```

---

### Task 7: Final Cross-Layer Verification

**Files:**
- Modify only if a verification failure identifies a specific defect in files already listed above.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: verified engine, VCV panel/plugin, deterministic render, clean worktree, and recorded size budget.

- [ ] **Step 1: Re-run the complete portable suite from a clean build**

Run:

```powershell
cmake -S . -B build
cmake --build build --target spky_tests render
ctest --test-dir build --output-on-failure
```

Expected: configure/build succeeds and CTest reports 100% pass.

- [ ] **Step 2: Re-run VCV generation guards and plugin build**

Run:

```powershell
Set-Location host/vcv
python res/test_panel.py
make -j2
Set-Location ../..
```

Expected: panel tests pass and the plugin builds.

- [ ] **Step 3: Verify determinism and storage explicitly**

Run the SONG-focused tests twice and compare their successful results:

```powershell
.\build\spky_tests.exe --test-case="song*,tick: SONG*,instrument FORM API*"
.\build\spky_tests.exe --test-case="song*,tick: SONG*,instrument FORM API*"
```

Expected: both runs pass. Confirm the compiled storage test uses the recorded pre-change byte value and enforces `(sizeof(ModLane) - baseline) * 10 < 5120`.

- [ ] **Step 4: Inspect the final diff against the approved spec**

Run:

```powershell
git diff c3c3c70 --check
git status --short
git log --oneline c3c3c70..HEAD
```

Expected: `git diff --check` emits nothing; status contains no unintended generated audio or unrelated changes; history shows the focused commits from Tasks 1–6.

- [ ] **Step 5: Commit any verification-only correction**

Only when Step 1–4 required a concrete correction, interactively stage only
that correction:

```powershell
git add -p
git commit -m "fix(song): satisfy cross-layer verification"
```

If no correction was needed, do not create an empty commit.

---

## Execution Notes

- Execute in an isolated `codex/` worktree because another task is active on shared branch `WAVE`. Use `superpowers:using-git-worktrees` before Task 1.
- Preserve the approved spec commit `c3c3c70` as the plan's baseline.
- Do not tune musical probabilities while debugging transport. First make form state, pending transitions, determinism, and storage tests pass; then evaluate the documented ear-tuning constants in the listening scenario.
- At every reviewer checkpoint, compare the implementation to the lifecycle order in the spec: evolve outgoing → apply pending work or advance → bind cadence before B → select incoming before step 0.
