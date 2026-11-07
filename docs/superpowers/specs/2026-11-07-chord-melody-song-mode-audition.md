# PICKUP: a chord-and-melody song mode on one deck

Status: design draft after two blind listening rounds and one adversarial review (three lenses: citations, design, tests). Not yet an approved spec; the open questions at the end need Bastian's answers.

## Goal

One deck plays long chords and short melodies by turns. A chord is struck at the top of each phrase and rings; in the last quarter of the phrase a small pickup of 2–4 notes from the scale leads into the next chord, and the next chord is rooted on the pickup's last note. Same deck, same four voices, same PITCH lane. It is a SONG arrangement, chosen on the SONG ladder, not a second deck, not a reserved voice and not a new deck mode.

**Citations** are pinned to `main` at b2c11633 (this branch adds only this document). "Measured" means a probe printed it; the probe sources lived in a session scratch directory and are not kept, so the printed numbers are quoted here. Findings marked *(review probe)* were printed by the reviewers' probes on a real Part or SynthEngine.

## How the shape was chosen

Two blind rounds of WAV renders through the real SynthEngine, ChordBuilder and AmbientReverb (one engine for both roles, so the shared DEC and the voice steal were heard as on the instrument), loudness-matched to one BS.1770 level.

**Round 1** — four timings, 4 seeds each. Ranked: **R4 Pickup** = **R2 Dissolve** (the fill eats the chord in the second half), then R1 Turns (2 bars chord, 2 bars tune, a gap between), R3 Long turns (HHM) last. Bastian: "the ones with the long decay are better".

**Round 2** — five A/B pairs, seed k0 (k1 not heard):

| Pair | Pick | Consequence |
|---|---|---|
| R1 with long DEC vs R2 | R2 | chord and tune must touch; the long decay alone is not it |
| R4 vs R2 | R4 | the pickup shape wins |
| scale vs chord tones (R2) | scale | melody from the scale |
| scale vs chord tones (R4) | scale | consistent across shapes |
| ABAB vs ABBB (R2) | ABBB | mostly pattern B |

Two-deck designs (chords on A, melody on B via PULL/CHOKE) and a reserved melody voice over a held pad were dropped before round 1: Bastian wants one deck, alternating.

## The mode, precisely

On a SYNTH or WAVE deck in STEP with SONG on the new mode, per phrase of L = STEPS slots:

1. **Slot 0 strikes a chord.** Size from COLOR as today (ChordBuilder edges 0.125/0.375/0.625, chord.h:31-33). Slot 0 fires in every phrase at every DENSE, DENSE 0 included (measured: 6144 of 6144 phrases, no STEPS turns; rank 0 is pinned to slot 0 by every pattern builder and mutation, song_form.h:152-161, :287, :361, :402, and `k >= 1` at lane.cpp:768). Exception: a live STEPS turn re-slots the lane without a boundary fire (lane.cpp:256-296) and can skip one chord; the mode recovers at the next wrap *(review probe: 16→8 inside slot 8 landed on slot 0 with no fire)*. Accepted, not engineered around.
2. **Slots 1 .. W−1 are silent**, W = `song_zones(L).turn_start` = 3L/4 (song_form.h:64-73) — the arrangement's existing turnaround boundary, no new constant.
3. **Slots W .. L−1 play single notes** wherever the lane fires (normal groove ranks at DENSE), at any COLOR. Each takes one voice by SynthEngine's normal rule: a free voice round-robin first, otherwise the oldest non-sustaining voice (synth_engine.cpp:188-210). **With a 4-note chord** every voice is busy, so the first pickup steals the chord's root and the chord thins as the pickup plays — the sound that won (the rig always struck 4). **With a 1–3-note chord** the previous phrase's last pickup voice is older than the chord, so the first pickup takes it and the chord thins one note later *(review probe: n=4 → first pickup took the chord root; n=3 → it took the old pickup voice)*. This happens every phrase at COLOR < 0.375 and in some phrases at COLOR 0.7 (MOTION's COLOR swing). Accepted for now; see open question 6.
4. **The next chord's root** is computed at the slot-0 strike from integer semitones:
   - **candidate**: the last *struck* pickup note − 12, if a pickup was struck since the previous chord; otherwise the previous root. "Struck" means `_fire_trigger` ran: a fire swallowed by CHOKE, a window the DENSE left empty, or a STEPS re-slot all count as "no pickup";
   - **snap**: `ChordBuilder::nearest(candidate, mask, root, floor)` with mask = `Part::_chord_mask()` (part.h:857-859; CHROM_MASK in CHROM), root = `_quant.root_semis()` (masks are root-relative, chord.h:160-164, quantizer.h:133,143-145). The snap matters on a bound PULL follower, whose pickups are quantized to the leader's absolute mask;
   - **anti-repeat**: a root equal to either of the last two roots moves a scale fourth up in that mask (a fifth where the fourth would be a tritone; +5 semitones in CHROM, where "degree + 3" would be a minor third), then single degrees up until it differs from both.
5. **Register:** let c be the semitone of the pitch path's centre (lane value 0: base 0.5 + TUNE + song offset, part.cpp:200-205). The chord root is folded into [c − 12, c); a pickup below c is lifted an octave, so the tune sits above the chord. The relation of c − 12 to the note floor (−12 semis, quantizer.h:73, `_pitch_floor_semis()`) is unmeasured: for roots −12 … −8 ChordBuilder rejects the fifth-below lay and flips it above the root (chord.h:95, 201), which changes the voicing shape inside the octave. A probe at implementation decides whether c needs a floor clamp.
6. **Material:** the mode's A/B table is ABBB (`song_symbol_at(ABBB, i)`), phrase 0 = A (RESET contract, lane.h:75-81).
7. **One DEC** rings both chord and pickup. No per-role decay.

## What the probes measured

**The pickup window in the real ModLane** (16 seeds × 8 phrases per row, SONG ABBB):

| STEPS | Window | DENSE for 2–4 notes | Notes |
|---|---|---|---|
| 16 | 4 slots | 0.6–1.0: 100 % of phrases | 0.6/0.7 → 2–3 (median 3), 0.8 → 2–4, 1.0 → always 4; 0.5 → 1–3; 0.4 → 0–3 (3 % empty) |
| 8 | 2 slots | only 1.0 guarantees 2 | 36–81 % of phrases have 1 note for DENSE 0.4–0.9 |
| 32 | 8 slots | 0.4 → 3–4 | 0.7 and up always 5–8; STEPS 32 is not reachable from the panel (param_table.h:58,77) |

VARY 0 and 0.5 gave identical STEPS-16 rows. The count is set by k = lround(DENSE·L) (lane.cpp:767) and the groove ranks, so it barely varies between seeds.

**The progression** (real pitch path: `reflect_into(base 0.5 + lane·RANGE + TUNE + offset)` and a real Quantizer, part.cpp:132-143 / 200-205; DORIAN, DENSE 0.8, 16 seeds × 16 phrases):
- **Without anti-repeat it stalls.** AAAB and ABBB repeat the previous root about half the time (48–53 %); ABAB ping-pongs (root equals the one two back 71–72 %). About 4 distinct roots per 16 chords. The last pickup keeps landing on the pattern's cadence note.
- **With anti-repeat** no root repeats the last two (by construction), 5.6–6.1 distinct roots per 16 chords in DORIAN, 4.7–4.9 in MIN_PENT (capped at one per degree). But the rule, not the tune, picks the root on 45–54 % of chords, and AAAB settles into period-4 cycles. Honest summary: the tune proposes, the rule disposes. The round-2 verdict was formed with this behaviour.
- **At the factory RANGE 0.0 the mode degenerates.** Every note quantizes to semitone 17; the tune is one repeated pitch; anti-repeat produces the same F–B♭–C loop on every seed (gen_panel.py:829 RANGE_A 0.0). The renders correspond to RANGE 0.5 (the rig's pitch law equals the real path at RANGE 0.5: identical statistics for ABAB, within 1–2 counts otherwise).

**Register.** Under the real path, 20 % (RANGE 0.5) to 29 % (RANGE 1.0) of pickup notes land below the chord root if the root is taken unfolded at last − 12; at RANGE 1.0 pickups reach the note floor (−12) and −7. The rig's fold and lift have no counterpart in Part, so rule 5 is new behaviour, not existing.

**The struck pitch can be mid-glide** *(review probe)*. On every PULL bind/unbind flip, leader-mask change and ROOT/scale turn the Quantizer arms its change slew (quantizer.h:108-129, 172-178) and lerps on the first call (quantizer.h:158-162). The PULL draw lands just before the fire's control tick (part.h:388-389, then 417-422), so the note is struck after 1 of 20 slew ticks — about 95 % of the previous note — and STEP voices latch it (synth_engine.cpp:257). Storing `_tg[LANE_PITCH]` as the pickup would therefore put the next chord root off the semitone grid (slot 0 passes root_norm through exactly, chord.h:150). Hence rule 4 works on the Quantizer's integer target note. The PULL-follower mid-glide strike itself exists on `main` today and is out of scope here.

**One deck can alternate chord size per fire** (real Part, SYNTH STEP): a COLOR-0.7 fire sounds 4 voices; a following COLOR-0 fire restrikes exactly one voice (the oldest, when all four are busy) while the other three ring on with envelopes identical to the chord-alone reference (±0.1 dB at chord tones). STEP voices do not retune when the pitch target moves while they ring (0.0 dB difference; only sustaining FLOW voices retune, synth_engine.cpp:384). So the next chord's root can be set from Part state without bending the last pickup. **No SynthEngine change is needed.**

**Sizes** (desktop clang and ARM GCC agree): ModLane 896 B in production, 920 B under SPKY_TESTING; the storage gate `(sizeof(ModLane) - 412) * 10u < 5u*1024u` (test_song_form.cpp:310-319) passes up to 923 B. One extra 4-byte member after `_cur_step` (lane.h:429) still fits by padding; two fail (928 B). Appending a SongMode value changes no size. **This design adds nothing to ModLane.**

**Appending a SongMode value alone makes it an alias of MIRROR** (bitwise identical Instrument output on SYNTH, WAVE, BODY and FEED, STEP and FLOW): without its own case `song_symbol_at` falls into Mirror's parity branch (song_form.h:99-104). And `clamp_song(99)` would return the new value instead of Off (breaks test_song_form.cpp:87 and test_instrument.cpp:960-970).

**FLOW does not pause the arrangement on note decks.** Only SAMPLER and BBD stop (lane.cpp:910-912, set by `set_flow_melody` at part.cpp:53 and :586); SYNTH/WAVE/BODY/FEED keep advancing A/B through flow-melody (lane.cpp:913-921). A STEP-only mode needs its own `_step_on` check.

**Where to silence: Part, not lane.** Gating slots 1..W−1 inside `ModLane::_effective_gate` (lane.cpp:773-781) works as a pure function of slot, but it lengthens slot 0's `_note_hold`, freezes the lane output on the chord pitch through the silence, changes SuperModulator's onset-gap RhythmView (super_modulator.cpp:169-188), and at VARY > 0 diverges the GROW random stream (up to 4.7 points at STEPS 8/32, none at 16). The audition rig silenced downstream, which is what Part-level suppression reproduces. Part-level suppression has side effects of its own, listed under "Engine design".

## Engine design

**Where it lives: Part.** ModLane is untouched apart from the SongMode case.

- `song_form.h`: append `SongMode::Pickup` (=7) before `kCount`; give it its own case in `song_symbol_at` returning the ABBB symbols. Make `clamp_song` (song_form.h:19-24) map out-of-range values to Off explicitly.
- `Part::process` (part.h:376-379): on a PITCH fire, `_note_suppressed = _inhibit || _pickup_silent(slot)`, and `_gate_ctr` is set only when not suppressed. This reuses CHOKE's swallow path: no trigger, no gate pulse, no gate sustain (gate() already honours `_note_suppressed`, part.h:303). The PULL draw still runs, so its stream does not depend on the mode (same rule as CHOKE, part.h:385-387). `_pickup_silent` is true when the mode is active, `0 < slot < song_zones(L).turn_start`. Slot from `_mod.pitch_cur_step()`, L from `_mod.pitch_steps()` (super_modulator.h:137-138); there is no pitch `wrapped()` accessor, so slot 0 is the phrase start (see rule 1 for the STEPS-turn exception).
- `Part::_fire_trigger` (part.cpp:636-654), when the mode is active:
  - **slot 0:** build the chord on the root from rule 4 instead of `_tg[LANE_PITCH]` at the `_chord.build` call (part.cpp:646), folded per rule 5. Then **latch the PULL leader mask**: `_chord_pc = pc_mask12(built chord)`. Today `_control_tick` recomputes it every 96-sample tick from the lane pitch (part.cpp:493-498), and the fire's refresh tick runs before `_fire_trigger` (part.h:417-426); in this mode the lane keeps moving through the silent slots, so a follower deck (instrument.cpp:350) would bind to pitch classes this deck never plays *(review probe: the mask changed through slots 1..11 while they would be silent)*. So `_control_tick` must not overwrite `_chord_pc` while the mode is active. Pickup pitch classes are not added: the follower holds the chord.
  - **slot ≥ W:** strike one voice, `trigger_chord(&pickup, 1)`, **without** calling `_chord.build`. build() writes ChordBuilder's voice-leading memory (`_prev`, `_prev_n`, chord.h:130-133) unconditionally; a full-size build around each pickup would make the next chord voice-lead against a phantom chord. Count 1 takes the COLOR-0 path (synth_engine.cpp:171-172, no `_stab_rng` draw). Pitch lifted per rule 5. Store the Quantizer's integer target note (a new accessor for its `_last_note`, which already carries TUNE and offset) as the last struck pickup and set `pickup_seen`.
  - Ordering is safe: the last pickup fires strictly before the next wrap, and the wrap sample carries slot 0's fire with the pattern already advanced (measured over 6144 phrases; lane.cpp:994-1001).
- **New Part state:** last struck pickup (int semitone), `pickup_seen` (bool, cleared at every slot-0 strike), the last two roots (two ints). Part has no size gate.
- **Reset:** Part has no `restart_song` today; `Instrument::restart` calls `p.mod().restart_song()` on the SuperModulator directly (instrument.h:603-611, super_modulator.h:79), and both hosts reach RESET only through it (Fireflow.cpp:789, shell/panel_scan.cpp:1169). Add `Part::restart_song()`, which clears the mode state and forwards to `_mod.restart_song()`, and route instrument.h:609 through it. `Part::init` clears the same state (VCV re-inits on a sample-rate change). After a reset the first chord is built from `_tg[LANE_PITCH]` as on a fresh init.
- **PITCH CV:** `Part::pitch_cv()` returns the lane pitch `_pitch_q` (part.h:209, 305), so the PITCH jack would carry the lane melody through the silent window and the unlifted pickup, while GATE follows the suppression (Fireflow.cpp:811-814). In this mode `pitch_cv()` reports the last struck pitch (the chord root at slot 0, the lifted pickup after) and holds it through the silent window.
- **Accepted side effects of Part-level suppression:** the VCV lane ring still brightens on silent fires (`lane_fired`, Fireflow.cpp:1237), and the sibling deck's FLUX THIN still reads this deck's RhythmView, which counts the silent onsets (super_modulator.cpp:169-188, instrument.cpp:310-311).
- **Active only when** the deck is SYNTH or WAVE, in STEP. On FLOW, BODY (one voice: trigger_chord clamps to kVoices, body_voice.h:27 with synth_engine.cpp:171), BBD (trigger is a no-op, bbd_engine.h:36), SAMPLER (chords flatten, part.h:620-624) and FEED, the mode is just the ABBB table.
- **Knob meaning on this rung:** COLOR = chord size, DENSE = pickup density (useful range about 0.6–1.0 at STEPS 16), DEC = ring of both, RANGE = melodic width (0 degenerates, see question 1), STEPS = phrase length (window = the last quarter).

## Surface

**Must change** (each goes red or reads out of bounds otherwise):
- `param_table.h:76` → `X(P_SONG_A, 0.f, 7.f, 8) X(P_SONG_B, 0.f, 7.f, 8)` (the step column too; nothing checks it today).
- One appended ladder rung `{form 2, song 7}` (song_ladder.h:27-37, 14 → 15; never inserted; rung 0 stays Off, test_song_ladder.cpp:16-18). Form 2 (Hierarchical) is what the renders used.
- `kSongWords` gets an 8th word (Fireflow.cpp:489-491; :496 reads out of bounds without it) **in the same commit as the enum**, and its pin in test_panel.py:1083-1091.
- The 13s that break with 15 rungs: test_control_params.cpp:62 and test_controls_map.cpp:59 → 14.f.
- `tests/test_param_impact.cpp`: `moves_audio()` compares each parameter at lo against hi, so SONG moves from "AAAB vs Off" to "AAAB vs Pickup". That flips two UNTRACED pins — SONG_B "dead on both modes" (:305) and SONG_A "alive only in STEP" (:371) — and fails 4 cases *(review probe build)*. The file forbids deleting an UNTRACED entry to go green. Decision: keep the old AAAB-vs-Off measurement as its own small case so the open SONG_B thread stays pinned, and let the lo/hi sweep re-pin with a dated note.

**Docs and comments** (nothing goes red today, so extend the guard):
- test_panel.py:1118-1121 gets the new word, which makes the host/vcv/README.md entry mandatory.
- host/vcv/README.md:58 ("14-rung"), :62 ("seven arrangements"), :65-73 (per-mode list); README.md:63 and :203 (the mode list); docs/roadmap.md:1503-1511 (SONG table).
- Stale counts in comments: param_table.h:47-48 and :51-57, control/control_law.h:836, Fireflow.cpp:481, gen_panel.py.

**Optional:** test_song_rung_state.cpp:22-68 uses its own local `n = 14` and stays green (it could read `kSongLadderCount`); the comment at test_song_ladder.cpp:33-35.

**Side effects, all fine:** the persist layout hash changes (persist_record.cpp:90 folds in `kSongLadderCount`, test_persist_record.cpp:139-149), so the first boot of a new Rev A image resets Bastian's persisted patch — tell him before a flash. The rung inherits `new_phrase` and `sampler_punch` on change (control_law.h:848-860). The SONG lamp keeps showing A/B changes (led_law.hpp:377-393).

## Tests

Each RED proved once; the sabotage for the RED is named.

- **Enum and clamp** (test_song_form.cpp): `clamp_song(7) == Pickup` (RED on main: 7 clamps to Off), `clamp_song(8) == Off`, `song_text(Pickup, 8) == "ABBBABBB"` (RED while Pickup falls into Mirror's branch, which gives ABBABAAB). Order: append the enum, see :87 go red, then fix the clamp. The existing :87 and test_instrument.cpp:960-970 stay.
- **Range pinned to the enum** (test_param_table.cpp): `kParams[P_SONG_A].hi == kCount − 1` and steps == kCount, same for B.
- **Ladder reach** (test_song_ladder.cpp): some rung carries `SongMode::Pickup` (structural, so it respects the "order is taste" comment, song_ladder.h:22-24).
- **Silent window, non-vacuous:** SYNTH STEP, STEPS 16, DENSE 1.0, several phrases, a mixed seed. First require `lane_fired(LANE_PITCH)` on at least N slots in 1..W−1, then assert no trigger and no rising gate edge at those fires (not "gate low across 1..W−1": slot 0's sustain may carry into slot 1). The "outside" half runs the same seed with SONG = ABBB and asserts the same slots trigger. RED: drop `_pickup_silent`.
- **One voice per pickup:** COLOR 0.7, every fire at slot ≥ W strikes exactly one voice (`VoiceCountProbe` trig_count idiom, synth_engine.h:281-296, or `probe_synth().probe_voice()`), and the next chord's strew equals a run with the pickup slots forced to COLOR 0. RED: pass the COLOR count instead of 1.
- **No phantom voice-leading:** the slot-0 chord after a pickup phrase equals one built with ChordBuilder's `_prev` set to the previous slot-0 chord. RED: call `_chord.build` on pickup fires.
- **Root = last pickup − 12:** RANGE ≥ 0.5, mixed seed, a case where last − 12 differs from both previous roots (asserted in the test). Observe the root through the engine's struck voice pitch, not through the new Part state.
- **On the grid under PULL:** a bound follower whose pickup is struck mid-glide still builds the next chord on an integer semitone. RED: store `_tg[LANE_PITCH]`.
- **Anti-repeat:** no root equals either of the last two, in DORIAN, MIN_PENT and CHROM (+5 there), RANGE ≥ 0.5.
- **No pickup:** DENSE 0 (slot 0 still fires, the window is empty) still moves the root; and a run where CHOKE swallows the whole window behaves the same (only struck notes count). RED: keep `pickup_seen` set across phrases.
- **Register:** RANGE 1.0, a case where a raw pickup lands below c; every sounded pickup is ≥ c and every root is in [c − 12, c). RED: disable the lift.
- **Leader mask:** a follower's mask through slots 1..W−1 equals the pitch classes of the slot-0 chord. RED: let `_control_tick` overwrite `_chord_pc`.
- **Reset:** after RESET (and after `Part::init`) the first slot-0 chord is built from `_tg[LANE_PITCH]` as on a fresh init. RED: skip the clear.
- **PITCH CV:** `pitch_cv()` holds the last struck pitch through the silent window.
- **STEPS turn:** turning 16 → 8 mid-phrase skips at most one chord, and the next wrap strikes one.
- **Inert elsewhere:** bitwise-identical Instrument output for SONG = Pickup vs SONG = ABBB on FLOW-SYNTH, FLOW-WAVE, BODY, BBD, SAMPLER and FEED. RED: drop the `_step_on` / engine check.
- **Outside the mode:** `ctrl_identity` and `wave_formant_sweep` stay unchanged. They only guard "nothing moved outside the mode": neither sets SONG nor runs a STEP deck (ctrl_identity is two SYNTH decks in FLOW; wave_formant_sweep has `set_step` false), so they cannot catch a pickup that draws `_stab_rng`; the one-voice test above does that. Optional: a render scenario with `set_song 7` on a STEP SYNTH deck (scenario.cpp:125 supports it).
- `ctest --test-dir build -E "^reva_"` after a Release build.

## Plan of work

1. **Enum** (one commit): song_form.h enum, clamp, symbol case, `kSongWords` and its test_panel pin (same commit, or the label loop reads out of bounds); the clamp and range tests.
2. **Surface** (one commit): ladder rung, P_SONG row, test_control_params / test_controls_map 13 → 14, test_param_impact split and re-pin, ladder-reach test.
3. **Part logic and its tests** — after rec-snap is merged (it edits `part.*`).
4. **Docs:** README.md, host/vcv/README.md (+ test_panel.py guard), roadmap.md, the stale comments.
5. **Finish:** the worktree has no env.sh, so source the main checkout's (`source <main>/env.sh`), then `cmake -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build` (ctest does not build), `ctest --test-dir build -E "^reva_"`; `host/vcv/build-local.sh install` and a Rack restart; if released, bump plugin.json (MAJOR stays 2) and rewrite docs/release-notes.md. Before flashing Rev A, warn about the persisted-patch reset.

Building in this worktree also needs `git submodule update --init lib/DaisySP` first (the CMake configure fails on the empty submodule).

## Round 3: the winner through the real Instrument

No engine change is needed for it. A host-side closed-loop driver around the real Instrument (it reads `lane_fired` and the PITCH `cur_step` after each `process()` and switches DENSE, COLOR and the PITCH base/depth at steps 0, W−1 and L−1) struck every requested root exactly and fired nothing in the silent window (8 phrases per deck, step length exact). It needs `-DSPKY_TESTING` for `Instrument::part()`, which renders byte-identical audio. The render host builds from this worktree in about 2 s and renders 40 s in 0.55 s.

Before round 3 is played to Bastian, the driver needs: the register rule (rule 5); a DENSE that is not switched mid-phrase (its first renders fired all 4 window slots in every phrase, against median 3 for an unswitched lane — likely the mid-phrase DENSE change); a seed other than k0 (the Instrument seeds deck A with 0x1234abcd = k0 and deck B with 0x9e3779b9, instrument.cpp:157,161); MOTION's COLOR swing left on, so 3-note chords and the later thinning of rule 3 are heard as they would ship; and no `_chord.build` on pickup fires.

## Open questions

1. **RANGE 0 (the factory value):** accept that the mode degenerates there (one pickup pitch, a fixed three-chord loop), or should the rung give the melody its own width?
2. **The register rule** (fold the chord into the bass octave, lift low pickups): confirm by ear in round 3, or leave the real pitch path alone?
3. **STEPS 8 gives 1–2-note pickups.** Keep "the last quarter" (the pickup scales with STEPS), or a fixed window such as the last min(L/4, 4) slots?
4. **Anti-repeat overrides the tune on about half the chords.** Fine as heard, or try a softer rule (only the immediately previous root) in round 3?
5. **Name and word on the panel:** "PICKUP" / "PICK"?
6. **Chords of 1–3 notes thin one pickup later** (rule 3). Accept, or silence the previous pickup's voice at the slot-0 strike so pickups always eat chord voices oldest-first? The second cuts the last pickup's tail at the chord.

## Appendix: audition record

**Rigs.** Round 1 and 2 used a scratch harness: one real ModLane (melodic, STEP, Part-style seeds `0x1234abcd + (k+2)*2654435761`), one SynthEngine, ChordBuilder (COLOR 0.7), AmbientReverb on a 0.4 send; 80 BPM, A Dorian, 16 bars of 2-bar phrases plus a 12 s tail with a 2 s fade; VARY 0.5; files matched to one BS.1770 loudness under a −1 dBFS peak cap; letters/x-y assigned at random. Pitch law `36·reflect(0.5 + 0.5·v)` snapped to the scale (equal to the real path at RANGE 0.5, without quantizer hysteresis or slew); no FLUX, GRIT or master bus. The rig struck 4-note chords every phrase and had no MOTION swing.

**Lessons from the rig itself** (each found by an independent check and fixed before listening):
- A 4-bar phrase cannot hold a chord at any DEC (DEC 1.0 is under env 0.01 after about 5.5 s), so R2/R4 first rendered as gap candidates; all candidates moved to 2-bar phrases.
- Under AAAB with 4-bar phrases, R2/R4 played one root for most of the file; R3 never played pattern B. Fixed with A/B-alternating tables, VARY 0.5 and anti-repeat.
- Plain RMS matching left 1.65 dB of perceived loudness difference; BS.1770 matching brought it to 0.1 dB.
- Round 1's "fourth up three times" closes a C↔G cycle in A Dorian; round 2 uses one fourth, then single degrees.

**Engine finding, unrelated to this mode:** an attack zipper on the 96-sample control grid — steps at `kCtrlInterval` boundaries in the first ~9–12 ms after strikes (531 strikes; median ratio 15.4 of tick-boundary to off-boundary second difference; stronger where a strike lands on still-ringing voices). It applies to the instrument as well. Mechanism unmeasured (candidates: the voice's per-tick `_vel` glide, per-tick envelope/filter pushes); probe before filing.

**Unmeasured:** the cause of COLOR 0.7 striking 3 or 4 notes from phrase to phrase in the real deck (MOTION's COLOR swing, part.cpp:434-438, or ChordBuilder at the register edges); FEED in this mode; the fold octave against the note floor (rule 5); the Rev A SONG pot hover at the new rung's seam (`knob_from_pot`'s `std::round`, shell/controls.cpp:49-55).
