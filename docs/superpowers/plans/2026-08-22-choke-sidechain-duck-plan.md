# CHOKE sidechain-duck zones — implementation plan

Date: 2026-08-22. Branch: `feat/choke-sidechain-duck` (off `main` @ 03dbe24).
Design approved in-chat (bounded path); this file exists so SDD briefs/reviews
have a single source of requirements. No separate spec file.

## Overview

Rework the bipolar CHOKE control from 5 snapped detents into a continuous
control with three zones per side. New: a dedicated sidechain **duck** stage
(modeled on the existing Bloom duck) that lowers the yielding deck while the
priority deck sounds. The existing choke event-windows stay, moved to the
outer zones, with the duck active underneath them.

Per side, with `a = |choke|` in 0..1 (sign picks priority as today —
negative = A priority, positive = B priority):

| Zone | Range | Behaviour |
|---|---|---|
| Off | `a == 0` | Bit-exact bypass: no duck, no choke (as today at noon) |
| Duck | `0 < a <= 0.5` | Duck depth ramps linearly 0 → 100 %; no event choke |
| Choke-held | `0.5 < a <= 0.75` | Duck at 100 % **plus** events blocked while the priority side holds a note (today's stage 1: `gate() || flow()`) |
| Choke-decay | `a > 0.75` | Duck at 100 % **plus** events blocked through the whole audible decay (today's stage 2: additionally `max_voice_env() > 1e-4`) |

Zone quantisation lives in the **engine** mapping (wide plateaus, no
hysteresis needed). The VCV knob loses its snap; the hardware pot never had
one — both hosts then behave identically.

## Global Constraints

- **Shell etiquette (binding, Windows/Git Bash auto-approval):**
  1. Never modify a file through the shell — no `sed -i`, `>`, `>>`, `tee`,
     `perl -pi`. Use the Edit tool for existing files, Write for new ones.
  2. Never use `cd` — not even before a read-only command. The shell already
     starts at the repo root. A long compound that genuinely needs a working
     directory goes into a script file in the session scratchpad.
  3. Never chain a write (`rm`, `mv`, `git add`, `git commit`) behind `&&`
     or `;` — each shell write is its own single-purpose call.
  4. Repo-relative paths in write calls.
- **Build (engine + tests):** `source env.sh` then
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` and `cmake --build build`;
  `ctest --test-dir build --output-on-failure`. Release is **mandatory** —
  Debug fails the render-identity gates. Never MSVC.
- **Build (VCV):** always `host/vcv/build-local.sh`, never hand-rolled
  (system `g++` is an ARM cross-compiler). Panels are generated: edit
  `host/vcv/res/gen_panel.py` / `gen_hw_panel.py`, never the SVG or
  `generated_*.hpp` by hand; guards `res/test_panel.py` / `res/test_hw_panel.py`
  run as plain python scripts (pytest is not installed), from `host/vcv/`.
- **Probe rule:** no runtime claim goes into code comments, reports, or
  review replies until a probe printed it. Probe recipe:
  `docs/engine-map.md` §6.
- Everything written into the repo is **English**. Conversation language does
  not leak into files.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
  (not the Anthropic default).
- Duck timing/depth constants are **by-ear starting points**: mark them as
  such in comments; do not present them as final. No new bit-exactness gates
  except the noon-bypass identity below.
- TDD: every behaviour lands with a test that was seen red first. A test that
  cannot go red gets fixed.
- No subagents: the implementer never dispatches its own helpers or
  reviewers.

## Task 1 — Engine: duck stage + zone remap (`engine/instrument.*`, tests)

**Probe first** (throwaway, scratchpad): print the current choke window
behaviour at `_choke` ∈ {0, 0.3, 0.5, 0.6, 0.75, 0.9, 1.0} in STEP — which
values engage stage 1 / stage 2 today (`engine/instrument.cpp:187` region).
The probe output anchors the remap; quote it in the report.

**Implement** (TDD):

1. **Zone mapping** in the CHOKE block of `Instrument::process()`:
   - choke stage 1 window now requires `a > 0.5` (today: `a > 0`);
   - stage 2 (env-floor window) now requires `a > 0.75` (today: `a > 0.5`).
2. **Duck stage**, modeled on the Bloom duck that already lives in
   `instrument.cpp`/`instrument.h` (envelope → target → smoothed gain):
   - Detector: peak envelope of the **priority** deck's part output
     (`pl/prr` of `pri`), computed per sample after the priority deck's
     `process()` call. One envelope instance; on sign flips of `_choke`
     the roles swap and the envelope state carries over.
   - Depth: `d = min(a, 0.5) / 0.5` (0→1 across the duck zone, pinned at 1
     in both choke zones).
   - Gain: floor-style like the Bloom duck — full-depth floor ≈ 0.15
     linear, attack ≈ 5 ms, release ≈ 150 ms. All three are by-ear starting
     points; name them as constants, comment them as by-ear.
   - **Apply at the mix point only:** the yielding deck's dry contribution
     to `l/r` (both the null-reverb path and the reverb path) **and** its
     reverb wet-send gain. `al/ar/bl/br`, `_dry_tap`, and `_deck_tap` must
     stay untouched — they feed BODY excitation and the deck bus (see the
     existing comment at the reverb mix).
   - No zipper: the applied gain is smoothed (OnePole or the Bloom-duck
     residual scheme).
3. **Bypass:** at `_choke == 0` the output is bit-exact with the pre-change
   engine (no duck math applied, gain path untouched).

**Tests** (doctest, `tests/`):

- Noon identity: `_choke == 0` render equals a reference rendered with the
  duck code compiled but idle (bit-exact).
- Duck zone: priority deck playing, yielding deck sounding → yielding deck's
  contribution drops; deeper knob = deeper drop (monotonic across ≥3 knob
  values in `0..0.5`).
- Zone plateaus: at `a = 0.6` the event windows behave as today's stage 1 at
  the old ±0.5 detent; at `a = 0.9` as today's stage 2 at ±1 (probe-anchored).
- Duck reaches the reverb send: with wet-only routing on the yielding deck,
  the ducked render still differs from the unducked one.
- Full suite green in Release. If an existing golden/identity scenario uses
  `choke != 0`, its reference changes are part of this task — regenerate per
  repo convention and say so in the report; do not weaken the noon gate.

## Task 2 — VCV host: continuous knob, zone display, panels (`host/vcv/`)

1. `src/Fireflow.cpp`: remove the CHOKE snap (today: snapped −2..+2, scaled
   `* 0.5f` at `Fireflow.cpp:998`). The param becomes continuous; keep the
   engine range −1..+1 via whatever param range is cleanest for patch
   compatibility being a non-goal (dev alpha — saved patches may break
   freely).
2. Replace the 5-state tooltip (around `Fireflow.cpp:384`) with a zone-aware
   display: "Off" at 0; "A ducks B n %" / "B ducks A n %" in the duck zone;
   "A chokes B while playing" / "… thru decay" (and mirrored) in the outer
   zones. Wording stays close to today's strings.
3. Panels: check whether `gen_panel.py`/`gen_hw_panel.py` encode CHOKE
   detent/snap metadata; if so, update and regenerate both panels via the
   generators, run `res/test_panel.py` and `res/test_hw_panel.py` as plain
   scripts. If the generators carry no snap metadata, leave the SVGs alone —
   no cosmetic zone markings in this task (YAGNI).
4. `res/` INIT default for CHOKE stays 0.
5. Build via `host/vcv/build-local.sh`; report the guard-script and build
   output.

Depends on Task 1 (engine mapping semantics; same `set_choke` API, so the
dependency is behavioural, not an interface change).
