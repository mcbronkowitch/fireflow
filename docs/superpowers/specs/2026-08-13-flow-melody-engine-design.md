# FLOW melody engine — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Precedes:** the SHAPE/SMOOTH rework, then the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`

**Revision:** third draft, after three reviewers over two passes.

- Draft 1's central defect — it listed `_sh_slot()` as a finding and never changed
  it, which made the whole mechanism a no-op — was found independently by two
  reviewers and is fixed in §4.1.
- Draft 2 added four mechanisms to close four holes; the final pass found a defect
  in every one of them. Two are fixed here (§4.10, §4.11). The other two were
  **removed rather than repaired**, because in both cases a simpler option did the
  same job: the live phrase length is gone (§4.9) and the WANDER dead zone is
  replaced by a two-token weather exclusion (§9).

Everything the reviewers verified is cited inline so the plan does not re-derive it.

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
lane would erase the distinction between the two modes. `docs/roadmap.md`'s Planned
entry is corrected by this spec — see §12.

## 2. Design decisions taken

Rulings 1–4 from the brainstorming session of 2026-08-13; 5–8 taken after the
review passes.

1. **A slot sequencer without rhythm.** The melodic lane gets real slots in FLOW,
   but only half the STEP machine: no gate pattern, no note lengths, no ties —
   only *which note, and when it changes*. Rejected: taming the LFO with a
   hold/dead-zone stage (leaves FORM/SONG unreachable, so the audit's largest
   finding survives), and a two-layer sequencer-plus-bend design (invents surface
   this spec does not need; addable later).
2. **One cycle is one phrase pass**, the same relation STEP already has. DENSITY
   selects k of L slots through the existing groove ranking; the slots it skips
   **hold** the previous note. Rejected: one note per cycle wrap (makes the melody
   necessarily the slowest thing in the system).
3. **`Part` decides whether a PITCH lane is a note at all.** SAMPLER and BBD keep
   today's continuous LFO. Rejected: one mechanism for all decks (a measurable
   regression on two engines that have just been through ear review), and a new
   per-deck control (new surface on two hosts and the hardware panel for a
   question the engine can already answer).
4. **SHAPE has no role on the melodic lane in FLOW melody mode.** It stays fully
   alive on the four texture lanes (`set_shape` reaches all lanes,
   `super_modulator.cpp:77`). What SHAPE should mean for a melody belongs to the
   SHAPE/SMOOTH rework, which exists precisely so it can design against a working
   lane instead of against the blend the audit could not fully trace. **This is not
   free — see §6.1.**
5. **The FLOW phrase length is a constant, not `P_STEPS_A`** (§4.9). Draft 2 made
   Glow's STEPS the FLOW phrase length; the final review found that growing L past
   the generated length plays ungenerated slots, plus three follow-on defects. The
   requirement never asked for a variable phrase length.
6. **`M_WANDER` leaves the weather layer** (§9), so VARIATION 0 is exactly 0 and
   the standing note actually stands. Replaces draft 2's dead zone, which was in
   the wrong place and in the wrong units.
7. **The note rate has a floor** (§4.10). Without one, `k × cycle rate` reaches
   > 100 fires/s on a non-drone FLOW terrain.
8. **SMOOTH's slew is capped against the slot interval** (§4.11), the minimum
   needed so a melody is heard as notes at all. The rest of SMOOTH stays with its
   own rework.

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
the pitch, and `mutate_pattern_groove` swaps groove ranks (`song_form.h:171-182`) —
so rank 0 can *leave* slot 0, the single open slot migrates, and it then reads a
different entry of `pitch[]`. Both are wanted, and §10 gates both.

The two axes are already wired to the right scope: `set_density` reaches
`LANE_PITCH` only (`super_modulator.h:34`), so DENSITY has always been the melody
control — it simply had no destination in FLOW.

**DRIFT can no longer move the note at all.** Because §4.4 drops `shape_value` from
this lane, `_shape_offset` (which `center.cpp:143` writes every control tick),
`_ev_shape` and `_ev_phase` all leave the melodic lane's value path. The audit's
observation that "on a drifting terrain the melody fades in and out of reach on its
own" stops applying to FLOW. That, plus §9, is what makes the standing note stand.

### 3.2 The terrain tables need no edit

`taste.h:855-859` gives the DENSITY story the drone archetype window `{0, .45}`
(`taste.h:859`), with `P_DENSITY_A` breakpoints `{.02,.08} … {.7,.95}`
(`taste.h:856`). Archetype index 0 is `ARCH_DRONE` (`flow_ids.h:7`, span order at
`taste.h:947`). With L fixed at 8 (§4.9) the reading is the same on both hosts:

| Archetype | Macro | `P_DENSITY_A` | k | Result |
|---|---|---|---|---|
| drone | 0.0 | .02 – .08 | 1 (clamped) | one standing note |
| drone | 1.0 | .26 – .44 | 2 – 4 | two to four notes, long held |
| arp | 1.0 | .7 – .95 | 6 – 8 | melody |

The table's own comment states the intent — *"a drone at full DENSITY lands where an
arp sits at half"* — so the curve was written for this behaviour and has only ever
lacked an audio path. **No value in `taste.h` changes in this work.**

### 3.3 Mode taxonomy

| State | Condition | Behaviour |
|---|---|---|
| STEP | `_step_mode` | unchanged |
| FLOW melody | `_melodic && !_step_mode && _flow_melody` | this spec |
| FLOW LFO | `_melodic && !_step_mode && !_flow_melody` | unchanged |

`_flow_melody` is a new `ModLane` flag. It is deliberately **not** a reuse of
`_melodic`: clearing `_melodic` on a Sampler deck would also change
`_effective_gate`, `_mutate_slot` and `_evolve_outgoing_pattern` for that lane and
would not be bit-identical. (`set_melodic` is written in exactly one place,
`super_modulator.cpp:14`, unconditionally true for `LANE_PITCH`.)

**The flag defaults to `false`**, and that decides three things at once:

- It keeps the repo's zero-init convention, the one
  `fireflow-control-merge-init-trap` records being violated four times in a single
  branch: the boot value is the legacy behaviour, and the new behaviour must be
  asked for.
- A bare `ModLane` or `SuperModulator` in a doctest keeps today's behaviour, which
  is why `tests/test_song_lane.cpp:193` and `:324` and `tests/test_rhythm_ring.cpp:144`
  stay **green** instead of failing outright (§10.15).
- It makes `Part`'s push mandatory rather than decorative, so a missing push is a
  silent revert to the old sound rather than a silent adoption of the new one.

Two private predicates carry the taxonomy. Neither is named `melody()`: that would
sit next to `set_melodic`/`_melodic` and read as a getter for it — the `set_depth`
collision shape from `spotykach-gotchas`.

```cpp
// "this lane is running the FLOW melody engine right now"
bool _flow_melody_on() const { return _melodic && !_step_mode && _flow_melody; }
// "this lane runs the melody system at all" (STEP or FLOW melody)
bool _melody_engine_on() const { return _melodic && (_step_mode || _flow_melody); }
```

New `static constexpr` members on `ModLane`, beside `kSeqSlots`:

| Constant | Value | Origin |
|---|---|---|
| `kFlowPhraseSlots` | 8 | §4.9 |
| `kFlowNoteMinSamples` | 60 ms × `_sr` | §4.10, first guess by arithmetic |
| `kFlowPhraseMinSamples` | `kFlowPhraseSlots × kFlowNoteMinSamples` | §4.10, **derived** — not a separate tunable |
| `kFlowSlewFrac` | 0.35 | §4.11, first guess by arithmetic |

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

`step_index` is a public static (`lane.h:93`), so the branch is computable as
written. A straight grid, not `shuffle_step_index` — that choice is load-bearing
beyond the scoping reason in §11: `tests/test_instrument.cpp:653` asserts a FLOW
deck stays bit-exact under a live shared SHUFFLE turn, and a shuffled FLOW grid
would redden it.

Raster on `_phase`, not `phase_eff()`. `center.cpp:33-37` argues for locking the
raw phase; the consequence is that `_ev_phase` and `_ev_shape` become inert on this
lane while `_evolve_outgoing_pattern` keeps drawing and clamping them (§6.4).

### 4.1.1 `_cur_step`'s consumers — three, not two

`_cur_step` is now written in FLOW, where it previously stayed at its reset value.
There are **three** read sites:

| Site | Guard | Effect |
|---|---|---|
| `super_modulator.cpp:115` | `if (_step_on)` (`:114`) | none in FLOW |
| `Part::_fire_trigger` → `pitch_cur_step()` (`part.cpp:451`) | `ENGINE_SAMPLER` only | none — a Sampler deck is never in FLOW melody mode (§3.3) |
| `super_modulator.cpp:156` | **none** | `frac` (`:162-164`) is computed every tick and consumed only in the `follow()` branch (`:167`, `_step_on` only). Benign, but unguarded — an implementer following a two-item list will not look here. |

Nothing in `host/` reads `cur_step`/`steps()`; the test reads
(`test_step_grid_lock.cpp`, `test_lane_follow.cpp`, `test_lane_tick.cpp`) are all
STEP.

### 4.2 Phrase length

`_effective_length()` (`lane.cpp:189-192`) is **currently dead code — zero
callers.** It becomes the single source of truth:

```cpp
int ModLane::_effective_length() const {
    if (_flow_melody_on()) return kFlowPhraseSlots;   // 8, both hosts (§4.9)
    int n = _steps < 1 ? 1 : _steps;
    return n > kSeqSlots ? kSeqSlots : n;
}
```

Every present use of `_steps` as a *length* routes through it: `_generate_pattern_a`
(`lane.cpp:196`, `:201`), `_derive_pattern_b` (`lane.cpp:205`), `_start_note`'s `n`
(`lane.cpp:488-489`), and `set_step`'s length-change detection (`lane.cpp:143-144`).

**STEP stays bit-identical**, verified per site: `_start_note`'s expression is
character-for-character the helper's, and it never runs with `_flow_melody_on()`;
`expand_pattern_groove` already clamps to `[1,32]` internally
(`song_form.h:109-111`); both hosts clamp STEPS to ≤ 16. Two caveats:

- `set_step`'s `old_n`/`new_n` do not clamp the low end. Harmless only because
  `_steps >= 1` is established at `lane.cpp:141`.
- `lane_slots()` can hand a **texture** lane up to 64 (`test_lane_len.cpp:26`), so
  the helper's 32-clamp becomes reachable on non-melodic lanes if the length check
  is hoisted out of its `_melodic` guard. **Keep the guard.**

`set_step` has an ordering trap, because the helper now reads state that `set_step`
mutates: capture `const int old_len = _effective_length();` at entry, assign
`_step_mode` and `_steps`, then compare against a second call.

**Mode-flip behaviour, stated for all four transitions** so it is a rule and not
prose. `_effective_length()` is `kFlowPhraseSlots` in FLOW melody and `_steps`
otherwise, so `length_pending` fires exactly when those differ:

| Transition | Effective length | `length_pending` |
|---|---|---|
| STEP → STEP | `_steps` → `_steps'` | only if STEPS moved (today's behaviour) |
| STEP → FLOW (note engine) | `_steps` → 8 | only if `_steps != 8` |
| FLOW → STEP (note engine) | 8 → `_steps` | only if `_steps != 8` |
| FLOW → FLOW | 8 → 8 | never |

On Fireflow the STEPS knob passes through 1 on its way to 0 (`Fireflow.cpp:892-893`
sends `set_step(p, steps > 0, steps)`), so a mode flip can raise the flag twice
within a control tick or two. It is a flag, consumed once, so the deck regenerates
its phrase **once** on mode entry — which is a deliberate gesture and the same thing
a STEPS change does in STEP.

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
    } else if (_step_mode) {
        ++_note_age;                                     // rest step: STEP only
    }
}
```

`_start_note` stays STEP-only: note lengths and ties are rhythm, and a FLOW deck has
no gate to shape. The `else` gains a `_step_mode` guard: today it is unreachable in
FLOW (`gated` is unconditionally true there), so this is bit-identical for every
existing path, and it keeps a STEP-only counter from creeping on a FLOW deck.

### 4.4 The value

```cpp
float ModLane::_compute_raw() const {
    if (_flow_melody_on()) return _active_pattern().pitch[_sh_slot()];
    // ... unchanged ...
}
```

Arithmetically identical to `shape_value(ph, 1.f, pitch[slot])` — at `shape == 1`
the clamp at `waveforms.h:26` makes `i == 3, f == 1` and the return at
`waveforms.h:32` is `sh_hold` exactly — but written directly so SHAPE's inertness on
this lane is visible in the source rather than implied by a pinned argument.

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

**Correction (Task 12, after implementation).** The paragraph originally here
justified the deletion below by claiming a motif-cell raster "would only ever
have played the first motif instance, leaving FORM's arrangement inaudible."
That claim is false, and the implementation task that built §4.5 found the
mechanism that disproves it: `pg_target_len()` returns **8 for every
principle** (`engine/mod/phrase_gen.h:53`), so at `kFlowPhraseSlots = 8` the
sizing in `pg_derive_sizing` always comes out `k = 1, L = 8, r = 0` — exactly
**one** motif instance spans the whole phrase, and `cell_groove.len` equals
`pattern_groove.len` by construction, on every principle, always. There is no
reachable state in which the two rasters differ, so there was never a
behaviour to fix by deleting the `cell_groove` arm.

The deletion below is still correct, but for a different reason: it is
**unreachable-code removal**, not a behaviour fix. `_groove_k` has exactly one
caller, `_effective_gate`, which itself early-returns for any lane that is not
melodic — so the `cell_groove` arm this section deletes was dead by
construction (the `!_melodic` guard), independent of the motif/pattern sizing
argument above. Because no fixture can make the deleted code produce a
different result than the code that replaces it, **this deletion carries no
RED proof** — the plan's second RED recipe for it does not exist, and Task 3
dropped it rather than force one.

**The unreachability argument, correctly.** It is *not* the `!_melodic` early
return alone that makes the specific line dead: `lane.cpp:457` sits on the
melodic path with `_step_mode == false`, which is exactly the `cell_groove`
arm. It is dead because **both callers of `_effective_gate` are themselves
under `_step_mode`** — `lane.cpp:464` (`_step_mode ? _effective_gate(slot) :
true`) and `lane.cpp:491` inside `_start_note`, which only runs from
`lane.cpp:468` under `_melodic && _step_mode`. `_start_note`'s own
`cell_groove` branch (`lane.cpp:500-505`) is dead by the same argument and
goes with it. Both confirmed by two reviewers; RED proof #1 (the gate
expression itself) stands and is proved once, as the plan requires.

### 4.6 Cycle-wrap events

The early return is the audit's FORM/SONG finding; it narrows to the FLOW LFO state
only, and the melody-mode body is rate-limited (§4.10).

```cpp
void ModLane::_wrap_events() {
    const bool pending = /* unchanged */;
    if (_melodic && !_step_mode && !_flow_melody) return;   // FLOW LFO: unchanged
    if (_flow_melody_on() && _since_phrase < kFlowPhraseMinSamples) return;
    if (_flow_melody_on()) _since_phrase = 0;
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
`_apply_preroll_work` requires `_cur_step < 0`, which after §4.1 is true only before
the first sample, so a mid-flight FORM change or NEW press reaches audio at the next
wrap — up to 50 s. Accepted rather than fixed: SONG already advances per cycle, so a
structural change landing on a phrase boundary is musically coherent, and applying
mid-phrase would cut the melody in half. **It is not accepted at boot** — see §5.

### 4.7 `tick()`

`tick()` must implement the same slot walk. It is not needed in production —
`SuperModulator` drives `LANE_PITCH` through `process()` exclusively
(`super_modulator.cpp:109`; the `tick()`/`follow()` loop skips it at `:165-166`) —
but the function's documented contract is that it mirrors `process()`'s observable
sequence, `tests/test_lane_tick.cpp` exercises it directly, and the roadmap records
the cost of letting an isolated path drift from the production one.

**Scope warning, and a permitted stopping point.** This touches four sites: the
pending-mismatch entry (`lane.cpp:684-688`), `next_edge` (`:699-702`, which
hard-codes `1.0` off STEP), the wrap arm's `else _on_boundary()` (`:744`), and the
trailing `if (!_step_mode && !_frozen) _target = _compute_raw()` (`:751`), which must
be skipped in melody mode. `_since_fire`/`_since_phrase` advance by the interval
**before** the edge walk, so the window's first boundary does not read a stale
count; that quantises the floor to one control interval (~2 ms), which is inside its
own tolerance. `test_lane_tick.cpp`'s harness (`:40-54`) only ever configures STEP
melodic lanes, so §10.7 is a new case. §12 makes this the **last task, in its own
commit**, and permits it to end at a documented gap if the equivalence budget cannot
be re-derived cheaply.

### 4.8 Boot and mode entry

Three pieces of carried state, all previously harmless because FLOW had no slots:

- **`_cur_step` is stale across a mode change.** Entering FLOW melody with a
  `_cur_step` left from STEP means no boundary fires until the slot index happens to
  differ — the phrase's first note can be skipped or arrive a slot late. `set_step`
  clears `_cur_step` to `-1` on entry to *either* mode, mirroring what STEP entry
  already does for `_note_age`/`_note_hold` and the follower state
  (`lane.cpp:137-140`).
- **`_frozen` is stale across a mode change.** A lane leaving FLOW melody on a
  *closed* slot enters FLOW LFO with `_frozen == true`, and that branch is
  `if (!_frozen) _target = _compute_raw();` — the "LFO" would hold a constant for up
  to a full cycle. `set_step` and `set_flow_melody` both clear `_frozen`.
- **`_since_fire` and `_since_phrase` must be primed, not zeroed.** Left at 0 they
  make §4.10's floor suppress the *first* note — at boot, after RST, and on the
  first slot after mode entry, which would contradict §4.8's RST bullet and gates 8
  and 9 outright. `init()`, `reset()`, `set_step()` and `set_flow_melody()` all set
  both to their respective thresholds, so the next boundary always fires.

**`reset()` already clears `_cur_step`** (`lane.cpp:405`), so RST (`instrument.h:428`
→ `reset_phases()`) and `Center::_snap_phase` → `snap_pitch_phase()`
(`super_modulator.h:163`) restart the phrase at slot 0 and fire a note on the next
sample. **This is wanted** — RST is the resync gesture — but it is a new audible
consequence of RST on a FLOW deck, so §10.9 gates it. `reset()` also clears
`_frozen` and primes the two counters.

`SPOT` needs no handling: `SuperModulator::spot` skips `LANE_PITCH`
(`super_modulator.cpp:182`). One note for `ModLane`'s standalone contract, since
`kick()` remains reachable from tests: `kick()` wraps with `floor()`
(`lane.cpp:296-297`), so a kick crossing phase 1.0 skips `_wrap_events()` and the
song does not advance. Pre-existing; not fixed here.

### 4.9 The phrase length is a constant in FLOW melody mode

**Decision 2.5.** L is `kFlowPhraseSlots` = 8 on both hosts. There is no live
phrase-length change in FLOW.

Draft 2 made Glow's `P_STEPS_A` the FLOW phrase length, on the grounds that
`push_mode_and_steps` already pushes it in both modes (`flow.cpp:413-423`) and that
it sits in the DENSITY story (`taste.h:858`), so it would move with DENSITY for
free. The final review found that this does not work and cannot be made to work
cheaply:

- **`generate_phrase` fills only `[0, n)`** (`phrase_gen.h:165-200`) and
  `MelodyPattern::pitch[32]` is zero-initialised. Growing L past the generated
  length plays ungenerated slots — the root. `P_STEPS_A` runs 2 → 16 across the
  DENSITY sweep, so the melody would grow a run of identical root notes during
  precisely the gesture that demonstrates this design.
- Regenerating instead of re-spanning re-rolls the melody several times during the
  same sweep, which is what re-spanning was introduced to avoid.
- `derive_turnaround` copies A wholesale and then *permutes* B's `pattern_groove`
  (`song_form.h:272-289`), so re-spanning B from `cell_groove` would throw the
  turnaround away and leave B groove-identical to A.
- `_song.cadence_slot` is `length - 1` from the old length (`song_form.h:286`) and
  is consumed by `bind_song_cadence` at every `_advance_song` (`lane.cpp:259-263`),
  which *writes* `b.pitch[cadence_slot]` — stale after any length change.

Four defects on a mechanism the requirement never asked for. **`P_STEPS_A` stays
inert in FLOW** — which is what it is today, so this is not a regression, only one
fewer bonus. The host asymmetry draft 2 accepted disappears with it: both hosts now
get identical FLOW phrases from the same terrain.

### 4.10 The note-rate floor

**Decision 2.7.** Note rate is `k × cycle rate` with no upper bound. `P_RATE_A`'s
drone window is `{0,.25}` (`taste.h:972`) → 8–50 s cycles, the intended slow end;
but a non-drone archetype in FLOW reaches `{.55,.9}` ≈ 14 Hz, and `kModeW`
(`taste.h:689`) puts arp in FLOW 5 % of the time and fragment 25 %. At `k = L = 8`
that is over 100 fires per second, where `_gate_len` (5 ms) never expires so
`_gate_edge` never fires and the synth never re-attacks, `_chord.build()` runs its
up-to-27-candidate lay search per note, and every fire forces an extra
`Part::_control_tick()` (§6.2).

Two floors, one tunable:

- **Notes.** A boundary arriving sooner than `kFlowNoteMinSamples` after the last
  fire **holds instead of firing** (§4.3). This decimates to the floor rate rather
  than falling off a cliff, and DENSITY keeps its full effect everywhere below the
  ceiling.
- **Phrases.** `_wrap_events`'s melody-mode body runs at most once per
  `kFlowPhraseMinSamples` (§4.6). Without this the floor would bound the fires but
  not the wraps: at a 14 Hz cycle `_advance_song()` would advance 14 phrases per
  second, and a pending FORM/NEW would drive `_generate_pattern_a` +
  `derive_turnaround` at that rate — a CPU spike on the M6 target that the note
  floor does not touch. The constant is **derived** as `kFlowPhraseSlots ×
  kFlowNoteMinSamples`, so it is not a second thing to tune by ear.

`kFlowNoteMinSamples` = 60 ms of samples (≈ 16 notes/s), **a first guess set by
arithmetic** — above anything ambient, below anything that reads as a buzz. It is
sample-rate-derived, so 44.1 kHz behaves the same. §10.21 judges it.

### 4.11 SMOOTH against the slot interval

**Decision 2.8.** `_target` is now a staircase, so SMOOTH is the glide law of a
melody rather than the smoothing of an LFO. At SMOOTH 1 the time constant reaches
~0.5 s (`lane.cpp:279`); notes shorter than that never arrive and the melody
flattens into a wobble.

In melody mode `_update_slew()` clamps the slew time so a note reaches ~94 % of its
target within its own slot:

```
slot_samples = _phase_inc > 0
             ? 1.0 / (_phase_inc * (1 + _ev_rate) * _effective_length())
             : 0                              // stopped: clamp inert, see below
effective    = max(slot_samples, kFlowNoteMinSamples)   // the floor decimates, §4.10
t_s          = min(t_s, kFlowSlewFrac * effective / _sr)
```

Three details the draft-2 formula got wrong or left out:

- **`OnePole::init` takes seconds, not samples** (`onepole.h:14-16`:
  `k = 1/(time_s * sample_rate)`), and `_update_slew`'s `t` is seconds
  (`lane.cpp:279-280`). Without the `/ _sr` the clamp never binds.
- **Clamp against the *effective* interval, not the raw one.** Where §4.10's floor
  decimates, the raw slot interval is far shorter than the notes actually are, and
  clamping to it would make the glide much tighter than needed.
- **`_phase_inc == 0` must be guarded**, mirroring `step_samples()` (`lane.h:108-113`).
  In `double` it yields `inf` and the clamp goes inert, which is benign — but
  silently, and a silent inert guard is the shape this project fixes.

`_update_slew()` must therefore be called from `_update_inc()` **and** from
`set_flow_melody()`, since both the rate and the flag move the slot interval.
`_ev_rate` walks ±20 % at every wrap (`lane.cpp:556`) and is deliberately *not* a
recompute trigger: re-deriving the slew per wrap for a ±20 % term inside a 0.35
safety factor buys nothing, and the value is read at the next rate change anyway.
`set_fixed_slew`'s 20 ms override is already below any plausible clamp and is left
alone.

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

**Where it is pushed.** At the two sites where `_engine_id` is written —
`part.cpp:32` (`Part::init`) and `part.cpp:417` (`Part::_engine_swap`); review
confirmed there is no third. That is the convention `part.h` already documents for
`_engine_wants_in`: *"Written only where `_engine` is written … so the two cannot
drift apart."* It also gives the feature runtime engine switching, which the
init-time `set_melodic` never had.

**Why this expression, stated carefully.** Draft 1 called it "the FLOW restriction
of the condition `part.cpp:230` already uses … equivalent by construction". That is
misleading in a way that sends an implementer to the wrong place: `part.cpp:230` is
the **quantizer bypass** for `_pitch_q`, a different question that happens to name
the same two engines for the same underlying reason — on those decks the PITCH lane
is not a note. The two agree because the reason is shared, not because either
derives from the other. Do not wire `set_flow_melody` at that per-tick site.

`ENGINE_TEST_TONE` also gets melody mode. Harmless and intended — its PITCH lane
*is* a note — but it is not in the SYNTH/WAVE/BODY/ZAP list §2.3 implies, and
`wave_formant_sweep.json` puts a test tone on part B (§6.5).

**`set_flow_melody` carries a one-sided length check.** Flipping the flag changes
`_effective_length()` in FLOW, between `kFlowPhraseSlots` and `_steps`, and the two
directions are not symmetric:

- **Entering melody mode**, the pattern may have been generated at a different
  length — at boot, whenever a host has pushed a STEPS other than 8 before the
  first push of this flag. `pitch[]` is then filled only for `[0, _steps)`
  (`phrase_gen.h:165-200`) and slots past it are zero, so this **must** raise
  `_song.length_pending`.
- **Leaving melody mode**, it must not. The lane moves to the FLOW LFO path where
  §4.6 early-returns and nothing reads or regenerates the melody state; the groove
  is left at 8 and matches again on the way back. Raising the flag here would sit
  until the deck returned and then regenerate the melody for no reason.

So the check is written against the pattern rather than against the flip, which
makes it one-sided by construction:

```cpp
void ModLane::set_flow_melody(bool on) {
    _flow_melody  = on;
    _frozen       = false;                    // §4.8
    _since_fire   = kFlowNoteMinSamples;      // §4.8, §4.10
    _since_phrase = kFlowPhraseMinSamples;
    if (_flow_melody_on() &&
        _active_pattern().pattern_groove.len != _effective_length())
        _song.length_pending = true;
    _update_slew();                           // §4.11
}
```

`_apply_preroll_work` — whose guard §4.6 widens — then applies it **before the
first slot**, because `_cur_step < 0` still holds on the first `process()` call.
Both patterns are always built at the same length, so testing `_active_pattern()`
is enough.

**Boot ordering.** `_generate_pattern_a()` runs inside `ModLane::init` and reads
`_effective_length()` → `_flow_melody`. Order: `Part::init` calls `_mod.init(...)`
first (flag still `false`, pattern generated at `_steps`, which defaults to 8), then
pushes `set_flow_melody(...)`, whose check above closes any gap. `Part::init` is
called again mid-session by the VCV host (the `_step_seen` comment at `part.cpp:47`
documents this) and the same order holds on every call.

## 6. Consequences

### 6.1 SHAPE: this is the most audible change in the spec

§2.4 scopes SHAPE out of the design. It does **not** scope out the change: today a
FLOW SYNTH/WAVE/BODY pitch lane is a sine→pulse LFO, and the drone archetype caps
`P_SHAPE_A/B` at `{0,.25}` (the row at `taste.h:998`; the reasoning is at
`taste.h:102`) — so drone terrains currently get a **smooth sine pitch drift** and
after this change get a **stepped sequence**. That is the single largest sonic
change here, larger than any of the new controls, and it is a direct consequence of
§4.4 rather than a side effect. §10.19 listens for it first.

### 6.2 Retriggers scale with DENSITY — five consumers, not two

`_fired` now fires k times per cycle in FLOW instead of once. At `k == 1` that is
once per cycle, exactly today.

| Consumer | Effect at k > 1 |
|---|---|
| `part.h:291-295` | 5 ms GATE pulse per note; `_note_suppressed = _inhibit` re-latched per note |
| `part.h:329` → `Part::_fire_trigger` | `trigger_chord` per note — the chord progression, §6.3 |
| `part.h:321-326` `else if (fired) _control_tick();` | **the fire refresh.** `Quantizer::process`'s slew counts *calls*, each worth a full control interval (`part.h:305-311`), so the pitch glide and `ChordBuilder::set_color`'s zone hysteresis advance k× faster per cycle |
| `super_modulator.cpp:137-151` | the onset-gap ring, i.e. `rhythm()` reports k× shorter gaps — and that feeds FLUX's THIN rhythm reader, so **DENSITY now shapes the delay's skip pattern**, a cross-block coupling |
| `Fireflow.cpp:1449` | the lane LED blinks per note instead of per cycle |

### 6.3 COLOR works, with one inherited caveat

The chord layer is already live in FLOW (a FLOW deck fires once per wrap today);
what is new is that the root changes per slot, which is exactly a progression. The
caveat is pre-existing and shared with STEP, but this design sells chord
progressions as a headline: `_tg[LANE_PITCH]` is the **post-slew** value, so on the
fire sample `_chord.build()` chooses its lay and voice-leading against the
*previous* note's root, and `apply()` then re-applies those latched intervals to the
gliding root.

### 6.4 Two of GROW's four walks become dead weight

`_ev_phase` and `_ev_shape` no longer reach the melodic lane's value (§4.1) while
`_evolve_outgoing_pattern` keeps drawing and clamping them. They still consume RNG,
so the stream stays deterministic and comparable; they simply do nothing on this
lane. Not fixed here — removing a draw would change every STEP stream too. Recorded
so the SHAPE/SMOOTH rework inherits it.

### 6.5 Renders, determinism, CPU

**Every FLOW render changes** on SYNTH, WAVE, BODY, ZAP and TEST_TONE decks:
`_wrap_events` now consumes RNG where it consumed none, so streams diverge from the
first wrap. Accepted — the project has no bit-exactness gates.

**Both pinned render hashes are affected, and more than one commit moves them.**
`tests/check_render_hash.cmake` is driven from `CMakeLists.txt:221-230`
(`ctrl_identity.json` — sets no engine and no `set_step`, so **both decks are SYNTH
in FLOW**) and `:237-252` (`wave_formant_sweep.json` — part 0 is `wave` with
`set_step` flag `false`, part 1 a test tone). Because the flag defaults to `false`,
nothing moves until §5's push lands; then §4.10's floor moves it again
(`ctrl_identity.json` runs at rate 0.8/0.55 with default density 1 and `_steps` 8,
i.e. 8 fires per cycle, well inside the floor). **Re-base in every commit that moves
them**, not once at the end.

**Determinism is preserved.** Nothing here depends on timing or block size;
`kFlowNoteMinSamples` is derived from the sample rate rather than fixed in samples.

**CPU: cheaper in the lane, more expensive in `Part`, net unmeasured.** The lane
loses a per-sample `shape_value()` (containing `fast_sin`) and gains an integer slot
compare. `Part` gains, per note rather than per cycle, a `_chord.build()` lay search
(≤ 27 candidates), a voice retrigger and a `_control_tick()`. No bench round is
ordered; if M6 timing needs the number the rows are the `instrument_*` FLOW
workloads, and §4.10's two floors bound the worst case.

## 7. What stays bit-identical — and the two holes

Verified and safe: CHOKE does not depend on FLOW having no fires
(`instrument.cpp:214` opens the window on `gate() || flow()`, and `Part::flow()` is
`!_step_on`, `part.h:116`, so a FLOW priority deck already holds it permanently
open; `test_choke.cpp:226` stays green). `gate_state()`/`note_sustain()` are both
`_step_mode`-gated (`lane.h:68`, `:72`), so `Part::gate()` stays pulse-only in FLOW
and `test_part.cpp:573` holds. Nothing serializes `MelodyPattern` or lane state.

**Hole 1: "Sampler and BBD stay bit-identical" is true of the lane and false of
the mix.** The reverb is instrument-level with per-part sends
(`instrument.cpp:258-261`) and the master DRIVE/limiter is shared and is a
*threshold* (`spotykach-master-drive-is-a-threshold`). In a two-deck render whose
neighbour is a note engine in FLOW, the k-fold trigger density moves the tail and
the limiter, so the Sampler deck's **contribution to the master mix** is not
bit-identical even though its lane is. §10.13 is scoped to a single-deck render or a
per-deck tap accordingly; asserting it on a two-deck master bus would go red for a
non-defect and get quietly weakened.

**Hole 2 (found by Task 12's implementation, narrows the guarantee further,
owner-ruled): even a single-deck render is not bit-identical to `main`
end-to-end — only its own PITCH lane is.** `Part::init` boots every deck as
`ENGINE_SYNTH` (`part.cpp:32`, the pre-existing M2 boot default), and the
`set_flow_melody` push added by this work sits immediately after it
(`part.cpp:43`). Before a scenario's own `set_engine` action lands and its
click-free swap fade completes, the deck genuinely *is* a SYNTH deck running
the new melody engine in FLOW — for the fade's duration it fires a voice the
old continuous-LFO SYNTH never fired. Measured on
`host/render/scenarios/sampler_single_deck.json` (single active deck,
explicitly configured `sampler` on both sides of the comparison, not left on
the SYNTH default): **61,309 of 7,680,044 bytes differ, all inside the first
~36 s of a 40 s render, after which the two renders reconverge exactly for
the final ~4 s.** The shared master limiter's slow, asymptotic peak-follower
release (`engine/fx/limiter.h`) is what carries the boot-window transient for
that long; the transient's own audio is small (max sample delta ≈ −35 dBFS).

**The guarantee is narrowed, not the code — an explicit owner ruling.** State
it as: a Sampler/BBD deck's own PITCH lane is untouched by this work —
`_mod.set_flow_melody(false)` is pushed for both, the same value the prior
default already had, and the two renders' exact reconvergence after the boot
window demonstrates this rather than asserting it — but a render that ever
calls `set_engine` differs from `main` through the boot window, because the
deck is not a Sampler yet during it; that is a pre-existing architectural
interaction (the universal SYNTH boot default plus the click-free engine-swap
fade plus the shared master limiter) that this work exposes rather than
introduces, and fixing it would mean changing `Part`'s boot/swap sequencing,
which is out of this spec's scope. **Neither hash-gated render is affected**
(`ctrl_identity.json`, `wave_formant_sweep.json` never call `set_engine`, so
this path is never exercised there).

## 8. Hosts

No new control, no panel change, no new `ParamId` on either host. `host/render` gets
one scenario, `flow_melody.json`: a SYNTH deck in FLOW with DENSITY swept 0 → 1
across the render, so one WAV walks the whole continuum, with the pitch lane visible
in `mods.csv`. `set_density` is a real scenario verb (`scenario.cpp:142`), so the
scenario cannot silently no-op the way an unknown action would (`scenario.cpp:220`
ignores those).

## 9. The one flow-layer change

**Decision 2.6.** The standing note requires `_variation == 0` exactly.
`taste.h:927` gives `P_VARIATION_A` a bp0 cell of `{0,0}`, so WANDER at 0 draws
exactly zero — but the weather layer offsets `_eff[M_WANDER]` by up to ±0.10 scaled
by MOTION (`flow.cpp:448-449`, `kWeatherDepthMax = 0.10` at `taste.h:49`). At eff
0.10 the curve interpolates 40 % of the way to bp1, whose span is `{.05,.15}`, so on
a terrain that drew the top of that span `P_VARIATION_A` reaches ≈ 0.06 and
`_evolve_outgoing_pattern` plus `_mutate_groove` run at every wrap. The note is not
standing.

**`M_WANDER` joins `M_MOTION` and `M_PACE` in `weather_of`'s exclusion**
(`flow.cpp:312`). Two tokens, at a site that exists for exactly this, whose own
comment already carries the reasoning for `M_PACE`: *"It also keeps eff[M_PACE]
exactly 0.5 when nothing moves it, which the bit-identical-no-op claim depends on."*
The same sentence is now true of WANDER at 0.

Draft 2 clamped `_eff[M_WANDER]` below a `kWanderDeadzone` instead. That is
withdrawn, and both of its defects are recorded so the option is not revived
casually: the clamp sat in `recompute_and_push`'s per-parameter guard chain, which
runs on `v` **after** `eval_terrain` has already read `_eff` (`flow.cpp:448-455`),
so it could not have reached any curve; and its threshold was derived in
`P_VARIATION_A` units and applied in macro units, so the stated 0.07 still leaked
≈ 0.042. Corrected it would have had to be ≥ `kWeatherDepthMax` and cost 10 % of the
WANDER knob's travel, against the exclusion's zero.

The cost of the exclusion is stated plainly: **WANDER no longer breathes on its
own.** A terrain moves it only when the player does. That is the same trade `M_PACE`
already made.

`_eff[]` has exactly three consumers — `eval_terrain` for both terrains
(`flow.cpp:453`, `:455`), the `P_PACE` offset (`:591`) and the public `eff_macro()`
(`flow.h:115`); `param_now()`, the blend and `distance()` all read `_pushed`/`base[]`.
So this change touches the WANDER story and nothing else.

## 10. Verification

Every gate must be shown RED once before it is made green — the project rule that a
test which cannot fail gets fixed. Each names its RED recipe, because the memory
note's own conclusion is that the recipes that exist are the ones written into the
spec rather than invented by implementers. New file `tests/test_flow_melody.cpp`
unless a listed existing file is the better home.

**Behaviour, at the bare lane:**

1. **k == 1 is a standing note.** The pre-slew target is identical across ≥ 3 full
   cycles **and** `wrap_count_for_test()` (`lane.h:52`) shows the lane actually
   wrapped that many times. The wrap assertion is not optional: constancy alone
   passes on a stalled lane, the vacuity PACE §8 recorded. *RED: restore the
   per-sample `_compute_raw()`.*
2. **The fire count equals k per cycle**, for k = 1 … L, **at a rate below §4.10's
   floor** — the gate pins the rate, because above the floor the count is bounded by
   design and the assertion would be unsatisfiable. *RED: return `true` from the
   melody-mode branch of `_effective_gate`.*
3. **A closed slot holds.** The target at a closed slot equals the last open slot's.
   *RED: drop the `_frozen` guard.*
4. **FORM and SONG reach FLOW.** A FORM change alters the pitch sequence;
   `song_position()` advances once per wrap. *RED: restore `_wrap_events`'s early
   return.*
5. **VARIATION at k == 1 moves both things** (§3.1): the pitch walks, *and* the open
   slot migrates as ranks swap. At VARIATION == 0 both are bit-stable — the LOOP
   contract, in FLOW. *RED: widen `_mutate_groove`'s early return back to STEP-only
   for the migration half.*
6. **The melody-mode gate is value-dependent.** Two lanes identical but for
   `_density` produce different targets. *A counter proving the branch executed is
   explicitly not acceptable* — shape 4 in `fireflow-vacuous-test-gates`.
7. **`tick()` ≡ `kTickInterval` × `process()`** in melody mode (§4.7). *RED: leave
   `next_edge` at `lane.cpp:702`'s hard-coded `1.0`.*

**The four new mechanisms — each gets an assertion, not only an ear:**

8. **The note floor binds.** On a deliberately fast lane (cycle ≈ 14 Hz, k = L),
   fires per second ≤ `1 / kFlowNoteMinSamples`, and the lane still advances slots.
   *RED: remove the `_since_fire` check.*
9. **The phrase floor binds.** On the same lane, `song_position()` advances at most
   once per `kFlowPhraseMinSamples`. *RED: remove the `_since_phrase` check.*
10. **The slew clamp binds and has the right units.** At SMOOTH 1 on a melody-mode
    lane, the realised time constant ≤ `kFlowSlewFrac × effective_interval / _sr`,
    and a note reaches ≥ 90 % of its target within its slot. *RED: drop the `/ _sr`
    — draft 2's formula, which would never have bound.*
11. **WANDER is weatherless.** With the WANDER knob at 0 and MOTION at 1,
    `param_now(P_VARIATION_A) == 0.f` sampled across a full weather period
    (`kWeatherPeriodMaxS`). *RED: remove `M_WANDER` from the exclusion.* This gate
    would have caught draft 2's threshold error outright.

**Transitions and boot:**

12. **STEP ↔ FLOW mid-flight** (§4.8): the first slot after entry fires, and
    `_frozen` does not survive the transition. *RED: remove the `_cur_step = -1`
    clear.*
13. **RST and `snap_pitch_phase`** restart the phrase at slot 0 and fire — which
    requires the counter priming of §4.8. *RED: zero `_since_fire` in `reset()`
    instead of priming it.*
14. **The flag's default and boot order** (§3.3, §5): a bare `ModLane` runs the FLOW
    LFO path; a `Part` with a SYNTH engine runs melody mode from its first sample
    with `pattern_groove.len == kFlowPhraseSlots`, including where the host pushed a
    STEPS other than 8. *RED: flip the default to `true`.*
15. **An engine swap in FLOW** (SYNTH → Sampler → SYNTH) leaves
    `pattern_groove.len == _effective_length()` **at the first wrap after the swap
    back** — not "at every point", which is unsatisfiable by design (§5).

**Non-regression:**

16. A Sampler deck and a BBD deck in FLOW keep their own PITCH lane
    **untouched** — measured on a **single-deck render or a per-deck tap**, §7
    explains why a two-deck master bus cannot carry this assertion. **Narrowed
    by §7 Hole 2, owner-ruled:** this is not whole-render bit-identity to
    `main`. A render that calls `set_engine` differs from `main` through the
    boot window (measured: 61,309/7,680,044 bytes, all before ~36 s, exact
    reconvergence after) because the deck is not a Sampler yet during that
    window; a render that never calls `set_engine` is unaffected.
17. A STEP render is bit-identical to `main`.
18. The three tests that pin FLOW-LFO behaviour — `test_song_lane.cpp:193`, `:324`
    and `test_rhythm_ring.cpp:144` — stay green under the `false` default and are
    **retitled** to name the path they pin. Each gains a melody-mode sibling,
    because otherwise they document a path production no longer takes.
    `test_gate_density.cpp:59`'s stated contract ("FLOW: no per-step gate ⇒ no
    freeze source") becomes false for melody mode and must be re-scoped rather than
    left green by the accident of `_density` defaulting to 1 (`lane.cpp:84`).

**Audit gates, corrected:**

19. `tests/test_param_impact.cpp`: `P_FORM_A/B` and `P_SONG_A/B` are currently
    **excluded** from both gates (`:107-109`, documented at `:96-105`), so nothing
    there can "invert" — the exclusion must be **deleted** for the gate to exist.
    What goes red on its own is the mode-exclusive **exact-set** comparison at
    `:309-312` (`report()`, `:255-263`), which lists `P_DENSITY_A, P_DENSITY_B,
    P_STEPS_A, P_STEPS_B, P_SHUFFLE, P_TEMPO_BPM, P_COUPLE`. Three of those revive
    here — `P_STEPS_A/B` do **not**, per §4.9.
20. **A caveat on gate 19, decided in advance.** The audit records a *second,
    untraced* gate on FORM/SONG — forcing `SHAPE_A` to 1.0 made FORM audible on only
    1 of 6 STEP terrains — and §11 defers it. The likeliest suspect is downstream of
    the lane: `P_DEPTH_A` is the audit's strongest unowned parameter and multiplies
    every lane output before its destination (`part.cpp:105`), and
    `_active[LANE_PITCH]` gates it too, so a terrain can draw the melody to
    inaudibility whatever the lane does. The gate therefore **controls for DEPTH and
    `_active`**; if FORM/SONG still measure zero with those held, that is an
    out-of-scope finding to record, **not** a failure of this design. Deciding this
    in advance is the point.

**Listening checkpoints** (owner, not automatable):

21. The drone → melody continuum via `flow_melody.json`, against §1.
22. **The SHAPE consequence of §6.1** — a drone terrain's smooth sine pitch drift
    becoming a stepped sequence. Judge this before the new controls; it is the
    larger change.
23. The retrigger consequences of §6.2, specifically the faster Quantizer glide and
    FLUX's THIN pattern following DENSITY.
24. `kFlowNoteMinSamples` (§4.10) and `kFlowSlewFrac` (§4.11) — both first guesses by
    arithmetic. Gates 8–10 prove the mechanisms bind; only the ear sets the values.
25. Whether losing WANDER's self-motion (§9) is the right trade.

## 11. Non-goals

- **SHAPE on the melodic lane in FLOW** — §2.4; the SHAPE/SMOOTH rework owns it, and
  the audit records an untraced second gate in that blend. This is a scoping
  decision about *design*, not about *impact*: §6.1 is in scope and audible.
- **The rest of SMOOTH** — only §4.11's clamp is taken, and only because without it
  the melody is not heard as notes.
- **`P_STEPS_A` in FLOW** — §4.9. Inert today, inert after.
- **SHUFFLE in FLOW** — a rhythmic control; a drone has no rhythm to swing. Also
  load-bearing for `test_instrument.cpp:653` (§4.1).
- **TEMPO in FLOW** — §1.2.
- **`taste.h` retuning** — §3.2; the curve already spans the requirement.
- **`kick()`'s skipped wrap** (§4.8) — pre-existing, unreachable from production.
- **The audit's two open threads** — the second silent-deck cause (12 of 52
  terrains, BODY on the silent side) and the second FORM/SONG gate (§10.20).

## 12. Ordering and deliverables

**Commit order.** With the flag defaulting to `false`, `_melody_engine_on()`
degenerates to `_melodic && _step_mode` until §5's push lands, so commits 1–4 are
inert by construction — the real constraint is that **§5 lands after §4.1, §4.6 and
§4.8**, not an ordering among those three. (Draft 2 claimed the §4.1/§4.6 order was
load-bearing because `_apply_preroll_work` is guarded on `_cur_step < 0`; with a
`false` default it cannot fire either way. The claim is withdrawn.)

1. `_sh_slot()` + `process()`'s slot walk (§4.1).
2. `_effective_length()` + `set_step`'s check and its ordering trap (§4.2).
3. `_effective_gate`/`_groove_k` rewrite, then the `cell_groove` deletions (§4.5) —
   the deletion depends on the call-site change being in.
4. `_wrap_events` and the three widened guards (§4.6); mode-entry and counter
   priming (§4.8).
5. The note and phrase floors (§4.10) and the slew clamp (§4.11).
6. `Part`/`SuperModulator` wiring (§5) — **the commit that makes any of it audible.**
7. The weather exclusion (§9) — independent, may land anywhere.
8. `tick()` (§4.7) — **last, own commit**, documented gap permitted.

Render hashes (§6.5) are re-based **in each commit that moves them** — expect
commits 5 and 6.

**Documentation deliverables, not follow-ups** (house convention, PACE §5):

- `docs/roadmap.md` — the Planned entry loses its TEMPO row (§1.2 contradicts it)
  and gains the result; the milestone moves to Done.
- `docs/2026-08-13-glow-macro-audit.md` — Result 3 and the per-macro DENSITY verdict
  become historical; add a pointer rather than editing the measurement.
- `host/vcv/README.md` — DENSITY's description changes meaning on both modules.
- `CLAUDE.md` needs no change; no file moves.
