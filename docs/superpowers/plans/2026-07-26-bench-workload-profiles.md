# Bench Workload Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a bench measurement image contain only the workload families its question needs, so the firmware stops overflowing SRAM every time an engine is added.

**Architecture:** One `BENCH_FAMILIES` make variable selects which `workloads_*.cpp` files compile and defines a `BENCH_FAMILY_*` macro per selection. A new family registry (`bench/families.h/.cpp`) replaces the two hardcoded family lists in `main.cpp` and `runner.cpp`, guarded by those macros. The firmware reports the families it actually contains in its output header; `run.py` gains a profile manifest, filters its existing row protocol to the profile's families, and refuses a capture whose reported families disagree with what was requested.

**Tech Stack:** C++17 cross-compiled with `arm-none-eabi-gcc` (DaisyToolchain) via GNU make; Python 3 for `run.py` and its `unittest`-style contract tests.

**Spec:** `docs/superpowers/specs/2026-07-26-bench-workload-profiles-design.md`

**Branch:** `bench-workload-profiles`, off `main`.

## Global Constraints

- Nothing outside `bench/` and `docs/` changes. The repo-root `Makefile`, `main.cpp`, `app.cpp`, `src/**` and `engine/**` are untouched.
- No workload's measurement semantics change. `bench/cycles.h`, `bench/runner.cpp`'s `run_workload`, `bench/anchor.cpp` and the QSPI programming path are not to be modified except where a step names them explicitly.
- **Family execution order is table order and must not depend on link order** (`bench/workload.h`'s own comment). The registry preserves the current order exactly, with `sampler` last — `bench/main.cpp` documents why: the sampler rows overwrite the SDRAM arena that earlier families used.
- The bench toolchain is `arm-none-eabi-gcc`, already on PATH. **Do NOT `source env.sh`** for the firmware build — that is the desktop clang environment and is not what builds the bench. Python steps need no environment setup.
- `full` is expected to fail to link (`SRAM_EXEC` over by ~12.4 KB, `SRAM` by ~23.4 KB at `main` tip). That is pre-existing and out of scope; it must fail at the **link** step, never at manifest load or family mismatch.
- Every commit message ends with exactly:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`

**Commands:**

```bash
# firmware, a chosen profile's families
cd bench && make -j8 BENCH_FAMILIES="system" build/bench.elf

# firmware through run.py, no hardware
cd bench && python run.py --profile system --build-only

# host-side contract tests, no hardware
cd bench && python -m pytest test_run_contract.py -v
```

Check how the existing contract tests are invoked before assuming pytest — if they are plain `unittest`, run them the way `bench/README.md` documents and use that runner throughout.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `bench/families.h` | The `Family` record and the registry's declaration. One place that says what a family is. |
| `bench/families.cpp` | The registry table, one `#if BENCH_FAMILY_*`-guarded entry per family, in execution order. Also `families_csv()`, the firmware's self-report string. |
| `bench/profiles.py` | The profile manifest: name → families + gates. Imported by `run.py` and by the contract tests. |

**Modified:**

| File | Change |
|---|---|
| `bench/Makefile` | `BENCH_FAMILIES` variable; family→source and family→define maps; conditional `CPP_SOURCES` and `C_DEFS`; a hard error on an unknown family name. |
| `bench/main.cpp:63-95` | Seven hand-written per-family loops collapse to one loop over the registry. |
| `bench/runner.cpp:50-57` | `find_workload`'s hardcoded `tables[]`, `counts[]` and literal `7` are replaced by the registry. |
| `bench/report.cpp` | `BENCH_BEGIN` gains a families field. |
| `bench/report.h` | `report_begin` signature gains the families string. |
| `bench/run.py` | `--profile`; build passes `BENCH_FAMILIES`; parser accepts the new field; validation filters to the profile and cross-checks reported families; gate ledger; evidence filename and CSV column. |
| `bench/test_run_contract.py` | Four new cases (spec §6). Existing canned `BENCH_BEGIN` lines gain the new field. |
| `bench/test_task8_contract.py` | Same header-field update wherever it builds canned captures. |
| `bench/README.md` | "the one command" becomes one command per profile, plus two sentences of history. |

---

## Task 1: The family registry and the build switch

**Files:**
- Create: `bench/families.h`, `bench/families.cpp`
- Modify: `bench/Makefile`, `bench/main.cpp:63-95`, `bench/runner.cpp:50-57`

**Interfaces:**
- Consumes: the existing `k*Workloads` / `k*Count` externs in `bench/workload.h`.
- Produces:
  - `struct bench::Family { const char* name; const Workload* rows; int count; };`
  - `extern const bench::Family kFamilies[]; extern const int kFamilyCount;`
  - `const char* bench::families_csv();` — space-separated family names, in registry order, e.g. `"system voice"`.

**Why the registry, not seven `#if`s in place.** The family list currently exists twice inside the firmware — as seven copy-pasted loops in `main.cpp` and as two parallel arrays plus a literal `7` in `runner.cpp`. Guarding both in place would make it two guarded copies that can drift. One registry, guarded once, is the whole point.

- [ ] **Step 1: Prove the starting state**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench"
make -j8 build/bench.elf 2>&1 | tail -20
```
Expected: FAILS to link with `region SRAM_EXEC overflowed` and `region SRAM overflowed`. Record the exact byte figures — they are the baseline every later step measures against.

- [ ] **Step 2: Write the registry header**

Create `bench/families.h`:

```cpp
#pragma once

#include "workload.h"

namespace bench {

// One workload family: a name, its row table, and how many rows it has.
//
// This registry is the ONLY place the set of families is written down inside
// the firmware. Before it existed, main.cpp carried seven hand-written loops
// and runner.cpp carried two parallel arrays plus a literal 7 -- two copies of
// the same list, which is exactly what a compile-time family switch must not
// have.
//
// Registry order IS execution order, and must not depend on link order
// (workload.h's contract). Keep `sampler` last: its rows overwrite the SDRAM
// arena earlier families load into -- see main.cpp's note.
struct Family {
    const char*     name;
    const Workload* rows;
    int             count;
};

extern const Family kFamilies[];
extern const int    kFamilyCount;

// Space-separated family names in registry order, e.g. "system voice".
// Emitted in the BENCH_BEGIN header so the host can prove the image it
// measured is the image it asked for.
const char* families_csv();

} // namespace bench
```

- [ ] **Step 3: Write the registry**

Create `bench/families.cpp`. Each entry is guarded by the macro the Makefile defines for that family:

```cpp
#include "families.h"

namespace bench {

const Family kFamilies[] = {
#if BENCH_FAMILY_SYSTEM
    { "system",  kCoreWorkloads,    kCoreCount    },
#endif
#if BENCH_FAMILY_VOICE
    { "voice",   kVoiceWorkloads,   kVoiceCount   },
#endif
#if BENCH_FAMILY_MEM
    { "mem",     kMemWorkloads,     kMemCount     },
#endif
#if BENCH_FAMILY_MOD
    { "mod",     kModWorkloads,     kModCount     },
#endif
#if BENCH_FAMILY_ABL
    { "abl",     kAblWorkloads,     kAblCount     },
#endif
#if BENCH_FAMILY_TAPS
    { "taps",    kTapsWorkloads,    kTapsCount    },
#endif
#if BENCH_FAMILY_SAMPLER
    // Last on purpose: the sampler rows use the 8 MB SDRAM arena as their load
    // source and overwrite whatever earlier families left in it.
    { "sampler", kSamplerWorkloads, kSamplerCount },
#endif
};

const int kFamilyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);

static_assert(sizeof(kFamilies) / sizeof(kFamilies[0]) > 0,
              "at least one BENCH_FAMILY_* must be defined");

const char* families_csv()
{
    static char buf[128];
    char*       out = buf;
    const char* end = buf + sizeof(buf) - 1;
    for (int i = 0; i < kFamilyCount; ++i) {
        if (i > 0 && out < end) *out++ = ' ';
        for (const char* c = kFamilies[i].name; *c && out < end; ++c)
            *out++ = *c;
    }
    *out = '\0';
    return buf;
}

} // namespace bench
```

The `#if` on an undefined macro evaluates to 0 in C++, so an unselected family simply drops out. Do **not** use `#ifdef` — the Makefile defines the selected ones as `=1` and leaves the rest undefined, and `#if` handles both uniformly.

- [ ] **Step 4: Collapse main.cpp's seven loops into one**

In `bench/main.cpp`, add `#include "families.h"` and replace the whole block of seven per-family loops (currently lines 63-95, from `for (int i = 0; i < bench::kCoreCount; ++i)` through the sampler loop) with:

```cpp
    for (int f = 0; f < bench::kFamilyCount; ++f) {
        const bench::Family& fam = bench::kFamilies[f];
        for (int i = 0; i < fam.count; ++i) {
            const bench::Workload& w = fam.rows[i];
            bench::report_row(w, bench::run_workload(w));
        }
    }
```

Move `main.cpp`'s existing comment about the sampler family running last into `families.cpp` next to the sampler entry — the ordering fact now lives where the order is decided. Leave every other comment in `main.cpp` alone, especially the long boot-info and anchor notes.

- [ ] **Step 5: Point find_workload at the registry**

In `bench/runner.cpp`, add `#include "families.h"` and replace `find_workload`'s body:

```cpp
const Workload* find_workload(const char* name)
{
    for (int f = 0; f < kFamilyCount; ++f)
        for (int i = 0; i < kFamilies[f].count; ++i)
            if (std::strcmp(kFamilies[f].rows[i].name, name) == 0)
                return &kFamilies[f].rows[i];
    return nullptr;
}
```

The literal `7` and the two parallel arrays go away with it. That literal is exactly the kind of thing that silently measures the wrong rows when a family is added.

- [ ] **Step 6: Add the build switch**

In `bench/Makefile`, above the `CPP_SOURCES` block, add the family maps and the selection. Note the mapping is not uniform — the `voice` family lives in `workloads_daisysp.cpp`:

```make
# --- workload family selection ---------------------------------------
# Which families this image contains. Override on the command line, or let
# run.py pass it from a profile:
#
#   make BENCH_FAMILIES="system voice" build/bench.elf
#
# Selecting a subset drops the family's workload translation unit AND its
# BENCH_FAMILY_* define, so families.cpp's registry entry disappears too; the
# engine code only that family referenced then goes with --gc-sections. That
# is where the SRAM comes back.
BENCH_FAMILIES ?= system voice mem mod abl taps sampler

FAMILY_SOURCE_system  = workloads_system.cpp
FAMILY_SOURCE_voice   = workloads_daisysp.cpp
FAMILY_SOURCE_mem     = workloads_memory.cpp
FAMILY_SOURCE_mod     = workloads_mod.cpp
FAMILY_SOURCE_abl     = workloads_abl.cpp
FAMILY_SOURCE_taps    = workloads_taps.cpp
FAMILY_SOURCE_sampler = workloads_sampler.cpp

FAMILY_DEFINE_system  = BENCH_FAMILY_SYSTEM
FAMILY_DEFINE_voice   = BENCH_FAMILY_VOICE
FAMILY_DEFINE_mem     = BENCH_FAMILY_MEM
FAMILY_DEFINE_mod     = BENCH_FAMILY_MOD
FAMILY_DEFINE_abl     = BENCH_FAMILY_ABL
FAMILY_DEFINE_taps    = BENCH_FAMILY_TAPS
FAMILY_DEFINE_sampler = BENCH_FAMILY_SAMPLER

# Fail loudly on a typo rather than silently building a smaller image.
$(foreach f,$(BENCH_FAMILIES),\
  $(if $(FAMILY_SOURCE_$(f)),,$(error unknown bench family '$(f)')))

FAMILY_SOURCES = $(foreach f,$(BENCH_FAMILIES),$(FAMILY_SOURCE_$(f)))
C_DEFS += $(foreach f,$(BENCH_FAMILIES),-D$(FAMILY_DEFINE_$(f))=1)
```

Then in `CPP_SOURCES`, remove the seven `workloads_*.cpp` lines and replace them with `$(FAMILY_SOURCES)`, and add `families.cpp` next to `runner.cpp`. Leave every `../engine/**` line in place: `--gc-sections` drops what nothing references, and `wt_bank.cpp` is always needed because `main.cpp` hashes the bank.

- [ ] **Step 7: Verify the mechanism and measure the saving**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench"
make clean && make -j8 BENCH_FAMILIES="system" build/bench.elf 2>&1 | tail -15
make clean && make -j8 BENCH_FAMILIES="system voice" build/bench.elf 2>&1 | tail -15
make clean && make -j8 build/bench.elf 2>&1 | tail -15
make -j8 BENCH_FAMILIES="system nonsense" build/bench.elf 2>&1 | tail -3
```
Expected, in order: `system` links with a region table well under the limits; `system voice` links; the default (all families) still fails to link with the same overflow as Step 1 — unchanged, because nothing was removed from it; the typo run stops with `unknown bench family 'nonsense'` and builds nothing.

Record every region table. The `system`-only figures are the evidence that the mechanism does what the spec claims.

- [ ] **Step 8: Commit**

```bash
git add bench/families.h bench/families.cpp bench/Makefile bench/main.cpp bench/runner.cpp
git commit -m "feat(bench): one family registry, selectable at build time

main.cpp carried seven hand-written per-family loops and runner.cpp two
parallel arrays plus a literal 7 -- two copies of the same list. Both now
read one guarded registry, and BENCH_FAMILIES selects which families compile
at all. Dropping a family drops its translation unit and its registry entry,
and --gc-sections takes the engine code only it referenced.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 2: The firmware reports what it contains

**Files:**
- Modify: `bench/report.h`, `bench/report.cpp:66-80`, `bench/main.cpp`, `bench/run.py:246-272` (the `parse` function)
- Test: `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `bench::families_csv()` (Task 1).
- Produces: `BENCH_BEGIN` gains an eighth field, the space-separated family list. `run.py`'s `parse()` returns a header dict with a new `"families"` key holding a `tuple` of family name strings.

**Why.** The manifest says what was *meant*. The failure mode everyone eventually hits is measuring a stale image after passing `--no-build`. The firmware reporting its own families, and the host insisting they match, is what catches that.

- [ ] **Step 1: Write the failing host test**

Add to `bench/test_run_contract.py`, following the file's existing style for building canned capture lines:

```python
def test_parse_reads_the_families_field():
    lines = [
        "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
        + "0" * 64 + ",dead,system voice\n",
        "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
        "BENCH_END\n",
    ]
    header, rows, _ = run.parse(lines)
    assert header["families"] == ("system", "voice")


def test_parse_rejects_a_header_without_families():
    """An old firmware image must not validate against the new host."""
    lines = [
        "BENCH_BEGIN,abc1234,480000000,96,dcache+icache," + "0" * 64 + ",dead\n",
        "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
        "BENCH_END\n",
    ]
    assert run.parse(lines) is None
```

Match the file's actual import name for `run.py` and its assertion style (plain `assert` vs `self.assertEqual`) — read it first and follow it.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench" && python -m pytest test_run_contract.py -k families -v
```
Expected: FAIL — `parse` returns `None` for the 8-field line, because the current guard is `if len(f) != 7: continue`.

- [ ] **Step 3: Widen the firmware header**

In `bench/report.h`, change the declaration to:

```cpp
void report_begin(const char* githash, const char* qspi_sha256,
                  const char* families);
```

In `bench/report.cpp`, extend the format string and the call. The current format is:

```cpp
        "BENCH_BEGIN,%s,%lu,96,%s,%s,%08lx%08lx%08lx\n";
```

Append `,%s` at the end for the families string, and pass `families` as the final argument. Keep every existing field in place and in order — `run.py` reads them positionally and the field meanings must not shift.

In `bench/main.cpp`, add `#include "families.h"` if Step 4 of Task 1 did not already, and change the call to:

```cpp
    bench::report_begin(BENCH_GIT_HASH, qspi_sha256, bench::families_csv());
```

- [ ] **Step 4: Teach the parser**

In `bench/run.py`'s `parse()`, change the `BENCH_BEGIN` branch:

```python
        if line.startswith("BENCH_BEGIN,"):
            f = line.rstrip("\n").split(",")
            # 8 fields since the families field landed. A 7-field header is a
            # pre-profiles firmware image: reject it rather than guess, so a
            # stale build cannot be measured against a profile it never knew
            # about.
            if len(f) != 8:
                continue
            header = {
                "githash": f[1],
                "clock": f[2],
                "block": f[3],
                "cache": f[4],
                "qspi_sha256": f[5],
                "device_id": f[6],
                "families": tuple(f[7].split()),
            }
```

Note the added `rstrip("\n")` — without it the families tuple's last entry would carry a trailing newline. Check whether the existing code already strips lines upstream; if it does, leave it out rather than stripping twice.

- [ ] **Step 5: Update the other canned headers**

Every existing `BENCH_BEGIN` line in `bench/test_run_contract.py` and `bench/test_task8_contract.py` now needs the eighth field. Grep for `BENCH_BEGIN` across both files and add a families value consistent with what each test's rows imply — a test whose rows are all `system` should say `system`.

- [ ] **Step 6: Run the tests and a build**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench"
python -m pytest test_run_contract.py test_task8_contract.py -v
make -j8 BENCH_FAMILIES="system" build/bench.elf 2>&1 | tail -12
```
Expected: all host tests PASS; the firmware still links.

- [ ] **Step 7: Commit**

```bash
git add bench/report.h bench/report.cpp bench/main.cpp bench/run.py bench/test_run_contract.py bench/test_task8_contract.py
git commit -m "feat(bench): firmware reports the families it actually contains

BENCH_BEGIN gains a families field and the host rejects a 7-field header
outright. The manifest says what was meant; this says what was built. The
gap between them is a stale --no-build image, which is a real failure mode
and not a hypothetical one.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 3: The profile manifest and filtered validation

**Files:**
- Create: `bench/profiles.py`
- Modify: `bench/run.py` (`build`, `validate_captures`, `main`'s argument parser)
- Test: `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `run.parse()`'s `header["families"]` (Task 2), the existing `BENCH_PROTOCOL_ROWS_BY_FAMILY` dict in `run.py`.
- Produces:
  - `bench/profiles.py`: `PROFILES: dict[str, Profile]` where `Profile` is a `namedtuple("Profile", "families gates")`; `families` a tuple of family names, `gates` a frozenset of gate names.
  - `run.py --profile NAME`, default `full`.
  - `run.validate_captures(captures, profile)` — the existing signature gains the profile.

**The gate names.** Exactly one gate is profile-scoped: `"wave_acceptance"`. Everything else — row-set exactness, duplicate rows, identity across repeats, checksum stability — is universal and runs for every profile.

- [ ] **Step 1: Write the manifest**

Create `bench/profiles.py`:

```python
"""Which workload families a bench image contains, and which acceptance gates
apply to it.

A profile names FAMILIES, never rows. run.py's BENCH_PROTOCOL_ROWS_BY_FAMILY
stays the single source of truth for rows and is merely filtered to the
profile's families -- so there is no second place where a row list can drift.

Gates: most of run.py's checks are universal and run for every profile. Only
`wave_acceptance` is profile-scoped, because it needs rows (synth_2x4,
wave_2x4) that only the `system` family supplies.
"""

from collections import namedtuple

Profile = namedtuple("Profile", "families gates")

WAVE_ACCEPTANCE = "wave_acceptance"

PROFILES = {
    # Carries the WAVE regression guard. Fits comfortably, which is the point:
    # that guard has been unenforceable since the full image stopped linking.
    "system": Profile(
        families=("system",),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
    # The complete run, as before profiles existed. Expected to FAIL TO LINK
    # until the engine shrinks or the region grows -- that debt is real and is
    # meant to be visible to whoever runs the bare command.
    "full": Profile(
        families=(
            "system", "voice", "mem", "mod", "abl", "taps", "sampler",
        ),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
}

DEFAULT_PROFILE = "full"


def resolve(name):
    """Look up a profile by name, or raise with the valid names listed."""
    try:
        return PROFILES[name]
    except KeyError:
        raise KeyError(
            "unknown bench profile %r (known: %s)"
            % (name, ", ".join(sorted(PROFILES)))
        )
```

There is deliberately no `body` profile: this branch is off `main`, where the `body` workload family does not exist. The BODY branch adds its own entry when it picks this work up.

- [ ] **Step 2: Write the failing tests**

Add to `bench/test_run_contract.py`:

```python
from profiles import PROFILES, WAVE_ACCEPTANCE, resolve


def _begin(families, githash="abc1234", device="dead"):
    return ("BENCH_BEGIN,%s,480000000,96,dcache+icache,%s,%s,%s\n"
            % (githash, "0" * 64, device, families))


def _row(family, name, avg, mx, checksum):
    return ("BENCH,%s,%s,%d,%d,0.00,0.00,%s\n"
            % (family, name, avg, mx, checksum))


def test_system_profile_validates_against_its_filtered_rowset():
    """A system-only capture is complete for the system profile."""
    names = run.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
    lines = [_begin("system")]
    for i, n in enumerate(names):
        avg = 100 if n != "synth_2x4" else 400
        avg = 200 if n == "wave_2x4" else avg
        lines.append(_row("system", n, avg, avg + 1, "%08x" % i))
    lines.append("BENCH_END\n")
    capture = run.parse(lines)
    run.validate_captures([capture, capture], resolve("system"))


def test_reported_families_must_match_the_requested_profile():
    """A stale image built for a different profile is rejected."""
    names = run.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
    lines = [_begin("system voice")]          # firmware says more than asked
    for i, n in enumerate(names):
        avg = 400 if n == "synth_2x4" else (200 if n == "wave_2x4" else 100)
        lines.append(_row("system", n, avg, avg + 1, "%08x" % i))
    lines.append("BENCH_END\n")
    capture = run.parse(lines)
    with pytest.raises(run.BenchValidationError) as excinfo:
        run.validate_captures([capture, capture], resolve("system"))
    assert "families" in str(excinfo.value)


def test_unknown_profile_name_is_rejected():
    with pytest.raises(KeyError):
        resolve("nonsense")


def test_a_profile_without_wave_acceptance_does_not_run_it():
    """The gate must be genuinely skipped, not accidentally satisfied.

    Neither shipped profile omits wave_acceptance, so this uses a synthetic
    one. Without it, nothing proves the gate is actually conditional -- a
    `if WAVE_ACCEPTANCE in profile.gates` that was never false would pass
    every test in this file.
    """
    from profiles import Profile

    ungated = Profile(families=("system",), gates=frozenset())
    names = run.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
    lines = [_begin("system")]
    for i, n in enumerate(names):
        # wave_2x4 deliberately SLOWER than synth_2x4: this capture would fail
        # wave_acceptance outright. A profile that does not declare the gate
        # must accept it anyway.
        avg = 100 if n == "synth_2x4" else (900 if n == "wave_2x4" else 100)
        lines.append(_row("system", n, avg, avg + 1, "%08x" % i))
    lines.append("BENCH_END\n")
    capture = run.parse(lines)

    run.validate_captures([capture, capture], ungated)      # accepted

    gated = Profile(families=("system",), gates=frozenset({WAVE_ACCEPTANCE}))
    with pytest.raises(run.BenchValidationError):
        run.validate_captures([capture, capture], gated)    # rejected
```

Match the file's existing import and assertion conventions.

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench" && python -m pytest test_run_contract.py -k "profile or families" -v
```
Expected: FAIL — `validate_captures` takes one argument, and nothing checks families.

- [ ] **Step 4: Filter the row protocol and check the families**

In `bench/run.py`, replace the module-level `BENCH_PROTOCOL_ROWSET` construction with a function, and thread the profile through `validate_captures`:

```python
from profiles import DEFAULT_PROFILE, WAVE_ACCEPTANCE, resolve


def protocol_rowset(profile):
    """The (family, name) pairs a capture from this profile must contain."""
    return frozenset(
        (family, name)
        for family in profile.families
        for name in BENCH_PROTOCOL_ROWS_BY_FAMILY[family]
    )
```

In `validate_captures(captures, profile)`, at the top of the per-capture loop, add the families cross-check before the row-set check — a mismatched image makes every later message misleading, so it must fail first:

```python
        reported = tuple(header["families"])
        if reported != tuple(profile.families):
            raise BenchValidationError(
                "run %d reports families %s but the profile requested %s "
                "(stale image? try without --no-build)"
                % (run_index, " ".join(reported) or "none",
                   " ".join(profile.families))
            )
```

Replace every use of the old module-level `BENCH_PROTOCOL_ROWSET` inside the function with `expected_rowset = protocol_rowset(profile)`, computed once before the loop.

Then make the WAVE gate conditional. Wrap the existing `synth_2x4`/`wave_2x4` block:

```python
        if WAVE_ACCEPTANCE in profile.gates:
            names = {row["name"] for row in rows}
            required = {"synth_2x4", "wave_2x4"}
            if not required.issubset(names):
                raise BenchValidationError(
                    "run %d is missing WAVE acceptance rows: %s"
                    % (run_index, ", ".join(sorted(required - names)))
                )
            # ... the existing avg/max/budget comparisons, unchanged ...
```

Leave the comparisons themselves exactly as they are.

- [ ] **Step 5: Wire up the CLI and the build**

In `run.py`'s `main()`, add the argument next to `--repeat`:

```python
    ap.add_argument(
        "--profile", default=DEFAULT_PROFILE,
        help="which workload families to build and measure "
             "(see bench/profiles.py)")
```

Resolve it early — before any build or hardware contact — so an unknown name fails instantly:

```python
    profile = resolve(args.profile)
```

Change `build()` to take the profile's families and pass them to make:

```python
def build(families):
    subprocess.run(
        ["make", "-j8", "build/qspi-programmer.elf"],
        cwd=PROGRAMMER_DIR,
        check=True,
    )
    subprocess.run(
        ["make", "-j8", "BENCH_FAMILIES=%s" % " ".join(families),
         "build/bench.elf"],
        cwd=HERE,
        check=True,
    )
    return prepare_existing_artifacts()
```

Update its call site to `build(profile.families)` and `validate_captures`'s call site to pass `profile`.

- [ ] **Step 6: Run the tests and both build paths**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench"
python -m pytest test_run_contract.py test_task8_contract.py -v
python run.py --profile system --build-only
python run.py --profile nonsense --build-only 2>&1 | tail -3
python run.py --build-only 2>&1 | tail -6
```
Expected: host tests PASS; `--profile system --build-only` succeeds; the unknown name fails immediately with the known-profiles list and **before** any make runs; the bare command (profile `full`) fails at the **link** step with the region overflow — not at manifest load, not on a family mismatch. Confirm which failure you actually got and say so in your report; the spec requires that distinction to stay visible.

- [ ] **Step 7: Commit**

```bash
git add bench/profiles.py bench/run.py bench/test_run_contract.py
git commit -m "feat(bench): profiles select families, validation follows

A profile names families, never rows: BENCH_PROTOCOL_ROWS_BY_FAMILY stays the
one source of truth for rows and is filtered to the profile. The families the
firmware reports must equal the families the profile asked for, checked before
anything else so a stale image cannot produce a misleading row-set error.

The WAVE acceptance gate becomes profile-scoped -- it needs system-family rows
-- while row-set exactness, duplicate detection, identity and checksum
stability stay universal.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 4: Evidence carries the profile and the gate ledger

**Files:**
- Modify: `bench/run.py` (`write_results`, around lines 611-680)
- Test: `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `profiles.resolve(name)` and `WAVE_ACCEPTANCE` from Task 3, and the two test helpers Task 3 added to `bench/test_run_contract.py` — `_begin(families, githash="abc1234", device="dead")` returning a `BENCH_BEGIN` line, and `_row(family, name, avg, mx, checksum)` returning a `BENCH` line. Reuse them; do not write second copies.
- Produces: `run.write_results(out_dir, captures, profile, profile_name)` — the existing three-argument function gains the profile and its name. Captures land at `docs/bench/YYYY-MM-DD-<githash>-<profile>.md` and `.csv`; the CSV gains a `profile` column; the Markdown gains a gate-ledger section.

- [ ] **Step 1: Write the failing test**

Add to `bench/test_run_contract.py`:

```python
def test_written_evidence_names_the_profile_and_ledgers_the_gates(tmp_path):
    names = run.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
    lines = [_begin("system")]
    for i, n in enumerate(names):
        avg = 400 if n == "synth_2x4" else (200 if n == "wave_2x4" else 100)
        lines.append(_row("system", n, avg, avg + 1, "%08x" % i))
    lines.append("BENCH_END\n")
    capture = run.parse(lines)

    base = run.write_results(str(tmp_path), [capture, capture],
                             resolve("system"), "system")

    assert base.endswith("-system")
    md = open(base + ".md", encoding="utf-8").read()
    assert "system" in md
    assert "wave_acceptance" in md
    # Every universal gate is recorded as having run.
    assert "row set" in md.lower()
    csv_text = open(base + ".csv", encoding="utf-8").read()
    assert "profile" in csv_text.splitlines()[0]
    assert ",system," in csv_text
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench" && python -m pytest test_run_contract.py -k evidence -v
```
Expected: FAIL — `write_results` takes three arguments and writes no ledger.

- [ ] **Step 3: Extend write_results**

In `bench/run.py`, change the signature to `write_results(out_dir, captures, profile, profile_name)` and the base path (currently line 615) to:

```python
    base = os.path.join(
        out_dir, "%s-%s-%s" % (stamp, header["githash"], profile_name))
```

Add a gate ledger to the Markdown, written right after the existing title line. Universal gates always ran if validation passed — validation is what got you here:

```python
    universal = (
        "row set matches the profile exactly (no missing, no extra rows)",
        "no duplicate rows",
        "QSPI digest and device fingerprint identical across runs",
        "per-row checksums identical across runs",
    )
    fh.write("## Gate ledger\n\n")
    fh.write("Profile `%s` — families: %s\n\n"
             % (profile_name, ", ".join("`%s`" % f for f in profile.families)))
    fh.write("Applied and passed:\n\n")
    for g in universal:
        fh.write("- %s\n" % g)
    if WAVE_ACCEPTANCE in profile.gates:
        fh.write("- `wave_acceptance`: wave_2x4 no slower than synth_2x4, "
                 "below the %d-cycle block budget\n" % BUDGET_CYCLES)
    fh.write("\nNot applicable to this profile:\n\n")
    if WAVE_ACCEPTANCE not in profile.gates:
        fh.write("- `wave_acceptance` — needs the `system` family's "
                 "synth_2x4 and wave_2x4 rows, which this profile "
                 "does not contain\n")
    else:
        fh.write("- none\n")
    fh.write("\n")
```

Add `profile_name` to the CSV: put a `profile` column in the header row next to the existing run index, and write `profile_name` on every data row. Read the existing CSV writer first and follow its column order rather than inventing one.

Update the call site in `main()` to `write_results(args.out_dir, captures, profile, args.profile)`.

- [ ] **Step 4: Run the tests**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench" && python -m pytest test_run_contract.py test_task8_contract.py -v
```
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add bench/run.py bench/test_run_contract.py
git commit -m "feat(bench): evidence names its profile and ledgers its gates

Captures become YYYY-MM-DD-<githash>-<profile>. Each one records which gates
ran and which were not applicable, with the reason -- so a partial run cannot
skip a gate silently; the skip is part of the evidence. Existing captures keep
their names and stay valid.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 5: Documentation and the hardware run

**Files:**
- Modify: `bench/README.md`
- Create: `docs/bench/YYYY-MM-DD-<githash>-system.md` and `.csv` (written by `run.py`)

**This task needs the hardware.** Connect an ST-Link V3 over SWD, power the Seed, turn the monitor level down.

- [ ] **Step 1: Rewrite README's command section**

In `bench/README.md`, replace the "The one command" section's single command with one per profile, and add two sentences of history above it so the next person to hit the wall recognises the shape:

> The bench image cannot hold every workload family at once — at `d294556` it linked with 40 bytes of `SRAM_EXEC` and 24 bytes of `SRAM` to spare, and the next engine took it over. A profile selects which families an image contains.

Document `--profile`, list the profiles from `bench/profiles.py` with their purpose, and state plainly that `full` currently fails to link and why that is deliberate. Keep every other section — the QSPI programming instructions, the anchor-mode notes, the flags list — intact, adding `--profile` to the flags list.

- [ ] **Step 2: Run the system profile on hardware**

```bash
cd "c:/Users/bernd/Documents/AI/Spotykach/bench" && python run.py --profile system
```
Expected: exit 0, two agreeing runs, a capture written to `../docs/bench/` with `-system` in its name.

- [ ] **Step 3: Read the ledger and the WAVE gate**

Open the written Markdown. Confirm the gate ledger names profile `system`, family `system`, and records `wave_acceptance` as applied and passed. That gate has been unenforceable since the STEP mod grid lock landed; this capture is the first evidence since then that `wave_2x4` is still no slower than `synth_2x4` and still under the block budget.

If the gate *fails*, stop and report it. That is not a bug in this work — it is the regression the guard exists to catch, finally visible again, and it needs a decision rather than a fix here.

- [ ] **Step 4: Commit**

```bash
git add bench/README.md docs/bench/
git commit -m "docs(bench): profiles in the README, first system capture

The WAVE acceptance gate runs again for the first time since the grid lock
made the full image unbuildable.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 5: Finish the branch**

Use `superpowers:finishing-a-development-branch`. This branch is a dependency of `body-resonator-engine`: BODY's blocked hardware gate resumes once this reaches `main` and BODY merges it, adding its own `body` profile entry at that point.

---

## Notes for the implementer

**The literal `7`.** `runner.cpp`'s old `find_workload` looped `t < 7` over two parallel arrays. Adding a family meant editing three things in agreement. That is the defect class this whole change exists to remove — if you find yourself adding a second list of families anywhere, stop and put it in the registry instead.

**`full` must fail at the link.** Not at manifest load, not on a family mismatch. If a future manifest bug ever made `full` fail earlier, the known size problem would mask it. Task 3 Step 6 checks this explicitly; keep that distinction alive if you touch the failure paths.

**Do not source `env.sh`** for firmware builds. It sets up desktop clang; the bench uses `arm-none-eabi-gcc`, already on PATH. Python steps need nothing.
