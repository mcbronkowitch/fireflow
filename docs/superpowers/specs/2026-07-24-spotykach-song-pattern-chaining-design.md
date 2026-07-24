# Spotykach Song-Form Pattern Chaining — Persistent `AAAB` Turnaround

**Date:** 2026-07-24
**Status:** Approved design; written-spec review pending
**Scope:** PITCH lane in STEP mode, phrase/rhythm generation, VCV FORM control,
and tests. FLOW, non-melodic lanes, synth/sampler engines, scales, and hardware
firmware mapping are unchanged.

## 1. Purpose

The melody engine already composes a recognizable phrase and lets the bipolar
MELODY control evolve it:

- **RENEW** regenerates coherent units;
- **LOOP** freezes pitch and rhythm;
- **GROW** mutates the existing phrase.

Those mechanisms operate on one pattern cycle. They create good local variation,
but not a larger form. This feature adds a second, independent time scale:

```text
Pattern identity:       A       A       A       B
Duration:             STEPS   STEPS   STEPS   STEPS
MELODY evolution:       ↑       ↑       ↑       ↑
```

Pattern B is a persistent, stronger relative of A. Its beginning recalls A and
its ending becomes a distinct turnaround that leads back to A. With 16 STEPS the
result is a 64-step supercycle. In general, SONG lasts `4 * STEPS`.

SONG becomes the factory-default form on both instrument Parts. Pattern A and
Pattern B in this document are form symbols inside one Part; they must not be
confused with the instrument's left Part A and right Part B.

## 2. Decisions

1. **SONG and MELODY are independent.** `AAAB` determines which pattern plays;
   RENEW/LOOP/GROW continue to determine how the currently completed pattern
   evolves.
2. **A and B are full persistent snapshots.** Each owns pitch, motif metadata,
   note gates, note lengths, and a pattern-long groove.
3. **The v1 form is fixed `AAAB`.** The controller stores the form as four
   symbols so future `ABAB`, `AABB`, or `BBBA` forms do not require a transport
   redesign, but those forms are out of scope.
4. **B is derived once, then evolves independently.** It is not recreated on
   every B occurrence.
5. **A evolves three times and B once per supercycle.** The existing wrap-based
   MELODY behavior runs after every pattern, not only after all four patterns.
6. **B has a real pattern-long groove.** Its ending can differ rhythmically
   from its beginning; it is not an 8-step cell tiled twice.
7. **Pattern switches are boundary-safe.** FORM, NEW, and required SONG rebuilds
   become audible only at the next pattern wrap.
8. **Factory basis is HIERARCHICAL on both Parts.**
9. **Preset compatibility is not required during beta.**

## 3. FORM control

The VCV `PRIN` button is replaced by a compact, snapped `FORM` knob in the same
PLAY-row position. `NEW` remains beside it. The six named values, in order, are:

1. `SONG · AAAB` — factory default;
2. `TWO MOTIFS`;
3. `ONE + VAR`;
4. `HIERARCHICAL`;
5. `CALL / RESPONSE`;
6. `OSTINATO`.

The Rack parameter quantity exposes those exact names in the tooltip and to
automation. FORM becomes a normal Rack parameter instead of the current
button-driven non-parameter principle index.

The lane also stores `last_basis`, a normal `Principle`. Factory reset sets it to
`Hierarchical` on both Parts.

### 3.1 FORM transitions

- **Normal principle → SONG:** finish the active pattern, capture it as Pattern A,
  expand its groove to pattern length, derive Pattern B, reset the form position,
  and play A.
- **SONG → normal principle:** finish the active pattern and generate a fresh
  phrase with the selected principle.
- **Normal principle → another normal principle:** finish the active pattern,
  update `last_basis`, and generate a fresh phrase with the selected principle.
- **FORM changes during a pattern:** the last selected FORM value wins at the
  next wrap.

Selecting a normal principle updates `last_basis`. Returning to SONG therefore
uses the most recently selected character instead of a hidden fixed generator.

### 3.2 NEW

- **NEW in SONG:** at the next pattern wrap, generate a fresh A with
  `last_basis`, expand its groove to pattern length, derive a fresh B, clear the
  lane-wide EVOLVE offsets, reset the form position, and play A.
- **NEW in a normal principle:** retain the existing behavior: generate a fresh
  phrase with the selected principle at the next wrap.
- If FORM and NEW are both pending, apply the FORM selection first and NEW to
  the resulting mode. Entering SONG with NEW therefore generates a fresh A
  instead of capturing the outgoing phrase.

### 3.3 Persistence

Rack saves FORM as a parameter and saves `last_basis` as the small associated
non-parameter state required while FORM is SONG. Factory reset restores
`SONG · AAAB` plus `Hierarchical`.

"Persistent A/B" means the two snapshots survive every supercycle and retain
their independent evolution for the lifetime of the lane. Serializing the live
generated pitches and grooves into a Rack patch is out of scope, matching the
current phrase buffer behavior.

## 4. State and component boundaries

The implementation uses fixed-size POD state and no heap allocation.

### 4.1 `PatternGroove`

A SONG pattern owns:

```cpp
struct PatternGroove {
    uint8_t rank_of_slot[32];
    uint8_t note_len[32];
    uint8_t len;
};
```

For `n = min(STEPS, 32)`:

- `len == n`;
- `rank_of_slot[0..n)` is a permutation of `0..n-1`;
- slot 0 has rank 0 and remains the unmaskable start anchor;
- `note_len` remains in `[1, 4]`;
- DENSITY computes `k = clamp(round(density * n), 1, n)` and a slot fires when
  `rank_of_slot[slot] < k`.

Normal non-SONG principles retain the existing motif-length `GrooveCell` behavior.
SONG expands that cell into a pattern groove by stable ordering of
`(cell rank, occurrence index)`. This initially preserves the existing groove's
priority character while assigning every absolute pattern slot its own rank.
Subsequent SONG mutations can therefore change the second half independently.

### 4.2 `MelodyPattern`

A pattern snapshot groups the state that currently lives as parallel fields on
`ModLane`:

```cpp
struct MelodyPattern {
    float pitch[32];
    bool gate[32];
    uint8_t motif_id[32];
    PhraseLayout layout;
    GrooveCell cell_groove;
    PatternGroove pattern_groove;
};
```

The lane owns two entries. Entry 0 is A and entry 1 is B. Outside SONG, entry 0
also serves as the normal active phrase, so no third full phrase copy is needed.
The lane accesses the active entry by index; it does not store a self-pointer,
which keeps copied `ModLane` values safe.

Normal principles read `cell_groove` exactly as today. SONG reads
`pattern_groove`. Capturing a normal phrase into A expands `cell_groove` into
`pattern_groove`; fresh SONG generation follows the same path. B keeps its copied
cell only as inert POD state and plays its independently transformed
`pattern_groove`.

Lane-wide performance state stays lane-wide rather than per pattern:

- phase, current step, shuffle latch;
- DENSITY, SMOOTH, RANGE, and MELODY;
- `_ev_phase`, `_ev_shape`, and `_ev_rate`;
- note age/hold and output smoothing.

This avoids discontinuities when A changes to B.

### 4.3 `SongForm`

The form controller owns only:

- the two `MelodyPattern` entries;
- form position `0..3`;
- the constant symbol sequence `{A, A, A, B}`;
- active pattern index;
- selected FORM and `last_basis`;
- pending FORM/NEW/rebuild flags;
- B's cadence slot and the A-opening value to which it was last bound.

It does not process audio, choose scales, or advance sample time.

### 4.4 Turnaround generator

A dedicated deterministic, allocation-free function derives B from A. It reads
and writes caller-owned POD buffers and draws only from the lane `Rng`. This keeps
the musical transformation independently unit-testable and keeps `ModLane` from
accumulating phrase-composition policy.

## 5. Pattern and transport lifecycle

At every melodic STEP pattern wrap, the order is:

1. apply the existing RENEW or GROW evolution to the pattern that just completed;
2. retain that mutation in its A or B snapshot;
3. apply pending FORM, NEW, or length rebuild work if present;
4. otherwise advance the form position through `{A, A, A, B}`;
5. select the incoming snapshot before its step 0 boundary fires.

Because A occupies three consecutive form positions, its one stored snapshot is
heard, mutated, and heard again. No copies named A1/A2/A3 exist.

LOOP performs no random draws and no evolution. After initial construction and
any required cadence synchronization, the entire `4 * STEPS` supercycle repeats
bit-identically.

FLOW has no SONG playback. Entering FLOW pauses the SONG form position and does
not mutate the two pattern snapshots. Returning to STEP resumes at the stored
form position. Non-melodic lanes never execute SONG behavior.

`ModLane` is a uniform type inside the five-lane `SuperModulator`, so adding a
second fixed snapshot to that type also increases the inline POD size of
non-melodic lane instances. The expected whole-instrument increase is only a few
kilobytes and avoids a pointer/lifetime split in the sample-critical lane path.
The implementation plan must record `sizeof(ModLane)` before and after and keep
the total increase below 5 KiB for both Parts combined; exceeding that bound
requires revisiting storage ownership before merge.

## 6. Deriving the turnaround

Let `n` be the effective pattern length.

For `n >= 2`, define:

```text
related_end = max(1, floor(n / 2))
turn_start  = max(related_end, floor(3 * n / 4))

related zone:   [0, related_end)
departure zone: [related_end, turn_start)
turnaround zone:[turn_start, n)
```

Examples:

| STEPS | Related beginning | Departure | Turnaround | SONG length |
|---:|---:|---:|---:|---:|
| 16 | 1–8 | 9–12 | 13–16 | 64 |
| 12 | 1–6 | 7–9 | 10–12 | 48 |
| 8 | 1–4 | 5–6 | 7–8 | 32 |
| 3 | 1 | 2 | 3 | 12 |
| 2 | 1 | empty | 2 | 8 |

At `n == 1`, the only engine slot serves as both related anchor and cadence. VCV
does not expose that case.

### 6.1 Related beginning

Start by copying A. In the first half, permit only small, seeded deviations:

- bounded pitch nudges rather than motif replacement;
- at most one local adjacent groove-rank change;
- small note-length nudges.

The exact draw thresholds are ear-tuning constants, but the normative constraint
is that this zone remains closer to A than either later zone. It need not be an
exact copy.

### 6.2 Departure

The middle quarter increases mutation width and probability:

- more pitch slots may change;
- intervals may be larger;
- groove ranks and note lengths may depart more strongly;
- A's main structural anchors need not all survive.

The normative constraint is a monotonic strength envelope:

```text
related strength < departure strength < turnaround strength
```

### 6.3 Turnaround

The final quarter is regenerated as a new local contour and a new rhythmic fill.
It must contain at least one pitch or groove difference from A when `n >= 2`.

- Its contour starts continuously from the preceding B pitch.
- Its groove is reranked within the full pattern rather than copied from the
  first half.
- The final slot is B's cadence slot and receives initial high groove priority
  directly behind the pattern-start anchor.
- Its final pitch is softly biased toward A's opening pitch.

Composition stays in normalized pitch space. Scale quantization remains
downstream in the existing pitch layer.

## 7. Soft cadence binding

A evolves three times as often as B, so A's opening pitch may move away from the
target for which B was first composed. B therefore retains one controlled
relationship to A:

- immediately before B is next heard, compare A's current slot-0 pitch with the
  A-opening value stored at the previous bind;
- if it changed, move B's cadence-slot pitch halfway toward the current A opening:

```text
B_cadence = lerp(B_cadence, A_opening, 0.5)
```

- update the remembered A-opening value;
- consume no RNG and change no other B slot.

Initial B derivation performs the same binding and stores the reference. If A did
not change, repeated B loads do nothing. LOOP therefore remains bit-stable rather
than asymptotically changing the cadence on every supercycle.

The cadence slot starts at groove rank 1. SONG groove mutation must not demote
slot 0 from rank 0; the normal variation rules may subsequently evolve the
cadence slot's rhythmic priority. Its pitch binding remains intact.

## 8. MELODY behavior inside SONG

### 8.1 LOOP

- A and B receive no mutation.
- No SONG RNG draws occur during playback.
- The `AAAB` supercycle is exact.

### 8.2 GROW

- Existing fired-slot pitch mutation and lane-wide EVOLVE walks remain.
- SONG uses pattern-groove versions of the existing outer-zone rank and
  note-length mutations.
- Only the pattern that just completed mutates.

### 8.3 RENEW

- Existing principle-aware motif-unit regeneration operates on the completed
  snapshot.
- SONG uses pattern-groove versions of the current renewal-side groove
  decisions.
- B is not re-derived from A; it remains its own evolving snapshot.
- The soft cadence binding is the only structural link retained after initial
  derivation.

At one `AAAB` pass, A therefore receives three wrap opportunities and B one.

## 9. STEPS and edge behavior

VCV continues to expose snapped STEPS in `2..16`. The portable lane remains
robust for `1..32`; extending the VCV range beyond 16 is a separate feature.

Changing the effective length queues one rebuild at the next pattern wrap:

1. use the new length;
2. generate A from `last_basis`;
3. derive B;
4. reset the form position to A;
5. clear invalid note-hold and EVOLVE state as a fresh phrase does today.

If STEPS, FORM, and NEW all change in one pattern, the wrap coalesces them into
one rebuild using the final settings. Arrays are never read beyond the effective
length, and empty departure zones at short lengths are legal.

## 10. Determinism and failure rules

- Every random draw goes through the lane `Rng`.
- Turnaround derivation has a documented, fixed draw order.
- FORM playback and cadence binding consume no random draws.
- Pending changes never switch buffers mid-pattern.
- Invalid engine FORM values are clamped to a valid named mode at the public API
  boundary; Rack's snapped parameter supplies valid values normally.
- All state is fixed-size POD; no heap, virtual dispatch, or libDaisy dependency
  enters the generator.
- The total uniform-lane storage increase is measured and must remain below the
  5 KiB whole-instrument bound defined in §5.
- The shared wrap function remains the only place where both `process()` and
  `tick()` advance SONG state.

## 11. Testing

### 11.1 Unit tests for pattern/form helpers

1. Form sequence is exactly `A, A, A, B, A...`.
2. Pattern groove is a permutation, slot 0 is rank 0, lengths remain `[1,4]`,
   and `len == n` for all `n in 1..32`.
3. Expanding a tiled groove yields deterministic unique absolute ranks.
4. B derivation is deterministic for equal A and equal RNG seed.
5. For representative lengths `2, 3, 8, 12, 16, 32`, zones match the table and
   remain in bounds.
6. B's related zone is closer to A than its turnaround zone over a fixed seed
   corpus.
7. B's final zone differs from A and its second-half groove is not forced to
   repeat the first half.
8. Cadence binding changes only B's cadence pitch, moves it exactly halfway,
   consumes no RNG, and is a no-op when A's opening did not change.

### 11.2 Lane tests

1. At LOOP, two complete SONG supercycles are bit-identical in pitch, fired-step
   set, note lengths, and form symbols.
2. With variation active, A receives three wrap mutations and B one.
3. A and B retain independent state across many supercycles.
4. GROW and RENEW operate on the outgoing snapshot before the incoming one plays.
5. NEW, FORM, and STEPS do not alter the current pattern mid-cycle.
6. Combined pending changes cause one coherent rebuild and start at A.
7. FLOW pauses and resumes the form position without mutating snapshots.
8. Equal seeds plus equal control timelines remain bit-identical.
9. `process()` and `tick()` choose the same form symbol and produce equivalent
   observable boundary behavior.
10. Existing non-SONG principles, non-melodic lanes, and normal FLOW tests remain
    green.
11. A compile-time or host-side size check reports the `ModLane` delta and keeps
    the two-Part total below 5 KiB.

### 11.3 VCV tests

1. PRIN is replaced by a snapped FORM parameter in the same compact position.
2. The six display names and order are exact.
3. Both Parts default to `SONG · AAAB`.
4. Both Parts' factory `last_basis` is `HIERARCHICAL`.
5. FORM and `last_basis` survive save/reload.
6. STEPS remains snapped to `2..16`.

### 11.4 Listening pass

Render a 16-step HIERARCHICAL phrase at LOOP and confirm:

- three recognizable A passes;
- a related B opening;
- a clearly different melodic and rhythmic final quarter;
- a convincing return from B to A;
- no click, skipped boundary, or mid-pattern replacement.

Repeat at STEPS 12, 3, and 2, then exercise moderate GROW and RENEW over several
supercycles.

## 12. Non-goals

- Forms other than `AAAB`.
- User-editable turnaround strengths or zone boundaries.
- A separate song-variation knob.
- More than two pattern identities.
- Live snapshot editing.
- Saving generated A/B note data inside Rack patches.
- VCV STEPS above 16.
- SONG behavior in FLOW or non-PITCH lanes.
- Hardware control mapping or firmware UI implementation.
