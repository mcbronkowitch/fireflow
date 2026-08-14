# Glow becomes Touch 2 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the FireFlow Glow VCV module's control surface and faceplate
with a 1:1 replica of the Synthux Touch 2 board — 12 touch pads, 6 trim knobs,
2 faders, 2 switches, one stereo out — so the hardware can be rehearsed in Rack
before it exists.

**Architecture:** The panel generator `res/gen_flow_panel.py` stays the single
source of truth and is rewritten to consume a measured geometry table; it emits
`res/Glow.svg` and `src/generated_flow_panel.hpp`. All Rack-free module logic
(pad gesture, fader/switch mapping, TSV export) lives in a new
`host/vcv/src/touch_pads.hpp` tested headlessly by doctest. `Glow.cpp` keeps its
`Module` class, its engine wiring and its persistence route, and swaps only what
the panel drives.

**Tech Stack:** C++17, VCV Rack SDK 2.6.6 (MinGW/GCC 15.2.0), Python 3 for the
panel generator and its guards, doctest for the desktop suite, CMake + Ninja +
clang for engine and tests.

**Spec:** `docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md`.
Section references below (§4.3, §5.3, …) point into it.

## Global Constraints

- **Everything written into the repo is English** — code, comments, commit
  messages, docs. Only the conversation is German.
- **Commit trailer** is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
  Never the default Anthropic one.
- **`-DCMAKE_BUILD_TYPE=Release` is not optional.** A Debug configure makes
  `spky_tests` and `ctrl_identity` fail on unrelated render hashes.
- **Never `source env.sh` and then build the VCV plugin.** Build the plugin only
  via `host/vcv/build-local.sh`; the system `g++` on this machine is the Daisy
  ARM cross-compiler and fails with "MinGW not found".
- **Panel guards run from `host/vcv/`**, and the generator runs *before* the
  guard: `test_committed_files_match_the_generator` byte-compares both artifacts
  against a fresh generation.
- **No coordinate, caption or colour is written into `Glow.cpp`.** It reads them
  from `generated_flow_panel.hpp`. Widget *class* choices and the two screw
  positions are C++ decisions and stay so.
- **`host/vcv/src/touch_pads.hpp` must contain no Rack type** — no
  `plugin.hpp`, no `rack.hpp`, no `rack::`, no `nvg*`, no `Vec`/`Widget`/
  `Module`, and **no `json_t`** (jansson comes from Rack). Header-only,
  `#pragma once`, includes limited to the C++ standard library and headers under
  `engine/`.
- **Every new assertion is proven red once** before it counts as a gate.
  `test_flow_panel.py` accumulates into `FAILS` and exits non-zero only at the
  end, so "red" means running the whole suite and seeing that assertion named.
- **No patch migration.** Glow shipped as a dev alpha, no patches containing it
  exist, and the repo's standing rule is that alpha patches break freely. Do not
  add a version marker or a `dataFromJson` reset path.

---

## File Structure

> **Superseded, 11 Aug 2026 (historical record, not rewritten):** every mention
> below of committing the reference photo to `host/vcv/res/ref/` was reversed by
> the owner for privacy before implementation began — this repo is public and
> the photo is of a Synthux product. It lives in the private website repo and
> only its path is committed (`FireFlow_Website/docs/reference/touch2-fx-2026-08-11.png`).
> The design spec §3.2 carries the current rule.

| File | Responsibility |
|---|---|
| `host/vcv/res/ref/touch2-fx-2026-08-11.png` | **new** — the pinned geometry source |
| `host/vcv/res/touch2_geometry.py` | **new** — the measured control centres, in mm. Data only, no drawing. |
| `host/vcv/res/test_touch2_geometry.py` | **new** — guards the measured table on its own, before any SVG exists |
| `host/vcv/res/gen_flow_panel.py` | **rewritten** — consumes the table, emits SVG + header |
| `host/vcv/res/test_flow_panel.py` | **largely rewritten** — new geometry, new enum contract, rect collisions |
| `host/vcv/res/Glow.svg` | regenerated |
| `host/vcv/src/generated_flow_panel.hpp` | regenerated |
| `host/vcv/src/touch_pads.hpp` | **new** — pad gesture, fader/switch mapping, TSV export. Rack-free. |
| `tests/test_touch_pads.cpp` | **new** — doctest for the above |
| `host/vcv/src/glow_ui.hpp` | shrinks — `kCvMacro`, `cv_to_macro`, `clock_bpm`, `led_level` go |
| `tests/test_glow_ui.cpp` | shrinks with its subjects |
| `host/vcv/src/Glow.cpp` | control surface, pad wiring, `TouchPlate` widget, context menu |
| `CMakeLists.txt` | one new test source |
| `host/vcv/plugin.json`, `host/vcv/README.md`, `docs/roadmap.md`, `docs/release-notes.md` | copy that describes the old surface |

**Build-breakage note.** Task 3 rewrites the generated header; `Glow.cpp` does
not compile against it until Task 4. This is deliberate and called out in Task
3's commit message. Tasks 1, 2, 5 and 6 each leave every gate green.

---

### Task 1: `touch_pads.hpp` — the Rack-free module logic

Everything here is pure: no Rack, no engine mutation, no I/O. It is written
first because it is the only part that can be fully tested before anything else
exists.

**Files:**
- Create: `host/vcv/src/touch_pads.hpp`
- Create: `tests/test_touch_pads.cpp`
- Modify: `CMakeLists.txt:136` (add the test source next to `tests/test_glow_ui.cpp`)

**Interfaces:**
- Consumes: `engine/flow/flow_ids.h` (`Macro`, `Archetype`, `ARCH_COUNT`),
  `engine/flow/terrain.h` (`arch_of`), `engine/flow/terrain_code.h`
  (`kTerrainCodeLen`, `decode_code`).
- Produces, all in `namespace spkyvcv`:
  - `constexpr int kPadCount = 12;`
  - `constexpr double kPadHoldS = 0.4;`
  - `enum class PadAction { NONE, WAKE, REROLL };`
  - `struct PadEvent { PadAction action; int pad; };`
  - `struct PadGesture` with `PadEvent update(const bool* down, double now_s)`,
    public `int live` and `bool excursion`, and `void reset()`
  - `struct Place { char code[…]; std::string name; std::string note; };`
  - `enum class FaderTarget { OFF, TEMPO, MASTER };`
  - `enum class SwitchTarget { OFF, LOCK, SCALE };`
  - `float fader_tempo_bpm(float knob01)`
  - `float fader_master_gain(float knob01)`
  - `bool lock_switch(int pos)`
  - `struct TonalityGate { int scale_ovr; int root_ovr; };`
  - `TonalityGate scale_switch(int pos, int menu_scale, int menu_root)`
  - `std::string sanitize_label(const std::string& in, std::size_t cap)`
  - `const char* arch_name(int arch)`
  - `std::string export_pool_tsv(const Place* places, int n)`

- [ ] **Step 1: Write the failing test**

Create `tests/test_touch_pads.cpp`:

```cpp
// tests/test_touch_pads.cpp
#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include "vcv/src/touch_pads.hpp"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

// A helper that builds the 12-bool "which pads are down" vector.
static void press(bool* d, int pad) {
    for (int i = 0; i < kPadCount; ++i) d[i] = (i == pad);
}
static void none(bool* d) {
    for (int i = 0; i < kPadCount; ++i) d[i] = false;
}

TEST_CASE("pads: a press wakes immediately, with no latency") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 3);
    const PadEvent e = g.update(d, 0.0);
    CHECK(e.action == PadAction::WAKE);
    CHECK(e.pad == 3);
    CHECK(g.live == 3);
    CHECK(g.excursion == false);
}

TEST_CASE("pads: holding past the threshold rerolls once, not repeatedly") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 5);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    // Still short of the threshold.
    CHECK(g.update(d, kPadHoldS * 0.5).action == PadAction::NONE);
    // Crossing it fires exactly one reroll.
    const PadEvent r = g.update(d, kPadHoldS + 0.01);
    CHECK(r.action == PadAction::REROLL);
    CHECK(r.pad == 5);
    // Holding on does NOT fire again.
    CHECK(g.update(d, kPadHoldS + 1.0).action == PadAction::NONE);
    CHECK(g.update(d, kPadHoldS + 2.0).action == PadAction::NONE);
}

TEST_CASE("pads: releasing without reaching the threshold rerolls nothing") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 1);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    none(d);
    CHECK(g.update(d, 0.1).action == PadAction::NONE);
    // Time passing after the release must not arm anything.
    CHECK(g.update(d, 5.0).action == PadAction::NONE);
}

TEST_CASE("pads: tapping the same pad again is a plain wake -- the excursion "
          "needs no special case") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 7);
    g.update(d, 0.0);
    g.update(d, kPadHoldS + 0.01);
    g.excursion = true;                    // the module sets this on success
    none(d);
    g.update(d, 1.0);
    press(d, 7);
    const PadEvent e = g.update(d, 2.0);
    CHECK(e.action == PadAction::WAKE);
    CHECK(e.pad == 7);
    CHECK(g.excursion == false);           // the wake clears it
}

TEST_CASE("pads: a second pad pressed while one is held is ignored") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 2);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    d[9] = true;                           // both down now
    CHECK(g.update(d, 0.1).action == PadAction::NONE);
    CHECK(g.live == 2);
}

TEST_CASE("pads: a pad already down at the first update does not fire -- "
          "a restored patch must not wake or reroll itself") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 4);
    // reset() models the post-load state: params were forced to 0, and the
    // gesture must treat whatever it sees next as the baseline, not an edge.
    g.reset();
    g.prime(d);
    CHECK(g.update(d, 0.0).action == PadAction::NONE);
    CHECK(g.update(d, kPadHoldS + 1.0).action == PadAction::NONE);
}

TEST_CASE("faders: TEMPO spans P_TEMPO_BPM's declared range") {
    CHECK(fader_tempo_bpm(0.f) == doctest::Approx(50.f));
    CHECK(fader_tempo_bpm(1.f) == doctest::Approx(140.f));
    CHECK(fader_tempo_bpm(0.5f) == doctest::Approx(95.f));
}

TEST_CASE("faders: MASTER is unity at the top and silent at the bottom") {
    CHECK(fader_master_gain(1.f) == doctest::Approx(1.f));
    CHECK(fader_master_gain(0.f) == doctest::Approx(0.f));
}

TEST_CASE("switches: LOCK uses the end positions, centre reads as off") {
    CHECK(lock_switch(0) == false);
    CHECK(lock_switch(1) == false);
    CHECK(lock_switch(2) == true);
    CHECK(lock_switch(-1) == false);       // a corrupt patch must not lock
    CHECK(lock_switch(99) == false);
}

TEST_CASE("switches: SCALE gates the menu's values and never invents one") {
    const int S = SCALE_DORIAN, R = 5;
    CHECK(scale_switch(0, S, R).scale_ovr == -1);
    CHECK(scale_switch(0, S, R).root_ovr == -1);
    CHECK(scale_switch(1, S, R).scale_ovr == S);
    CHECK(scale_switch(1, S, R).root_ovr == -1);
    CHECK(scale_switch(2, S, R).scale_ovr == S);
    CHECK(scale_switch(2, S, R).root_ovr == R);
    // Out of range is AUTO, the rule scale_of_knob already applies.
    CHECK(scale_switch(7, S, R).scale_ovr == -1);
    CHECK(scale_switch(-3, S, R).root_ovr == -1);
}

TEST_CASE("labels: TSV-hostile characters are stripped, not escaped") {
    CHECK(sanitize_label("a\tb", 32) == "ab");
    CHECK(sanitize_label("a\nb\r\nc", 32) == "abc");
    CHECK(sanitize_label("abcdef", 3) == "abc");
    CHECK(sanitize_label("", 32).empty());
}

TEST_CASE("export: the header row and column order match pool.tsv") {
    Place p[kPadCount];
    const std::string tsv = export_pool_tsv(p, kPadCount);
    CHECK(tsv.compare(0, 33, "code\tarch\tdate\tfp\tpad\tname\tnote\n") == 0);
}

TEST_CASE("export: every pad emits a row, and the empty interior columns "
          "still emit their tabs") {
    Place p[kPadCount];
    for (int i = 0; i < kPadCount; ++i)
        std::snprintf(p[i].code, sizeof p[i].code, "%s", kHouseCode);
    p[0].name = "First light";
    p[0].note = "It carries at 0.2";

    const std::string tsv = export_pool_tsv(p, kPadCount);
    int lines = 0;
    for (char c : tsv) if (c == '\n') ++lines;
    CHECK(lines == kPadCount + 1);          // header plus twelve rows

    const std::size_t rowStart = tsv.find('\n') + 1;
    const std::size_t rowEnd = tsv.find('\n', rowStart);
    const std::string row = tsv.substr(rowStart, rowEnd - rowStart);
    int tabs = 0;
    for (char c : row) if (c == '\t') ++tabs;
    CHECK(tabs == 6);                       // seven columns
    CHECK(row.find(kHouseCode) == 0);
    CHECK(row.find("\t\t\t1\t") != std::string::npos);   // date, fp empty; pad 1
    CHECK(row.find("First light") != std::string::npos);
    CHECK(row.find("It carries at 0.2") != std::string::npos);
}

TEST_CASE("export: the arch column is spelled with the enum's short name") {
    CHECK(std::strcmp(arch_name(ARCH_DRONE), "DRONE") == 0);
    CHECK(std::strcmp(arch_name(ARCH_PULSE), "PULSE") == 0);
    CHECK(std::strcmp(arch_name(ARCH_ARP), "ARP") == 0);
    CHECK(std::strcmp(arch_name(ARCH_FRAGMENT), "FRAGMENT") == 0);
    CHECK(std::strcmp(arch_name(-1), "") == 0);
    CHECK(std::strcmp(arch_name(ARCH_COUNT), "") == 0);
}

TEST_CASE("export: an undecodable code leaves arch empty rather than "
          "dropping the row") {
    Place p[1];
    std::snprintf(p[0].code, sizeof p[0].code, "%s", "not-a-code");
    const std::string tsv = export_pool_tsv(p, 1);
    int lines = 0;
    for (char c : tsv) if (c == '\n') ++lines;
    CHECK(lines == 2);
    CHECK(tsv.find("not-a-code\t\t") != std::string::npos);
}
```

- [ ] **Step 2: Wire the test into CMake and run it to verify it fails**

Add one line to `CMakeLists.txt`, immediately after `tests/test_glow_ui.cpp`
(line 136):

```cmake
    tests/test_touch_pads.cpp
```

Run:

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: FAIL at compile time with `vcv/src/touch_pads.hpp: No such file or
directory`.

- [ ] **Step 3: Write the header**

Create `host/vcv/src/touch_pads.hpp`:

```cpp
// host/vcv/src/touch_pads.hpp
//
// FireFlow Glow on Simple Touch 2: the module logic that needs no Rack type --
// the pad gesture, the assignable fader/switch mappings and the pool.tsv
// export. Kept out of Glow.cpp so the desktop doctest suite can test it
// headlessly, the same split glow_ui.hpp and bbd_edge_state.hpp already use.
//
// No <rack.hpp>, no jansson, no widgets. Glow.cpp is the only file that knows
// what a Module is.
#pragma once
#include <cstddef>
#include <cstdio>
#include <string>
#include "flow/flow_ids.h"
#include "flow/terrain.h"
#include "flow/terrain_code.h"

namespace spkyvcv {

inline constexpr int kPadCount = 12;

// Hold threshold for "lean on a pad to reroll it" (spec §5.3). A STARTING
// VALUE tuned by ear, not a measurement: NEW's 1.5 s is far too sluggish for a
// pad, and a mouse button is not a capacitive plate anyway. Whoever retunes it
// should not mistake this number for a result.
inline constexpr double kPadHoldS = 0.4;

enum class PadAction { NONE, WAKE, REROLL };

struct PadEvent {
    PadAction action = PadAction::NONE;
    int pad = -1;
};

// The pad gesture (spec §5.3). Waking happens on PRESS, not on release: that
// keeps a tap latency-free, and it is what makes "tap the same pad again"
// return to the curated state without a special case -- the second tap is a
// plain wake, and a wake IS the curated state.
//
// State is one live pad and one flag, not twelve states, because an excursion
// is transient by design (parent spec §3: "tap the pad again -> back to the
// curated state. No undo mechanism is needed for this").
struct PadGesture {
    int  live = -1;          // which pad's place is playing, -1 = none yet
    bool excursion = false;  // has the live pad been rerolled since its wake

    void reset() {
        live = -1;
        excursion = false;
        _held = -1;
        _fired = false;
        for (int i = 0; i < kPadCount; ++i) _prev[i] = false;
    }

    // Adopt `down` as the baseline WITHOUT emitting an edge. Called after a
    // patch load: Rack saves momentary params, so a pad can come back pressed,
    // and treating that as a rising edge would wake -- then reroll 400 ms
    // later -- on top of the terrain that was just restored. Same rule
    // GestureBridge encodes for NEW.
    void prime(const bool* down) {
        for (int i = 0; i < kPadCount; ++i) _prev[i] = down[i];
    }

    PadEvent update(const bool* down, double now_s) {
        PadEvent ev;

        // A rising edge only counts while nothing else is held: two pads down
        // at once (MIDI-mapped, or a stuck param) must not interleave.
        if (_held < 0) {
            for (int i = 0; i < kPadCount; ++i) {
                if (down[i] && !_prev[i]) {
                    _held = i;
                    _heldSince = now_s;
                    _fired = false;
                    live = i;
                    excursion = false;
                    ev.action = PadAction::WAKE;
                    ev.pad = i;
                    break;
                }
            }
        } else if (!down[_held]) {
            _held = -1;
        } else if (!_fired && now_s - _heldSince >= kPadHoldS) {
            _fired = true;
            ev.action = PadAction::REROLL;
            ev.pad = _held;
        }

        for (int i = 0; i < kPadCount; ++i) _prev[i] = down[i];
        return ev;
    }

private:
    bool   _prev[kPadCount] = {};
    int    _held = -1;
    double _heldSince = 0.0;
    bool   _fired = false;
};

// One curated place as the module holds it. The code is the identity; name and
// note are the human half and travel into the export (spec §6.3, §6.4).
struct Place {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    std::string name;
    std::string note;
};

enum class FaderTarget  { OFF, TEMPO, MASTER };
enum class SwitchTarget { OFF, LOCK, SCALE };

// Fader 0..1 -> BPM across P_TEMPO_BPM's declared range (flow_params.h).
// The terrain owns the tempo and Flow re-pushes it on every terrain change, so
// the call site has to re-apply this EVERY control tick -- see Glow.cpp.
inline float fader_tempo_bpm(float knob01) { return 50.f + knob01 * 90.f; }

// Linear output gain. Default is unity at the top; a module that boots at half
// gain is a bug report.
inline float fader_master_gain(float knob01) { return knob01; }

// A three-position switch driving a two-valued target uses the end positions;
// the centre reads as the lower one. Anything out of range reads as off, the
// rule scale_of_knob already applies -- a corrupt patch must not lock the
// generator.
inline bool lock_switch(int pos) { return pos == 2; }

struct TonalityGate {
    int scale_ovr = -1;   // -1 = AUTO
    int root_ovr  = -1;   // -1 = AUTO
};

// The SCALE switch GATES the menu's values; it never selects one. Position 0
// is AUTO, and so is anything out of range.
inline TonalityGate scale_switch(int pos, int menu_scale, int menu_root) {
    TonalityGate g;
    if (pos == 1) {
        g.scale_ovr = menu_scale;
    } else if (pos == 2) {
        g.scale_ovr = menu_scale;
        g.root_ovr = menu_root;
    }
    return g;
}

// Strip, do not escape: a tab or a newline in a name would break the TSV of
// export_pool_tsv, and an escaped one would come back wrong through a
// hand-edited pool.tsv. Truncates to `cap` characters.
inline std::string sanitize_label(const std::string& in, std::size_t cap) {
    std::string out;
    out.reserve(in.size() < cap ? in.size() : cap);
    for (char c : in) {
        if (c == '\t' || c == '\n' || c == '\r') continue;
        if (out.size() >= cap) break;
        out.push_back(c);
    }
    return out;
}

inline constexpr std::size_t kNameCap = 32;
inline constexpr std::size_t kNoteCap = 120;

inline const char* arch_name(int arch) {
    switch (arch) {
        case spky::flow::ARCH_DRONE:    return "DRONE";
        case spky::flow::ARCH_PULSE:    return "PULSE";
        case spky::flow::ARCH_ARP:      return "ARP";
        case spky::flow::ARCH_FRAGMENT: return "FRAGMENT";
        default:                        return "";
    }
}

// The pool.tsv rows for the twelve pads (parent spec §4.3), column order
// code / arch / date / fp / pad / name / note.
//
// date, fp and note-if-unwritten stay EMPTY on purpose: the fingerprint is
// computed by the gate in tests/, and a second producer of it in a second
// language is exactly the silent divergence that gate exists to catch.
// They are interior columns, so their tabs are still emitted.
//
// Line ending is \n, not \r\n: the destination is a repo file.
inline std::string export_pool_tsv(const Place* places, int n) {
    std::string out = "code\tarch\tdate\tfp\tpad\tname\tnote\n";
    for (int i = 0; i < n; ++i) {
        const Place& p = places[i];
        spky::flow::TerrainState st;
        const bool ok = spky::flow::decode_code(p.code, st);
        char pad[8];
        std::snprintf(pad, sizeof pad, "%d", i + 1);

        out += p.code;
        out += '\t';
        out += ok ? arch_name(spky::flow::arch_of(st.master)) : "";
        out += "\t\t\t";              // date, fp
        out += pad;
        out += '\t';
        out += p.name;
        out += '\t';
        out += p.note;
        out += '\n';
    }
    return out;
}

}  // namespace spkyvcv
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: PASS.

- [ ] **Step 5: Prove one assertion can go red**

Temporarily change `kPadHoldS` to `4.0` in `touch_pads.hpp`, rebuild, and run
the suite. Expected: the "holding past the threshold rerolls once" case fails.
Put `0.4` back and re-run to green.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/touch_pads.hpp tests/test_touch_pads.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(glow): the pad gesture gets a home the desktop suite can reach

Twelve pads, one live pad and one flag -- not twelve states, because an
excursion is transient by design. Waking on press rather than release is
what makes "tap the same pad again" need no special case: the second tap
is a plain wake, and a wake is the curated state.

prime() exists because Rack saves momentary params. Without it a patch
saved mid-hold comes back pressed, reads as a rising edge, and reroll
fires 400 ms after load on top of the terrain that was just restored.

No Rack type crosses into the header, so it builds and tests on the
desktop the way glow_ui.hpp already does.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The geometry table

The panel cannot be drawn without measured centres, and the first draft of the
spec failed exactly here. This task produces the numbers as a reviewable
artifact of their own, guarded before any SVG exists.

**Files:**
- Create: `host/vcv/res/ref/touch2-fx-2026-08-11.png`
- Create: `host/vcv/res/touch2_geometry.py`
- Create: `host/vcv/res/test_touch2_geometry.py`
- Modify: `CMakeLists.txt` (a new `add_test` beside `flow_panel_guard`)

**Interfaces:**
- Consumes: nothing.
- Produces, importable as `import touch2_geometry as geo`:
  `geo.PLATE_W`, `geo.PLATE_H`, `geo.PX_PER_MM`, `geo.SRC_IMAGE`,
  `geo.PADS` (12 `(x, y)` tuples in mm, reading order),
  `geo.KNOBS` (6 tuples, upper row left→right then lower row left→right),
  `geo.FADERS` (2 tuples), `geo.FADER_TRAVEL` (mm),
  `geo.SWITCHES` (2 tuples), `geo.JACKS` (2 tuples, upper then lower).

- [ ] **Step 1: Pin the source image**

Copy the reference photo into the repo:

```bash
mkdir -p host/vcv/res/ref
cp "/c/Users/bernd/Pictures/Screenshots/Screenshot 2026-08-11 100919.png" \
   host/vcv/res/ref/touch2-fx-2026-08-11.png
```

**Check with the owner before committing this file.** It is a photograph of a
Synthux product and `mcbronkowitch/fireflow` is public. If it may not be
committed, the fallback is to store it outside the repo and put its absolute
path in `SRC_IMAGE` — the guard in Step 3 does not read the image, so nothing
breaks; only the provenance weakens.

**If a vendor faceplate template has arrived** (spec §2.4 — Touch 2 ships five
swappable plates, so the vector files exist somewhere), use it instead: put it
here, name it in `SRC_IMAGE`, and read exact coordinates out of it. Then Step 3
is transcription rather than measurement and the ±-error note in the file header
can say so. Do not wait for it; it blocks nothing.

- [ ] **Step 2: Write the failing guard**

Create `host/vcv/res/test_touch2_geometry.py`:

```python
#!/usr/bin/env python3
"""Guard rails for the measured Touch 2 control centres.

These assertions catch the classes of measuring error that matter -- a
miscounted control, a transposed pair, a decimal point in the wrong place --
without pretending to know the board's true dimensions. They deliberately do
NOT assert tolerances on the copper positions themselves: a gate whose red has
no remedy is not a gate.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_touch2_geometry.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import touch2_geometry as geo

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_plate_is_16hp():
    check(abs(geo.PLATE_W - 81.28) < 0.001,
          "plate width is %.3f, want 81.28 (16 HP)" % geo.PLATE_W)
    check(abs(geo.PLATE_H - 128.5) < 0.001,
          "plate height is %.3f, want 128.5" % geo.PLATE_H)


def test_counts():
    for name, want in (("PADS", 12), ("KNOBS", 6), ("FADERS", 2),
                       ("SWITCHES", 2), ("JACKS", 2)):
        have = len(getattr(geo, name))
        check(have == want, "%s has %d entries, want %d" % (name, have, want))


def test_everything_is_on_the_plate():
    for name in ("PADS", "KNOBS", "FADERS", "SWITCHES", "JACKS"):
        for i, (x, y) in enumerate(getattr(geo, name)):
            check(0.0 < x < geo.PLATE_W,
                  "%s[%d] x=%.2f is off the plate" % (name, i, x))
            check(0.0 < y < geo.PLATE_H,
                  "%s[%d] y=%.2f is off the plate" % (name, i, y))


def test_no_two_centres_coincide():
    """A copy-paste slip while measuring shows up here and nowhere else."""
    pts = []
    for name in ("PADS", "KNOBS", "FADERS", "SWITCHES", "JACKS"):
        for i, p in enumerate(getattr(geo, name)):
            pts.append(("%s[%d]" % (name, i), p))
    for i, (na, a) in enumerate(pts):
        for nb, b in pts[i + 1:]:
            d = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
            check(d > 1.0, "%s and %s are %.2f mm apart -- measuring slip?"
                  % (na, nb, d))


def test_zones_are_in_the_right_order_top_to_bottom():
    """The board reads jacks, then knobs, then pads. A transposed axis or a
    flipped image would break this and nothing else."""
    jack_y = max(y for _, y in geo.JACKS)
    knob_y = min(y for _, y in geo.KNOBS)
    pad_y = min(y for _, y in geo.PADS)
    check(jack_y < knob_y,
          "jacks (%.1f) must sit above the knobs (%.1f)" % (jack_y, knob_y))
    check(knob_y < pad_y,
          "knobs (%.1f) must sit above the pad field (%.1f)" % (knob_y, pad_y))


def test_faders_flank_the_knob_field():
    xs = [x for x, _ in geo.KNOBS]
    for i, (x, _) in enumerate(geo.FADERS):
        check(x < min(xs) or x > max(xs),
              "FADERS[%d] x=%.2f sits inside the knob field" % (i, x))
    check(geo.FADER_TRAVEL > 10.0,
          "fader travel %.1f mm is implausibly short" % geo.FADER_TRAVEL)


def test_switches_sit_inside_the_pad_field():
    pad_top = min(y for _, y in geo.PADS)
    for i, (_, y) in enumerate(geo.SWITCHES):
        check(y >= pad_top,
              "SWITCHES[%d] y=%.2f is above the pad field (%.2f) -- the "
              "TouchFX sketch puts both switches among the pads"
              % (i, y, pad_top))


def test_the_source_is_named():
    check(bool(geo.SRC_IMAGE),
          "SRC_IMAGE must name where these numbers came from")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("geometry OK")
```

Run:

```bash
cd host/vcv && python res/test_touch2_geometry.py
```

Expected: FAIL with `ModuleNotFoundError: No module named 'touch2_geometry'`.

- [ ] **Step 3: Measure, and write the table**

Open `host/vcv/res/ref/touch2-fx-2026-08-11.png` at a zoom where individual
pixels are readable. The image is the plate edge to edge, so the calibration is
`PLATE_W / image_width_px` mm per pixel — for a 417 px wide image, 81.28 / 417 =
0.19492 mm/px.

For each control, read the **centre** in pixels and multiply. Order matters and
is fixed by the guard and by Task 3:

- `PADS` — reading order, top-left to bottom-right. Where the artwork makes a
  pad's extent ambiguous, take the centre of the copper-coloured area.
- `KNOBS` — the TouchFX sketch's order: upper row left→right (`S31 S32 S33
  S34`), then lower row left→right (`S30`, `S35`).
- `FADERS` — left (`S36`) then right (`S37`). `FADER_TRAVEL` is the slot length
  in mm, not the handle.
- `SWITCHES` — left (`S09/S10`) then right (`S07/S08`).
- `JACKS` — upper then lower.

Create `host/vcv/res/touch2_geometry.py`. The structure is fixed; the numbers
below are **the shape of the answer, not the answer** — replace every one with
a measured value:

```python
#!/usr/bin/env python3
"""Measured control centres of the Synthux Simple Touch 2, in millimetres.

Data only: no drawing, no Rack, no SVG. res/gen_flow_panel.py consumes this,
and res/test_touch2_geometry.py guards it on its own.

SOURCE AND ITS LIMITS
---------------------
These numbers are read off a PHOTOGRAPH (SRC_IMAGE), calibrated to the plate's
16 HP width. Perspective and lens are in them. They are good enough for a Rack
panel -- Rack draws at ~2.95 px/mm and nothing it renders can resolve the
error -- and they are NOT a manufacturing source. This file is not a faceplate
draft. When the board arrives, a 600 dpi flatbed scan replaces this table and
everything downstream regenerates.

Channel names are the board's own, from the TouchFX sketch's ASCII drawing
(Synthux-Academy/simple-touch-instruments, daisyduino/TouchFX):

    |-| (*)   (*)   (*)    (*) |-|
    | | S31   S32   S33    S34 | |
    |||                        |||
    |_| (*)                (*) |_|
    S36 S30                S35 S37

      S10 o o S09    o S07
                    o S08
"""

SRC_IMAGE = "res/ref/touch2-fx-2026-08-11.png"

PLATE_W = 81.28          # 16 HP
PLATE_H = 128.5          # Eurorack 3U
PX_PER_MM = 417.0 / PLATE_W    # replace 417.0 with the real image width

# Upper row S31 S32 S33 S34 (left to right), then lower row S30, S35.
KNOBS = [
    (20.5, 46.2), (34.7, 46.2), (50.3, 46.2), (65.3, 46.2),
    (19.5, 63.4), (65.9, 63.4),
]

# S36 (left), S37 (right).
FADERS = [(7.8, 55.0), (78.0, 55.0)]
FADER_TRAVEL = 24.0

# S09/S10 (left), S07/S08 (right) -- both sit INSIDE the pad field.
SWITCHES = [(29.5, 86.0), (48.0, 90.5)]

# Upper, lower.
JACKS = [(7.4, 18.5), (7.4, 29.2)]

# Reading order, top-left to bottom-right.
PADS = [
    (10.0, 82.0), (30.0, 82.0), (52.0, 82.0), (72.0, 82.0),
    (10.0, 98.0), (30.0, 98.0), (52.0, 98.0), (72.0, 98.0),
    (10.0, 114.0), (30.0, 114.0), (52.0, 114.0), (72.0, 114.0),
]
```

- [ ] **Step 4: Run the guard to verify it passes**

```bash
cd host/vcv && python res/test_touch2_geometry.py
```

Expected: `geometry OK`.

- [ ] **Step 5: Prove the guard can go red**

Swap two `PADS` entries so they hold identical coordinates, re-run, and confirm
`test_no_two_centres_coincide` names them. Then move one `JACKS` entry below the
knobs and confirm `test_zones_are_in_the_right_order_top_to_bottom` fires.
Restore both and re-run to green.

- [ ] **Step 6: Wire the guard into ctest**

In `CMakeLists.txt`, immediately before the `flow_panel_guard` block
(around line 272):

```cmake
add_test(
    NAME touch2_geometry_guard
    COMMAND ${Python3_EXECUTABLE} res/test_touch2_geometry.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)
```

Run `ctest --test-dir build --output-on-failure -R touch2_geometry_guard`.
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/ref host/vcv/res/touch2_geometry.py \
        host/vcv/res/test_touch2_geometry.py CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(glow): the Touch 2 geometry stops being an assumption

The first draft of the spec asserted photo-derived positions and supplied
none of them, so it could not be implemented. This is the table: twelve
pad centres, six knobs, two faders, two switches, two jacks, in
millimetres, calibrated to the plate's 16 HP width.

The source photo is committed beside it, because a source that can be
deleted by tidying a Screenshots folder is not a source.

The guard asserts what a measuring slip actually looks like -- a
miscounted control, two centres on top of each other, a flipped axis --
and deliberately asserts no tolerance on the copper positions themselves.
A gate whose red has no remedy is not a gate.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: The panel generator

**This task knowingly breaks the plugin build.** It rewrites the generated
header's enums; `Glow.cpp` compiles again in Task 4. The gate for *this* task is
the panel guard and the rendered SVG, not `build-local.sh`.

**Files:**
- Modify: `host/vcv/res/gen_flow_panel.py` (rewritten)
- Modify: `host/vcv/res/test_flow_panel.py` (largely rewritten)
- Regenerate: `host/vcv/res/Glow.svg`, `host/vcv/src/generated_flow_panel.hpp`

**Interfaces:**
- Consumes: `touch2_geometry` from Task 2.
- Produces, in `generated_flow_panel.hpp`, namespace `spkyvcv::glow`:
  - `enum WidgetKind { WK_MACRO, WK_PAD, WK_FADER, WK_SWITCH, WK_OUT };`
  - `enum ParamId` — `MOTION, DENSITY, BRIGHT, DIRT, WANDER, SPACE,
    PAD_1 … PAD_12, FADER_L, FADER_R, SW_L, SW_R, NUM_PARAMS` (22 params)
  - `enum InputId { NUM_INPUTS };` — no table, no entries
  - `enum OutputId { OUT_L, OUT_R, NUM_OUTPUTS };`
  - `enum LightId { NUM_LIGHTS };`
  - `kPanelW = 81.280f`, `kPanelH = 128.500f`
  - `kParamCtls[]`, `kOutputCtls[]`, `kTexts[]` as today
  - per-kind footprints: `kKnobR`, `kPadW`, `kPadH`, `kPadRadius`,
    `kFaderW`, `kFaderH`, `kSwitchW`, `kSwitchH`, `kJackR`

- [ ] **Step 1: Rewrite the guard first, and watch it fail**

Replace `host/vcv/res/test_flow_panel.py`'s contract and geometry tests. Keep
`test_no_overlap`, `test_on_panel`, `test_labels_clear_every_glyph` and
`test_committed_files_match_the_generator` **but** move the first three onto
rectangles. The replacement file:

```python
#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel (Simple Touch 2).

Runs the generator in-process and asserts what must never drift: the enum
ORDER, the control complement of the board, rectangle-based collisions, and
that the committed SVG/header still match what the generator emits.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_flow_panel.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flow_panel as g

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def approx(a, b, tol=0.01):
    return abs(a - b) <= tol


# --- the frozen contract: enum ORDER defines ids in every saved patch --------
PARAM_ORDER = (['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'] +
               ['PAD_%d' % (i + 1) for i in range(12)] +
               ['FADER_L', 'FADER_R', 'SW_L', 'SW_R'])
OUTPUT_ORDER = ['OUT_L', 'OUT_R']


def test_enum_order():
    check([c.enum for c in g.PARAMS] == PARAM_ORDER,
          "param enum order drifted: %s" % [c.enum for c in g.PARAMS])
    check(g.INPUTS == [],
          "the board has no inputs; INPUTS must stay empty")
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER,
          "output enum order drifted: %s" % [c.enum for c in g.OUTPUTS])


def test_macro_params_match_flow_macro_order():
    # engine/flow/flow_ids.h: M_MOTION, M_DENSITY, M_BRIGHT, M_DIRT,
    # M_WANDER, M_SPACE. Glow.cpp indexes params[MOTION + m] directly, so the
    # first six params MUST be the six macros in that order -- which is why
    # re-sorting the KNOBS is done with kKnobMacro and not by moving enums.
    check([c.enum for c in g.PARAMS][:6] ==
          ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'],
          "the first six params must mirror flow_ids.h's Macro order")


def test_panel_size():
    check(approx(g.W, 81.28), "panel width is %.3f, want 81.28 (16 HP)" % g.W)
    check(approx(g.Hh, 128.5), "panel height is %.3f, want 128.5" % g.Hh)


def test_control_complement_matches_the_board():
    counts = {}
    for c in g.PARAMS + g.OUTPUTS:
        counts[c.kind] = counts.get(c.kind, 0) + 1
    for kind, want, what in ((g.MACRO, 6, "trim knobs"),
                             (g.PAD, 12, "touch pads"),
                             (g.FADER, 2, "faders"),
                             (g.SWITCH, 2, "switches"),
                             (g.OUT, 2, "jacks")):
        check(counts.get(kind, 0) == want,
              "want %d %s, have %d" % (want, what, counts.get(kind, 0)))


def test_geometry_comes_from_the_measured_table():
    """Positions must not be re-typed into the generator by hand."""
    import touch2_geometry as geo
    pads = [(c.x, c.y) for c in g.PARAMS if c.kind == g.PAD]
    check(pads == [(x, y) for x, y in geo.PADS],
          "pad centres drifted from touch2_geometry.PADS")
    jacks = [(c.x, c.y) for c in g.OUTPUTS]
    check(jacks == [(x, y) for x, y in geo.JACKS],
          "jack centres drifted from touch2_geometry.JACKS")


def test_knob_positions_follow_kKnobMacro():
    """The six macros keep enum order; which knob each SITS on is a table."""
    import touch2_geometry as geo
    for pos, macro_idx in enumerate(g.KNOB_MACRO):
        c = g.PARAMS[macro_idx]
        check((c.x, c.y) == geo.KNOBS[pos],
              "%s is not on knob position %d" % (c.enum, pos))


def _rect(c):
    w, h = g.footprint_of(c)
    return (c.x - w / 2.0, c.y - h / 2.0, c.x + w / 2.0, c.y + h / 2.0)


def test_no_overlap():
    all_ctls = g.PARAMS + g.OUTPUTS
    for i, a in enumerate(all_ctls):
        ax0, ay0, ax1, ay1 = _rect(a)
        for b in all_ctls[i + 1:]:
            bx0, by0, bx1, by1 = _rect(b)
            hit = (ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1)
            check(not hit, "%s and %s overlap" % (a.enum, b.enum))


def test_on_panel():
    for c in g.PARAMS + g.OUTPUTS:
        x0, y0, x1, y1 = _rect(c)
        check(x0 >= 2.0 and x1 <= g.W - 2.0,
              "%s runs off the plate horizontally" % c.enum)
        check(y0 >= 2.0 and y1 <= g.Hh - 2.0,
              "%s runs off the plate vertically" % c.enum)


def test_labels_clear_every_glyph():
    all_ctls = g.PARAMS + g.OUTPUTS
    for c in all_ctls:
        lx, ly = g.label_xy(c)
        for other in all_ctls:
            if other is c:
                continue
            x0, y0, x1, y1 = _rect(other)
            check(not (x0 < lx < x1 and y0 < ly < y1),
                  "%s's label baseline sits inside %s" % (c.enum, other.enum))


def test_only_the_pads_carry_printed_captions():
    """A printed TEMPO beside a fader assigned to `off` would be a lie baked
    into an SVG. Function names are runtime tooltips (spec 3.3)."""
    for c in g.PARAMS:
        if c.kind == g.PAD:
            check(c.label.isdigit(),
                  "pad %s must print its number, prints %r" % (c.enum, c.label))
        else:
            check(c.label == "",
                  "%s must print no caption, prints %r" % (c.enum, c.label))


def test_silkscreen_copy():
    words = [t.str for t in g.TEXTS]
    check('FireFlow' in words, "the logo must read FireFlow")
    check('GLOW' in words, "the logo must read GLOW")
    ys = {t.y for t in g.TEXTS if t.str in ('FireFlow', 'GLOW')}
    check(len(ys) == 1, "FireFlow and GLOW must sit on ONE line")
    joined = " ".join(words + [c.label for c in g.PARAMS + g.OUTPUTS])
    check('ton-k' not in joined and 'ton k' not in joined.lower(),
          "ton-k is the brand and must not appear on the panel")


def test_logo_font_weights():
    fireflow = next((t for t in g.TEXTS if t.str == "FireFlow"), None)
    glow = next((t for t in g.TEXTS if t.str == "GLOW"), None)
    check(fireflow is not None, "FireFlow text entry not found")
    check(glow is not None, "GLOW text entry not found")
    check(fireflow.weight is not None and glow.weight is not None,
          "both wordmark halves must carry a weight")
    check(fireflow.weight < glow.weight,
          "FireFlow must be lighter than GLOW")


def test_alpha_pennant_survives():
    panel = g.svg()
    check('id="alphaPennant"' in panel,
          "the early-alpha faceplate needs its pennant")
    check('ALPHA' in [t.str for t in g.TEXTS],
          "the pennant label must reach Rack's runtime text overlay")


def test_committed_files_match_the_generator():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    for path, produced in ((os.path.join(here, "Glow.svg"), g.svg()),
                           (os.path.join(root, "src",
                                         "generated_flow_panel.hpp"), g.header())):
        if not os.path.exists(path):
            FAILS.append("%s is missing -- run res/gen_flow_panel.py" % path)
            continue
        with open(path) as f:
            on_disk = f.read()
        check(on_disk == produced,
              "%s differs from the generator's output -- it was hand-edited, "
              "or the generator was changed without re-running it" % path)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("panel OK")
```

Run:

```bash
cd host/vcv && python res/test_flow_panel.py
```

Expected: FAIL — `AttributeError: module 'gen_flow_panel' has no attribute 'PAD'`.

- [ ] **Step 2: Rewrite the generator**

Replace `host/vcv/res/gen_flow_panel.py`. Keep the `gen_panel as base` import
for the palette, and keep `Ctl` / `Txt` / `text_svg` / `header()`'s shape.

```python
#!/usr/bin/env python3
"""Single source of truth for the FireFlow Glow VCV panel (Simple Touch 2).

16 HP, laid out on the measured control centres of a Synthux Simple Touch 2
(res/touch2_geometry.py): twelve touch pads, six trim knobs, two faders, two
switches and one stereo out.

This is a VCV panel. It is NOT the faceplate draft -- see touch2_geometry.py
for why the millimetres here are good enough for Rack and not for a router.

Palette is shared with the big Fireflow panel (gen_panel.py) so the two modules
read as one instrument. Layout is not shared.

Emits (both committed):
  - res/Glow.svg                     the faceplate
  - src/generated_flow_panel.hpp     enums + control/text tables

Run from host/vcv/:  python3 res/gen_flow_panel.py
The C++ never hardcodes a coordinate, label or colour -- it reads them from the
generated header, so graphics and widget placement can never drift apart.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as base
import touch2_geometry as geo

HP = 16
W  = HP * base.MM_PER_HP          # 81.28 mm
Hh = geo.PLATE_H                  # 128.5 mm

# --- printed footprints -------------------------------------------------------
# Widget classes are chosen in Glow.cpp; these are what the PLATE prints, and
# what the collision guard measures. Glow.cpp documents each widget against the
# figure here, the way it already does for the macro knobs.
KNOB_R    = 4.5                   # 9 mm trim knobs
PAD_W     = 17.0
PAD_H     = 13.0
PAD_R     = 2.2                   # corner radius
FADER_W   = 6.8                   # VCVSlider is 6.72 mm wide
FADER_H   = geo.FADER_TRAVEL
SWITCH_W  = 5.0
SWITCH_H  = 9.0
JACK_R    = 4.2

LOGO_Y = 10.0
ALPHA_FLAG_X = W - 6.9
ALPHA_FLAG_Y = 14.2
ALPHA_FLAG_H = 3.8

# --- control kinds ------------------------------------------------------------
MACRO  = "MACRO"
PAD    = "PAD"
FADER  = "FADER"
SWITCH = "SWITCH"
OUT    = "OUT"

FOOTPRINT = {
    MACRO:  (KNOB_R * 2, KNOB_R * 2),
    PAD:    (PAD_W, PAD_H),
    FADER:  (FADER_W, FADER_H),
    SWITCH: (SWITCH_W, SWITCH_H),
    OUT:    (JACK_R * 2, JACK_R * 2),
}
LBL_DY = {MACRO: 0.0, PAD: 0.0, FADER: 0.0, SWITCH: 0.0, OUT: -5.6}
LBL_SZ = {MACRO: 2.2, PAD: 2.6, FADER: 2.2, SWITCH: 2.2, OUT: 2.2}
WKMAP  = {MACRO: "WK_MACRO", PAD: "WK_PAD", FADER: "WK_FADER",
          SWITCH: "WK_SWITCH", OUT: "WK_OUT"}

# Knob POSITION -> index into PARAMS, i.e. which macro sits on which knob.
# The six macros keep flow_ids.h's enum order in PARAMS (Glow.cpp indexes
# params[MOTION + m] and six static_asserts pin it); re-sorting the panel is a
# change to this table alone. Same shape as glow_ui.hpp's kCvMacro.
KNOB_MACRO = [0, 1, 2, 3, 4, 5]   # S31 S32 S33 S34, then S30, S35


class Ctl(object):
    def __init__(self, enum, kind, x, y, label, tip):
        self.enum, self.kind = enum, kind
        self.x, self.y = x, y
        self.label, self.tip = label, tip


class Txt(object):
    def __init__(self, x, y, size, rgb, anchor, s, weight=None):
        self.x, self.y, self.size = x, y, size
        self.rgb, self.anchor, self.str = rgb, anchor, s
        self.weight = weight


def footprint_of(c):
    return FOOTPRINT[c.kind]


def label_xy(c):
    return (c.x, c.y + LBL_DY[c.kind])


# --- the tables ---------------------------------------------------------------
_MACRO_NAMES = ["MOTION", "DENSITY", "BRIGHT", "DIRT", "WANDER", "SPACE"]
_MACRO_TIPS = [
    "MOTION -- how much everything moves",
    "DENSITY -- how much happens",
    "BRIGHT -- spectral centre",
    "DIRT -- clean to driven",
    "WANDER -- predictable to wandering",
    "SPACE -- close to vast",
]
# Board channel per knob POSITION, from the TouchFX sketch.
_KNOB_CHAN = ["S31", "S32", "S33", "S34", "S30", "S35"]

PARAMS = [None] * 6
for _pos, _macro in enumerate(KNOB_MACRO):
    _x, _y = geo.KNOBS[_pos]
    PARAMS[_macro] = Ctl(_MACRO_NAMES[_macro], MACRO, _x, _y, "",
                         "%s  [%s]" % (_MACRO_TIPS[_macro], _KNOB_CHAN[_pos]))

for _i, (_x, _y) in enumerate(geo.PADS):
    PARAMS.append(Ctl("PAD_%d" % (_i + 1), PAD, _x, _y, str(_i + 1),
                      "Place %d -- tap: go there. Hold: reroll all six macro "
                      "domains, the ground stays. Tap again: back." % (_i + 1)))

for _i, (_x, _y) in enumerate(geo.FADERS):
    PARAMS.append(Ctl(["FADER_L", "FADER_R"][_i], FADER, _x, _y, "",
                      "Fader %s -- assignable from the context menu"
                      % ["S36", "S37"][_i]))

for _i, (_x, _y) in enumerate(geo.SWITCHES):
    PARAMS.append(Ctl(["SW_L", "SW_R"][_i], SWITCH, _x, _y, "",
                      "Switch %s -- assignable from the context menu"
                      % [["S09", "S10"][0], ["S07", "S08"][0]][_i]))

# The board has no inputs. The generator must emit no kInputCtls table for an
# empty list: `static const PanelCtl kInputCtls[] = {};` is a zero-length
# array, which is ill-formed in standard C++.
INPUTS = []

OUTPUTS = [
    Ctl("OUT_L", OUT, geo.JACKS[0][0], geo.JACKS[0][1], "L", "Main out, left"),
    Ctl("OUT_R", OUT, geo.JACKS[1][0], geo.JACKS[1][1], "R", "Main out, right"),
]

TEXTS = [
    Txt(W * 0.5 - 1.1, LOGO_Y, 4.2, base.MUTED, 2, "FireFlow", 300),
    Txt(W * 0.5, LOGO_Y, 4.2, base.INK, 1, "GLOW", 700),
    Txt(W - 3.0, 16.75, 1.15, base.WHITE, 0, "ALPHA", 700),
]
```

Then the SVG and header emitters. Replace `knob_svg` / `button_svg` /
`sel_svg` / `jack_svg` with one per kind, and keep `text_svg` unchanged:

```python
def mm(v):
    return "%.3f" % v


ANCHOR_SVG = {0: "middle", 1: "start", 2: "end"}


def knob_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="url(#knobCap)" stroke="%s" '
        'stroke-width="0.28"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.45" stroke-linecap="round"/>\n'
        % (mm(c.x), mm(c.y), mm(KNOB_R), base.GRAPHITE,
           mm(c.x), mm(c.y - KNOB_R * 0.40), mm(c.x), mm(c.y - KNOB_R * 0.82),
           base.WHITE))


def pad_svg(c):
    """A plate the widget draws over. The SVG tile is the printed footprint;
    TouchPlate paints the live/excursed state on top of it at runtime."""
    return (
        '  <rect class="touchPlate" x="%s" y="%s" width="%s" height="%s" '
        'rx="%s" fill="%s" fill-opacity="0.55" stroke="%s" '
        'stroke-width="0.28"/>\n'
        % (mm(c.x - PAD_W / 2.0), mm(c.y - PAD_H / 2.0), mm(PAD_W), mm(PAD_H),
           mm(PAD_R), base.PAPER_DEEP, base.LINE))


def fader_svg(c):
    return (
        '  <rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s" '
        'stroke="%s" stroke-width="0.28"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.6" stroke-linecap="round"/>\n'
        % (mm(c.x - FADER_W / 2.0), mm(c.y - FADER_H / 2.0), mm(FADER_W),
           mm(FADER_H), mm(FADER_W / 2.0), base.PAPER_DEEP, base.LINE,
           mm(c.x), mm(c.y - FADER_H / 2.0 + 1.2),
           mm(c.x), mm(c.y + FADER_H / 2.0 - 1.2), base.WELL))


def switch_svg(c):
    return (
        '  <rect x="%s" y="%s" width="%s" height="%s" rx="1.0" fill="%s" '
        'stroke="%s" stroke-width="0.28"/>\n'
        % (mm(c.x - SWITCH_W / 2.0), mm(c.y - SWITCH_H / 2.0), mm(SWITCH_W),
           mm(SWITCH_H), base.GRAPHITE, base.LINE))


def jack_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s"/>\n'
        % (mm(c.x), mm(c.y), mm(JACK_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y), mm(JACK_R * 0.38), base.WELL))


SVG_FOR = {MACRO: knob_svg, PAD: pad_svg, FADER: fader_svg,
           SWITCH: switch_svg, OUT: jack_svg}


def text_svg(x, y, size, rgb, anchor, s, weight=None):
    if weight is None:
        return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
                'font-size="%s" fill="%s" text-anchor="%s">%s</text>\n'
                % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], s))
    return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
            'font-size="%s" fill="%s" text-anchor="%s" font-weight="%d">%s</text>\n'
            % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], weight, s))


def svg():
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
               'viewBox="0 0 %s %s">\n' % (mm(W), mm(Hh), mm(W), mm(Hh)))
    out.append('  <defs>\n')
    out.append('  <radialGradient id="knobCap" cx="38%" cy="32%" r="75%">\n')
    out.append('    <stop offset="0" stop-color="#3a3d35"/>\n')
    out.append('    <stop offset="0.55" stop-color="%s"/>\n' % base.GRAPHITE)
    out.append('    <stop offset="1" stop-color="#15160f"/>\n')
    out.append('  </radialGradient>\n')
    out.append('  <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">\n')
    out.append('    <stop offset="0" stop-color="%s"/>\n' % base.PAPER_HI)
    out.append('    <stop offset="0.48" stop-color="%s"/>\n' % base.PAPER)
    out.append('    <stop offset="1" stop-color="%s"/>\n' % base.PAPER_LO)
    out.append('  </linearGradient>\n')
    out.append('  </defs>\n')
    out.append('  <rect x="0" y="0" width="%s" height="%s" fill="url(#plate)"/>\n'
               % (mm(W), mm(Hh)))
    out.append('  <rect id="glowPanelInnerBorder" x="0.650" y="0.650" '
               'width="%s" height="%s" rx="1.2" fill="none" stroke="%s" '
               'stroke-width="0.28"/>\n'
               % (mm(W - 1.3), mm(Hh - 1.3), base.LINE))
    out.append('  <circle cx="4.000" cy="8.650" r="0.650" fill="%s"/>\n' % base.GREEN)
    out.append('  <line id="glowBrandRuleLeft" x1="5.900" y1="8.650" '
               'x2="8.800" y2="8.650" stroke="%s" stroke-width="0.35"/>\n'
               % base.GREEN)
    out.append('  <line id="glowBrandRuleRight" x1="%s" y1="8.650" '
               'x2="%s" y2="8.650" stroke="%s" stroke-width="0.35"/>\n'
               % (mm(W - 9.1), mm(W - 6.2), base.COPPER))
    out.append('  <circle cx="%s" cy="8.650" r="0.650" fill="%s"/>\n'
               % (mm(W - 4.3), base.COPPER))
    out.append('  <polygon id="alphaPennant" points="%s,%s %s,%s %s,%s %s,%s %s,%s" '
               'fill="%s"/>\n'
               % (mm(W), mm(ALPHA_FLAG_Y), mm(ALPHA_FLAG_X), mm(ALPHA_FLAG_Y),
                  mm(ALPHA_FLAG_X + 1.7), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H / 2.0),
                  mm(ALPHA_FLAG_X), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H),
                  mm(W), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H), base.COPPER))
    for c in PARAMS + OUTPUTS:
        out.append(SVG_FOR[c.kind](c))
    for t in TEXTS:
        out.append(text_svg(t.x, t.y, t.size, t.rgb, t.anchor, t.str, t.weight))
    for c in PARAMS + OUTPUTS:
        if c.label:
            lx, ly = label_xy(c)
            out.append(text_svg(lx, ly, LBL_SZ[c.kind], base.INK, 0, c.label))
    out.append('</svg>\n')
    return "".join(out)
```

And the header, with the empty-input rule made explicit:

```python
def rgb(hexcol):
    return "0x" + hexcol.lstrip("#").upper()


def ctl_row(c):
    lx, ly = label_xy(c)
    return ('    { %s, %s, {%sf, %sf}, "%s", {%sf, %sf}, 0, %sf, %s, "%s" },\n'
            % (c.enum, WKMAP[c.kind], mm(c.x), mm(c.y), c.label,
               mm(lx), mm(ly), mm(LBL_SZ[c.kind]), rgb(base.INK), c.tip))


def enum_block(name, ctls, last):
    body = "".join("    %s,\n" % c.enum for c in ctls)
    return "enum %s {\n%s    %s\n};\n" % (name, body, last)


def header():
    out = []
    out.append("// GENERATED by res/gen_flow_panel.py -- do not edit by hand.\n")
    out.append("// Geometry comes from res/touch2_geometry.py, which was measured\n")
    out.append("// off %s. Those numbers are photo-derived and\n" % geo.SRC_IMAGE)
    out.append("// provisional; they get corrected against the board when it\n")
    out.append("// arrives. This panel is a VCV panel, NOT a faceplate draft.\n")
    out.append("#pragma once\n")
    out.append("namespace spkyvcv { namespace glow {\n")
    out.append("struct XY { float x, y; };\n")
    out.append("enum WidgetKind { WK_MACRO, WK_PAD, WK_FADER, WK_SWITCH, WK_OUT };\n")
    out.append("struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label;"
               " XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb;"
               " const char* tip; };\n")
    out.append("// anchor: 0 = middle, 1 = start (left-aligned), 2 = end (right-aligned)\n")
    out.append("struct PanelTxt { XY mm; float size; unsigned rgb;"
               " unsigned char anchor; int weight; const char* str; };\n")
    out.append(enum_block("ParamId", PARAMS, "NUM_PARAMS"))
    # The board has no inputs. An empty enum still yields NUM_INPUTS == 0, but
    # an empty ARRAY would be `PanelCtl kInputCtls[] = {}` -- a zero-length
    # array, a GCC extension and ill-formed in standard C++. So the enum is
    # emitted and the table is not; Glow.cpp has no configInput loop.
    out.append(enum_block("InputId", INPUTS, "NUM_INPUTS"))
    out.append(enum_block("OutputId", OUTPUTS, "NUM_OUTPUTS"))
    out.append("enum LightId {\n    NUM_LIGHTS\n};\n")
    out.append("static constexpr float kPanelW = %sf;\n" % mm(W))
    out.append("static constexpr float kPanelH = %sf;\n" % mm(Hh))
    out.append("static constexpr float kKnobR    = %sf;\n" % mm(KNOB_R))
    out.append("static constexpr float kPadW     = %sf;\n" % mm(PAD_W))
    out.append("static constexpr float kPadH     = %sf;\n" % mm(PAD_H))
    out.append("static constexpr float kPadR     = %sf;\n" % mm(PAD_R))
    out.append("static constexpr float kFaderW   = %sf;\n" % mm(FADER_W))
    out.append("static constexpr float kFaderH   = %sf;\n" % mm(FADER_H))
    out.append("static constexpr float kSwitchW  = %sf;\n" % mm(SWITCH_W))
    out.append("static constexpr float kSwitchH  = %sf;\n" % mm(SWITCH_H))
    out.append("static constexpr float kJackR    = %sf;\n" % mm(JACK_R))
    for name, ctls in (("kParamCtls", PARAMS), ("kOutputCtls", OUTPUTS)):
        out.append("static const PanelCtl %s[] = {\n" % name)
        for c in ctls:
            out.append(ctl_row(c))
        out.append("};\n")
    out.append("static const PanelTxt kTexts[] = {\n")
    for t in TEXTS:
        out.append('    { {%sf, %sf}, %sf, %s, %d, %d, "%s" },\n'
                   % (mm(t.x), mm(t.y), mm(t.size), rgb(t.rgb), t.anchor,
                      t.weight, t.str))
    out.append("};\n")
    out.append("} } // namespace spkyvcv::glow\n")
    return "".join(out)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    with open(os.path.join(here, "Glow.svg"), "w") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_flow_panel.hpp"), "w") as f:
        f.write(header())
    print("wrote res/Glow.svg and src/generated_flow_panel.hpp")
```

- [ ] **Step 3: Regenerate and run the guard**

```bash
cd host/vcv && python res/gen_flow_panel.py && python res/test_flow_panel.py
```

Expected: `panel OK`. If `test_no_overlap` fires, shrink `PAD_W`/`PAD_H` — the
tile size is ours, the centres are not. That is the only remedy, and it is the
intended one.

- [ ] **Step 4: Look at the rendered panel**

Open `host/vcv/res/Glow.svg` in a browser and confirm the three zones read as
the board: jacks top-left, wordmark top-right, four-over-two knobs flanked by
two faders, and a pad field with two switches in it. If the pad field reads as
accidental rather than designed, that is a finding for the owner — report it,
do not silently switch to a tidy grid.

- [ ] **Step 5: Prove one new assertion can go red**

Change `KNOB_MACRO` to `[1, 0, 2, 3, 4, 5]`, regenerate, and run the guard.
Expected: `test_knob_positions_follow_kKnobMacro` names MOTION. Put it back and
re-run to green.

- [ ] **Step 6: Commit**

```bash
cd "$(git rev-parse --show-toplevel)"
git add host/vcv/res/gen_flow_panel.py host/vcv/res/test_flow_panel.py \
        host/vcv/res/Glow.svg host/vcv/src/generated_flow_panel.hpp
git commit -m "$(cat <<'EOF'
feat(glow): the plate becomes the board

16 HP on the measured Touch 2 centres: twelve pads, six trim knobs, two
faders, two switches, one stereo out. Geometry comes from
touch2_geometry.py, and the guard asserts it comes from there rather than
being re-typed by hand.

Two things the old generator could not do. Collisions are rectangles now,
because a tile is not a circle and neither is a fader -- the scalar radius
model had nothing to say about either. And the empty input list emits an
enum but NO table: `PanelCtl kInputCtls[] = {}` is a zero-length array,
which is a GCC extension and ill-formed C++.

Only the pads print a caption. A printed TEMPO beside a fader the context
menu can set to `off` would be a lie baked into an SVG, so function names
live in tooltips.

Glow.cpp does not compile against this header yet; that is the next
commit.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: `Glow.cpp` — the new control surface

Brings the plugin back to a building, loading, sounding state on the new panel.
The context menu's new entries come in Task 5; this task keeps the menu it has
minus the lock toggle.

**Files:**
- Modify: `host/vcv/src/Glow.cpp`
- Modify: `host/vcv/src/glow_ui.hpp` (remove `kCvMacro`, `cv_to_macro`,
  `clock_bpm`, `led_level`)
- Modify: `tests/test_glow_ui.cpp` (remove the cases for those four)

**Interfaces:**
- Consumes: `touch_pads.hpp` (Task 1), `generated_flow_panel.hpp` (Task 3).
- Produces, for Task 5: `Glow::places[12]`, `Glow::pads`,
  `Glow::drawTwelve(uint32_t seed)`, `Glow::pinCurrent(int pad)`,
  `Glow::menuScale`, `Glow::menuRoot`, `Glow::faderTarget[2]`,
  `Glow::switchTarget[2]`, and the `UiOp` staging enum extended with
  `NEW_FULL`, `NEW_PARTIAL`, `UNDO`.

- [ ] **Step 1: Strip `glow_ui.hpp` and its tests, and verify the suite fails**

Delete from `host/vcv/src/glow_ui.hpp`: `kCvMacro`, `cv_to_macro`,
`clock_bpm`, `led_level`, **`KnobTracker`**, **`GestureBridge`**, and the
`#include "flow/gesture.h"`.

`KnobTracker` and `GestureBridge` are the decision the spec (§7) left to this
plan, so here it is: **both go.** `KnobTracker` exists only for the
hold-NEW-and-turn-a-knob gesture, and that gesture moved into a menu item where
there is nothing to track. `GestureBridge`'s rising-edge rule survives, but as
`PadGesture::prime`/`_prev` in `touch_pads.hpp`, tested there — keeping a second
copy in `glow_ui.hpp` with no caller is exactly the dead code §7 exists to
prevent. `engine/flow/gesture.h` itself is **untouched**: it is engine code and
the Seed firmware will want it.

**Keep** `kScaleKnobOrder`, `scale_of_knob`, `clamp_root_override`,
`RefuseFlash`, `GlowSave`, `GlowRestorePlan`, `glow_restore_plan`,
`glow_restore`, `glow_capture`.

Delete the matching `TEST_CASE`s from `tests/test_glow_ui.cpp` — the ones
naming `kCvMacro`, `cv_to_macro`, `clock_bpm`, `led_level`, `KnobTracker` and
`GestureBridge`. Keep everything else, especially the `kScaleKnobOrder` /
`kScaleW` pinning, which stays live because the SCALE switch still gates that
list.

Run:

```bash
source env.sh && cmake --build build
```

Expected: FAIL — `Glow.cpp` still references `CV_MOT`, `CLK`, `NEW_BTN`,
`NEW_L`, `spkyvcv::clock_bpm`, `spkyvcv::led_level`.

- [ ] **Step 2: Replace the module's control wiring**

In `Glow.cpp`:

Replace the CV static_assert (lines 47–54) — the GENRE assert goes with the
GENRE control, the CV one with the jacks. Add the include and the new state:

```cpp
#include "touch_pads.hpp"
#include "flow/flow_rng.h"      // spky::Rng for drawTwelve()'s seeded sequence
```

and delete `#include "flow/gesture.h"` — the module no longer decodes gestures.

Inside `struct Glow`, replace the `gest` / `newBtn` / clock members with:

```cpp
    spkyvcv::PadGesture pads;
    spkyvcv::Place places[spkyvcv::kPadCount];
    spkyvcv::RefuseFlash refuse;

    // Which target each assignable control drives (spec 4.3). Module state,
    // saved in dataToJson. Atomic because appendContextMenu writes them on the
    // UI thread while controlTick reads them on the audio thread -- the same
    // standing-value shape rootOverride already uses, not a UiOp (UiOp is a
    // one-shot exchange for an operation).
    std::atomic<int> faderTarget[2] {
        int(spkyvcv::FaderTarget::TEMPO), int(spkyvcv::FaderTarget::MASTER) };
    std::atomic<int> switchTarget[2] {
        int(spkyvcv::SwitchTarget::LOCK), int(spkyvcv::SwitchTarget::SCALE) };

    // The values the SCALE switch GATES. The switch never selects one.
    std::atomic<int> menuScale { spky::SCALE_AEOLIAN };
    std::atomic<int> menuRoot  { 0 };

    // GENRE is a draw constraint, and it moved from a knob to a menu item --
    // so it is now written on the UI thread. Atomic and pushed from
    // controlTick, exactly like rootOverride: this file's standing rule is
    // that every Flow mutation happens on the audio thread, and a menu item
    // calling flow.set_genre() directly would be the first exception.
    std::atomic<int> menuGenre { spky::flow::ARCH_ANY };

    // Every fresh module draws the same twelve places, so "pad 7" means the
    // same thing in a note, a video and somebody else's rack. A drawn place is
    // NOT curated -- parent spec 2 calls the draw a slot machine -- so these
    // twelve make the module playable, and nothing more. Pin curated terrain
    // onto a pad before treating a session as evidence.
    static constexpr uint32_t kPoolSeed = 0xF10Cu;

    float masterGain = 1.f;
```

Replace the `Glow()` constructor body's param loop:

```cpp
    Glow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (const auto& c : kParamCtls) {
            switch (c.kind) {
                case WK_MACRO:
                    configParam(c.id, 0.f, 1.f, 0.5f,
                                kMacroNames[c.id - MOTION]);
                    break;
                case WK_PAD: {
                    // configButton clears randomizeEnabled for us (Module.hpp:169),
                    // which is what we want: a Randomize that pokes twelve
                    // momentary pads is a fault, not a dice roll.
                    const int i = c.id - PAD_1;
                    auto* pq = configButton<PadQuantity>(c.id, c.label);
                    pq->place = &places[i];
                    pq->pad = i;
                    pq->description = c.tip;
                    break;
                }
                case WK_FADER:
                    // TEMPO's default sits mid-travel (95 BPM); MASTER's sits
                    // at unity, because a module that boots at half gain is a
                    // bug report.
                    configParam(c.id, 0.f, 1.f,
                                c.id == FADER_R ? 1.f : 0.5f, c.tip);
                    if (auto* pq = paramQuantities[c.id])
                        pq->randomizeEnabled = false;
                    break;
                case WK_SWITCH:
                    configSwitch(c.id, 0.f, 2.f, 0.f, c.tip,
                                 { "Down", "Centre", "Up" });
                    if (auto* pq = paramQuantities[c.id])
                        pq->randomizeEnabled = false;
                    break;
                // Named rather than defaulted so a future kind is a compile
                // error here instead of a param that silently gets no config.
                case WK_OUT: break;
            }
        }
        // No configInput loop: the board has no inputs, and the generator
        // emits no kInputCtls table for an empty list.
        for (const auto& c : kOutputCtls) configOutput(c.id, c.tip);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            fxmem.bbd[p][0] = bbd[p][0];
            fxmem.bbd[p][1] = bbd[p][1];
        }
        fxmem.reverb = &reverb;
        ctrlDiv.setDivision(kCtrlDiv);
    }
```

Add above `struct Glow`, next to the static_asserts:

```cpp
// The macro knobs keep c.label empty on the plate (spec 3.3: a printed caption
// would freeze an assignment the rehearsal is allowed to move), so their Rack
// names come from here rather than from the generated table.
static const char* kMacroNames[spky::flow::MACRO_COUNT] = {
    "MOTION", "DENSITY", "BRIGHT", "DIRT", "WANDER", "SPACE"
};

// A pad's NAME is runtime data (spec 6.3), so it cannot come from the
// generated header the way every other caption does -- configButton fixes its
// string at construction. This is the one deliberate carve-out from "the panel
// table is the only source": the tooltip label is computed live from the
// module's Place array, while the plate itself still prints only the number.
struct PadQuantity : SwitchQuantity {
    const spkyvcv::Place* place = nullptr;
    int pad = 0;
    std::string getLabel() override {
        if (place && !place->name.empty())
            return string::f("Pad %d  %s", pad + 1, place->name.c_str());
        return string::f("Pad %d", pad + 1);
    }
};

static_assert(int(spkyvcv::kPadCount) == PAD_12 - PAD_1 + 1,
              "the panel's pad count must match touch_pads.hpp");
static_assert(PAD_1 == SPACE + 1,
              "controlTick indexes params[PAD_1 + i]; the pads must be "
              "contiguous and follow the six macros");
static_assert(FADER_R == FADER_L + 1,
              "faderPos() indexes params[FADER_L + i]");
static_assert(SW_R == SW_L + 1,
              "switchPos() indexes params[SW_L + i]");
```

- [ ] **Step 3: Replace the draw, the reset and the tick**

Add the draw helper to `struct Glow`:

```cpp
    // Twelve places, three per archetype, from a fixed seed. draw_new's
    // genre branch can in principle exhaust kGenreDrawCap and return a
    // default TerrainState whose archetype does not match -- terrain.cpp
    // calls that edge ~1e-18 and leaves it uncorrected. Verifying with
    // arch_of costs one stream seed, so verify rather than inherit the
    // assumption twelve times over.
    void drawTwelve() {
        spky::Rng seq;
        seq.seed(kPoolSeed);
        spky::flow::TerrainState cur = {};
        int i = 0;
        for (int arch = 0; arch < spky::flow::ARCH_COUNT; ++arch) {
            for (int k = 0; k < 3; ++k) {
                spky::flow::TerrainState st = cur;
                for (int tries = 0; tries < 8; ++tries) {
                    st = spky::flow::draw_new(cur, seq, arch);
                    if (spky::flow::arch_of(st.master) == arch) break;
                }
                cur = st;
                spky::flow::encode_code(st, places[i].code,
                                        int(sizeof places[i].code));
                places[i].name.clear();
                places[i].note.clear();
                ++i;
            }
        }
        pads.reset();
    }

    void pinCurrent(int pad) {
        if (pad < 0 || pad >= spkyvcv::kPadCount) return;
        spky::flow::encode_code(flow.state(), places[pad].code,
                                int(sizeof places[pad].code));
    }

    bool wakePad(int pad) {
        spky::flow::TerrainState st;
        if (pad < 0 || pad >= spkyvcv::kPadCount) return false;
        if (!spky::flow::decode_code(places[pad].code, st)) return false;
        flow.wake(st);
        woken = true;
        return true;
    }
```

Replace `wakeHouse()`'s body so pad 1 is the house place:

```cpp
    void wakeHouse() {
        if (places[0].code[0] == '\0') drawTwelve();
        if (!wakePad(0)) {
            spky::flow::TerrainState st;
            if (!spky::flow::decode_code(spky::flow::kHouseCode, st)) st = {};
            flow.wake(st);
            woken = true;
        }
        // wake() itself never touches the lock, so without this a locked
        // module would land on pad 1 still locked.
        flow.set_lock(false);
        pads.live = 0;
        pads.excursion = false;
    }
```

Replace `onReset()`:

```cpp
    void onReset() override {
        // Tonality FIRST, before reinit/wakeHouse -- reinit() and wakeHouse()
        // force-push every parameter, so clearing afterwards would push the
        // PREVIOUS tonality once and self-correct a control period later.
        rootOverride = -1;
        menuScale = spky::SCALE_AEOLIAN;
        menuRoot = 0;
        faderTarget[0] = int(spkyvcv::FaderTarget::TEMPO);
        faderTarget[1] = int(spkyvcv::FaderTarget::MASTER);
        switchTarget[0] = int(spkyvcv::SwitchTarget::LOCK);
        switchTarget[1] = int(spkyvcv::SwitchTarget::SCALE);
        flow.set_genre(spky::flow::ARCH_ANY);
        flow.set_scale_override(-1);
        flow.set_root_override(-1);
        drawTwelve();                 // the SAME twelve: the seed is fixed
        reinit(curSr > 0.f ? curSr : 48000.f);
        wakeHouse();
    }
```

Replace `controlTick()`'s gesture and tempo sections:

```cpp
    void controlTick(float sr) {
        // Host-owned settings first: wake() below force-pushes every
        // parameter, so an override applied after it would miss that push.
        const int swScalePos = switchPos(spkyvcv::SwitchTarget::SCALE);
        const spkyvcv::TonalityGate ton =
            swScalePos < 0 ? spkyvcv::TonalityGate{}
                           : spkyvcv::scale_switch(swScalePos,
                                                   menuScale.load(),
                                                   menuRoot.load());
        flow.set_scale_override(ton.scale_ovr);
        flow.set_root_override(ton.root_ovr >= 0 ? ton.root_ovr
                                                 : rootOverride.load());

        const int swLockPos = switchPos(spkyvcv::SwitchTarget::LOCK);
        if (swLockPos >= 0) flow.set_lock(spkyvcv::lock_switch(swLockPos));

        flow.set_genre(menuGenre.load());

        switch (uiOp.exchange(UiOp::NONE)) {
            case UiOp::SET_TERRAIN: flow.wake(uiState); woken = true; break;
            case UiOp::SET_LOCK:    flow.set_lock(uiLock); break;
            case UiOp::NEW_FULL:    if (!flow.new_full()) refuse.mark(flow.now_s()); break;
            case UiOp::NEW_PARTIAL: if (!flow.new_partial(uiMask)) refuse.mark(flow.now_s()); break;
            case UiOp::UNDO:        if (!flow.undo()) refuse.mark(flow.now_s()); break;
            case UiOp::RESTORE:
                flow.wake(uiState);
                flow.set_lock(uiLock);
                flow.restore_undo(uiUndo, uiHaveUndo);
                woken = true;
                break;
            case UiOp::NONE: default: break;
        }

        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
            flow.set_macro(m, params[MOTION + m].getValue());

        // --- the pads (spec 5.3) -----------------------------------------
        const double t = flow.now_s();
        bool down[spkyvcv::kPadCount];
        for (int i = 0; i < spkyvcv::kPadCount; ++i)
            down[i] = params[PAD_1 + i].getValue() > 0.5f;
        const spkyvcv::PadEvent ev = pads.update(down, t);
        if (ev.action == spkyvcv::PadAction::WAKE) {
            if (!wakePad(ev.pad)) refuse.mark(t);
        } else if (ev.action == spkyvcv::PadAction::REROLL) {
            // Under LOCK, wake() is not a gesture and is not refused, but
            // new_partial IS (flow.h). So pads still change place while holds
            // do nothing -- LOCK guards the generator, not the recall.
            const bool ok = flow.new_partial(0x3F);
            pads.excursion = ok;
            if (!ok) refuse.mark(t);
        }

        flow.tick();

        // Tempo: the terrain owns it and Flow re-pushes it on EVERY terrain
        // change, so a host-side fader has to be re-applied every tick or one
        // wake would silently hand the place its own tempo back. `off` is how
        // you ask for exactly that.
        const int fTempo = faderPos(spkyvcv::FaderTarget::TEMPO);
        if (fTempo >= 0)
            inst.set_tempo_bpm(spkyvcv::fader_tempo_bpm(
                params[FADER_L + fTempo].getValue()));

        const int fMaster = faderPos(spkyvcv::FaderTarget::MASTER);
        masterGain = fMaster >= 0
            ? spkyvcv::fader_master_gain(params[FADER_L + fMaster].getValue())
            : 1.f;
    }

    // Which physical control (0 = left, 1 = right) is assigned to `want`,
    // or -1 if neither is. Assigning both to the same target is allowed and
    // the left one wins -- a rehearsal rig should not refuse a knob setting.
    int faderPos(spkyvcv::FaderTarget want) const {
        for (int i = 0; i < 2; ++i)
            if (faderTarget[i].load() == int(want)) return i;
        return -1;
    }
    int switchPos(spkyvcv::SwitchTarget want) const {
        for (int i = 0; i < 2; ++i)
            if (switchTarget[i].load() == int(want))
                return int(params[SW_L + i].getValue() + 0.5f);
        return -1;
    }
```

Extend the `UiOp` enum and add `uiMask`:

```cpp
    enum class UiOp { NONE, SET_TERRAIN, SET_LOCK, RESTORE,
                      NEW_FULL, NEW_PARTIAL, UNDO };
    std::atomic<UiOp> uiOp { UiOp::NONE };
    uint8_t uiMask = 0x3F;              // NEW_PARTIAL
```

Replace `process()`'s clock block and output scaling:

```cpp
    void process(const ProcessArgs& args) override {
        if (args.sampleRate != curSr) reinit(args.sampleRate);
        if (ctrlDiv.process()) controlTick(args.sampleRate);

        float outl = 0.f, outr = 0.f;
        inst.process(nullptr, nullptr, &outl, &outr, 1);
        outputs[OUT_L].setVoltage(clamp(outl * masterGain, -1.f, 1.f) * 5.f);
        outputs[OUT_R].setVoltage(clamp(outr * masterGain, -1.f, 1.f) * 5.f);
    }
```

- [ ] **Step 4: Persist the new state, and stop the pads firing on load**

Extend `dataToJson`:

```cpp
    json_t* dataToJson() override {
        const spkyvcv::GlowSave s = spkyvcv::glow_capture(flow);
        json_t* root = json_object();
        json_object_set_new(root, "terrain", json_string(s.code));
        json_object_set_new(root, "lock", json_boolean(s.lock));
        if (s.have_undo) json_object_set_new(root, "undo", json_string(s.undo));
        json_object_set_new(root, "root", json_integer(rootOverride.load()));
        json_object_set_new(root, "menuScale", json_integer(menuScale.load()));
        json_object_set_new(root, "menuRoot", json_integer(menuRoot.load()));

        json_t* fa = json_array();
        json_t* sw = json_array();
        for (int i = 0; i < 2; ++i) {
            json_array_append_new(fa, json_integer(faderTarget[i].load()));
            json_array_append_new(sw, json_integer(switchTarget[i].load()));
        }
        json_object_set_new(root, "faders", fa);
        json_object_set_new(root, "switches", sw);

        json_t* pl = json_array();
        for (const auto& p : places) {
            json_t* o = json_object();
            json_object_set_new(o, "code", json_string(p.code));
            json_object_set_new(o, "name", json_string(p.name.c_str()));
            json_object_set_new(o, "note", json_string(p.note.c_str()));
            json_array_append_new(pl, o);
        }
        json_object_set_new(root, "places", pl);
        return root;
    }
```

Add to `dataFromJson`, before the existing terrain block:

```cpp
        if (json_t* v = json_object_get(root, "menuScale"))
            if (json_is_integer(v)) {
                const int s = int(json_integer_value(v));
                menuScale = (s >= 0 && s < spky::SCALE_LIST_COUNT)
                                ? s : spky::SCALE_AEOLIAN;
            }
        if (json_t* v = json_object_get(root, "menuRoot"))
            if (json_is_integer(v))
                menuRoot = spkyvcv::clamp_root_override(
                    int(json_integer_value(v))) < 0
                        ? 0 : int(json_integer_value(v));
        auto readTargets = [&](const char* key, std::atomic<int>* dst,
                               int lo, int hi) {
            json_t* a = json_object_get(root, key);
            if (!json_is_array(a)) return;
            for (int i = 0; i < 2 && i < int(json_array_size(a)); ++i) {
                json_t* e = json_array_get(a, i);
                if (!json_is_integer(e)) continue;
                const int v = int(json_integer_value(e));
                if (v >= lo && v <= hi) dst[i] = v;
            }
        };
        readTargets("faders", faderTarget, 0, int(spkyvcv::FaderTarget::MASTER));
        readTargets("switches", switchTarget, 0, int(spkyvcv::SwitchTarget::SCALE));

        if (json_t* a = json_object_get(root, "places")) {
            if (json_is_array(a)) {
                for (int i = 0; i < spkyvcv::kPadCount
                                && i < int(json_array_size(a)); ++i) {
                    json_t* o = json_array_get(a, i);
                    if (!json_is_object(o)) continue;
                    json_t* c = json_object_get(o, "code");
                    if (json_is_string(c))
                        std::snprintf(places[i].code, sizeof places[i].code,
                                      "%s", json_string_value(c));
                    json_t* n = json_object_get(o, "name");
                    if (json_is_string(n))
                        places[i].name = spkyvcv::sanitize_label(
                            json_string_value(n), spkyvcv::kNameCap);
                    json_t* t = json_object_get(o, "note");
                    if (json_is_string(t))
                        places[i].note = spkyvcv::sanitize_label(
                            json_string_value(t), spkyvcv::kNoteCap);
                }
            }
        }
```

And in `onAdd`, after the restore branch, force the pads quiet:

```cpp
        // Rack saves momentary params, so a patch stored mid-hold comes back
        // with a pad pressed. Zero them and prime the gesture with what it
        // sees, or the first controlTick reads a rising edge, wakes, and
        // rerolls 400 ms later on top of the terrain just restored.
        for (int i = 0; i < spkyvcv::kPadCount; ++i)
            params[PAD_1 + i].setValue(0.f);
        bool downNow[spkyvcv::kPadCount] = {};
        pads.prime(downNow);
```

- [ ] **Step 5: Replace the widget**

Add the `TouchPlate` widget above `struct GlowWidget`:

```cpp
// The one control Rack does not ship. app::Switch is ALREADY "a ParamWidget
// which, in momentary mode, sets the value to maxValue when the mouse is held
// and minValue when released" (app/Switch.hpp) -- deriving from ParamWidget
// instead would mean re-implementing onDragStart/onDragEnd by hand.
// app::SvgSwitch is the subclass that adds frames; Switch itself has no
// artwork, which is exactly the custom-drawn case.
//
// The widget stays stupid: tap-versus-hold is decided by the module in
// controlTick, never here. One place owns the gesture.
struct TouchPlate : app::Switch {
    Glow* mod = nullptr;
    int pad = 0;

    TouchPlate() {
        momentary = true;
        box.size = mm2px(Vec(kPadW, kPadH));
    }

    void draw(const DrawArgs& args) override {
        // Runtime state, not panel state -- which is why there are no
        // LightIds for the pads. Twelve lights in the generated header would
        // put runtime state into a panel table.
        const bool live = mod && mod->pads.live == pad;
        const bool exc = live && mod->pads.excursion;
        const bool flash = mod && mod->refuse.active(mod->flow.now_s());

        NVGcolor collar;
        if (flash && live)    collar = nvgRGB(0xb9, 0x65, 0x32);
        else if (exc)         collar = nvgRGB(0xb9, 0x65, 0x32);
        else if (live)        collar = nvgRGB(0x1d, 0x6f, 0x5f);
        else                  collar = nvgRGBA(0xd7, 0xcd, 0xbb, 0xff);

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f,
                       box.size.y - 1.f, mm2px(kPadR));
        nvgStrokeColor(args.vg, collar);
        nvgStrokeWidth(args.vg, live ? mm2px(0.55f) : mm2px(0.28f));
        nvgStroke(args.vg);
        app::Switch::draw(args);
    }
};
```

Replace `GlowWidget`'s param loop:

```cpp
        for (const auto& c : kParamCtls) {
            const Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_MACRO:
                    // Trimpot is Rack's only small knob; the plate prints 9 mm
                    // and Trimpot is the nearest stock size. Same reckoning
                    // the old panel made for RoundLargeBlackKnob.
                    addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    break;
                case WK_PAD: {
                    auto* p = createParamCentered<TouchPlate>(pos, module, c.id);
                    p->mod = module;
                    p->pad = c.id - PAD_1;
                    addParam(p);
                    break;
                }
                case WK_FADER:
                    // VCVSlider is 19.843 x 76.535 px at 75 DPI = 6.72 x 25.92
                    // mm, handle 3.98 mm. The board's fader measures about
                    // 24 mm, so the stock widget fits unmodified -- a lucky
                    // fit, recorded so nobody re-derives it.
                    addParam(createParamCentered<VCVSlider>(pos, module, c.id));
                    break;
                case WK_SWITCH:
                    // NKK is the three-position toggle. The board's switches
                    // are centre-off (two digital pins each), so three
                    // positions is the hardware, not a choice.
                    addParam(createParamCentered<NKK>(pos, module, c.id));
                    break;
                case WK_OUT: break;
            }
        }
        for (const auto& c : kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)),
                                                       module, c.id));
```

In `GlowText::draw`, delete the `kInputCtls` loop (line 592–593).

In `appendContextMenu`, delete the "Terrain lock" `createBoolMenuItem` block —
the LOCK switch owns that state now, and a physical switch plus a menu item for
one state is a synchronisation bug waiting to be filed.

- [ ] **Step 6: Build everything and verify**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS (including `flow_panel_guard` and `touch2_geometry_guard`).

Then, in a **fresh shell without `env.sh`**:

```bash
host/vcv/build-local.sh install
```

Expected: builds `plugin.dll` and installs. Open Rack, add FireFlow Glow, and
confirm: it makes sound; tapping a pad changes the place and lights its collar
green; holding a pad ~0.4 s turns the collar copper; tapping it again returns
it to green; the left fader changes tempo; the right fader changes level; the
right switch's three positions change tonality behaviour.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Glow.cpp host/vcv/src/glow_ui.hpp tests/test_glow_ui.cpp
git commit -m "$(cat <<'EOF'
feat(glow): the module moves onto the board's surface

Twelve pads on the pad gesture, six trim knobs on the macros, two
assignable faders, two assignable centre-off switches, stereo out. CV,
CLK and the NEW button are gone from the plate.

Three details that are not obvious from the spec. TouchPlate derives from
app::Switch, which already implements momentary press/release -- deriving
from ParamWidget would have meant rebuilding it. The TEMPO fader is
re-applied every control tick, because Flow re-pushes the terrain's own
tempo on every wake and would otherwise take the fader back. And the pads
are zeroed and primed in onAdd, since Rack persists momentary params and
a patch saved mid-hold would otherwise reroll itself 400 ms after load.

Pad state is drawn by the widget, not by Lights: twelve LightIds would
put runtime state into a panel table. The menu's terrain-lock toggle is
gone -- the LOCK switch owns that state, and two owners is a sync bug.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: The workshop menu

Everything the board cannot do lives here. The board is the stage; Rack is the
workshop, and this menu never ships to the Touch.

**Files:**
- Modify: `host/vcv/src/Glow.cpp` (`appendContextMenu` only)

**Interfaces:**
- Consumes: `Glow::places`, `Glow::pads`, `Glow::drawTwelve`,
  `Glow::pinCurrent`, `Glow::uiOp`/`uiMask`, `spkyvcv::export_pool_tsv`,
  `spkyvcv::sanitize_label`.
- Produces: nothing further.

- [ ] **Step 1: Add a text field that commits a place's name or note**

Above `struct GlowWidget`, beside `TerrainCodeField`:

```cpp
// A menu text field that writes a sanitized string back through a callback on
// Enter and closes the menu. ui::TextField itself has no commit behaviour.
struct LabelField : ui::TextField {
    std::function<void(const std::string&)> commit;
    std::size_t cap = spkyvcv::kNameCap;
    void onSelectKey(const SelectKeyEvent& e) override {
        if (commit && e.action == GLFW_PRESS
            && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            commit(spkyvcv::sanitize_label(text, cap));
            e.consume(this);
            if (MenuOverlay* overlay = getAncestorOfType<MenuOverlay>())
                overlay->requestDelete();
            return;
        }
        ui::TextField::onSelectKey(e);
    }
};
```

- [ ] **Step 2: Add the generator entries**

Inside `appendContextMenu`, after the existing "Genre" label:

```cpp
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Workshop"));

        menu->addChild(createMenuItem("Draw a new terrain", "", [m]() {
            m->uiOp = Glow::UiOp::NEW_FULL;
        }));

        menu->addChild(createSubmenuItem("Reroll one macro", "",
            [m](Menu* sub) {
                static const char* kNames[] = { "MOTION", "DENSITY", "BRIGHT",
                                                "DIRT", "WANDER", "SPACE" };
                for (int i = 0; i < spky::flow::MACRO_COUNT; ++i) {
                    sub->addChild(createMenuItem(kNames[i], "", [m, i]() {
                        m->uiMask = uint8_t(1u << i);
                        m->uiOp = Glow::UiOp::NEW_PARTIAL;
                    }));
                }
            }));

        menu->addChild(createMenuItem("Undo terrain", "", [m]() {
            m->uiOp = Glow::UiOp::UNDO;
        }));

        menu->addChild(createIndexSubmenuItem(
            "Genre (what a draw may pick)",
            { "Any", "Drone", "Pulse", "Arp", "Fragment" },
            [m]() { return m->menuGenre.load() + 1; },
            [m](int i) { m->menuGenre = i - 1; }));
```

- [ ] **Step 3: Add the assignment submenus**

```cpp
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Assignments"));
        static const std::vector<std::string> kFaderNames =
            { "Off", "Tempo", "Master" };
        static const std::vector<std::string> kSwitchNames =
            { "Off", "Lock", "Scale" };
        for (int i = 0; i < 2; ++i) {
            menu->addChild(createIndexSubmenuItem(
                i == 0 ? "Left fader (S36)" : "Right fader (S37)",
                kFaderNames,
                [m, i]() { return m->faderTarget[i].load(); },
                [m, i](int v) { m->faderTarget[i] = v; }));
        }
        for (int i = 0; i < 2; ++i) {
            menu->addChild(createIndexSubmenuItem(
                i == 0 ? "Left switch (S09/S10)" : "Right switch (S07/S08)",
                kSwitchNames,
                [m, i]() { return m->switchTarget[i].load(); },
                [m, i](int v) { m->switchTarget[i] = v; }));
        }

        std::vector<std::string> scales;
        for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
            scales.push_back(spky::SCALE_NAMES[spkyvcv::kScaleKnobOrder[i]]);
        menu->addChild(createIndexSubmenuItem(
            "Scale (what the SCALE switch fixes)", scales,
            [m]() {
                for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
                    if (spkyvcv::kScaleKnobOrder[i] == m->menuScale.load())
                        return i;
                return 0;
            },
            [m](int i) { m->menuScale = spkyvcv::kScaleKnobOrder[i]; }));
```

- [ ] **Step 4: Add the per-pad submenu and the export**

```cpp
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Places", "", [m](Menu* sub) {
            for (int i = 0; i < spkyvcv::kPadCount; ++i) {
                const std::string title =
                    string::f("Pad %d", i + 1) +
                    (m->places[i].name.empty() ? "" : "  " + m->places[i].name);
                sub->addChild(createSubmenuItem(title, "", [m, i](Menu* pm) {
                    pm->addChild(createMenuItem("Pin current terrain here", "",
                        [m, i]() { m->pinCurrent(i); }));
                    pm->addChild(createMenuLabel("Name"));
                    auto* nf = new LabelField;
                    nf->box.size.x = 180.f;
                    nf->cap = spkyvcv::kNameCap;
                    nf->setText(m->places[i].name);
                    nf->commit = [m, i](const std::string& s) {
                        m->places[i].name = s;
                    };
                    pm->addChild(nf);
                    pm->addChild(createMenuLabel("Note -- why it was kept"));
                    auto* tf = new LabelField;
                    tf->box.size.x = 180.f;
                    tf->cap = spkyvcv::kNoteCap;
                    tf->setText(m->places[i].note);
                    tf->commit = [m, i](const std::string& s) {
                        m->places[i].note = s;
                    };
                    pm->addChild(tf);
                }));
            }
        }));

        // The note is the perishable one: parent spec 4.3 defines it as "one
        // sentence: why it was kept", capturable only in the seconds after the
        // judgement. Exporting it here is the whole point of storing it.
        menu->addChild(createMenuItem("Copy all twelve as pool.tsv", "", [m]() {
            const std::string tsv =
                spkyvcv::export_pool_tsv(m->places, spkyvcv::kPadCount);
            glfwSetClipboardString(APP->window->win, tsv.c_str());
        }));

        menu->addChild(createMenuItem("Redraw all twelve places", "", [m]() {
            m->drawTwelve();
        }));
```

- [ ] **Step 5: Build and exercise every entry**

```bash
host/vcv/build-local.sh install
```

In Rack, walk the menu: draw a terrain, reroll one macro, undo, set the left
fader to Off (confirm tempo returns to the terrain's own), name pad 3, add a
note, copy the TSV and paste it into a scratch file to confirm seven
tab-separated columns and a header row.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/Glow.cpp
git commit -m "$(cat <<'EOF'
feat(glow): the workshop moves into the menu the board will never have

Draw, per-macro reroll, undo, genre, the fader and switch assignments, and
a submenu per pad for pinning, naming and the note. Plus a one-click
pool.tsv of all twelve rows.

The per-macro reroll is kept deliberately rather than dropped with the
NEW button: pads only ever call new_partial(0x3F), so without a caller for
a partial mask that API would have no user left and would rot.

The note field is here because parent spec 4.3 wants one sentence on why a
place was kept, and that sentence exists only in the seconds after the
judgement. A later pass over a TSV does not write it.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: The copy that still describes the old surface

**Files:**
- Modify: `host/vcv/plugin.json`
- Modify: `host/vcv/README.md` (the `## FireFlow Glow` section, ~504–660)
- Modify: `docs/roadmap.md` (Glow's status)
- Modify: `docs/release-notes.md`

- [ ] **Step 1: Find every stale claim**

```bash
cd "$(git rev-parse --show-toplevel)"
grep -rn "NEW button\|six macro knobs\|12 HP\|CV MOT\|CLK" \
     host/vcv/plugin.json host/vcv/README.md docs/roadmap.md docs/release-notes.md
```

Expected hits at least: `plugin.json`'s Glow description ("six macro knobs and a
NEW button"), `README.md:507` ("12 HP, six macro knobs, one button and two small
switches"), `README.md:540` (`### NEW — one button, four gestures`),
`README.md:561` (`### GENRE and SCALE`), `README.md:586` (Randomize),
`README.md:590` (Initialize), `README.md:598-599` (the CV and CLK jack rows),
`README.md:635` (`### The house seed`).

- [ ] **Step 2: Rewrite `plugin.json`'s Glow description**

```json
      "description": "The flow machine on a Synthux Touch 2 surface: twelve places on twelve pads, six macro trim knobs, two assignable faders, two assignable switches.",
```

- [ ] **Step 3: Rewrite the README's Glow section**

Replace the section with the new surface. It must state, at minimum:

- 16 HP; twelve pads, six trim knobs, two faders, two switches, stereo out.
- The pad gesture: tap wakes, hold ~0.4 s rerolls all six macro domains with
  the ground intact, tap again returns. Collar green = live, copper = excursed.
- Under LOCK, pads still change place; holds are refused. LOCK guards the
  generator, not the recall.
- Faders and switches are assignable from the context menu, and the defaults
  are Tempo/Master and Lock/Scale.
- Tempo: the terrain owns it; the fader overrides it; `Off` gives it back.
- Initialize: the same twelve places (the seed is fixed), names and notes
  cleared, assignments back to default, pad 1 live, lock and tonality cleared.
- Randomize: the six macros only.
- The workshop menu, and the sentence that justifies it: the board is the
  stage, Rack is the workshop, and the menu never ships to the Touch.
- That the twelve default places are **drawn, not curated**, so a session meant
  to answer a hardware question must use pinned curated terrain.
- The panel is a VCV panel and not a faceplate draft, and why.

- [ ] **Step 4: Update the roadmap and release notes**

In `docs/roadmap.md`, replace Glow's status entry with the Touch 2 surface and
a pointer to `docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md`.

In `docs/release-notes.md`, replace the "FireFlow Glow remains at 0.1" and
"Glow is unchanged" lines. `release-notes.md` is the *current* release's body,
not a changelog — rewrite it to describe this change, per `CLAUDE.md`.

- [ ] **Step 5: Verify nothing stale survives**

Re-run the grep from Step 1. Expected: no hits outside the specs and plans
directories (which are history and stay as written).

- [ ] **Step 6: Full green, then commit**

```bash
cd host/vcv && python res/gen_flow_panel.py && python res/test_flow_panel.py
cd "$(git rev-parse --show-toplevel)"
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

```bash
git add host/vcv/plugin.json host/vcv/README.md docs/roadmap.md docs/release-notes.md
git commit -m "$(cat <<'EOF'
docs(glow): the manual stops describing a module that no longer exists

The README still promised 12 HP, six macro knobs, one NEW button with four
gestures, and eight jacks including CV and CLK. plugin.json still sold a
NEW button in the module browser.

The replacement documents the pad gesture and what it does under LOCK, the
assignable faders and switches with their defaults, what Initialize and
Randomize now touch, and the workshop menu.

It also says the thing most likely to be forgotten later: the twelve
default places are DRAWN, not curated, so a session meant to answer a
hardware question has to pin curated terrain first.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

## Notes for whoever executes this

**The two places most likely to bite.**

1. `test_committed_files_match_the_generator` byte-compares. Any time you touch
   `gen_flow_panel.py`, re-run it before the guard, or you will chase a failure
   that is only staleness.
2. Never `source env.sh` in the shell you run `build-local.sh` in. The two
   toolchains must not mix; the system `g++` here is the Daisy ARM cross
   compiler.

**If the pad field looks wrong** after Task 3 Step 4, that is a finding, not a
bug to design around. Report it to the owner with the rendered SVG. The spec
(§5.2) explicitly chose true centres over a tidy grid, and it also said that if
the measured field cannot be made to look deliberate, that is worth knowing
early.

**What this plan does not build,** and it is on purpose: `engine/flow/places/
pool.tsv`, the fingerprint gate, the candidate-drawing tool and the audition
template. Those are the parent spec's §10 and they are a separate piece of work
— the one the review flagged as the actual critical path.
