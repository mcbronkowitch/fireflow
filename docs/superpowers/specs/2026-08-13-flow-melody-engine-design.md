# FLOW melody engine — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Precedes:** the SHAPE/SMOOTH rework, then the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`
**Revision:** second draft, after a two-reviewer pass. The first draft's central
defect — it listed `_sh_slot()` as a finding and never changed it, which made the
whole mechanism a no-op — was found independently by both reviewers and is fixed
in §4.1. Everything both reviewers verified is cited inline so the plan does not
re-derive it.

## 1. The problem

In the free mode the melodic lane is a continuous LFO, not a sequencer. Every
step-grid concept the melody system is built from is therefore inert there, and
`kModeW` puts 85 % of drone terrains in the free mode.

The owner's requirement, in his own framing: a FLOW deck must span **one standing
note that never changes**, through **a drone with minimal variation**, to **a slow
melody — or, via COLOR, a slow chord progression**. One continuum, one control.

### 1.1 What was measured

From the audit (method: two `Instrument` + `Flow` pairs woken on the same seed,
rendered in lockstep, differing in exactly one value; `rel_diff` 0.000000 means
bit-identical):

| Finding | Measured | Mechanism |
|---|---|---|
| DENSITY moves no audio in FLOW | bit-identical on 22/22 FLOW terrains, 40 s window | `_effective_gate` is consulted only under `_step_mode` (`lane.cpp:464`) |
| FORM/SONG never applied in FLOW | dead on 40/40 terrains, both modes, 60 s | `_wrap_events` returns early for the melodic lane (`lane.cpp:578`) |
| `_effective_gate`'s FLOW branch is dead code | — | `lane.cpp:454-457`; see §4.5 for the correct unreachability argument |
| The whole S&H end collapses to one slot | — | `_sh_slot()` returns 0 for every cycle (`lane.cpp:426`) — **fixed in §4.1** |
| `_target` never holds | — | `_target = _compute_raw()` runs per sample (`lane.cpp:619`) |

### 1.2 One correction to the roadmap's framing

The roadmap lists a fourth finding — *TEMPO moves no audio in FLOW* — under the
same cause. **It is not the same cause and it is not in scope.** Free lanes take
their rate from `free_hz(_rate_norm) * _pace` and never read `_bpm`
(`super_modulator.cpp:28-29`); that is what "free" means, and PACE (spec
2026-08-12) is the speed control that reaches free mode. Making TEMPO reach a free
lane would erase the distinction between the two modes. `docs/roadmap.md`'s
Planned entry is corrected by this spec — see §11.

## 2. Design decisions taken

Taken in the brainstorming session of 2026-08-13, plus four rulings taken after the
review pass (2.5 – 2.8). Recorded so the plan does not re-derive them.

1. **A slot sequencer without rhythm.** The melodic lane gets real slots in FLOW,
   but only half the STEP machine: no gate pattern, no note lengths, no ties —
   only *which note, and when it changes*. Rejected: taming the LFO with a
   hold/dead-zone stage (leaves FORM/SONG unreachable, so the audit's largest
   finding survives), and a two-layer sequencer-plus-bend design (invents surface
   this spec does not need; addable later).
2. **One cycle is one phrase pass**, the same relation STEP already has. DENSITY
   selects k of L slots through the existing groove ranking; the slots it skips
   **hold** the previous note. Rejected: one note per cycle wrap (makes the melody
   necessarily the slowest thing in the system), and deriving the raster from
   `STEPS` alone (Fireflow spends `STEPS == 0` on the mode switch itself, §4.2).
3. **`Part` decides whether a PITCH lane is a note at all.** SAMPLER and BBD keep
   today's continuous LFO. Rejected: one mechanism for all decks (a measurable
   regression on two engines that have just been through ear review), and a new
   per-deck control (new surface on two hosts and the hardware panel for a
   question the engine can already answer).
4. **SHAPE has no role on the melodic lane in FLOW melody mode.** It stays fully
   alive on the four texture lanes (`set_shape` reaches all lanes,
   `super_modulator.cpp:77`). What SHAPE should mean for a melody belongs to the
   SHAPE/SMOOTH rework, which exists precisely so it can design against a working
   lane instead of against the blend the audit could not fully trace. **This is
   not free — see §6.1.**
5. **VARIATION gets a dead zone at the bottom, in the flow layer** (§9.1). The
   standing note requires `_variation == 0` exactly, and the weather layer makes
   that unreachable on a settled Glow terrain.
6. **A live phrase-length change re-spans the groove and keeps the pitches**
   (§4.9). Regenerating would re-roll the melody several times during the very
   gesture this design exists to demonstrate.
7. **The note rate has a floor** (§4.10). Without one, `k × cycle rate` reaches
   > 100 fires/s on a non-drone FLOW terrain.
8. **SMOOTH's slew is capped against the slot interval in melody mode** (§4.11),
   the minimum needed so a melody is heard as notes at all. The rest of SMOOTH
   stays with its own rework.

## 3. The mechanism

### 3.1 The two axes

| Control | Meaning in FLOW melody mode |
|---|---|
| **DENSITY** | how many distinct notes the drone uses — k of L |
| **VARIATION** | whether those notes change over time |

At `k == 1` only the rank-0 slot is open, and that slot recurs once per cycle: the
lane emits the same pitch forever. At `k == L` every slot changes the note and the
result is a melody, L notes per cycle. Everything between holds the previous note
across the closed slots.

**At `k == 1`, VARIATION moves the note two ways, not one.** `_mutate_slot` walks
the pitch, and `mutate_pattern_groove` swaps groove ranks
(`song_form.h:171-182`) — so rank 0 can *leave* slot 0, the single open slot
migrates, and it then reads a different entry of `pitch[]`. Both are wanted, and
§10 gates both; the first draft described only the pitch walk.

The two axes are already wired to the right scope: `set_density` reaches
`LANE_PITCH` only (`super_modulator.h:34`), so DENSITY has always been the melody
control — it simply had no destination in FLOW.

**DRIFT can no longer move the note at all.** Because §4.4 drops `shape_value`
from this lane, `_shape_offset` (which `center.cpp:143` writes every control tick),
`_ev_shape` and `_ev_phase` all leave the melodic lane's value path. The audit's
observation that "on a drifting terrain the melody fades in and out of reach on its
own" stops applying to FLOW. This is the direct answer to the "standing note"
half of the requirement and was missing from the first draft.

### 3.2 The terrain tables need no edit

`taste.h:855-859` gives the DENSITY story the drone archetype window `{0, .45}`
(`taste.h:859`), with `P_DENSITY_A` breakpoints `{.02,.08} … {.7,.95}`
(`taste.h:856`). Archetype index 0 is `ARCH_DRONE` (`flow_ids.h:7`, span order
confirmed at `taste.h:945`).

Reading the curve **on Fireflow**, where L is the constant 8 (§4.2):

| Archetype | Macro | `P_DENSITY_A` | k | Result |
|---|---|---|---|---|
| drone | 0.0 | .02 – .08 | 1 (clamped) | one standing note |
| drone | 1.0 | .26 – .44 | 2 – 4 | two to four notes, long held |
| arp | 1.0 | .7 – .95 | 6 – 8 | melody |

**On Glow the table is different, because L is itself `P_STEPS_A`**, which sits in
the same story (`taste.h:858`) and windows to ≈ 2–4 at macro 0 and ≈ 6–9 at macro
1. Both k and L rise together there, which is the story's intent and compounds
correctly.

The table's own comment states it — *"a drone at full DENSITY lands where an arp
sits at half"* — so the curve was written for this behaviour and has only ever
lacked an audio path. **No value in `taste.h` changes in this work**, with the one
exception in §9.1, which is a guard and not a curve.

### 3.3 Mode taxonomy

Three states for `LANE_PITCH`, and the second is new:

| State | Condition | Behaviour |
|---|---|---|
| STEP | `_step_mode` | unchanged |
| FLOW melody | `_melodic && !_step_mode && _flow_melody` | this spec |
| FLOW LFO | `_melodic && !_step_mode && !_flow_melody` | unchanged |

`_flow_melody` is a new `ModLane` flag. It is deliberately **not** a reuse of
`_melodic`: clearing `_melodic` on a Sampler deck would also change
`_effective_gate`, `_mutate_slot` and `_evolve_outgoing_pattern` for that lane and
would not be bit-identical.

**The flag defaults to `false`.** This inverts the first draft, and the reason is
worth stating because it decides three other things at once:

- It keeps the repo's zero-init convention, the one
  `fireflow-control-merge-init-trap` records being violated four times in a single
  branch: the boot value is the legacy behaviour, and the new behaviour must be
  asked for.
- It means a bare `ModLane` or `SuperModulator` in a doctest keeps today's
  behaviour, which is why `tests/test_song_lane.cpp:193` and `:324` and
  `tests/test_rhythm_ring.cpp:144` stay **green** instead of going red (§10.4).
  With a `true` default all three would have failed outright.
- It makes `Part`'s push mandatory rather than decorative, so a missing push is a
  silent revert to the old sound rather than a silent adoption of the new one —
  the direction this project's init trap says to prefer.

Two private predicates carry the taxonomy through the rest of this document.
Neither is named `melody()`: that would sit next to `set_melodic`/`_melodic` and
read as a getter for it, the `set_depth` collision shape from
`spotykach-gotchas`.

```cpp
// "this lane is running the FLOW melody engine right now"
bool _flow_melody_on() const { return _melodic && !_step_mode && _flow_melody; }
// "this lane runs the melody system at all" (STEP or FLOW melody)
bool _melody_engine_on() const { return _melodic && (_step_mode || _flow_melody); }
```

`kFlowPhraseSlots = 8` joins `kSeqSlots` as a `static constexpr int` on `ModLane`.

## 4. Changes in `engine/mod/lane.*`

### 4.1 The slot walk — and `_sh_slot()`

**`_sh_slot()` is the fix, not `process()`.** `lane.cpp:425-429` returns 0 whenever
`!_step_mode`, and both §4.3's `_on_boundary` and §4.4's `_compute_raw` index
through it. Changing `process()` alone leaves the lane emitting `pitch[0]` forever
— and because `expand_pattern_groove` pins `rank_of_slot[0]` to 0
(`song_form.h:152-161`), `_effective_gate(0)` is then always open, so nothing ever
freezes and DENSITY stays exactly as dead as the audit found it. Both changes are
one change:

```cpp
int ModLane::_sh_slot() const {
    if (!_step_mode && !_flow_melody) return 0;   // FLOW LFO: unchanged
    int s = _cur_step < 0 ? 0 : _cur_step;        // STEP and FLOW melody
    return s % kSeqSlots;
}
```

```cpp
// process(), replacing the !_step_mode branch
} else if (_flow_melody_on()) {
    const int slot = step_index(static_cast<float>(_phase), _effective_length());
    if (slot != _cur_step) { _cur_step = slot; _on_boundary(); }
    // no per-sample recompute: _target holds between boundaries
} else {
    if (wrapped) _on_boundary();
    if (!_frozen) _target = _compute_raw();     // unchanged
}
```

A straight grid, not `shuffle_step_index`. That choice is load-bearing beyond the
scoping reason in §11: `tests/test_instrument.cpp:653` asserts a FLOW deck stays
bit-exact under a live shared SHUFFLE turn, and a shuffled FLOW grid would redden
it.

Raster on `_phase`, not `phase_eff()`. `center.cpp:33-37` argues for locking the
raw phase, which supports this; the consequence is that `_ev_phase` and `_ev_shape`
become inert on this lane while `_evolve_outgoing_pattern` keeps drawing and
clamping them every wrap (§6.4).

### 4.1.1 `_cur_step`'s consumers — three, not two

`_cur_step` is now written in FLOW, where it previously stayed at its reset value.
There are **three** read sites, and the plan must not stop at two:

| Site | Guard | Effect |
|---|---|---|
| `super_modulator.cpp:115` | `if (_step_on)` (`:114`) | none in FLOW |
| `Part::_fire_trigger` → `pitch_cur_step()` (`part.cpp:451`) | `ENGINE_SAMPLER` only | none — a Sampler deck is never in FLOW melody mode (§3.3) |
| `super_modulator.cpp:156` | **none** | `frac` (`:162-164`) is computed every tick and consumed only in the `follow()` branch (`:167`, `_step_on` only). Benign, but it is not guarded and an implementer following a two-item list will not look here. |

Nothing in `host/` reads `cur_step`/`steps()`; the test reads
(`test_step_grid_lock.cpp`, `test_lane_follow.cpp`, `test_lane_tick.cpp`) are all
STEP.

**The ordering between this section and §4.6 is load-bearing.**
`_apply_preroll_work` (`lane.cpp:249`) is guarded by `_cur_step < 0`, which in FLOW
is *permanently* true today. §4.6 widens that guard to melody mode. If the two land
in separate commits with §4.6 first, pending FORM/SONG/NEW work runs on **every
sample**. §12 fixes the commit order.

### 4.2 Phrase length

`_effective_length()` (`lane.cpp:189-192`) is **currently dead code — zero
callers.** It becomes the single source of truth and gains the FLOW default:

```cpp
int ModLane::_effective_length() const {
    int n = _steps;
    if (_flow_melody_on() && n < 2) n = kFlowPhraseSlots;   // 8
    if (n < 1) n = 1;
    return n > kSeqSlots ? kSeqSlots : n;
}
```

The `< 2` clause exists because the two hosts disagree about what `STEPS` means in
FLOW:

- **Glow** pushes `P_STEPS_A` (2..16) through `push_mode_and_steps` in *both* modes
  (`flow.cpp:413-423`), so a Glow terrain gets a real, drawn FLOW phrase length —
  and `P_STEPS_A` sits in the DENSITY story itself, so it moves with DENSITY as
  that story intends. A fifth dead parameter comes alive.
- **Fireflow** spends `STEPS == 0` on the mode switch (`Fireflow.cpp:892-893`),
  which `set_step` clamps to 1. It therefore gets the constant 8.

**This is a host asymmetry, not merely a documented limitation.** The same terrain
produces audibly different melodies on the two hosts: on Fireflow every FLOW deck
has an 8-slot phrase, so `P_STEPS_A` and FORM's arrangement say less there. Giving
Fireflow a FLOW phrase length needs a panel position and belongs to the hardware
panel work; until then the asymmetry is accepted and stated.

Every present use of `_steps` as a *length* routes through the helper:
`_generate_pattern_a` (`lane.cpp:196`, `:201`), `_derive_pattern_b`
(`lane.cpp:205`), `_start_note`'s `n` (`lane.cpp:488-489`), and `set_step`'s
length-change detection (`lane.cpp:143-144`).

**STEP stays bit-identical**, verified per site: `_start_note`'s expression is
character-for-character the helper's, and it never runs with `_flow_melody_on()`;
`expand_pattern_groove` already clamps to `[1,32]` internally
(`song_form.h:109-111`); both hosts clamp STEPS to ≤ 16. Two caveats the plan must
respect:

- `set_step`'s `old_n`/`new_n` do not clamp the low end. Harmless only because
  `_steps >= 1` is established at `lane.cpp:141`.
- `lane_slots()` can hand a **texture** lane up to 64 (`test_lane_len.cpp:26`), so
  the helper's 32-clamp becomes reachable on non-melodic lanes if the length check
  is hoisted out of its `_melodic` guard. **Keep the guard.**

`set_step` also has an ordering trap, because the helper now reads state that
`set_step` mutates: capture `const int old_len = _effective_length();` at entry,
assign `_step_mode` and `_steps`, then compare against a second call.

One consequence worth having on purpose: on a STEP → FLOW flip the effective length
does not change on either host (Glow 8 → 8; Fireflow 8 → `_steps` 1 → helper 8), so
`_song.length_pending` does not fire and **the melody survives a mode change**.

### 4.3 The boundary

```cpp
void ModLane::_on_boundary() {
    int slot = _sh_slot();
    bool gated = (_step_mode || _flow_melody_on()) ? _effective_gate(slot) : true;
    if (gated && _flow_melody_on() && _since_fire < kFlowNoteMinSamples)
        gated = false;                                   // note-rate floor, §4.10
    _frozen = !gated;
    if (gated) {
        _fired = true;
        _since_fire = 0;
        if (_melodic && _step_mode) _start_note(slot);   // rhythm: STEP only
        if (_variation > 0.f && (!_melodic || _step_mode || _flow_melody))
            _mutate_slot(slot);
        _target = _compute_raw();
    } else {
        ++_note_age;
    }
}
```

`_start_note` stays STEP-only: note lengths and ties are rhythm, and a FLOW deck
has no gate to shape.

### 4.4 The value

```cpp
float ModLane::_compute_raw() const {
    if (_flow_melody_on()) return _active_pattern().pitch[_sh_slot()];
    // ... unchanged ...
}
```

Arithmetically identical to `shape_value(ph, 1.f, pitch[slot])` — at `shape == 1`
the clamp at `waveforms.h:26` makes `i == 3, f == 1` and the return at
`waveforms.h:32` is `sh_hold` exactly — but written directly so SHAPE's inertness
on this lane is visible in the source rather than implied by a pinned argument.

### 4.5 The gate, and a deletion

For melodic lanes `_effective_gate` and `_groove_k` stop distinguishing the two
modes and use `pattern_groove` — the phrase-length expansion — in both:

```cpp
bool ModLane::_effective_gate(int slot) const {
    const MelodyPattern& p = _active_pattern();
    if (!_melodic) return p.gate[slot];
    const int gl = p.pattern_groove.len < 1 ? 1 : p.pattern_groove.len;
    return p.pattern_groove.rank_of_slot[slot % gl] < _groove_k();
}
```

This correction decides the design's musical worth. The audit's dead FLOW branch
reads `cell_groove`, whose `len` is the **motif** length (`_generate_pattern_a`
passes `layout.motif_len` to `pg_gen_groove`, `lane.cpp:199`). A raster on the
motif cell would only ever play the first motif instance, so FORM's arrangement —
the A A B A that `pg_build_arrangement` lays out — would stay inaudible, and the
audit's largest finding would survive the fix meant to close it. The dead branch is
**deleted, not wired**.

**The first draft gave the wrong reason for calling it dead, and the reason
matters** because the plan will be asked to justify a deletion. It is *not* the
`!_melodic` early return: `lane.cpp:457` sits on the melodic path with
`_step_mode == false`, which is exactly the `cell_groove` arm. It is dead because
**both callers of `_effective_gate` are themselves under `_step_mode`** —
`lane.cpp:464` (`_step_mode ? _effective_gate(slot) : true`) and `lane.cpp:491`
inside `_start_note`, which only runs from `lane.cpp:468` under
`_melodic && _step_mode`.

`_start_note`'s own `cell_groove` branch (`lane.cpp:500-505`) is dead by the same
call-site argument and may be deleted with it. Both deletions were confirmed by
review; the plan still proves the RED once by restoring one and showing a test that
catches it.

### 4.6 Cycle-wrap events

The early return is the audit's FORM/SONG finding; it narrows to the FLOW LFO
state only.

```cpp
void ModLane::_wrap_events() {
    const bool pending = /* unchanged */;
    if (_melodic && !_step_mode && !_flow_melody) return;   // FLOW LFO: unchanged
    _evolve_outgoing_pattern();
    if (_melody_engine_on()) {
        if (pending) _apply_pending_song_work();
        else         _advance_song();
    }
}
```

Inside `_evolve_outgoing_pattern`, `_renew_units()`'s guard and `_mutate_groove`'s
early return both widen from `_melodic && _step_mode` to `_melody_engine_on()`.
`_apply_preroll_work`'s guard widens the same way.

`_advance_song()` running per cycle means **one phrase per cycle** — at
`kRateFreeMin` (0.02 Hz) a 50 s phrase, which is the intended feel.

**Accepted: a runtime FORM/NEW change waits up to a full cycle.**
`_apply_preroll_work` requires `_cur_step < 0`, which after §4.1 is true only
before the first sample, so a mid-flight FORM change or NEW press reaches audio at
the next wrap — up to 50 s. This is accepted rather than fixed: SONG already
advances per cycle, so a structural change landing on a phrase boundary is
musically coherent, and the alternative (applying mid-phrase) would cut the melody
in half. **It is not accepted at boot** — see §5.

### 4.7 `tick()`

`tick()` must implement the same slot walk. It is not needed in production —
`SuperModulator` drives `LANE_PITCH` through `process()` exclusively
(`super_modulator.cpp:109`; the `tick()`/`follow()` loop skips it at `:165-166`) —
but the function's documented contract is that it mirrors `process()`'s observable
sequence, `tests/test_lane_tick.cpp` exercises it directly, and the roadmap records
the cost of letting an isolated path drift from the production one.

**Scope warning, and a permitted stopping point.** This touches four sites, not
one: the pending-mismatch entry (`lane.cpp:684-688`), `next_edge` (`:699-702`,
which hard-codes `1.0` off STEP), the wrap arm's `else _on_boundary()` (`:744`), and
the trailing `if (!_step_mode && !_frozen) _target = _compute_raw()` (`:751`), which
must be skipped in melody mode. `test_lane_tick.cpp`'s harness (`:40-54`) only ever
configures STEP melodic lanes, so §10.7 is a new case, not an extension. §12 makes
this the **last task, in its own commit**, and permits it to end at a documented gap
if the equivalence budget cannot be re-derived cheaply — a documented gap in a path
production never takes is cheaper than a blocked milestone.

### 4.8 Boot and mode entry

Three pieces of carried state, all previously harmless because FLOW had no slots:

- **`_cur_step` is stale across a mode change.** Entering FLOW melody with a
  `_cur_step` left from STEP means no boundary fires until the FLOW slot index
  happens to differ — the phrase's first note can be skipped or arrive a slot late.
  `set_step` clears `_cur_step` to `-1` on entry to *either* mode, so the first
  sample in the new mode fires slot 0. This mirrors what STEP entry already does
  for `_note_age`/`_note_hold` and the follower state (`lane.cpp:137-140`).
- **`_frozen` is stale across a mode change.** A lane leaving FLOW melody on a
  *closed* slot enters FLOW LFO with `_frozen == true`, and that branch is
  `if (!_frozen) _target = _compute_raw();` — the "LFO" would hold a constant for
  up to a full cycle. `set_step` and `set_flow_melody` both clear `_frozen`.
- **`reset()` already clears `_cur_step`** (`lane.cpp:405`), so RST
  (`instrument.h:428` → `reset_phases()`) and `Center::_snap_phase` →
  `snap_pitch_phase()` (`super_modulator.h:163`) restart the phrase at slot 0 and
  fire a note on the next sample. **This is wanted** — RST is the resync gesture
  and a melody restarting at its own beginning is what it should do — but it is a
  new audible consequence of RST on a FLOW deck, so §10 gates it rather than
  leaving it to be discovered. `reset()` also clears `_frozen`.

`SPOT` needs no handling: `SuperModulator::spot` skips `LANE_PITCH`
(`super_modulator.cpp:182`). One note for `ModLane`'s standalone contract, since
`kick()` remains reachable from tests: `kick()` wraps with `floor()`
(`lane.cpp:296-297`), so a kick crossing phase 1.0 skips `_wrap_events()` and the
song does not advance. Pre-existing; not fixed here.

### 4.9 A live phrase-length change re-spans, it does not re-roll

**Decision 2.6.** `P_STEPS_A` lives in the DENSITY story, so a DENSITY sweep 0 → 1
— the exact gesture of §8's demo scenario and §10.12's listening check — crosses
several integer STEPS values on Glow. Under the first draft each crossing raised
`_song.length_pending`, which the newly-live `_wrap_events` consumes via
`_apply_pending_song_work` → `_generate_pattern_a()` + `_clear_fresh_phrase_state()`:
the melody would be re-rolled under the player several times during the
demonstration of a continuum.

Instead, in melody mode a length change re-runs **only** the expansion:

```cpp
expand_pattern_groove(pattern.cell_groove, _effective_length(),
                      pattern.pattern_groove);
```

`pitch[]`, `gate[]`, `motif_id[]`, `layout` and `cell_groove` are untouched, for
both patterns. The melody survives; only how many slots it spreads across changes.
This is deterministic, allocation-free and consumes no RNG, so it cannot perturb
the terrain's stream.

`_song.length_pending` therefore keeps its present meaning — *regenerate* — and is
raised only by `set_step` when **STEP** is involved. The melody-mode length path
does not use it. The plan states this at both sites; the two must not be conflated.

### 4.10 The note-rate floor

**Decision 2.7.** Note rate is `k × cycle rate` with no upper bound. `P_RATE_A`'s
drone window is `{0,.25}` (`taste.h:972`) → 8–50 s cycles, which is the intended
slow end; but a non-drone archetype in FLOW reaches `{.55,.9}` ≈ 14 Hz, and at
`k = L = 8` that is over 100 fires per second. At that rate `_gate_len` (5 ms) never
expires so `_gate_edge` never fires and the synth never re-attacks, `_chord.build()`
runs its up-to-27-candidate lay search per note, and every fire forces an extra
`Part::_control_tick()` (§6.2).

A boundary that arrives sooner than `kFlowNoteMinSamples` after the last fire
**holds instead of firing** (§4.3). One `int _since_fire` incremented per sample in
`process()` and per interval in `tick()`. The result decimates to the floor rate
rather than falling off a cliff, and DENSITY keeps its full effect everywhere below
the ceiling.

`kFlowNoteMinSamples` = 60 ms of samples (≈ 16 notes/s), **a first guess set by
arithmetic, not by ear** — above anything ambient, below anything that reads as a
buzz. It is sample-rate-derived, not a constant count, so 44.1 kHz behaves the
same. §10.13 is where it gets judged.

### 4.11 SMOOTH against the slot interval

**Decision 2.8.** `_target` is now a staircase, so SMOOTH is the glide law of a
melody rather than the smoothing of an LFO. At SMOOTH 1 the time constant reaches
~0.5 s (`lane.cpp:279`); notes shorter than that never arrive and the melody
flattens into a wobble.

In melody mode `_update_slew()` clamps tau to `kFlowSlewFrac` × the slot interval,
so a note reaches ~94 % of its target within its own slot:

```
slot_interval_samples = 1 / (_phase_inc * (1 + _ev_rate) * _effective_length())
tau <= kFlowSlewFrac * slot_interval_samples          // kFlowSlewFrac = 0.35
```

`_update_slew()` must therefore also be called from `_update_inc()`, since the slot
interval moves with rate and PACE. `set_fixed_slew`'s 20 ms override is already
below any plausible clamp and is left alone.

`kFlowSlewFrac = 0.35` is **a first guess by arithmetic** (1 − e^(−1/0.35) ≈ 0.94).
This is the minimum needed for the melody to be heard as notes; everything else
about SMOOTH stays with its own rework (§11).

## 5. Changes in `engine/parts/` and `engine/mod/super_modulator.*`

```cpp
void SuperModulator::set_flow_melody(bool on) { _lanes[LANE_PITCH].set_flow_melody(on); }
```

`Part` pushes it from the engine id:

```cpp
_mod.set_flow_melody(_engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD);
```

**Where it is pushed.** At the two sites where `_engine_id` is written — `part.cpp:32`
(`Part::init`) and `part.cpp:417` (`Part::_engine_swap`); review confirmed there is
no third. That is the convention `part.h` already documents for `_engine_wants_in`:
*"Written only where `_engine` is written … so the two cannot drift apart."* It also
gives the feature runtime engine switching, which the init-time `set_melodic`
never had.

**Why this expression, stated carefully.** The first draft called it "the FLOW
restriction of the condition `part.cpp:230` already uses … equivalent by
construction". That is misleading in a way that would send an implementer to the
wrong place: `part.cpp:230` is the **quantizer bypass** for `_pitch_q`, a different
question that happens to name the same two engines for the same underlying reason —
on those decks the PITCH lane is not a note. The two conditions agree because the
reason is shared, not because either derives from the other. Do not wire
`set_flow_melody` at that per-tick site.

One consequence of the expression as written: `ENGINE_TEST_TONE` also gets melody
mode. That is harmless and intended (its PITCH lane *is* a note), but it is not in
the SYNTH/WAVE/BODY/ZAP list §2.3 implies, and `wave_formant_sweep.json` puts a
test tone on part B — see §10.10.

**`set_flow_melody` carries `set_step`'s length check.** Flipping the flag can
change `_effective_length()` — a FLOW deck swapped SYNTH → Sampler goes 8 →
`_steps`, and back on the return trip. The setter compares `_effective_length()`
before and after and re-spans the groove per §4.9. Without it the lane keeps a
`pattern_groove` built for the other length; every read is `slot % groove_length`,
so the symptom is a wrong groove rather than a crash — precisely the defect class
that survives a test suite unless it is named.

**Boot ordering, and why it is not a 50 s wait.** `_generate_pattern_a()` runs
inside `ModLane::init` and now reads `_effective_length()` → `_flow_melody`. Order:
`Part::init` calls `_mod.init(...)` first (flag still `false`, so the pattern is
generated at today's length), then pushes `set_flow_melody(...)`, which re-spans
per §4.9. Because §4.9 re-spans instead of regenerating, no pending work is
outstanding and the first slot is correct on the first sample — the boot case does
not depend on `_apply_preroll_work` at all. `Part::init` is called again mid-session
by the VCV host (the `_step_seen` comment at `part.cpp:47` documents this) and the
same order holds on every call.

## 6. Consequences

### 6.1 SHAPE: this is the most audible change in the spec

§2.4 scopes SHAPE out of the design. It does **not** scope out the change: today a
FLOW SYNTH/WAVE/BODY pitch lane is a sine→pulse LFO, and the drone archetype caps
`P_SHAPE_A/B` at `{0,.25}` (`taste.h:102`) — so drone terrains currently get a
**smooth sine pitch drift** and after this change get a **stepped sequence**. That
is the single largest sonic change here, larger than any of the new controls, and
it is a direct consequence of §4.4 rather than a side effect. Named here so the
listening check in §10.12 knows what it is listening for.

### 6.2 Retriggers scale with DENSITY — five consumers, not two

`_fired` now fires k times per cycle in FLOW instead of once. At `k == 1` that is
once per cycle, exactly today. The consumers:

| Consumer | Effect at k > 1 |
|---|---|
| `part.h:291-295` | 5 ms GATE pulse per note; `_note_suppressed = _inhibit` re-latched per note |
| `part.h:329` → `Part::_fire_trigger` | `trigger_chord` per note — the chord progression, §6.3 |
| `part.h:321-326` `else if (fired) _control_tick();` | **the fire refresh.** `Quantizer::process`'s slew counts *calls*, each worth a full control interval (`part.h:305-311`), so the pitch glide and `ChordBuilder::set_color`'s zone hysteresis advance k× faster per cycle |
| `super_modulator.cpp:137-151` | the onset-gap ring, i.e. `rhythm()` reports k× shorter gaps — and that feeds FLUX's THIN rhythm reader, so **DENSITY now shapes the delay's skip pattern**, a cross-block coupling |
| `Fireflow.cpp:1449` | the lane LED blinks per note instead of per cycle |

The first draft named the first two. The fire refresh and the FLUX coupling are
real behaviour changes and belong in the listening check.

### 6.3 COLOR works, with one inherited caveat

The chord layer is already live in FLOW (a FLOW deck fires once per wrap today);
what is new is that the root changes per slot, which is exactly a progression.
The caveat is pre-existing and shared with STEP, but this design sells chord
progressions as a headline: `_tg[LANE_PITCH]` is the **post-slew** value, so on the
fire sample `_chord.build()` chooses its lay and voice-leading against the
*previous* note's root, and `apply()` then re-applies those latched intervals to the
gliding root. Named so it is not discovered at §10.12.

### 6.4 Two of GROW's four walks become dead weight

`_ev_phase` and `_ev_shape` no longer reach the melodic lane's value (§4.1) while
`_evolve_outgoing_pattern` keeps drawing and clamping them at every wrap. They still
consume RNG, so the stream stays deterministic and comparable; they simply do
nothing on this lane. Not fixed here — removing a draw would change every STEP
stream too. Recorded so the SHAPE/SMOOTH rework inherits it.

### 6.5 Renders, determinism, CPU

**Every FLOW render changes** on SYNTH, WAVE, BODY, ZAP and TEST_TONE decks:
`_wrap_events` now consumes RNG where it consumed none, so streams diverge from the
first wrap. Accepted — the project has no bit-exactness gates.

**Both pinned render hashes are affected, and this is now an answer rather than a
check.** `tests/check_render_hash.cmake` is driven from `CMakeLists.txt:221-230`
(`ctrl_identity.json` — sets no engine and no `set_step`, so **both decks are SYNTH
in FLOW**) and `:237-252` (`wave_formant_sweep.json` — part 0 is `wave` with
`set_step` flag `false`, so **WAVE in FLOW**). Both must be re-based, in the commit
that changes the behaviour, not a later one.

**Determinism is preserved.** Nothing introduced here depends on timing, sample rate
or block size beyond `kFlowNoteMinSamples`, which is derived from the sample rate
rather than fixed in samples.

**CPU: cheaper in the lane, more expensive in `Part`, net unmeasured.** The lane
loses a per-sample `shape_value()` (containing `fast_sin`) and gains an integer slot
compare. `Part` gains, per note rather than per cycle, a `_chord.build()` lay search
(≤ 27 candidates), a voice retrigger and a `_control_tick()`. The first draft
claimed "neutral to slightly cheaper" by counting only the first half. No bench
round is ordered; if M6 timing needs the number the rows are the `instrument_*`
FLOW workloads, and §4.10's floor bounds the worst case.

## 7. What stays bit-identical — and the one hole

Verified by review and safe: CHOKE does not depend on FLOW having no fires
(`instrument.cpp:214` opens the window on `gate() || flow()`, and `Part::flow()` is
`!_step_on`, `part.h:116`, so a FLOW priority deck already holds it permanently
open; `test_choke.cpp:226` stays green). `gate_state()`/`note_sustain()` are both
`_step_mode`-gated (`lane.h:68`, `:72`), so `Part::gate()` stays pulse-only in FLOW
and `test_part.cpp:573` holds. Nothing serializes `MelodyPattern` or lane state —
both hosts persist parameter values only.

**The hole: "Sampler and BBD stay bit-identical" is true of the lane and false of
the mix.** The reverb is instrument-level with per-part sends
(`instrument.cpp:258-261`) and the master DRIVE/limiter is shared and is a
*threshold* (`spotykach-master-drive-is-a-threshold`). In any two-deck render where
the neighbour is a note engine in FLOW, the k-fold trigger density moves the tail
and the limiter, so the Sampler deck's **contribution to the master mix** is not
bit-identical even though its lane is. §10.8 is scoped to a single-deck render or a
per-deck tap accordingly; asserting it on a two-deck master bus would go red for a
non-defect and get quietly weakened.

## 8. Hosts

No new control, no panel change, no new `ParamId` on either host. `host/render`
gets one scenario, `flow_melody.json`: a SYNTH deck in FLOW with DENSITY swept
0 → 1 across the render, so one WAV walks the whole continuum, with the pitch lane
visible in `mods.csv`. `set_density` is a real scenario verb (`scenario.cpp:142`),
so the scenario cannot silently no-op the way an unknown action would
(`scenario.cpp:220` ignores those).

## 9. The one flow-layer change

### 9.1 A dead zone at the bottom of WANDER

**Decision 2.5.** The standing note requires `_variation == 0` exactly. `taste.h:927`
gives `P_VARIATION_A` a bp0 cell of `{0,0}`, so WANDER at 0 draws exactly zero —
but the weather layer offsets `_eff[M_WANDER]` by up to ±0.10 scaled by MOTION
(`flow.cpp:449`, `kWeatherDepthMax = 0.10`). At eff 0.10 the curve interpolates 40 %
of the way to bp1, whose span is `{.05,.15}`, so on a terrain that drew the top of
that span **`P_VARIATION_A` reaches ≈ 0.06** and `_evolve_outgoing_pattern` plus
`_mutate_groove` run at every wrap. The note is not standing.

`_eff[M_WANDER]` is clamped to 0 below `kWanderDeadzone`, in the guard chain of
`Flow::recompute_and_push` where `P_RANGE`, `P_PACE` and the vetoes already sit.

**Why the flow layer and not `ModLane::set_variation`.** The engine stays untouched,
so no STEP render changes and Fireflow's VARIATION knob keeps its full travel; the
clamp applies to Glow terrains only, in both modes, which is exactly where the
weather that causes the problem lives.

`kWanderDeadzone` is a **first guess by arithmetic**: it must exceed 0.06 in
`P_VARIATION_A` terms, so ≈ 0.07, which costs the bottom 12–28 % of the WANDER knob
depending on the terrain's bp1 draw. That is a real cost and the story's own name
for that region is "frozen", so it is defensible — but if listening finds it too
expensive, the alternative stands: exclude `M_WANDER` from `weather_of` exactly as
`M_PACE` is excluded and for the same reason (PACE spec §4.4). That alternative
costs no knob travel and gives up WANDER breathing on its own. §10.14 decides.

## 10. Verification

Every gate must be shown RED once before it is made green — the project rule that a
test which cannot fail gets fixed. Each gate names its RED recipe, because the
memory note's own conclusion is that the recipes that exist are the ones written
into the spec rather than invented by implementers. New file
`tests/test_flow_melody.cpp` unless a listed existing file is the better home.

**Behaviour, at the bare lane:**

1. **k == 1 is a standing note.** The pre-slew target is identical across ≥ 3 full
   cycles **and** `wrap_count_for_test()` (`lane.h:52`) shows the lane actually
   wrapped that many times. The wrap assertion is not optional: constancy alone
   passes on a stalled lane, which is exactly the vacuity PACE §8 recorded. *RED:
   restore the per-sample `_compute_raw()`.*
2. **The fire count equals k**, for k = 1 … L, per cycle. *RED: return `true` from
   the melody-mode branch of `_effective_gate`.* The first draft also asked for "L
   distinct targets"; that is dropped, because `generate_phrase` repeats motifs by
   design (A A B A) and duplicate pitches across slots are normal — the assertion
   would have been flaky and then quietly weakened.
3. **A closed slot holds.** The target at a closed slot equals the last open
   slot's, not a fresh value. *RED: drop the `_frozen` guard.*
4. **FORM and SONG reach FLOW.** A FORM change alters the pitch sequence;
   `song_position()` advances once per wrap. *RED: restore `_wrap_events`'s early
   return.*
5. **VARIATION at k == 1 moves both things** (§3.1): the pitch walks, *and* the open
   slot migrates as ranks swap. At VARIATION == 0 both are bit-stable — the LOOP
   contract, in FLOW. *RED: widen `_mutate_groove`'s early return back to STEP-only
   for the migration half.*
6. **The melody-mode gate is value-dependent.** Two lanes identical but for
   `_density` produce different targets. *A counter proving the branch executed is
   explicitly not acceptable* — that is shape 4 in `fireflow-vacuous-test-gates`.
7. **`tick()` ≡ `kTickInterval` × `process()`** in melody mode (§4.7). *RED: leave
   `next_edge` at `lane.cpp:702`'s hard-coded `1.0`.*

**Transitions and boot** — none of these had a gate in the first draft:

8. **STEP ↔ FLOW mid-flight** (§4.8): the first slot after entry fires, and
   `_frozen` does not survive the transition. *RED: remove the `_cur_step = -1`
   clear.*
9. **RST and `snap_pitch_phase`** restart the phrase at slot 0 and fire.
10. **The flag's default and boot order** (§3.3, §5): a bare `ModLane` runs the FLOW
    LFO path; a `Part` with a SYNTH engine runs melody mode from its first sample,
    with `pattern_groove.len == _effective_length()` — including on Fireflow, where
    the pattern is generated at length 1 and re-spanned to 8. *RED: flip the
    default to `true`.*
11. **An engine swap in FLOW** (SYNTH → Sampler → SYNTH) leaves
    `pattern_groove.len == _effective_length()` **at the first wrap after the swap
    back** — not "at every point", which is unsatisfiable by design: while the deck
    is a Sampler, §4.6's early return means the melody state is legitimately not
    maintained.
12. **A live length change re-spans, it does not re-roll** (§4.9): sweep DENSITY
    0 → 1 on a Glow terrain across several `P_STEPS_A` crossings and assert
    `pitch[]` is byte-identical throughout while `pattern_groove.len` follows. This
    is the design's own demonstration gesture and had no gate at all.

**Non-regression:**

13. A Sampler deck and a BBD deck in FLOW are **bit-identical to `main`**, measured
    on a **single-deck render or a per-deck tap** — §7 explains why a two-deck
    master bus cannot carry this assertion.
14. A STEP render is bit-identical to `main`.
15. The three tests that pin FLOW-LFO behaviour — `test_song_lane.cpp:193`, `:324`
    and `test_rhythm_ring.cpp:144` — stay green under the `false` default (§3.3) and
    are **retitled** to name the path they pin. Each gains a melody-mode sibling,
    because otherwise they document a path production no longer takes.
    `test_gate_density.cpp:59`'s stated contract ("FLOW: no per-step gate ⇒ no
    freeze source") becomes false for melody mode and must be re-scoped rather than
    left green by the accident of `_density` defaulting to 1 (`lane.cpp:84`).

**Audit gates, corrected:**

16. `tests/test_param_impact.cpp`: `P_FORM_A/B` and `P_SONG_A/B` are currently
    **excluded** from both gates (`:107-109`, documented at `:96-105`), so nothing
    there can "invert" as the first draft claimed — the exclusion must be **deleted**
    for the gate to exist. What does go red on its own is the mode-exclusive
    **exact-set** comparison at `:309-312` (`report()`, `:255-263`), which lists
    `P_DENSITY_A, P_DENSITY_B, P_STEPS_A, P_STEPS_B, P_SHUFFLE, P_TEMPO_BPM,
    P_COUPLE`; four of those revive here.
17. **A caveat on gate 16, decided in advance.** The audit records a *second,
    untraced* gate on FORM/SONG — forcing `SHAPE_A` to 1.0 made FORM audible on
    only 1 of 6 STEP terrains — and §11 defers it. The likeliest suspect is
    downstream of the lane: `P_DEPTH_A` is the audit's strongest unowned parameter
    and multiplies every lane output before its destination (`part.cpp:105`), and
    `_active[LANE_PITCH]` gates it too, so a terrain can draw the melody to
    inaudibility whatever the lane does. The gate therefore **controls for DEPTH and
    `_active`**; if FORM/SONG still measure zero with those held, that is an
    out-of-scope finding to record, **not** a failure of this design. Deciding this
    in advance is the point — deciding it after a red gate is how a gate gets
    weakened.

**Listening checkpoints** (owner, not automatable):

18. The drone → melody continuum via `flow_melody.json`, against §1.
19. **The SHAPE consequence of §6.1** — a drone terrain's smooth sine pitch drift
    becoming a stepped sequence. Judge this before the new controls; it is the
    larger change.
20. The retrigger consequences of §6.2 across DENSITY 0 → 1, specifically the
    faster Quantizer glide and FLUX's THIN pattern following DENSITY.
21. `kFlowNoteMinSamples` (§4.10) and `kFlowSlewFrac` (§4.11) — both first guesses
    by arithmetic.
22. `kWanderDeadzone` (§9.1) — and with it the ruling on whether the dead knob
    travel is acceptable or `M_WANDER` should leave `weather_of` instead.

## 11. Non-goals

Each deliberately out of scope, with the reason, so a reviewer does not read them as
oversights:

- **SHAPE on the melodic lane in FLOW** — §2.4; the SHAPE/SMOOTH rework owns it, and
  the audit records an untraced second gate in that blend. Note this is a scoping
  decision about *design*, not about *impact*: §6.1 is in scope and audible.
- **The rest of SMOOTH** — only §4.11's clamp is taken, and only because without it
  the melody is not heard as notes.
- **SHUFFLE in FLOW** — a rhythmic control; a drone has no rhythm to swing. Also
  load-bearing for `test_instrument.cpp:653` (§4.1).
- **TEMPO in FLOW** — §1.2.
- **`taste.h` curve retuning** — §3.2; the curve already spans the requirement.
  §9.1 adds a guard, not a curve.
- **A Fireflow FLOW phrase-length control** — §4.2; it needs a panel position.
- **`kick()`'s skipped wrap** (§4.8) — pre-existing, unreachable from production.
- **The audit's two open threads** — the second silent-deck cause (12 of 52
  terrains, BODY on the silent side) and the second FORM/SONG gate (§10.17).

## 12. Ordering and deliverables

**Commit order is constrained, not stylistic:**

1. `_sh_slot()` + `process()`'s slot walk + `_cur_step` clearing (§4.1, §4.8) —
   **before** §4.6, because `_apply_preroll_work` is guarded on `_cur_step < 0` and
   widening that guard first makes pending work run every sample (§4.1.1).
2. `_effective_length()` + `set_step`'s check + `set_flow_melody`'s check + §4.9's
   re-span — **together**, or a FLOW deck keeps a groove built for the wrong length.
3. `_effective_gate`/`_groove_k` rewrite, then the `cell_groove` deletions (§4.5) —
   the deletion depends on the call-site change already being in.
4. `_wrap_events` and the three widened guards (§4.6).
5. `Part`/`SuperModulator` wiring (§5).
6. The note-rate floor (§4.10) and the slew clamp (§4.11).
7. The flow-layer dead zone (§9.1) — independent, may land anywhere after 4.
8. Render-hash re-base (§6.5) in the commit that changes the behaviour.
9. `tick()` (§4.7) — **last, own commit**, documented gap permitted.

**Documentation deliverables, not follow-ups** (house convention, PACE §5):

- `docs/roadmap.md` — the Planned entry loses its TEMPO row (§1.2 contradicts it)
  and gains the result; the milestone moves to Done.
- `docs/2026-08-13-glow-macro-audit.md` — Result 3 and the per-macro DENSITY verdict
  become historical; add a pointer rather than editing the measurement.
- `host/vcv/README.md` — DENSITY's description changes meaning on both modules.
- `CLAUDE.md` needs no change; no file moves.
