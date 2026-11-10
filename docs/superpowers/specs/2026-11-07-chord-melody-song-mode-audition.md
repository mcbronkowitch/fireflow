# DISSOLVE and TURNS: chord-and-melody song modes on one deck

Status: design draft after three blind listening rounds and one adversarial review (three lenses: citations, design, tests). Bastian asked for 2–3 such modes on the SONG knob (2026-11-10); after round 3 he chose **DISSOLVE and TURNS** (2026-11-10). PICKUP, which won round 2, was picked in neither round-3 set and is dropped. Not yet an approved spec; the open questions at the end need his answers.

## Goal

One deck plays long chords and short melodies by turns. Same deck, same four voices, same PITCH lane. Two shapes, each a SONG arrangement on its own rung at the end of the SONG ladder — not a second deck, not a reserved voice and not a new deck mode:

- **DISSOLVE** — a chord at the top of each phrase rings; from the half of the phrase single scale notes play over it and eat it, and the next chord is rooted on the last of them.
- **TURNS** — the roles alternate per phrase: a chord phrase (one chord, then silence while it rings), then a melody phrase (single scale notes only), whose last note roots the next chord.

**Citations** are pinned to `main` at b2c11633 (this branch adds only this document). "Measured" means a probe printed it; the probe sources lived in a session scratch directory and are not kept (round 3's rig is copied to `C:\tmp\fireflow-r3\rig`), so the printed numbers are quoted here. Findings marked *(review probe)* were printed by the reviewers' probes on a real Part or SynthEngine.

## How the shapes were chosen

Three blind rounds of WAV renders, loudness-matched to one BS.1770 level. Rounds 1 and 2 ran a scratch rig on the real SynthEngine, ChordBuilder and AmbientReverb (one engine for both roles, so the shared DEC and the voice steal were heard as on the instrument); round 3 ran the real Instrument with the modes prototyped inside Part.

**Round 1** — four timings, 4 seeds each. Ranked: **R4 Pickup** (chord, silence, a short pickup in the last quarter) = **R2 Dissolve** (the melody eats the chord in the second half), then R1 Turns (a chord phrase, then a melody phrase), R3 Long turns (HHM) last. Bastian: "the ones with the long decay are better".

**Round 2** — five A/B pairs, seed k0 (k1 not heard):

| Pair | Pick | Consequence |
|---|---|---|
| R1 with long DEC vs R2 | R2 | chord and tune must touch; the long decay alone is not it |
| R4 vs R2 | R4 | the pickup shape wins |
| scale vs chord tones (R2) | scale | melody from the scale |
| scale vs chord tones (R4) | scale | consistent across shapes |
| ABAB vs ABBB (R2) | ABBB | mostly pattern B |

**Round 3** — Pickup, Dissolve and Turns through the real Instrument, two new seeds, ranked per seed (details and measurements in "Round 3" below). Set 1 → Turns, set 2 → Dissolve, set 2's Turns "also ok", "only because there felt to be no foreign tones". Pickup was picked in neither set. The liked renders are the ones with the least semitone/tritone rubbing; the melody rubs least against the ringing chord in Turns.

Two-deck designs (chords on A, melody on B via PULL/CHOKE) and a reserved melody voice over a held pad were dropped before round 1: Bastian wants one deck, alternating. R3 Long came last in round 1 and chord-tone melodies lost twice in round 2; both use the scale for the melody and the ABBB table, the two material choices round 2 settled.

## The modes, precisely

The two modes share everything except **which fires are silent and which strike a chord**:

| Mode | Chord fire | Silent fires | Single-note fires | Window source |
|---|---|---|---|---|
| DISSOLVE | slot 0 of every phrase | slots 1 .. H−1, H = `song_zones(L).related_end` = L/2 | slots ≥ H | existing constant (song_form.h:64-73) |
| TURNS | slot 0 of each chord phrase | slots ≥ 1 of each chord phrase | every fire of each melody phrase, slot 0 included | phrase parity: the 1st, 3rd, … phrase after a reset is a chord phrase |

TURNS counts phrases by slot-0 fires in Part, reset with the rest of the mode state (see "Reset"). Its chord phrases need DEC near the top to reach into the melody phrase (round 1: a 2-bar chord at DEC 1.0 is under env 0.01 after about 5.5 s).

The rules below use the DISSOLVE wording; for TURNS read "the chord phrase" for "slots 1 .. H−1" and "the melody phrase" for "slots ≥ H". On a SYNTH or WAVE deck in STEP with SONG on one of the modes, per phrase of L = STEPS slots:

1. **Slot 0 strikes a chord.** Size from COLOR as today (ChordBuilder edges 0.125/0.375/0.625, chord.h:31-33). Slot 0 fires in every phrase at every DENSE, DENSE 0 included (measured: 6144 of 6144 phrases, no STEPS turns; rank 0 is pinned to slot 0 by every pattern builder and mutation, song_form.h:152-161, :287, :361, :402, and `k >= 1` at lane.cpp:768). Exception: a live STEPS turn re-slots the lane without a boundary fire (lane.cpp:256-296) and can skip one chord; the mode recovers at the next wrap *(review probe: 16→8 inside slot 8 landed on slot 0 with no fire)*. Accepted, not engineered around.
2. **Slots 1 .. H−1 are silent**, H = `song_zones(L).related_end` = L/2 (song_form.h:64-73) — an existing boundary of the arrangement, no new constant.
3. **Slots H .. L−1 play single notes** wherever the lane fires (normal groove ranks at DENSE), at any COLOR. Each takes one voice by SynthEngine's normal rule: a free voice round-robin first, otherwise the oldest non-sustaining voice (synth_engine.cpp:188-210). **With a 4-note chord** every voice is busy, so the first melody note steals the chord's root and the chord thins as the melody plays. **With a 1–3-note chord** the previous phrase's last melody voice is older than the chord, so the first melody note takes it and the chord thins one note later *(review probe: n=4 → the first note took the chord root; n=3 → it took the old melody voice)*. This happens every phrase at COLOR < 0.375 and in some phrases at COLOR 0.7 (MOTION's COLOR swing). Measured in round 3: by the next slot 0, DISSOLVE keeps 0–1 of the phrase's own chord tones above env 0.05; TURNS keeps 1.75–1.88. Accepted for now; see open question 6.
4. **The next chord's root** is computed at the chord strike from integer semitones:
   - **candidate**: the last *struck* single note − 12, if one was struck since the previous chord; otherwise the previous root. "Struck" means `_fire_trigger` ran: a fire swallowed by CHOKE, a window the DENSE left empty, or a STEPS re-slot all count as "no melody";
   - **snap**: `ChordBuilder::nearest(candidate, mask, root, floor)` with mask = `Part::_chord_mask()` (part.h:857-859; CHROM_MASK in CHROM), root = `_quant.root_semis()` (masks are root-relative, chord.h:160-164, quantizer.h:133,143-145). The snap matters on a bound PULL follower, whose notes are quantized to the leader's absolute mask;
   - **anti-repeat**: a root equal to either of the last two roots moves a scale fourth up in that mask (a fifth where the fourth would be a tritone; +5 semitones in CHROM, where "degree + 3" would be a minor third), then single degrees up until it differs from both.
5. **Register:** let b = the contract's 0 plus TUNE and the song offset, in semitones (`lround((_tune_norm() + _offset_norm()) · 36)`, part.h:836-856; b = 0 at TUNE noon and key C). The chord root is folded into [b, b + 12); a single note below b + 12 is lifted by octaves until it is not, so the tune sits above the chord. This is the rig's register (chords in the bottom octave of the 36-semitone span, the tune above) and what the round-3 prototype does; the lane centre is b + 18. At TUNE noon the chord's fifth-below lay reaches −7 at most, above the note floor (−12, quantizer.h:73). Turning TUNE down far enough (b below −4) makes ChordBuilder reject the fifth-below lay and flip it above the root (chord.h:95, 201), changing the voicing shape inside the octave; a probe at implementation decides whether b needs a floor clamp.
6. **Material:** the modes' A/B table is ABBB (`song_symbol_at(ABBB, i)`), phrase 0 = A (RESET contract, lane.h:75-81).
7. **One DEC** rings both chord and melody. No per-role decay.

## What the probes measured

**The melody window in the real ModLane.** Measured for PICKUP's last-quarter window (16 seeds × 8 phrases, SONG ABBB): at STEPS 16 the 4 slots held 2–4 notes in 100 % of phrases for DENSE 0.6–1.0; at STEPS 8 the 2 slots held 1 note in 36–81 % of phrases for DENSE 0.4–0.9. The count is set by k = lround(DENSE·L) (lane.cpp:767) and the groove ranks, so it barely varies between seeds; VARY 0 and 0.5 gave identical rows. For DISSOLVE's 8-slot window only the round-3 renders exist: 40 notes over 8 phrases at DENSE 0.6 on both seeds (5 per phrase). TURNS' melody phrase gave 7 notes per phrase at DENSE 0.45.

**The progression** (real pitch path: `reflect_into(base 0.5 + lane·RANGE + TUNE + offset)` and a real Quantizer, part.cpp:132-143 / 200-205; DORIAN, DENSE 0.8, 16 seeds × 16 phrases, PICKUP window):
- **Without anti-repeat it stalls.** AAAB and ABBB repeat the previous root about half the time (48–53 %); ABAB ping-pongs (root equals the one two back 71–72 %). About 4 distinct roots per 16 chords. The last note keeps landing on the pattern's cadence note.
- **With anti-repeat** no root repeats the last two (by construction), 5.6–6.1 distinct roots per 16 chords in DORIAN, 4.7–4.9 in MIN_PENT (capped at one per degree). But the rule, not the tune, picks the root on 45–54 % of chords, and AAAB settles into period-4 cycles. Round 3 showed the same for DISSOLVE (3–5 of 9 roots moved, four-chord loops). Honest summary: the tune proposes, the rule disposes.
- **At the factory RANGE 0.0 the modes degenerate.** Every note quantizes to semitone 17; the tune is one repeated pitch; anti-repeat produces the same F–B♭–C loop on every seed (gen_panel.py:829 RANGE_A 0.0). The renders correspond to RANGE 0.5.

**Register.** Under the real path, 20 % (RANGE 0.5) to 29 % (RANGE 1.0) of melody notes land below the chord root if the root is taken unfolded at last − 12; at RANGE 1.0 notes reach the note floor (−12) and −7. The rig's fold and lift have no counterpart in Part, so rule 5 is new behaviour, not existing.

**The struck pitch can be mid-glide** *(review probe)*. On every PULL bind/unbind flip, leader-mask change and ROOT/scale turn the Quantizer arms its change slew (quantizer.h:108-129, 172-178) and lerps on the first call (quantizer.h:158-162). The PULL draw lands just before the fire's control tick (part.h:388-389, then 417-422), so the note is struck after 1 of 20 slew ticks — about 95 % of the previous note — and STEP voices latch it (synth_engine.cpp:257). Storing `_tg[LANE_PITCH]` as the last note would therefore put the next chord root off the semitone grid (slot 0 passes root_norm through exactly, chord.h:150). Hence rule 4 works on the Quantizer's integer target note. The PULL-follower mid-glide strike itself exists on `main` today and is out of scope here.

**One deck can alternate chord size per fire** (real Part, SYNTH STEP): a COLOR-0.7 fire sounds 4 voices; a following COLOR-0 fire restrikes exactly one voice (the oldest, when all four are busy) while the other three ring on with envelopes identical to the chord-alone reference (±0.1 dB at chord tones). STEP voices do not retune when the pitch target moves while they ring (0.0 dB difference; only sustaining FLOW voices retune, synth_engine.cpp:384). So the next chord's root can be set from Part state without bending the last note. **No SynthEngine change is needed.**

**Sizes** (desktop clang and ARM GCC agree): ModLane 896 B in production, 920 B under SPKY_TESTING; the storage gate `(sizeof(ModLane) - 412) * 10u < 5u*1024u` (test_song_form.cpp:310-319) passes up to 923 B. One extra 4-byte member after `_cur_step` (lane.h:429) still fits by padding; two fail (928 B). Appending SongMode values changes no size. **This design adds nothing to ModLane.**

**Appending a SongMode value alone makes it an alias of MIRROR** (bitwise identical Instrument output on SYNTH, WAVE, BODY and FEED, STEP and FLOW): without its own case `song_symbol_at` falls into Mirror's parity branch (song_form.h:99-104). And `clamp_song(99)` would return the new value instead of Off (breaks test_song_form.cpp:87 and test_instrument.cpp:960-970).

**FLOW does not pause the arrangement on note decks.** Only SAMPLER and BBD stop (lane.cpp:910-912, set by `set_flow_melody` at part.cpp:53 and :586); SYNTH/WAVE/BODY/FEED keep advancing A/B through flow-melody (lane.cpp:913-921). A STEP-only mode needs its own `_step_on` check.

**Where to silence: Part, not lane.** Gating the silent slots inside `ModLane::_effective_gate` (lane.cpp:773-781) works as a pure function of slot, but it lengthens slot 0's `_note_hold`, freezes the lane output on the chord pitch through the silence, changes SuperModulator's onset-gap RhythmView (super_modulator.cpp:169-188), and at VARY > 0 diverges the GROW random stream (up to 4.7 points at STEPS 8/32, none at 16). The rigs and the round-3 prototype silenced in Part. Part-level suppression has side effects of its own, listed under "Engine design".

## Engine design

**Where it lives: Part.** ModLane is untouched apart from the SongMode cases. The round-3 prototype (`C:\tmp\fireflow-r3\rig\prototype.diff`, about 130 lines) is a working sketch of the core of this section; what it leaves out is listed under "Round 3".

- `song_form.h`: append `SongMode::Dissolve` (=7) and `Turns` (=8) before `kCount`; give them their own case in `song_symbol_at` returning the ABBB symbols (one helper, `song_is_chord_melody(mode)`, serves both this and Part). Make `clamp_song` (song_form.h:19-24) map out-of-range values to Off explicitly.
- `Part::process` (part.h:376-379): on a PITCH fire, `_note_suppressed = _inhibit || _cm_silent()`, and `_gate_ctr` is set only when not suppressed. This reuses CHOKE's swallow path: no trigger, no gate pulse, no gate sustain (gate() already honours `_note_suppressed`, part.h:303). The PULL draw still runs, so its stream does not depend on the mode (same rule as CHOKE, part.h:385-387). `_cm_silent` is true when a mode is active and the fire is silent per the table above (DISSOLVE `0 < slot < related_end`, TURNS `slot > 0` in a chord phrase); the same call records whether the fire is a chord fire, so `_fire_trigger` does not re-derive it. Slot from `_mod.pitch_cur_step()`, L from `_mod.pitch_steps()` (super_modulator.h:137-138); there is no pitch `wrapped()` accessor, so slot 0 is the phrase start (see rule 1 for the STEPS-turn exception).
- `Part::_fire_trigger` (part.cpp:636-654), when a mode is active:
  - **chord fire:** build the chord on the root from rule 4 instead of `_tg[LANE_PITCH]` at the `_chord.build` call (part.cpp:646), folded per rule 5. Then **latch the PULL leader mask**: `_chord_pc = pc_mask12(built chord)`. Today `_control_tick` recomputes it every 96-sample tick from the lane pitch (part.cpp:493-498), and the fire's refresh tick runs before `_fire_trigger` (part.h:417-426); in these modes the lane keeps moving through the silent slots, so a follower deck (instrument.cpp:350) would bind to pitch classes this deck never plays *(review probe: the mask changed through slots 1..11 while they would be silent)*. So `_control_tick` must not overwrite `_chord_pc` while a mode is active. Melody pitch classes are not added: the follower holds the chord.
  - **single-note fire:** strike one voice, `trigger_chord(&note, 1)`, **without** calling `_chord.build`. build() writes ChordBuilder's voice-leading memory (`_prev`, `_prev_n`, chord.h:130-133) unconditionally; a full-size build around each melody note would make the next chord voice-lead against a phantom chord. Count 1 takes the COLOR-0 path (synth_engine.cpp:171-172, no `_stab_rng` draw). Pitch lifted per rule 5. Store the Quantizer's integer target note (a new accessor for its `_last_note`, which already carries TUNE and offset) as the last struck note and set `melody_seen`.
  - Ordering is safe: the last melody note fires strictly before the next wrap, and the wrap sample carries slot 0's fire with the pattern already advanced (measured over 6144 phrases; lane.cpp:994-1001).
- **New Part state:** last struck note (int semitone), `melody_seen` (bool, cleared at every chord strike), the last two roots (two ints), the phrase counter (TURNS' parity), the chord-fire flag. Part has no size gate.
- **Reset:** Part has no `restart_song` today; `Instrument::restart` calls `p.mod().restart_song()` on the SuperModulator directly (instrument.h:603-611, super_modulator.h:79), and both hosts reach RESET only through it (Fireflow.cpp:789, shell/panel_scan.cpp:1169). Add `Part::restart_song()`, which clears the mode state and forwards to `_mod.restart_song()`, and route instrument.h:609 through it. `Part::init` clears the same state (VCV re-inits on a sample-rate change). After a reset the first chord is built from `_tg[LANE_PITCH]` as on a fresh init, and TURNS starts with a chord phrase.
- **PITCH CV:** `Part::pitch_cv()` returns the lane pitch `_pitch_q` (part.h:209, 305), so the PITCH jack would carry the lane melody through the silent slots and the unlifted notes, while GATE follows the suppression (Fireflow.cpp:811-814). In these modes `pitch_cv()` reports the last struck pitch (the chord root at a chord fire, the lifted note after) and holds it through the silent slots.
- **Accepted side effects of Part-level suppression:** the VCV lane ring still brightens on silent fires (`lane_fired`, Fireflow.cpp:1237), and the sibling deck's FLUX THIN still reads this deck's RhythmView, which counts the silent onsets (super_modulator.cpp:169-188, instrument.cpp:310-311). Other lanes keep stepping in the silent slots and SynthEngine pushes timbre, cutoff and width to the ringing chord each tick (synth_engine.cpp:316-341), so a silent slot can still sound like an articulation (round 3, verifier) — instrument behaviour, the same on every arrangement.
- **Active only when** the deck is SYNTH or WAVE, in STEP. On FLOW, BODY (one voice: trigger_chord clamps to kVoices, body_voice.h:27 with synth_engine.cpp:171), BBD (trigger is a no-op, bbd_engine.h:36), SAMPLER (chords flatten, part.h:620-624) and FEED, the modes are just the ABBB table.
- **Knob meaning on these rungs:** COLOR = chord size, DENSE = melody density (DISSOLVE has 8 and TURNS 15 single-note slots at STEPS 16; round 3 used 0.6 and 0.45), DEC = ring of both, RANGE = melodic width (0 degenerates, see question 1), STEPS = phrase length (the windows scale with it).

## Surface

**Must change** (each goes red or reads out of bounds otherwise):
- `param_table.h:76` → `X(P_SONG_A, 0.f, 8.f, 9) X(P_SONG_B, 0.f, 8.f, 9)` (the step column too; nothing checks it today).
- Two appended ladder rungs, `{form 2, song 7}` (DISSOLVE) then `{2, 8}` (TURNS) — the last two positions of the knob (song_ladder.h:27-37, 14 → 16; never inserted; rung 0 stays Off, test_song_ladder.cpp:16-18). Form 2 (Hierarchical) is what the renders used. Their order is taste (song_ladder.h:22-24); suggested from more chord to more melody: DISSOLVE, TURNS (see open question 7).
- `kSongWords` gets two more words (Fireflow.cpp:489-491; :496 reads out of bounds without them) **in the same commit as the enum**, and their pins in test_panel.py:1083-1091.
- The 13s that break with 16 rungs: test_control_params.cpp:62 and test_controls_map.cpp:59 → 15.f.
- `tests/test_param_impact.cpp`: `moves_audio()` compares each parameter at lo against hi, so SONG moves from "AAAB vs Off" to "AAAB vs TURNS" (the last appended mode). That flips two UNTRACED pins — SONG_B "dead on both modes" (:305) and SONG_A "alive only in STEP" (:371) — and fails 4 cases *(review probe build, with one appended mode)*. The file forbids deleting an UNTRACED entry to go green. Decision: keep the old AAAB-vs-Off measurement as its own small case so the open SONG_B thread stays pinned, and let the lo/hi sweep re-pin with a dated note.

**Docs and comments** (nothing goes red today, so extend the guard):
- test_panel.py:1118-1121 gets the new words, which makes the host/vcv/README.md entries mandatory.
- host/vcv/README.md:58 ("14-rung"), :62 ("seven arrangements"), :65-73 (per-mode list); README.md:63 and :203 (the mode list); docs/roadmap.md:1503-1511 (SONG table).
- Stale counts in comments: param_table.h:47-48 and :51-57, control/control_law.h:836, Fireflow.cpp:481, gen_panel.py.

**Optional:** test_song_rung_state.cpp:22-68 uses its own local `n = 14` and stays green (it could read `kSongLadderCount`); the comment at test_song_ladder.cpp:33-35.

**Side effects, all fine:** the persist layout hash changes (persist_record.cpp:90 folds in `kSongLadderCount`, test_persist_record.cpp:139-149), so the first boot of a new Rev A image resets Bastian's persisted patch — tell him before a flash. The rungs inherit `new_phrase` and `sampler_punch` on change (control_law.h:848-860). The SONG lamp keeps showing A/B changes (led_law.hpp:377-393).

## Tests

Each RED proved once; the sabotage for the RED is named.

- **Enum and clamp** (test_song_form.cpp): `clamp_song(7) == Dissolve`, `clamp_song(8) == Turns` (RED on main: both clamp to Off), `clamp_song(9) == Off`, `song_text(m, 8) == "ABBBABBB"` for both (RED while they fall into Mirror's branch, which gives ABBABAAB). Order: append the enum, see :87 go red, then fix the clamp. The existing :87 and test_instrument.cpp:960-970 stay.
- **Range pinned to the enum** (test_param_table.cpp): `kParams[P_SONG_A].hi == kCount − 1` and steps == kCount, same for B.
- **Ladder reach** (test_song_ladder.cpp): both modes are carried by some rung (structural, so it respects the "order is taste" comment, song_ladder.h:22-24).
- **Silent slots, non-vacuous, per mode:** SYNTH STEP, STEPS 16, DENSE 1.0, several phrases, a mixed seed. First require `lane_fired(LANE_PITCH)` on at least N of the mode's silent fires (DISSOLVE slots 1..7, TURNS slots ≥ 1 of chord phrases), then assert no trigger and no rising gate edge at those fires (not "gate low": the chord's sustain may carry into the next slot). The "outside" half runs the same seed with SONG = ABBB and asserts the same slots trigger. RED: drop `_cm_silent`.
- **TURNS parity:** after a reset the 1st phrase strikes a chord and nothing else, the 2nd strikes only single notes (slot 0 included), the 3rd a chord again. RED: start the counter at 1.
- **One voice per melody note:** COLOR 0.7, every single-note fire strikes exactly one voice (`VoiceCountProbe` trig_count idiom, synth_engine.h:281-296, or `probe_synth().probe_voice()`), and the next chord's strew equals a run with the melody slots forced to COLOR 0. RED: pass the COLOR count instead of 1.
- **No phantom voice-leading:** the chord after a melody equals one built with ChordBuilder's `_prev` set to the previous chord. RED: call `_chord.build` on single-note fires.
- **Root = last note − 12:** RANGE ≥ 0.5, mixed seed, a case where last − 12 differs from both previous roots (asserted in the test). Observe the root through the engine's struck voice pitch, not through the new Part state.
- **On the grid under PULL:** a bound follower whose last note is struck mid-glide still builds the next chord on an integer semitone. RED: store `_tg[LANE_PITCH]`.
- **Anti-repeat:** no root equals either of the last two, in DORIAN, MIN_PENT and CHROM (+5 there), RANGE ≥ 0.5.
- **No melody:** DENSE 0 (slot 0 still fires, the window is empty) still moves the root; and a run where CHOKE swallows the whole window behaves the same (only struck notes count). RED: keep `melody_seen` set across phrases.
- **Register:** RANGE 1.0, a case where a raw note lands below b + 12; every sounded single note is ≥ b + 12 and every root is in [b, b + 12). RED: disable the lift.
- **Leader mask:** a follower's mask through the silent slots equals the pitch classes of the last chord. RED: let `_control_tick` overwrite `_chord_pc`.
- **Reset:** after RESET (and after `Part::init`) the first chord is built from `_tg[LANE_PITCH]` as on a fresh init, and TURNS starts with a chord phrase. RED: skip the clear.
- **PITCH CV:** `pitch_cv()` holds the last struck pitch through the silent slots.
- **STEPS turn:** turning 16 → 8 mid-phrase skips at most one chord, and the next wrap strikes one.
- **Inert elsewhere:** bitwise-identical Instrument output for each mode vs SONG = ABBB on FLOW-SYNTH, FLOW-WAVE, BODY, BBD, SAMPLER and FEED. RED: drop the `_step_on` / engine check.
- **Outside the modes:** `ctrl_identity` and `wave_formant_sweep` stay unchanged. They only guard "nothing moved outside the modes": neither sets SONG nor runs a STEP deck (ctrl_identity is two SYNTH decks in FLOW; wave_formant_sweep has `set_step` false), so they cannot catch a melody note that draws `_stab_rng`; the one-voice test above does that. Optional: a render scenario with `set_song 7` on a STEP SYNTH deck (scenario.cpp:125 supports it).
- `ctest --test-dir build -E "^reva_"` after a Release build.

## Plan of work

1. **Enum** (one commit): song_form.h enum, clamp, symbol cases, `kSongWords` and its test_panel pins (same commit, or the label loop reads out of bounds); the clamp and range tests.
2. **Surface** (one commit): the two ladder rungs, P_SONG row, test_control_params / test_controls_map 13 → 15, test_param_impact split and re-pin, ladder-reach test.
3. **Part logic and its tests** — after rec-snap is merged (it edits `part.*`). The round-3 prototype is the starting sketch.
4. **Docs:** README.md, host/vcv/README.md (+ test_panel.py guard), roadmap.md, the stale comments.
5. **Finish:** the worktree has no env.sh, so source the main checkout's (`source <main>/env.sh`), then `cmake -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build` (ctest does not build), `ctest --test-dir build -E "^reva_"`; `host/vcv/build-local.sh install` and a Rack restart; if released, bump plugin.json (MAJOR stays 2) and rewrite docs/release-notes.md. Before flashing Rev A, warn about the persisted-patch reset.

Building in this worktree also needs `git submodule update --init lib/DaisySP` first (the CMake configure fails on the empty submodule).

## Round 3: all three through the real Instrument (PICKUP dropped after it)

**How.** Not the host-side closed-loop driver planned earlier (it switched DENSE mid-phrase, which changed the groove), but the three modes **prototyped inside Part** in a scratch copy of the engine (about 130 lines: the enum and symbol case, `_cm_on_fire` for the silence/chord decision in `Part::process`, `_cm_fire` for root, fold, lift and the single-note strike in `_fire_trigger`, a seed override in `Instrument::init`). Every knob is constant for the whole render, so what is heard is what the mode itself does. Not in the prototype: the PULL mask latch, the quantizer-target root (it rounds `_tg[LANE_PITCH]`, exact without PULL or scale turns), `Part::restart_song`, the PITCH CV hold, and the snap to `nearest` (it steps up to the next scale degree instead).

**Settings.** Deck A alone, SYNTH, STEP 16, 160 BPM at noon RATE (0.375 s per step = 8ths at 80 BPM, as rounds 1–2), DORIAN, RANGE 0.5, VARY 0.5, COLOR 0.7 with MOTION's swing on, DEC 1.0, ATTACK 0.25, reverb 0.4; DENSE 0.8 / 0.6 / 0.45 for PICKUP / DISSOLVE / TURNS. Two seeds, neither heard before: 0x9e3779b9 and the Part-style k = 1. One render per process (a re-initialised Instrument keeps patch state — `Part::init` leaves it alone by design — so a second render in the same process started from the first one's ending; caught by the verifier, fixed before listening). Eight phrases, then the ninth phrase's chord as the ending; right after it the lane is slowed to the free-rate floor and the four texture lanes are held at their current value (max deviation afterwards 0.000000). The slowed lane still steps once or twice in the 12 s left, but only to slots 1–2, which are silent in every mode, so **no sounding fire** follows. 12 s of ring-out, 2 s end fade, one BS.1770 loudness (all six at −20.00 ± 0.03 LUFS, peaks −3.2 to −5.1 dBFS). Blind: per seed one set, the three shapes lettered X/Y/Z at random, and never the same mapping in both sets. Key checked by correlation (1.000000 against the named shape). Renders are byte-reproducible.

**What the prototype printed** (per render, all six with 0 errors): no voice struck on any silent fire; every single-note fire struck exactly one voice; every root in [0, 12), every single note ≥ 12; chords of 3 or 4 notes (3 in 1–5 of 9 chords, from MOTION). Notes per render (seed s1 / s2): PICKUP 32 / 30, DISSOLVE 40 / 40, TURNS 28 / 28 (7 per melody phrase). Anti-repeat moved 3–5 of 9 roots in PICKUP and DISSOLVE, 2 of 5 in TURNS. PICKUP and DISSOLVE settle into four-chord loops on both seeds (s1: 10 3 0 …; s2: 5 2 10 3 …), and on the same seed they share most of the root sequence: the rule, not the shape, sets the progression.

**What the verifiers measured** (two independent lenses, code and audio, 2026-11-10; engine-side cross-checks found no discrepancy with the printed counters, and a re-derivation of the root rule matched all 34 chords):
- **"Silent" is silent at the voice level, not in the audio.** On some silent fires other lanes step while the chord rings — e.g. SOURCE jumping 0.0 → 0.50 — and SynthEngine pushes timbre, cutoff and width to every voice each control tick (synth_engine.cpp:316-341). The spectral flux of such a step reaches that of a struck note (DISSOLVE's gap had the strongest hits). Real instrument behaviour, the same on every arrangement; the listener is told.
- **In PICKUP and DISSOLVE the pickups replace the chord** rather than play over it: by the next slot 0, 0–1 of the phrase's own chord tones still sound above env 0.05, against four pickup voices; single notes peak up to about 5.6 dB above each chord tone (velocity 0.49–1.00 against 0.50–0.58). TURNS keeps 1.75–1.88 chord tones. This is the steal of rule 3, measured.
- **The ending chord's loudness** differs by up to 12 LU between the shapes of one set, from the LEVEL lane's value at that moment, not from the shape.

**Verdict** (Bastian, 2026-11-10): set 1 → **TURNS** best, set 2 → **DISSOLVE** best, "only because there felt to be no foreign tones"; set 2's TURNS "was also ok". PICKUP, the round-2 winner, was picked in neither set, and set 1's DISSOLVE was not mentioned. Key: set 1 X = TURNS, Y = PICKUP, Z = DISSOLVE; set 2 X = DISSOLVE, Y = PICKUP, Z = TURNS.

**What "foreign tones" matches** (probe on the six renders, voices only, amplitude proxy env × velocity, threshold 0.02, sampled every 10 ms up to the ending chord; harsh = interval class 1 (minor second / major seventh) or 6 (tritone) between two sounding voices, summed as the quieter voice's amplitude per second):

| Render | Heard | Harsh total | melody–melody | melody–chord | chord–chord | Melody outside the chord |
|---|---|---|---|---|---|---|
| TURNS s2 | ok | **6.3** | 0.4 | 0.6 | 5.2 | 32 % |
| TURNS s1 | best (set 1) | **9.0** | 1.3 | 0.0 | 7.7 | 31 % |
| DISSOLVE s2 | best (set 2) | **10.0** | 0.8 | 2.9 | 6.3 | 18 % |
| PICKUP s2 | — | 10.7 | 0.6 | 3.1 | 7.0 | 17 % |
| PICKUP s1 | — | 14.0 | 1.6 | 5.4 | 7.0 | 24 % |
| DISSOLVE s1 | — | 17.6 | 8.3 | 3.6 | 5.7 | 33 % |

- The three liked renders are the three with the least harsh rubbing. Set 1 separates clearly (9.0 against 14.0 and 17.6); set 2 barely (DISSOLVE 10.0 against PICKUP 10.7), so the measure explains set 1 and is only consistent with set 2.
- "Notes outside the current chord" does **not** track the verdict (TURNS has the most and was liked): scale notes as such are not the problem, rubbing is.
- Melody against the ringing chord is lowest in TURNS (0.0 / 0.6): its melody plays in a phrase of its own. PICKUP and DISSOLVE put the melody on the freshest part of the chord.
- DISSOLVE s1's 8.3 melody–melody comes from its dense stepwise runs ringing into each other at DEC 1.0.
- The largest share in every render is **inside the chords** (5.2–7.7): at COLOR 0.7 the four-note chords on F (F7: tritone A–E♭), E♭ (maj7) and B♭ (added maj7) rub by construction, and the anti-repeat's fourth-up walk visits exactly these degrees. Same in all three shapes, so it does not explain the ranking, but it is a candidate for the general "foreign" impression; the first chord of every render (F, root from the lane) carries the tritone.

## Open questions

1. **RANGE 0 (the factory value):** accept that the modes degenerate there (one melody pitch, a fixed three-chord loop), or should the rungs give the melody its own width?
2. **The register rule** (fold the chord into the bass octave, lift low notes): round 3 played it and the verdict did not object; keep it?
3. **STEPS 8:** DISSOLVE's window is then 4 slots and TURNS' melody phrase 8 slots. Unmeasured by ear; fine as the windows scale, or a minimum?
4. **Anti-repeat overrides the tune on about half the chords** and makes four-chord loops. Fine as heard, or a softer rule (only the immediately previous root)?
5. **Words on the panel:** "DISSOLVE" / "TURNS", and the short forms for the hardware word?
6. **Chords of 1–3 notes thin one melody note later** (rule 3). Accept, or silence the previous phrase's last melody voice at the chord strike so the melody always eats chord voices oldest-first? The second cuts that note's tail at the chord.
7. **Rung order:** DISSOLVE then TURNS (more chord to more melody), or the other way? Two rungs make the SONG ladder 16 positions on one Rev A pot; the pot's hover at the rung seams (`knob_from_pot`'s `std::round`, shell/controls.cpp:49-55) is unmeasured at that spacing.
8. **"Foreign tones"** (round 3): the liked renders rub least (semitone/tritone). Two levers exist and neither is in this design: a melody rule that avoids a semitone or tritone against the ringing chord, and the chords themselves (at COLOR 0.7 the four-note chords on F, E♭ and B♭ in C Dorian carry a tritone or major seventh, and the anti-repeat walk visits them). Leave both for now, or audition one?

## Appendix: audition record

**Rigs.** Round 1 and 2 used a scratch harness: one real ModLane (melodic, STEP, Part-style seeds `0x1234abcd + (k+2)*2654435761`), one SynthEngine, ChordBuilder (COLOR 0.7), AmbientReverb on a 0.4 send; 80 BPM, A Dorian, 16 bars of 2-bar phrases plus a 12 s tail with a 2 s fade; VARY 0.5; files matched to one BS.1770 loudness under a −1 dBFS peak cap; letters/x-y assigned at random. Pitch law `36·reflect(0.5 + 0.5·v)` snapped to the scale (equal to the real path at RANGE 0.5, without quantizer hysteresis or slew); no FLUX, GRIT or master bus. The rig struck 4-note chords every phrase and had no MOTION swing.

**Lessons from the rig itself** (each found by an independent check and fixed before listening):
- A 4-bar phrase cannot hold a chord at any DEC (DEC 1.0 is under env 0.01 after about 5.5 s), so R2/R4 first rendered as gap candidates; all candidates moved to 2-bar phrases.
- Under AAAB with 4-bar phrases, R2/R4 played one root for most of the file; R3 never played pattern B. Fixed with A/B-alternating tables, VARY 0.5 and anti-repeat.
- Plain RMS matching left 1.65 dB of perceived loudness difference; BS.1770 matching brought it to 0.1 dB.
- Round 1's "fourth up three times" closes a C↔G cycle in A Dorian; round 2 uses one fourth, then single degrees.

**Engine finding, unrelated to this mode:** an attack zipper on the 96-sample control grid — steps at `kCtrlInterval` boundaries in the first ~9–12 ms after strikes (531 strikes; median ratio 15.4 of tick-boundary to off-boundary second difference; stronger where a strike lands on still-ringing voices). It applies to the instrument as well. Mechanism unmeasured (candidates: the voice's per-tick `_vel` glide, per-tick envelope/filter pushes); probe before filing.

**Unmeasured:** the cause of COLOR 0.7 striking 3 or 4 notes from phrase to phrase in the real deck (MOTION's COLOR swing, part.cpp:434-438, or ChordBuilder at the register edges); FEED in this mode; the fold octave against the note floor (rule 5); the Rev A SONG pot hover at the new rung's seam (`knob_from_pot`'s `std::round`, shell/controls.cpp:49-55).
