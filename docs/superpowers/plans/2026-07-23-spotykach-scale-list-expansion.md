# Scale List Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Grow the global scale list from 6 to 13 entries — grouped modes / pentatonics / exotic — and give every entry a display name that the VCV tooltip and the scenario parser both read from one table.

**Architecture:** `engine/pitch/quantizer.h` owns the scale table: the `ScaleId` enum, `SCALE_MASKS`, and a new parallel `SCALE_NAMES`. The `Quantizer` class itself is untouched — it consumes a 12-bit mask and knows nothing about which scales exist. The VCV host gains a `ScaleQuantity` that turns the snapped knob index into a name, and `host/render/scenario.cpp` gains the seven new spellings. Nothing changes in the quantization math, the placement inside `Part`, or the persistence path.

**Tech Stack:** C++17, header-only engine, doctest, CMake + Ninja + clang for the desktop build, VCV Rack SDK for the host.

**Spec:** `docs/superpowers/specs/2026-07-23-spotykach-scale-list-expansion-design.md`

## Global Constraints

- The list is exactly 13 entries in this order: Aeolian, Dorian, Mixolydian, Lydian, Hirajoshi, Pygmy, Minor pent, Kumoi, Major pent, Phrygian, Hijaz, Harmonic minor, Whole tone.
- Boot default stays **Dorian**, which is now index 1.
- Masks are 12-bit; bit *i* set = semitone *i* above the root is allowed. Bit 0 is set in every entry.
- `SCALE_NAMES` lives in `engine/pitch/quantizer.h` beside `SCALE_MASKS` — one source of truth. No host-local copy of the names.
- **No saved-patch migration.** Old `.vcv` files silently shift scale; this is deliberate (spec, "Saved-patch compatibility"). The breaking change is recorded as a `BREAKING CHANGE:` trailer in the Task 1 commit, which is what feeds the release notes.
- Scenario JSONs reference scales by name, never by index. The six existing names keep their meaning.
- Build/test env: source the gitignored `env.sh` at the fork root before any `cmake`/`ctest` (puts clang and ninja on PATH). Ninja is single-config, so the test binary is at `build/spky_tests.exe`.
- The VCV host is **never** built by hand — always `./build-local.sh` from `host/vcv`. The system `g++` on this machine is the ARM cross-compiler and a hand-rolled build fails with "MinGW not found".

## File Structure

| File | Change | Responsibility |
|------|--------|----------------|
| `engine/pitch/quantizer.h` | Modify (lines 10–29) | The scale table: enum, masks, names |
| `tests/test_quantizer.cpp` | Modify (add one case) | Masks match their documented semitones |
| `host/render/scenario.cpp` | Modify (lines 91–98) | Name → index for scenario JSON |
| `tests/test_scenario.cpp` | Modify (add one case + helper) | New names reach the instrument |
| `host/vcv/src/Spotymod.cpp` | Modify (near line 39, line 184) | `ScaleQuantity` tooltip |
| `docs/roadmap.md` | Modify (lines 26, 94–95) | "6 scales" → the new list |
| `docs/superpowers/specs/2026-07-11-spotykach-scales-design.md` | Modify | Supersession notes |

---

### Task 1: The scale table

Grows the enum, masks and names in one commit, because a mask without its enum name does not compile and a name without its mask is untested. This is the only task that changes engine behaviour.

**Files:**
- Modify: `engine/pitch/quantizer.h:10-29`
- Test: `tests/test_quantizer.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `enum ScaleId` with the 13 members named below plus `SCALE_LIST_COUNT` (== 13); `constexpr uint16_t SCALE_MASKS[SCALE_LIST_COUNT]`; `constexpr const char* SCALE_NAMES[SCALE_LIST_COUNT]`. Tasks 2 and 3 use these names verbatim.

- [ ] **Step 1: Write the failing test**

Add `#include <string>` to the includes at the top of `tests/test_quantizer.cpp` (it currently has `<doctest/doctest.h>`, `<cmath>`, `<initializer_list>`, `"pitch/quantizer.h"`), then append this case at the end of the file:

```cpp
// The masks are hand-written hex. A mistyped digit otherwise surfaces only as
// a melody that sounds slightly wrong, so state the semitones independently
// and rebuild the mask from them.
TEST_CASE("quantizer: masks match their documented semitones and names") {
    struct Row { int id; const char* name; int n; int semis[7]; };
    static const Row kRows[] = {
        { SCALE_AEOLIAN,   "Aeolian",        7, {0,2,3,5,7,8,10} },
        { SCALE_DORIAN,    "Dorian",         7, {0,2,3,5,7,9,10} },
        { SCALE_MIXO,      "Mixolydian",     7, {0,2,4,5,7,9,10} },
        { SCALE_LYDIAN,    "Lydian",         7, {0,2,4,6,7,9,11} },
        { SCALE_HIRAJOSHI, "Hirajoshi",      5, {0,2,3,7,8} },
        { SCALE_PYGMY,     "Pygmy",          5, {0,2,3,7,10} },
        { SCALE_MIN_PENT,  "Minor pent",     5, {0,3,5,7,10} },
        { SCALE_KUMOI,     "Kumoi",          5, {0,2,3,7,9} },
        { SCALE_MAJ_PENT,  "Major pent",     5, {0,2,4,7,9} },
        { SCALE_PHRYGIAN,  "Phrygian",       7, {0,1,3,5,7,8,10} },
        { SCALE_HIJAZ,     "Hijaz",          7, {0,1,4,5,7,8,10} },
        { SCALE_HARM_MIN,  "Harmonic minor", 7, {0,2,3,5,7,8,11} },
        { SCALE_WHOLE,     "Whole tone",     6, {0,2,4,6,8,10} },
    };
    CHECK(sizeof(kRows) / sizeof(kRows[0]) == static_cast<size_t>(SCALE_LIST_COUNT));

    for (const auto& r : kRows) {
        CAPTURE(r.name);
        uint16_t want = 0;
        for (int i = 0; i < r.n; ++i) want |= static_cast<uint16_t>(1u << r.semis[i]);
        CHECK(SCALE_MASKS[r.id] == want);
        CHECK((SCALE_MASKS[r.id] & 1u) == 1u);            // root always allowed
        CHECK((SCALE_MASKS[r.id] & ~0x0FFFu) == 0u);      // fits in 12 bits
        CHECK(std::string(SCALE_NAMES[r.id]) == std::string(r.name));
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh
cmake -S . -B build && cmake --build build
```

Expected: **compile error**, not a test failure — `SCALE_MIXO`, `SCALE_HIRAJOSHI`, `SCALE_PYGMY`, `SCALE_KUMOI`, `SCALE_PHRYGIAN`, `SCALE_HIJAZ`, `SCALE_HARM_MIN` and `SCALE_NAMES` are all undeclared. That is the correct red state here: the test cannot link against a table that does not exist yet.

- [ ] **Step 3: Write the implementation**

Replace `engine/pitch/quantizer.h:10-29` (the comment block, the `enum ScaleId`, and the `SCALE_MASKS` array) with:

```cpp
// Global scale list in three groups: modes, pentatonics, exotic/handpan.
// Dark -> bright ordering survives inside each group, so the direction of the
// selection sweep still means what it always meant; the groups themselves run
// familiar -> exotic and are what makes the longer list blind-navigable
// (count blocks of 4/5/4, then step within one). Bit i set = semitone i
// relative to root is allowed.
enum ScaleId {
    // A -- modes
    SCALE_AEOLIAN = 0,
    SCALE_DORIAN,          // boot default
    SCALE_MIXO,
    SCALE_LYDIAN,
    // B -- pentatonics
    SCALE_HIRAJOSHI,
    SCALE_PYGMY,
    SCALE_MIN_PENT,
    SCALE_KUMOI,
    SCALE_MAJ_PENT,
    // C -- exotic / handpan
    SCALE_PHRYGIAN,
    SCALE_HIJAZ,
    SCALE_HARM_MIN,
    SCALE_WHOLE,
    SCALE_LIST_COUNT
};

constexpr uint16_t SCALE_MASKS[SCALE_LIST_COUNT] = {
    0x05AD,  // aeolian           0 2 3 5 7 8 10
    0x06AD,  // dorian            0 2 3 5 7 9 10
    0x06B5,  // mixolydian        0 2 4 5 7 9 10
    0x0AD5,  // lydian            0 2 4 6 7 9 11
    0x018D,  // hirajoshi         0 2 3 7 8
    0x048D,  // pygmy             0 2 3 7 10
    0x04A9,  // minor pentatonic  0 3 5 7 10
    0x028D,  // kumoi             0 2 3 7 9
    0x0295,  // major pentatonic  0 2 4 7 9
    0x05AB,  // phrygian          0 1 3 5 7 8 10
    0x05B3,  // hijaz             0 1 4 5 7 8 10
    0x09AD,  // harmonic minor    0 2 3 5 7 8 11
    0x0555,  // whole tone        0 2 4 6 8 10
};

// Display names, read by the VCV tooltip. Kept here rather than in the host so
// the two lists cannot drift apart.
constexpr const char* SCALE_NAMES[SCALE_LIST_COUNT] = {
    "Aeolian", "Dorian", "Mixolydian", "Lydian",
    "Hirajoshi", "Pygmy", "Minor pent", "Kumoi", "Major pent",
    "Phrygian", "Hijaz", "Harmonic minor", "Whole tone",
};
```

Hirajoshi, Pygmy and Kumoi share the core `0x008D` (0 2 3 7) and differ only in their fifth note — that is why they sit adjacent. Nothing below line 29 changes: `_scale = SCALE_MASKS[SCALE_DORIAN]` still resolves to the default.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, all cases. Two existing cases silently cover the new entries: `"quantizer: every scale mask maps output onto its own degrees"` (`tests/test_quantizer.cpp:40`) already loops `0 .. SCALE_LIST_COUNT` and now exercises 13 masks, and `"instrument: set_scale is global and reaches both parts"` (`tests/test_instrument.cpp:50`) passes `SCALE_WHOLE`, whose index moved from 5 to 12 — it must still pass, which proves the clamp in `Instrument::set_scale` (`engine/instrument.h:53-56`) tracks the new count.

- [ ] **Step 5: Commit**

```bash
git add engine/pitch/quantizer.h tests/test_quantizer.cpp
git commit -F - <<'EOF'
feat(pitch): 13 scales in three groups, with display names

Adds Mixolydian, Hirajoshi, Pygmy, Kumoi, Phrygian, Hijaz and harmonic
minor. Dark -> bright ordering survives inside each group; the groups
answer blind navigability that the old six-entry list answered by being
short enough to count. SCALE_NAMES joins SCALE_MASKS so host and engine
read one table.

BREAKING CHANGE: scale indices shifted. VCV patches saved with an earlier
release open on a different scale (old 4 = Lydian is now Hirajoshi). No
migration is provided. Scenario JSONs reference scales by name and are
unaffected.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 2: Scenario names

**Files:**
- Modify: `host/render/scenario.cpp:91-98`
- Test: `tests/test_scenario.cpp`

**Interfaces:**
- Consumes: `SCALE_MIXO`, `SCALE_HIRAJOSHI`, `SCALE_PYGMY`, `SCALE_KUMOI`, `SCALE_PHRYGIAN`, `SCALE_HIJAZ`, `SCALE_HARM_MIN` from Task 1.
- Produces: scenario `set_scale` accepts `aeolian | dorian | mixo | lydian | hirajoshi | pygmy | min_pent | kumoi | maj_pent | phrygian | hijaz | harm_min | whole`. Unknown input still yields Dorian.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scenario.cpp`. The helper takes a fresh `Instrument` per call so no hysteresis or change-slew carries between scales:

```cpp
// One fresh instrument per scale: PITCH depth 0 and base fixed, so pitch_cv
// settles on whatever the scale allows nearest to `base`, with nothing
// carried over from a previous scale.
static float settled_pitch_semis(const char* scale_name, float base) {
    Instrument inst;
    inst.init(48000.f);
    Event depth; depth.action = "set_target_depth"; depth.part = 0;
    depth.slot = LANE_PITCH; depth.value = 0.f;
    apply_event(inst, depth);
    Event b;     b.action = "set_target_base"; b.part = 0;
    b.slot = LANE_PITCH; b.value = base;
    apply_event(inst, b);
    Event s;     s.action = "set_scale"; s.svalue = scale_name;
    apply_event(inst, s);

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    return inst.pitch_cv(0) * 36.f;
}

TEST_CASE("scenario: the new scale names reach the instrument") {
    // 18 semis is degree 6. Hirajoshi (0 2 3 7 8) has neither 6 nor 5, so the
    // search walks up to 19; dorian would tie 17/19 and take 17.
    CHECK(settled_pitch_semis("hirajoshi", 0.5f) == doctest::Approx(19.f));

    // 16 semis is degree 4: in hijaz (0 1 4 5 7 8 10), not in dorian, which
    // ties 15/17 and takes the lower. The pair separates the two scales.
    CHECK(settled_pitch_semis("hijaz",    16.f / 36.f) == doctest::Approx(16.f));
    CHECK(settled_pitch_semis("nonsense", 16.f / 36.f) == doctest::Approx(15.f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh
cmake --build build && ./build/spky_tests.exe -tc="scenario: the new scale names reach the instrument"
```

Expected: FAIL. `parse_scale_name` does not know `"hirajoshi"` or `"hijaz"` yet, so both fall through to Dorian: the first check reports 17 where 19 was expected, the second 15 where 16 was expected. The third check (unknown → Dorian) already passes — it is there to stay green, as the regression guard on the fallback.

- [ ] **Step 3: Write the implementation**

Replace `parse_scale_name` at `host/render/scenario.cpp:91-98` with:

```cpp
static int parse_scale_name(const std::string& s) {
    if (s == "aeolian")   return SCALE_AEOLIAN;
    if (s == "mixo")      return SCALE_MIXO;
    if (s == "lydian")    return SCALE_LYDIAN;
    if (s == "hirajoshi") return SCALE_HIRAJOSHI;
    if (s == "pygmy")     return SCALE_PYGMY;
    if (s == "min_pent")  return SCALE_MIN_PENT;
    if (s == "kumoi")     return SCALE_KUMOI;
    if (s == "maj_pent")  return SCALE_MAJ_PENT;
    if (s == "phrygian")  return SCALE_PHRYGIAN;
    if (s == "hijaz")     return SCALE_HIJAZ;
    if (s == "harm_min")  return SCALE_HARM_MIN;
    if (s == "whole")     return SCALE_WHOLE;
    return SCALE_DORIAN;   // "dorian" and anything unknown -> the default
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, all cases. In particular the pre-existing `"scenario: quantizer actions reach the instrument"` (`tests/test_scenario.cpp:46`) must stay green — it uses `"whole"`, whose index moved, and proves the six old names still mean what they meant.

- [ ] **Step 5: Commit**

```bash
git add host/render/scenario.cpp tests/test_scenario.cpp
git commit -F - <<'EOF'
feat(host): scenario names for the seven new scales

set_scale now accepts mixo, hirajoshi, pygmy, kumoi, phrygian, hijaz and
harm_min. The six existing names keep their meaning, so the scenario
corpus is unaffected by the index shift.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 3: VCV tooltip

The SCALE knob is a snapped int param whose tooltip currently shows a bare number. Six was countable; thirteen is not.

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp` (add a struct near line 71, change the `configParam` at line 184)

**Interfaces:**
- Consumes: `spky::SCALE_NAMES`, `spky::SCALE_LIST_COUNT` from Task 1.
- Produces: nothing other tasks depend on.

- [ ] **Step 1: Add the quantity**

There is no test harness for the VCV host — verification is a build plus a look at the tooltip. Insert after `DustQuantity` (which ends at `host/vcv/src/Spotymod.cpp:71`), keeping the file's habit of a comment above each quantity:

```cpp
// SCALE tooltip: the raw index carried meaning at six entries and stopped
// carrying it at thirteen. Names come from the engine's table, never a copy.
struct ScaleQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int i = (int)std::round(getValue());
        if (i < 0) i = 0;
        if (i >= spky::SCALE_LIST_COUNT) i = spky::SCALE_LIST_COUNT - 1;
        return spky::SCALE_NAMES[i];
    }
};
```

- [ ] **Step 2: Wire it to the param**

Replace the SCALE branch at `host/vcv/src/Spotymod.cpp:184-186` with:

```cpp
                    if (c.id == SCALE)  // init patch is Lydian -- the bright end of group A
                        configParam<ScaleQuantity>(c.id, 0.f, (float)(spky::SCALE_LIST_COUNT - 1),
                                                   (float)spky::SCALE_LYDIAN, "Scale");
```

The range picks up the new count on its own and `SCALE_LYDIAN` resolves to its new index at compile time, so the init patch keeps sounding exactly as it does today. Leave the `snapEnabled = true` line at `:189` alone.

- [ ] **Step 3: Build the plugin**

```bash
cd host/vcv && ./build-local.sh
```

Expected: a clean build ending in the plugin being installed. Do **not** substitute a hand-rolled `make` — the system `g++` here is the ARM cross-compiler and the build dies with "MinGW not found".

- [ ] **Step 4: Verify the tooltip in Rack**

Open VCV Rack, add a fresh Spotymod, hover the SCALE knob. Expected: the tooltip reads `Scale: Lydian` on a fresh instance. Turn the knob fully left and right; expected: `Aeolian` at 0 and `Whole tone` at 12, with `Hirajoshi` four steps up from the bottom.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/src/Spotymod.cpp
git commit -F - <<'EOF'
feat(vcv): show the scale name in the SCALE tooltip

A bare index was countable at six entries and is not at thirteen. Names
are read from the engine's SCALE_NAMES so the panel cannot drift from the
table it is displaying.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 4: Documentation

**Files:**
- Modify: `docs/roadmap.md:26`, `docs/roadmap.md:94-95`
- Modify: `docs/superpowers/specs/2026-07-11-spotykach-scales-design.md`

**Interfaces:**
- Consumes: the final list from Task 1.
- Produces: nothing code depends on.

- [ ] **Step 1: Update the roadmap milestone row**

Replace `docs/roadmap.md:26`:

```markdown
| **+ Scales** | Pitch quantization (13 scales, SCALE/CHROM/FREE, root) layered onto the PITCH lane | ✅ **done** (engine + host; UI wiring deferred to M6) |
```

- [ ] **Step 2: Update the roadmap scale list**

Replace `docs/roadmap.md:94-95`:

```markdown
- **13 scales in three groups,** dark → bright inside each: modes (Aeolian,
  **Dorian (default)**, Mixolydian, Lydian), pentatonics (Hirajoshi, Pygmy,
  minor pentatonic, Kumoi, major pentatonic), exotic (Phrygian, Hijaz,
  harmonic minor, whole tone). Boot default: Dorian, both parts SCALE.
```

- [ ] **Step 3: Mark the old spec superseded**

In `docs/superpowers/specs/2026-07-11-spotykach-scales-design.md`, insert this above the six-row scale table (currently at line 27, the "Global scale list — 6 scales ordered dark → bright" paragraph):

```markdown
> **[Superseded by `2026-07-23-spotykach-scale-list-expansion-design.md`]**
> — the list below grew to 13 entries in three groups (modes / pentatonics /
> exotic). Dark → bright ordering now holds inside each group rather than
> across the whole list. The model around it is unchanged.
```

Then replace the last bullet of the "Out of scope" section (line 149–151, "Larger scale lists / user-defined scales…") with:

```markdown
- ~~Larger scale lists~~ — reversed on 2026-07-23: the list grew to 13,
  with grouping rather than brevity carrying blind navigability. See
  `2026-07-23-spotykach-scale-list-expansion-design.md`. User-defined
  scales remain out of scope.
```

- [ ] **Step 4: Verify nothing else claims six scales**

```bash
grep -rn "6 scales\|six scales" docs/ engine/ host/ tests/
```

Expected: no output. If a line turns up, update it the same way.

- [ ] **Step 5: Commit**

```bash
git add docs/roadmap.md docs/superpowers/specs/2026-07-11-spotykach-scales-design.md
git commit -F - <<'EOF'
docs: roadmap and the 2026-07-11 spec follow the 13-scale list

The old spec stays readable as what it was; a supersession note above its
table and a reversal note on its "larger scale lists" rejection point at
the new spec.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Done when

- `ctest --test-dir build --output-on-failure` is green, including the two new cases.
- The VCV SCALE tooltip names all 13 scales and a fresh instance still reads `Lydian`.
- No file in the repo still describes the list as having six entries.
- Four commits on `scale-list-expansion`, on top of the spec commit.
