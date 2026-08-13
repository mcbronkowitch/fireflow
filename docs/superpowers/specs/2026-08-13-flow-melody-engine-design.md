# FLOW melody engine — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Precedes:** the SHAPE/SMOOTH rework, then the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`

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
| `_effective_gate`'s FLOW branch is dead code | — | `lane.cpp:454-457`; both call sites sit behind `_step_mode` |
| The whole S&H end collapses to one slot | — | `_sh_slot()` returns 0 for every cycle (`lane.cpp:426`) |
| `_target` never holds | — | `_target = _compute_raw()` runs per sample (`lane.cpp:619`) |

### 1.2 One correction to the roadmap's framing

The roadmap lists a fourth finding — *TEMPO moves no audio in FLOW* — under the
same cause. **It is not the same cause and it is not in scope.** Free lanes take
their rate from `free_hz(_rate_norm) * _pace` and never read `_bpm`
(`super_modulator.cpp:28-29`); that is what "free" means, and PACE (spec
2026-08-12) is the speed control that reaches free mode. Making TEMPO reach a free
lane would erase the distinction between the two modes. See §8.

## 2. Design decisions taken

Taken in the brainstorming session of 2026-08-13, recorded so the plan does not
re-derive them.

1. **A slot sequencer without rhythm.** The melodic lane gets real slots in FLOW,
   but only half the STEP machine: no gate pattern, no note lengths, no ties —
   only *which note, and when it changes*. Rejected alternatives: taming the LFO
   with a hold/dead-zone stage (leaves FORM/SONG unreachable, so the audit's
   largest finding survives), and a two-layer sequencer-plus-bend design (invents
   surface this spec does not need; addable later).
2. **One cycle is one phrase pass**, the same relation STEP already has. DENSITY
   selects k of L slots through the existing groove ranking; the slots it skips
   **hold** the previous note. Rejected: one note per cycle wrap (makes the
   melody necessarily the slowest thing in the system), and deriving the raster
   from `STEPS` alone (Fireflow spends `STEPS == 0` on the mode switch itself —
   see §4.2).
3. **`Part` decides whether a PITCH lane is a note at all.** SAMPLER and BBD keep
   today's continuous LFO, byte for byte. Rejected: one mechanism for all decks
   (a measurable regression on two engines that have just been through ear
   review), and a new per-deck control (new surface on two hosts and the hardware
   panel for a question the engine can already answer).
4. **SHAPE has no role on the melodic lane in FLOW melody mode.** It stays fully
   alive on the four texture lanes (`set_shape` reaches all lanes,
   `super_modulator.cpp:77`). The question of what SHAPE should mean for a melody
   belongs to the SHAPE/SMOOTH rework, which is the next spec — and that rework
   exists precisely so it can design against a working lane instead of against
   the blend the audit could not fully trace.

## 3. The mechanism

### 3.1 The two axes

| Control | Meaning in FLOW melody mode |
|---|---|
| **DENSITY** | how many distinct notes the drone uses — k of L |
| **VARIATION** | whether those notes change over time |

At `k == 1` only the rank-0 slot is open, and that slot recurs once per cycle: the
lane emits the same pitch forever. With VARIATION at 0 that is a genuinely
standing note; with VARIATION > 0 the same single note walks slowly through
`_mutate_slot`'s tonic-gravity random walk. At `k == L` every slot changes the
note and the result is a melody, L notes per cycle. Everything between holds the
previous note across the closed slots.

The two axes are already wired to the right scope: `set_density` reaches
`LANE_PITCH` only (`super_modulator.h:34`), so DENSITY has always been the melody
control — it simply had no destination in FLOW.

### 3.2 The terrain tables need no edit

`taste.h:855` gives the DENSITY story the drone archetype window `{0, .45}`, with
`P_DENSITY_A` breakpoints `{.02,.08} … {.7,.95}`. Reading the curve at L = 8:

| Archetype | Macro | `P_DENSITY_A` | k | Result |
|---|---|---|---|---|
| drone | 0.0 | .02 – .08 | 1 (clamped) | one standing note |
| drone | 1.0 | ≈ .25 – .35 | 2 – 3 | two or three notes, long held |
| arp | 1.0 | .7 – .95 | 6 – 8 | melody |

The table's own comment states the intent — *"a drone at full DENSITY lands where
an arp sits at half"* — so the curve was written for this behaviour and has only
ever lacked an audio path. **No value in `taste.h` changes in this work.** If
listening later disagrees, that is a table edit, not a mechanism change.

### 3.3 Mode taxonomy

Three states for `LANE_PITCH`, and the second one is new:

| State | Condition | Behaviour |
|---|---|---|
| STEP | `_step_mode` | unchanged |
| FLOW melody | `_melodic && !_step_mode && _flow_melody` | this spec |
| FLOW LFO | `_melodic && !_step_mode && !_flow_melody` | unchanged, bit for bit |

`_flow_melody` is a new `ModLane` flag, default `true`, set by `Part`. It is
deliberately **not** a reuse of `_melodic`: clearing `_melodic` on a Sampler deck
would also change `_effective_gate`, `_mutate_slot` and `_evolve_outgoing_pattern`
for that lane and would not be bit-identical. A separate flag is the only version
that leaves the two continuous-pitch engines untouched.

Two private predicates carry the taxonomy through the rest of this document, and
both are named here rather than left to the reader:

```cpp
// "this lane is running the FLOW melody engine on this sample"
bool _flow_melody_active() const { return _melodic && !_step_mode && _flow_melody; }
// "this lane runs the melody system at all right now" (STEP or FLOW melody)
bool melody() const { return _melodic && (_step_mode || _flow_melody); }
```

`kFlowPhraseSlots = 8` joins `kSeqSlots` as a `static constexpr int` on `ModLane`.

## 4. Changes in `engine/mod/lane.*`

### 4.1 The slot walk

`process()`'s FLOW branch splits:

```cpp
} else if (_flow_melody_active()) {
    const int slot = step_index(static_cast<float>(_phase), _effective_length());
    if (slot != _cur_step) { _cur_step = slot; _on_boundary(); }
    // no per-sample recompute: _target holds between boundaries
} else {
    if (wrapped) _on_boundary();
    if (!_frozen) _target = _compute_raw();     // unchanged
}
```

A straight grid, not `shuffle_step_index`: SHUFFLE is a rhythmic control and stays
out of FLOW (§8).

`_cur_step` is now written in FLOW, where it previously stayed at its reset value.
Its two consumers are safe and the implementer must confirm both rather than
assume: `SuperModulator`'s deck-step clock reads it only under `_step_on`
(`super_modulator.cpp:114`), and `Part::_fire_trigger`'s `pitch_cur_step()` read is
`ENGINE_SAMPLER`-only — and a Sampler deck is never in FLOW melody mode by §3.3.

### 4.2 Phrase length

`_effective_length()` becomes the single source of truth and gains the FLOW
default:

```cpp
int ModLane::_effective_length() const {
    int n = _steps;
    if (_flow_melody_active() && n < 2) n = kFlowPhraseSlots;   // 8
    if (n < 1) n = 1;
    return n > kSeqSlots ? kSeqSlots : n;
}
```

The `< 2` clause exists because the two hosts disagree about what `STEPS` means in
FLOW:

- **Glow** pushes `P_STEPS_A` (2..16) through `push_mode_and_steps` in *both*
  modes (`flow.cpp:422`), so a Glow terrain gets a real, drawn FLOW phrase length —
  and `P_STEPS_A` sits in the DENSITY story itself (`taste.h:858`), so it moves
  with DENSITY exactly as that story's comment intends. A fifth dead parameter
  comes alive for free.
- **Fireflow** spends `STEPS == 0` on the mode switch (`Fireflow.cpp:893`), which
  `set_step` clamps to 1. It therefore gets the constant 8. This is a documented
  limitation, not a defect to fix here: giving Fireflow a FLOW phrase length needs
  a panel position, which belongs to the hardware panel work.

Every present use of `_steps` as a *length* routes through this helper:
`_generate_pattern_a` (both the `generate_phrase` and `expand_pattern_groove`
arguments), `_derive_pattern_b`, `_start_note`'s `n`, and `set_step`'s
length-change detection.

`set_step`'s detection has an ordering trap worth stating outright, because the
helper now reads state that `set_step` itself mutates: capture
`const int old_len = _effective_length();` at function entry, assign `_step_mode`
and `_steps`, then compare against a second call. The present code computes
`old_n`/`new_n` inline from `_steps` alone and would silently keep reading the old
definition if only the call sites were swapped.

**STEP stays bit-identical** because for `_steps >= 2`
the helper returns `min(_steps, 32)`, which is what those call sites compute
today, and both hosts clamp STEPS to ≤ 16.

One consequence worth having on purpose: on a STEP → FLOW flip the effective
length does *not* change on either host (Glow 8 → 8; Fireflow 8 → 1 → 8), so
`_song.length_pending` does not fire and **the melody survives a mode change**
rather than being regenerated under the player.

### 4.3 The boundary

```cpp
void ModLane::_on_boundary() {
    int slot = _sh_slot();
    bool gated = (_step_mode || _flow_melody_active()) ? _effective_gate(slot)
                                                       : true;
    _frozen = !gated;
    if (gated) {
        _fired = true;
        if (_melodic && _step_mode) _start_note(slot);      // rhythm: STEP only
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
    if (_flow_melody_active()) return _active_pattern().pitch[_sh_slot()];
    // ... unchanged ...
}
```

This is arithmetically identical to `shape_value(ph, 1.f, pitch[slot])` — at
`shape == 1` that function returns `sh_hold` exactly (`waveforms.h:26`) — and is
written directly so that SHAPE's inertness on this lane is *visible in the source*
rather than implied by a pinned argument. §2 decision 4.

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

This is the correction that decides the whole design's musical worth. The
audit's dead FLOW branch reads `cell_groove`, whose `len` is the **motif** length
(`_generate_pattern_a` passes `layout.motif_len` to `pg_gen_groove`). A raster on
the motif cell would only ever play the first motif instance, so FORM's
arrangement — the A A B A that `pg_build_arrangement` lays out — would stay
inaudible, and the audit's largest finding would survive the fix meant to close
it. The dead branch is therefore **deleted, not wired**.

Two claims the implementer must prove before deleting rather than accept from this
document, because both are reachability arguments:

- `_groove_k()`'s `cell_groove` branch is unreachable — its only caller is
  `_effective_gate`, which returns before reaching it whenever `!_melodic`.
- `_start_note`'s `cell_groove` branch is unreachable — the function is called
  only under `_melodic && _step_mode`.

If either proves reachable, keep it and say so in the plan.

### 4.6 Cycle-wrap events

The early return is the audit's FORM/SONG finding; it narrows to the FLOW LFO
state only.

```cpp
void ModLane::_wrap_events() {
    const bool pending = /* unchanged */;
    if (_melodic && !_flow_melody && !_step_mode) return;   // FLOW LFO: unchanged
    _evolve_outgoing_pattern();
    if (melody()) {
        if (pending) _apply_pending_song_work();
        else         _advance_song();
    }
}
```

Inside `_evolve_outgoing_pattern`, `_renew_units()`'s guard and `_mutate_groove`'s
early return both widen from `_melodic && _step_mode` to `melody()`. So in FLOW
melody mode VARIATION does three things, all of them wanted: GROW walks the
pitches (`_mutate_slot`, §4.3), RENEW regenerates whole motifs
(`_renew_units`), and the groove mutation shifts *which* slots are open — i.e.
which of the phrase's notes the drone visits.

`_apply_preroll_work`'s guard widens the same way, so pending FORM/SONG/NEW work
applies before the first slot of a FLOW deck rather than waiting for a wrap.

`_advance_song()` running per cycle means **one phrase per cycle** — a slow
arrangement at `kRateFreeMin` (0.02 Hz → 50 s per phrase), which is the
intended feel.

### 4.7 `tick()`

`tick()` must implement the same slot walk. It is not needed in production —
`SuperModulator` drives `LANE_PITCH` through `process()` exclusively
(`super_modulator.cpp:109`) and `tick()` only ever sees texture lanes — but the
function's documented contract is that it "mirrors `process()`'s observable
sequence", `tests/test_lane_tick.cpp` exercises it directly, and the roadmap
already records the cost of letting an isolated path drift from the one production
uses ("a row that isolates a component need not reproduce *how that component is
called*", per-sample call boundary round). Leaving melody mode out would make
`tick()` silently violate its own contract.

## 5. Changes in `engine/parts/` and `engine/mod/super_modulator.*`

```cpp
void SuperModulator::set_flow_melody(bool on) {
    _lanes[LANE_PITCH].set_flow_melody(on);
}
```

`Part` pushes it from the engine id:

```cpp
_mod.set_flow_melody(_engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD);
```

Two notes on that expression. First, it is the FLOW restriction of the condition
`part.cpp:230` already uses — `SAMPLER || (BBD && !_step_on)` — because
`!_step_on` is true throughout FLOW, which is the only mode the flag affects. The
two are equivalent by construction, and the plan should carry a comment saying so
at both sites so a future edit to one is visibly an edit to the other.

Second, it is pushed **where `_engine_id` is written** — `Part::init` and
`Part::_engine_swap`, both in `part.cpp` — and not per control tick. That is the
convention `part.h` already documents for `_engine_wants_in`: *"Written only where
`_engine` is written … so the two cannot drift apart."* It also gives the feature
runtime engine switching for free, which the init-time `set_melodic` never had.

**`set_flow_melody` must carry `set_step`'s length check.** Because it can flip
`_flow_melody_active()`, it can change `_effective_length()` — a FLOW deck swapped
from SYNTH to Sampler goes 8 → `_steps`, and back on the return trip. So the setter
compares `_effective_length()` before and after the assignment and raises
`_song.length_pending` when it moved, exactly as `set_step` does (§4.2). Without
it the lane would keep a `pattern_groove` built for the other length. The
mis-indexing is bounded — every read is `slot % groove_length` — so the symptom is
a wrong groove rather than a crash, which is precisely the kind of defect that
survives a test suite unless it is named.

## 6. Consequences that are not obvious

**Retriggers scale with DENSITY.** `_fired` now fires k times per cycle in FLOW
instead of once. `Part::process` turns each into a 5 ms GATE pulse and a
`trigger_chord` (`part.h:291-295`, `:329`). At `k == 1` that is once per cycle,
which is exactly today's behaviour; at `k == L` it is a soft attack per note. This
is judged correct — a melody wants articulation, a drone does not, and DENSITY
already separates them — but it is the item most likely to need an ear, so it is
named as a listening checkpoint in §9 rather than asserted.

**Every FLOW render changes** on SYNTH, WAVE, BODY and ZAP decks. `_wrap_events`
now consumes RNG draws in a state where it previously consumed none, so the
streams diverge from the first wrap. This is accepted: the project has no
bit-exactness gates, renders are sanity checks. It does mean the render-hash
files in `tests/check_render_hash.cmake` must be re-based if any pinned scenario
runs a FLOW SYNTH deck — the plan checks this rather than assuming either way.

**Determinism is preserved.** Nothing here introduces a draw that depends on
timing, sample rate or block size; the same seed still gives the same terrain.

**CPU: expected neutral to slightly cheaper, not measured.** Melody mode removes
a per-sample `shape_value()` call — which contains a `fast_sin` — from the PITCH
lane in FLOW and replaces it with an integer slot compare. No bench round is
ordered; if M6 timing later needs the number, the rows are the `instrument_*`
FLOW workloads.

## 7. Hosts

No new control, no panel change, no new `ParamId` on either host. `host/render`
gets one scenario, `flow_melody.json`: a SYNTH deck in FLOW with DENSITY swept
0 → 1 across the render, so one WAV walks the whole continuum from standing note
to melody, with the pitch lane visible in `mods.csv`.

## 8. Non-goals

Each of these is deliberately out of scope, with the reason, so a reviewer does
not read them as oversights:

- **SHAPE on the melodic lane in FLOW** — §2 decision 4; the SHAPE/SMOOTH rework
  owns it, and the audit records an untraced second gate in that blend.
- **SHUFFLE in FLOW** — a rhythmic control; a drone has no rhythm to swing.
- **TEMPO in FLOW** — §1.2. Free means unsynced; PACE is the speed control that
  reaches it.
- **`taste.h` retuning** — §3.2; the curve already spans the requirement.
- **A Fireflow FLOW phrase-length control** — §4.2; it needs a panel position.
- **The audit's two open threads** — the second silent-deck cause (12 of 52
  terrains, BODY on the silent side) and the second FORM/SONG gate. Both are
  untraced and neither is this mechanism.

## 9. Verification

Every gate below must be shown RED once before it is made green — the project rule
that a test which cannot fail gets fixed. New file `tests/test_flow_melody.cpp`
unless a listed existing file is the better home.

**Behaviour, at the bare lane:**

1. DENSITY at `k == 1`: the lane's pre-slew target is identical across ≥ 3 full
   cycles. RED by restoring the per-sample `_compute_raw()`.
2. DENSITY at `k == L`: L distinct boundary targets per cycle; the count of
   `fired()` events per cycle equals k across k = 1 … L.
3. A closed slot **holds** — the target at a closed slot equals the target of the
   last open one, not a fresh value.
4. FORM change alters the pitch sequence in FLOW; SONG advances `song_position()`
   once per cycle wrap in FLOW.
5. VARIATION at `k == 1`: the standing note moves over N cycles at VARIATION > 0
   and is bit-stable at VARIATION == 0 (the LOOP contract, in FLOW).
6. `_effective_gate` is reached in FLOW — a counter or a value-dependence proof,
   so the audit's dead branch is provably live in its replacement.

**Equivalence and non-regression:**

7. `test_lane_tick.cpp`: `tick()` and `kTickInterval` × `process()` produce the
   same observable sequence in FLOW melody mode (§4.7).
8. A Sampler deck and a BBD deck in FLOW render **bit-identical** to `main`. This
   is the guard on §2 decision 3 and the reason `_flow_melody` is a separate flag.
9. A STEP render is bit-identical to `main`.
9b. An engine swap in FLOW (SYNTH → Sampler → SYNTH) leaves the lane with a
    `pattern_groove` whose `len` matches `_effective_length()` at every point —
    the guard on the `set_flow_melody` length check in §5.

**Audit gates, inverted:**

10. `tests/test_param_impact.cpp`: `DENSITY_A`, `FORM_A` and `SONG_A` must now
    produce a **non-zero** `rel_diff` on FLOW terrains. The plan first checks
    whether the existing assertions pin the dead behaviour; if they do, they
    invert rather than gain a sibling.

**Listening checkpoints** (owner, not automatable):

11. The retrigger-per-note consequence of §6 across DENSITY 0 → 1 on a drone
    terrain.
12. The drone → melody continuum itself, via `flow_melody.json`, against the
    requirement in §1.

## 10. Open question

One, and it does not block implementation: **what a FLOW deck should do at
`k == 1` when the phrase regenerates** (a NEW press, a FORM change). The note
jumps to the new phrase's rank-0 pitch. Whether that jump should be immediate,
slewed by SMOOTH, or deferred to the next cycle is a listening decision; the plan
implements the simplest — it is already slewed by SMOOTH, since `_target` feeds
`_slew` — and the checkpoint in §9.12 is where it gets judged.
