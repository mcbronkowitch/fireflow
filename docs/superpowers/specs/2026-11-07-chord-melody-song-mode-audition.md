# Chord-and-melody song mode: audition plan

## Goal

The goal is a song mode with long sustained chords and, between them, short melodies drawn from the scale or the chord tones. Nobody knows yet how the melody should relate to the chords in time, so this plan auditions five audibly different shapes as WAV renders before any engine work. The renders use the real FireFlow synth voice, ChordBuilder and reverb. Only the winning shape gets a spec.

Scope for now: planning and scratch renders only. Status: brainstorm, not an approved spec.

**Citations.** Another session was editing `engine/instrument.*`, `engine/parts/part.*` and the sampler on `feat/rec-snap` while this was written, and committed twice during the run. Every `instrument.*` and `part.*` line number below is pinned to **286c3030** (`feat/rec-snap`); everything else is pinned to `main` at b2c11633. Re-check `git status` and re-resolve the citations before each round. C2, C3 and C4 all hook into the PULL/CHOKE block in `instrument.cpp`, which rec-snap touches, so engine work waits until rec-snap is merged.

**Probe artifacts.** The probes and pre-listen renders cited as "measured" ran in a session scratch directory that does not survive the session. The numbers are quoted here; the rig is rebuilt in the session that runs round 1.

## What the engine does today

**Measured (planning-run probes against the real engine sources; artifacts not kept):**
- **Chords come from one note.** ChordBuilder turns the single quantized PITCH note into 1–4 tones at COLOR edges 0.125/0.375/0.625 (chord.h:29-34). In A Dorian at COLOR 0.7 the intervals are `[0,-5,+3,+10]`, and above 0.85 the ninth replaces the fifth below. Voice-leading keeps common tones (chord probe, section B).
- **FLOW holds a chord indefinitely.** A 4-note chord held at env 0.701 after 30 s (voice.cpp:48). In the real Instrument, deck A at FLOW / COLOR 0.7 / DENSE 0 sustained 4 voices for 24 s with one gate rise (`explicit_probe.csv`).
- **One deck cannot hold a chord under a melody in FLOW.** One extra trigger re-strikes the whole chord (env 0.70→0.98), and the melody pitch is pulled back to the chord root within 3 ms (synth_engine.cpp:218, :383-389).
- **In STEP, a melody note steals the chord root.** On a 4-note chord the oldest voice (the root) goes first. A 3-note chord leaves the fourth voice free (synth_engine.cpp:193-210).
- **One decay per deck.** The same DEC goes to all 4 voices on every tick (synth_engine.cpp:314, :326). Dropping DEC mid-ring cut a chord voice from env 0.18 to 0.004.
- **The STEP note length never reaches a synth.** Lane-level hold is capped at 4 steps; at DENSE 0, 106 of 200 seeds held only 1 step. Only BBD and SAMPLER override `set_gate` (bbd_engine.h:110, sampler_engine.h:117), so on SYNTH/WAVE/BODY the DEC knob alone sets note length (engine_iface.h:97-102).
- **SONG only picks snapshot A or B.** `song_symbol_at` returns 0/1 per phrase, and every mode starts on A (song_form.h:75-105). One phrase = STEPS steps; with sync at noon RATE that is one step per beat (engine-map.md:622-634).
- **ModLane has no room.** `sizeof(ModLane)` = 920 on the desktop. The gate `(sizeof(ModLane) - 412) * 10u < 5u*1024u` (test_song_form.cpp:310-319) gives 5080 < 5120 at 920 and fails at 924, so **one more float in ModLane turns it red**. The headroom is about 3 B per lane, shared across ten lanes; the board figure is UNMEASURED. Any new per-mode state goes in Part or Instrument, not ModLane.
- **The SONG ladder has 14 rungs** (song_ladder.h:27). Up to 18 keeps `hyst_step` lossless on the VCV path only; on hardware `std::round` in `knob_from_pot` defeats the hysteresis (999 of 1000 re-rolls at a seam in the probe). Changing the count changes the persist layout hash.
- **The scratch render chain is fast.** SynthEngine + ChordBuilder (header-only) + AmbientReverb compile from 9 .cpp files (six synth/body, `fx/reverb.cpp`, two DaisySP) in about 3 s and render 12 s in 96 ms.

**Read from code, not measured:**
- **PULL** binds a follower note to the leader's chord pitch-class mask with probability (|PULL|−0.03)/0.97 (instrument.cpp:313-351, part.h:380-389). It has never had a listening pass (by-ear-decisions.md:436-458).
- **CHOKE's Held zone** blocks the yielding deck only while the priority deck's `gate()` is high or it is in FLOW (instrument.cpp:472-529). In STEP, `gate()` is pulse plus `pitch_sustain()` (part.h:302-304), 1–4 steps, so **with a STEP pad CHOKE −0.6 already lets B fire in A's tail**; the window start is set by note length, not by a controllable fraction. The real gaps: B is the deck that gets ducked under A's tail (depth 1 from |c| ≥ 0.5), and the window cannot be set. A pad duck with B as detector is CHOKE in (0, 0.5], duck-only with events free, and probably needs no new code. All of this is read from code, UNMEASURED (probe 4).
- **`set_inhibit` demotes.** `Part::set_inhibit` calls `_engine->set_hold(on)` (part.h:185-189), and `SynthEngineT::set_hold(true)` runs `_demote_all()` (synth_engine.cpp:129-139). An inhibited FLOW deck loses its drone, and releasing the inhibit re-strikes it. Any "suppress this deck's fires" gate in C2–C4 needs a path that suppresses lane fires **without** `set_hold`.
- **No progression concept.** The chord root is always the melody note (part.cpp:636-654). At STEP DENSE 0 only rank 0 fires, rank 0 is pinned to slot 0, and `derive_turnaround` skips slot 0 (`if (slot == 0) continue;`, song_form.h:206). The timeline probe printed slot 0 identical in A and B (+0.003 at 8 steps, −0.138 at 16). So with VARY 0, **a STEP DENSE 0 chord deck plays the same root every phrase in every SONG mode**. Whether VARY's evolve touches slot 0 is UNMEASURED.

**Unmeasured, and needed later:**
- confirm by probe that deck A's own lane gives one root forever at STEP DENSE 0, VARY 0 (code says so, above), and what VARY > 0 does to slot 0
- how varied real cadence notes are under `bind_song_cadence`
- the Part tick order (gravity set before the quantizer runs?)
- whether PULL's 40 ms live-follow audibly bends a ringing STEP voice
- the std::round hover on the Rev A SONG pot

## The candidates

Scored 1–5 (5 = best; for cost, 5 = cheapest).

| Draft variant | Ambient promise | Distinct | Cost | Risk | Verdict |
|---|---|---|---|---|---|
| two-deck 1 Overlay / music-first V2 Drift | 3 / 4 | 4 | 5 | 4 | merged → **C0** |
| one-deck V3 Overlay (reserved voice) | 4 | sounds like C0 | 1 | 2 | dropped (an engine question, not an ear question) |
| one-deck V1 Antiphon | 3 | 5 | 4 | 3 | **C1** |
| two-deck 2 Answer / music-first V1 / one-deck V2 Tail fill | 5 | 4 | 3 | 3 | merged → **C2**; the tail fill's voice-steal dissolve becomes a round-2 sibling |
| two-deck 3 Cadence | 4 | 5 | 3 | 3 | **C3** |
| music-first V3 Suspension | 5 | 5 | 2 | 2 | **C4** |

The five candidates span one axis: how the melody relates to the chord in time. In C0 they are simultaneous and unrelated. In C1 they alternate by phrase. In C2 the melody sits in the chord's tail. In C3 the chord follows the melody. In C4 the melody holds across the chord change.

### C0 Drift (control: what the instrument can do today)

**What you hear:** a held pad that changes chord every 2 bars. Over it, a 3-note loop over 7 beats repeats unchanged and keeps landing at a new place against the chords. Each note is snapped to the chord that is sounding. The figure is hypnotic, never breathes, and has no cadence.

**Rules:**
- Pad in FLOW.
- Loop: N=3 notes over P=7 beats.
- Each note is bound to the sounding chord's tones (p=1).
- Register: semis 26..33.

**Later engine shape:** the melody half exists today: deck B in STEP, STEPS 7, DENSE 0.4 (k = round(0.4·7) = 3 from code, lane.cpp:767 / phrase_gen.h:65-74, UNMEASURED), PULL −1 (instrument.h:584-591). **The chord half does not.** Deck A in STEP, STEPS 16, DENSE 0, COLOR 0.7 plays one root forever (see above), and STEPS 16 at noon RATE is 4 bars per fire, not 2. The rig's C0 plays a FLOW pad over a fixed progression, so it is "today's melody against a progression that does not exist yet", not "what the instrument can do today".

**Cost:** zero for the melody; C0 still needs a progression source to be real (question 2). This is also PULL's first listening pass.

### C1 Antiphon (whole phrases take turns)

**What you hear:**
- A 4-note chord rings alone for 2 bars and decays.
- A tune plays alone for the next 2 bars.
- The next chord is rooted on the tune's last note.
- The two never overlap: a clear call and response.

**Rules:**
- Role table: H M per 2-bar span.
- Melody source: the real lane pattern A/B at DENSE 0.45, one octave above the chord root.
- Chord at COLOR 0.7; the melody plays at COLOR 0.

**Later engine shape:**
- A new `SongMode::Chords` value with its own case.
- A pure function `phrase_role(mode, idx)`, so no new ModLane state.
- Force k=1 in hold phrases (lane.cpp:754).
- Part picks the chord size per role (part.cpp:636-654).
- Single deck.

**Cost:** small. The role switching is reproducible in the real Instrument with timed `set_density` / `set_color` events, but "the next chord is rooted on the tune's last note" is not: a k=1 hold phrase fires `pitch[0]`, which is the same in A and B. That rule needs the `_last_melody_q` code. On one deck DEC is shared by all voices, and a long chord DEC overlaps the next melody span and loses its root to the first melody note (synth_engine.cpp:193-210), so "never overlap" is a rule the engine must enforce, not a given. Risk: it is the most "armed mode"-like candidate, and the chord-layer spec (2026-07-17, lines 13-16) rejected armed chord modes.

### C2 Answer in the tail (chord calls, melody answers)

**What you hear:**
- A chord is struck on the downbeat and sounds alone for 4 beats.
- A 3–5-note fragment answers in beats 5–8, over the ringing tail.
- The pad dips about 5 dB under the fragment.
- The fragment ends on a held chord tone, and fragment lengths alternate (3, 5).

**Rules:**
- Window: beats 4..8 of each 8-beat span.
- Fragment:
  - the first note is the nearest chord tone
  - inner notes are scale passing tones by step
  - at most one leap, no larger than a fifth
  - the last note is held at least 1.5 beats
- Span roles H M M H M M M C over 8 spans (the density arc): H = chord alone, M = chord plus answer, C = cadence span (the answer ends on the root and the chord holds two spans).

**Later engine shape:**
- Two decks.
- An Instrument "tail gate" next to the PULL block: B may fire only while A's chord age is at least 4/8 of its span (the "beats 5–8" window). It must suppress B's lane fires without `set_inhibit`, which demotes through `set_hold`.
- First check whether CHOKE in STEP already gives an acceptable window (probe 4); the tail gate is only needed if the window must be controllable.
- A pad duck with B as the detector: CHOKE in (0, 0.5] may already do it.
- A SONG rung on B decides which spans get an answer.

**Cost:** medium (Instrument, Part, ladder).

### C3 Cadence (melody leads, chord punctuates)

**What you hear:**
- A scale tune of 6–9 notes plays alone.
- It lands on a held cadence note, and at that instant a 4-note chord blooms an octave below.
- The chord holds through the rest of the phrase and under the next tune.
- Harmony changes irregularly, and every change is caused by the melody.

**Rules:**
- One chord per melody phrase.
- Chord root = cadence note − 12.
- Melody: an 8th grid at fire probability 0.6, a Dorian stepwise walk with a 35 % pull to the cadence target.

**Later engine shape:**
- A FOLLOW role on deck A:
  - its own lane fires are suppressed by a new path, not `set_inhibit` (which would demote A's FLOW drone and re-strike it on release)
  - Instrument calls `trigger_manual()` on A when B fires its final open slot
  - a new Part override sets the pitch for that manual fire (part.cpp:243-266)
- A lane observer for "final open slot" built from `rank_of_slot` and `_groove_k`.

**Cost:** medium-low. Risk: static harmony if cadence notes repeat. The first pre-listen render of the two-deck draft gave 3 identical Cmaj7 chords (reported by that probe; no artifact kept).

### C4 Suspension line (the line holds across chord changes)

**What you hear:**
- A slow legato line, one note every 1–3 beats, in arcs of about 4 bars.
- At each chord change the line does not move. It holds its note into the new chord and then resolves down by step.
- The line moves while the chord holds, and holds while the chord moves.

**Rules:**
- No onset on beat 1 of a chord change.
- A held note that is not a tone of the new chord resolves down to the nearest chord tone 1–2 beats later.
- Register: semis 24..33.

**Later engine shape:**
- Deck B in FLOW (legato through demote, synth_engine.cpp:217-218).
- An anti-sync gate: no B fire within ±1 beat of an A fire. Not built on `set_inhibit`, which would demote B's held note at exactly the moment the suspension must hold.
- A "bind at fire, then hold" flag on the gravity path (part.h:118-122, quantizer.h:101-145). Today's live-follow would erase the suspension.
- A one-shot resolve rule.

**Cost:** the highest of the five. The latched bind must be a separate mode, not a change to PULL.

## The audition rig

**Tool.** One unified rig outside `engine/` (a scratch directory, or `tools/chord_audition/` on its own branch if it should be kept). The pre-listen renders from this planning run came from two rigs with different mixes (one-deck draft: dry + 0.35 wet; two-deck draft: send 0.4, gains 0.8/1.8), so they cannot be compared blind and serve as a pre-listen only.

The rig has three stages:

1. **`lane_dump.cpp`**: a real ModLane in the probe skeleton of engine-map.md §6.
   - Setup: one real ModLane; `set_melodic` before `init`; STEP 16; VARY 0; SONG AAAB.
   - Runs at every STEPS a candidate uses (7, 8, 16), not 16 only.
   - Output: `lane_s<steps>_k<k>.csv` with pattern A/B pitch, fired slots per DENSE value, and rank order.
   - Pitch mapping: lane values are bipolar and reach notes through `Part::pitch_pre_quant` (base + mod + TUNE + song offset, reflected, part.cpp:200-206), the by-ear RANGE law (D1 centre at semitone 18) and the quantizer. The dump replicates that mapping and writes semitones, or the registers in `compose.py` are arbitrary.
   - Why: melody material from the engine's own generator. **This transfers for C0 and C1 only.** C2, C3 and C4 use hand rules (nearest chord tone plus passing tones; a Dorian walk with a cadence pull; a legato line). Either drive those rules from the lane dump (lane pitches as the contour, the rule only snaps and times them) or state plainly that their verdict is about the rule, not the engine's generator.
2. **`compose.py`** (stdlib only, run without `-I`). It applies each candidate's rule to the lane material and the fixed progression, then writes `score_<cand>_k<k>.csv` with the columns `t_s, layer, kind, semis_or_root, dur_s, vel`.
   - Chord tones come from a Python copy of ChordBuilder's stack rule.
   - It asserts the lays a probe printed during planning (12/7/15/22, 10/5/14/21, 17/12/21/15); re-print them from the real `ChordBuilder` before trusting the copy.
   - Roots must be in-scale: the music-first probe reported that `build()` stacks chromatic tones on an off-scale root. Re-print that too.
   - Its own RNG is seeded from k. Without that, the three seeds give identical files for every candidate that ignores the lane material.
3. **`play.cpp`**: reads a score CSV and drives real SynthEngines with explicit chord and note events.
   - Pad: one SynthEngine on the real ChordBuilder, `set_chord` before `trigger_chord`.
   - **One pad law for all five candidates**: a long-DEC STEP strike that rings 2 bars. Otherwise raters score sustain versus decay instead of how the melody relates to the chord in time. A FLOW-pad variant of the winner goes to round 2.
   - Melody: a second SynthEngine with its own short `set_cycle` (FLOW for C4) — **except C1**, which is a one-deck design and renders on **one** SynthEngine with per-role `set_decay`, so the shared-DEC and root-steal behaviour is heard as it would be on the instrument.
   - Reverb: AmbientReverb on a 0.4 send.
   - Levels: one fixed melody/pad level for all files. Melody +8 dB is a choice, based on one probe mix where it sat 10.6 dB under the pad. Every file is RMS-normalised to −20 dBFS so loudness does not decide.
   - Build: `build.sh` sources the repo's `env.sh` inside the script, uses clang `-O2`, and compiles the 9 .cpp files of the synth + reverb chain. No `cd`, nothing written to the repo or `build/`, no cmake.

**Fixed parameters:**
- 80 BPM, 4/4, A Dorian, root A at contract semi 12 (220 Hz).
- COLOR 0.7 (4 notes).
- Harmonic rhythm 2 bars per chord.
- Progression i–III–VII–IV–i–III–VII–i for all candidates except C3, whose roots come from its cadence notes. All moves share at least 2 common tones except the final VII–i, which shares 1 (measured).
- Length 16 bars = 48 s, plus a 4 s tail.

**Seed policy.** Part-style seeds `0x1234abcd + (k+2)*2654435761` with k=0,1,2,3. k=0 is deck A's real PITCH-lane seed (engine-map.md:537-539), so round 3 can match a round-1 file. Never small seeds: xorshift32 cold-starts bias phrases low (engine-map.md:535-553).

**Outputs:**
- **Round 1:** `r1_<letter>_k<k>.wav`, 5 candidates × 3 seeds = 15 files. Letters P–T are assigned at random by `compose.py`, and the key goes to `key_r1.txt`, which stays unopened until scoring is done.
- **Round 2 (winner only):** pairs `r2_<pair>_{x,y}.wav`. Examples:
  - scale vs chord tones
  - PULL p=1 vs p=0.5 (C0)
  - guide-tone anticipation vs none (C2)
  - clean answer vs voice-steal dissolve (C2)
  - 100 % vs 50 % suspended changes (C4)
  - 2 vs 4 bars per chord

  Each pair also has a hidden key.
- **Round 3:** the winner through the real Instrument. Either a scenario JSON for a copy of `render.exe` (C0 and C1 need no code), or a prototype patch in a worktree off main with its own Release build dir. Checked against the CSV observers, because unknown actions are silent no-ops (scenario.cpp:407).

**Approximations to state when the files are played:**
- no FLUX, GRIT or master bus
- fixed timbre
- notes snapped without the quantizer's 0.30-semitone hysteresis and 40 ms slew
- PULL emulated as a bind probability, not the engine's per-fire draw
- the blinding is weak: C1's turn-taking and C3's melody-driven harmony are recognisable by ear, and the "harmony moves enough?" column can only separate C3, because the other four share one fixed progression

## Listening protocol

1. **Round 1, blind.**
   - Bastian gets P–T for seed k=1 in a shuffled order. He plays each file for at least 30 s, at one fixed monitor level.
   - Then seeds k=2 and k=3 in a different order, to check the verdict holds across seeds.
   - Scoring: 1–5 per file on the sheet below. He ranks the top two only after hearing everything.
   - Then the key is opened.
2. **Round 2.** A/B pairs for the winner, order randomised per pair. He picks x or y, or "no difference". A "no difference" means that sub-rule is not worth building.
3. **Round 3.** The winner through the real chain, one file. He judges whether it still sounds like the round-1 file.

Score sheet (one line per file):

| File | Between-ness: does the melody sit between the chords? | Little melodies: do they have a beginning and an end? | Does the harmony move enough? | Would I leave it running 10 min? | One word |
|---|---|---|---|---|---|
| P | 1–5 | 1–5 | 1–5 | 1–5 | |

## From winner to engine

**Shared steps for any winner except C0:**
- Append `SongMode::Chords` (=7) before `kCount` and give it its own case in `song_symbol_at`. Without one it falls into Mirror's parity branch (song_form.h:88-104). It must start on symbol 0 for the RESET contract (lane.h:75-81), and the spec must say what A and B mean for the winner.
- Appending moves `clamp_song`'s top value (song_form.h:19-24): an out-of-range `set_song` then lands on Chords instead of Off. Decide whether that is wanted.
- Say which deck's SONG governs a cross-deck winner (C2–C4: SongMode is per lane and per deck) and what the other deck's SONG does meanwhile.
- A rung change fires `new_phrase`, plus `sampler_punch` on a SAMPLER deck (control/control_law.h:851-863); the new rung inherits that.
- `param_table.h:76` → 0..7, 8 steps.
- Append a ladder rung (14→15, never insert; rung 0 stays Off, pinned by test_song_ladder.cpp:16-18).
- Add the word to `kSongWords` (Fireflow.cpp:489-496; `kSongWords[r.song]` reads out of bounds without it) and to its pin in test_panel.py:1084-1091.
- Update the tests that hard-code 13: test_control_params.cpp:62, test_controls_map.cpp:59, test_song_rung_state.cpp, and the comment in test_song_ladder.cpp:30-35 ("14 rungs … norm = x / 13").
- The persist hash changes, which is acceptable under dev alpha.
- The SONG lamp shows the role only if the role table equals the A/B table; C1's `phrase_role()` is separate from `active_pattern`, so it needs its own lamp hook or must be defined as A/B.

| Winner | Hooks | New state |
|---|---|---|
| C0 | none, or one preset rung | none |
| C1 | `phrase_role()` pure function; `_groove_k` override (lane.cpp:754); role-to-chord-size in `Part::_fire_trigger` | one float `_last_melody_q` in Part |
| C2 | tail gate in Instrument beside PULL (instrument.cpp:313-351); lane phase observer; optional guide-tone look-ahead | about 3 floats in Instrument |
| C3 | FOLLOW block; final-open-slot observer; Part override pitch for the next manual fire | one float plus a flag in Part |
| C4 | anti-sync gate; latched-bind flag; resolve-down rule | about 2 floats plus a flag |

A progression source other than the melody note (a degree walk indexed by `phrase_index`, quantized before `build()`) is a separate decision; see question 2.

**Probes that must print before the spec:**
1. Part tick order (confirm): the code says the PULL draw happens in front of the fire's own `_control_tick()` (part.h:380-389); print it.
2. Deck A's own lane at STEP DENSE 0 over 16 phrases (confirm one root forever at VARY 0), and what VARY > 0 does to slot 0.
3. `sizeof(ModLane)` if anything is added at all: the gate fails at 924 B (about 3 B per lane of headroom), and the board size is unmeasured.
4. CHOKE −0.6 against a long-DEC **STEP** deck A: record B's fire times and both decks' duck gains. The code says B may fire in A's tail but gets ducked; this decides whether C2 needs new code at all.
5. For C3: cadence-note variety under `bind_song_cadence` over seeds k=1..8.
6. For C4: does PULL's live follow audibly bend a ringing voice?
7. Leader-mask lag near chord changes (confirm): the code says up to `Center::kCtrlInterval` samples, about 2 ms (instrument.cpp:333-351); print it.
8. Rev A SONG pot hover at a rung seam (`knob_from_pot`'s `std::round` defeats `hyst_step`: 999 of 1000 re-rolls in the probe).

**Tests:**
- the new mode's symbol table, and symbol 0 at phase 0
- role gate behaviour, with the RED proved once
- ladder structure
- `ctrl_identity` and `wave_formant_sweep` hashes unchanged: the COLOR-0 path must still bail out at n≤1 before `_stab_rng` (gotchas.md:22-26)
- `ctest -E "^reva_"`

**Where the work happens:** a worktree off main after rec-snap merges, built Release with the main repo's `env.sh` sourced in a script. VCV work ends with `build-local.sh install`.

**Explicitly out of scope:**
- extra voices (no CPU headroom: 939 k worst against clicks from about 945 k)
- a reserved melody voice on one deck, unless two decks are rejected
- a new panel cell or a shift layer
- inversion control
- BODY, BBD and SAMPLER support
- retuning by-ear constants: kFlowNoteMinS, kStabSpreadS, chord spans and edges, FLOW sustain 0.7, and the CHOKE duck constants outside their own pass
- patch migration
- bit-exactness gates on audition WAVs

## Open questions

1. **May this mode use both decks** (A plays chords, B plays the melody)? If not, the only one-deck options are taking turns (C1) or a synth-engine change to reserve a melody voice.
2. **Should the chords progress on their own** (a new degree-walk source in Part), or keep today's law, where the chord root is the melody note? Only C3 works without a new source (its chords follow the melody's cadence notes). C0, C2 and C4 need one, and C1 needs at least the `_last_melody_q` rule: today a STEP DENSE 0 chord deck plays one root forever.
3. **How long is "long"?** 2 bars at 80 BPM (6 s) or 4–8 bars (12–24 s)? This sets round 1's harmonic rhythm.
4. **Where should the mode live:** an appended SONG rung (ladder and persist hash change, narrower rungs on the 9 mm pot), or a zone of PULL/CHOKE?
5. **Should PULL's first listening pass happen in this round?** C0 is effectively that pass, and kPullDead 0.03 and the linear bind curve are untuned.