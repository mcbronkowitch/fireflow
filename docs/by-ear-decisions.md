# By-ear decisions — tuned by listening, do not "fix"

Moved out of session memory on 2026-08-19 so that every agent — including
subagents, which never see the session memory — reads the same list. Every
value below looks like it could be "corrected" from a spec or from first
principles, but it was set (or confirmed) by Bastian listening, and must not
be reverted or "finished" without a new listening pass. Entries about the
flow/terrain layer struck on 2026-08-14 are omitted; their record is in
`docs/attic/`. The cross-cutting code traps are in
[`docs/gotchas.md`](gotchas.md).

The governing philosophy (Bastian's stated rule for the whole instrument,
from M5a): **a ceiling stays only if it prevents an actual failure, never
merely an unpleasant sound; where opening a path lets a value diverge, add
the bounding nonlinearity the instrument already has rather than re-imposing
a ceiling.**

## Filter, dynamics & master

- **Resonance cap 0.90, moved DOWN from the inherited 0.95.** The 0.95 was
  already unstable under a swept cutoff. 0.90 is a headroom limit, not a
  divergence limit — only 1.0 actually diverges. Do not restore 0.95.
- **Dynamics gain computer** (M4.6 by-ear round): `kEnvCeiling 0.4` (−8 dBFS
  post-comp envelope cap), asymmetric gain smoothing (down 0.5 ms / up 2 ms),
  attack scaling 5→2 ms with amount. `kMakeupComp` stays 0.9 — it is only
  safe *because* of the envelope ceiling; don't raise the makeup without
  re-checking the ceiling.
- **Fast-tanh CPU cut (v2.6.0) cleared by listening**, not by measurement:
  echo bloom at max feedback and master DRIVE at high settings were both
  checked by ear and judged inaudible despite the approximation error.
- **LVL/COMP's zone split and taper were accepted by ear** ("comp ist ok
  jetzt"): split `kLvlCompSplit = 0.6`, taper `kCompShape = 0.6`. The merged
  knob originally mapped the compressor linearly into its top fifth and the
  volume "pulled hard" there (make-up goes as `1 + 9a²`). The fix needs BOTH
  halves; widening alone only moves the cliff. `kCompTop` stays **0.7** —
  lowering it to ~0.55 is a known available lever that was deliberately not
  pulled; pulling it is a new voicing decision needing its own pass. The init
  value IS `kLvlCompSplit` and must move with it. Guarded by
  `test_comp_zone_makeup_rises_evenly_across_its_travel` in
  `host/vcv/res/test_panel.py`.

## Reverb

- **`kWetGain = 0.40f` (−8 dB) wet trim**, ear-tunable range 0.40–0.50, set
  after a 4-pass clipping saga in M4.5 (bloom plateaus near full scale at the
  Oliverb output taps).
- **MIX default 0.25 (M4.8)**, deliberately leaner than the old fixed join.
  MIX multiplies on top of `kWetGain`, so the pre-M4.8 balance actually sits
  at MIX 0.5 — the spec's claim that 0.25 ≈ old balance was an arithmetic
  error, corrected after listening.
- **DIFFUSION AP coefficient `0.90·n`, boot 0.7** (M4.9), A/B'd by ear
  against the old fixed diffusion; boot 0.7 → coeff 0.63 ≈ the old stock
  ~0.625 room.
- **The reverb mod split: three engine controls, one knob.** Since 2026-07-15
  the Oliverb-based `AmbientReverb` exposes three separate controls —
  DIFFUSION (`set_diffusion`, allpass coefficient only, pure room density),
  SMEAR (`set_diffuser_mod_depth`, LFO depth on the 4 input diffusers — what
  melts slap-echoes into a wet wash) and MOD (`set_mod_depth`, LFO depth on
  the tail delays — the audible pitch-wobble). Coupling all mod to DIFFUSION
  had meant taming the wobble also killed the wash. Since the 2026-08-09
  control reduction only **DIFF has a knob**; SMEAR and MOD are host-pushed
  constants (`set_reverb_smear(0.30f)`, `set_reverb_mod(0.15f)` in
  `Fireflow.cpp` and `bench/audition/init_patch.cpp`). **Confirmed by ear
  2026-08-09** ("smear ist ok") even though 0.30 nearly halves the old
  snapshot's 0.484 — the numbers came from Bastian's stated practice, not
  from the patch. Do not restore 0.484/0.237, do not propose giving SMEAR or
  WOBL a knob back, and do not treat the constants as provisional. The split
  stays fully driveable from render scenarios (`reverb_wash.json`).

## Modulation & sequencing

- **Pitch must not be a drastic target of other (center-section)
  modulations.** Rule set after the M4 by-ear pass, where weather/couple
  demos pitched wildly. `SuperModulator::spot` skips `LANE_PITCH` entirely
  (its comment: the melody is the anchor everything else stumbles around);
  the other 4 lanes still lurch. Applies to any future modulation source
  aimed at the pitch lane.
- **CHOKE's snapped-state semantics**, arrived at after three separate
  play-tests. A continuous curve "felt broken"; the final form (rev. 4) is
  five snapped states — off, per-side "choke" (blocked while the priority
  side holds a note), per-side "choke thru decay" (blocked through the
  audible decay, env > 1e-4). Do not go back to a continuous or probabilistic
  curve without re-running the play-test.
- **Entropy sequencer part-A LEVEL target turned off** (by-ear round of
  2026-07-12): STEP + S&H on LEVEL at low SMOOTH clicks audibly by
  construction on sustained tones — not a bug to "fix" by changing SMOOTH.
- **`kFlowNoteMinS = 0.060f` and `kFlowSlewFrac = 0.35f` in `lane.h` stand as
  chosen** (FLOW melody pass, accepted by ear 2026-08-13, "doch sind alle
  ok"). Both were first guesses set by *arithmetic*; the deferred listening
  pass happened and confirmed them. Code comments saying "A FIRST GUESS SET
  BY ARITHMETIC, not by ear" describe their origin, not an open item. The
  round's largest sonic change — a drone getting a stepped sequence where it
  used to get a smooth sine drift — was accepted; do not "restore" the glide.

## FLUX (BBD echo)

From the FLUX BBD rewrite's ear pass (2026-07-27/28) and its close-out:

- **FLUX DRIVE's ceiling is FIXED at `kSatCeil`, never `kSatCeil / g`.** The
  inverse-gain makeup law held small-signal loop gain at unity but shrank the
  saturator's ceiling by the dB range's own width — measured as 14 dB of
  echo-level loss across the knob, which is why DRIVE was first reported
  inaudible. A later reading may see the missing makeup gain as an omission;
  it is not. `kDriveLoDb`/`kDriveHiDb` = 0/+12 were chosen from a measured
  bracket, and **the corrected DRIVE was accepted by ear on 2026-07-30**
  ("drive ist ok") — nothing is owed to this work. Raising `kDriveHiDb` is an
  available lever but a new voicing decision.
- **`Flux::set_feedback` divides `bbd_drive_gain()` out of the coefficient,
  and the division belongs in `Flux`, not `BbdEcho`.** Without it the
  FEEDBACK setting for a 15 s tail slid 0.57 → 0.14 across DRIVE ("DRIVE
  schickt das Delay früh ins blooming feedback"). `BbdEcho` is left honest
  (its loop gain really is `feedback × g`, held by a test), so the division
  looks redundant from inside the model. It is not — removing it restores
  the fight.
- **FEEDBACK above ~0.56 self-oscillating at any DRIVE is NOT a bug.** A
  path-dependence probe found no latched state (hot and cold histories
  converge). DRIVE never held the tail up; only FEEDBACK does.
- **FLUX stays MONO — decided 2026-07-30, closed.** The stereo `BbdEcho` pair
  was collapsed to one line per deck (mono input `0.5·(l+r)`, fanned back to
  both channels): the centred echo is the intended sound and the lost width
  does not need recovering. Do not treat the collapse as provisional, do not
  "restore" stereo FLUX, do not add a widening layer. A listening pair exists
  if ever reopened (`host/render/scenarios/flux_mono_{dry,verb}.json`). The
  mono/stereo angle is essentially mined out: the synth voice, `Comp` and
  `Grit` already share state across L and R — what differs per channel is
  duplicated *calls*, not duplicated state.

## BODY

- **`kBrightTiltDb = 17`, `kBrightTiltShape = 3` in `body_voice.cpp`** — the
  loudness BodyVoice derives from brightness, because its FILTER is a timbre
  parameter that produces almost no loudness on its own (10.8 dB end to end
  vs SYNTH's 27.9). Confirmed by ear ("funktioniert, ist jetzt schön soft").
  FILT being a much stronger volume control on BODY than on SYNTH (−8.0 dB at
  −0.2 vs −2.6) is the point, not a bug — without it the left-end fade must
  erase the whole signal inside 0.2 of knob travel (the cliff this repaired;
  see [`gotchas.md`](gotchas.md)).
- **The dead zone below FILT −0.6 at a centred FILTER lane was left in place
  deliberately.** It affects every engine, comes from the fade window's
  geometry, and touches the documented invariant
  `kFiltLeftScale >= 1 + kFiltFadeRange` in `synth_engine.h`.
- **`kFlowSatCeil = 0.4f` in `body_voice.cpp`** — the soft ceiling on BODY's
  output while sustaining, confirmed by ear ("ja passt") after the measured
  worst FLOW peak fell from 629 to 0.283. It is gated on `_sustaining` and
  must stay that way: struck notes reach 1.088 at some settings, so an
  ungated ceiling would compress them too and change a character tuned by
  ear.

## Sampler

From the 2026-07-22 fix pass (register IDs in the test suite):

- **SCAN jumping to the MELO knob position on an ENG flip is NOT a bug
  (F-07).** A soft-takeover was built and deliberately reverted:
  `host/vcv/README.md` states under "Known limitations" that knob position
  holds across engines with no separate memory and no soft takeover, on
  purpose, because the hardware has none and both sides should behave alike.
  A future reviewer will re-report this — it is a design decision.
- **Feedback saturation knee `kFbSatKnee = 0.90`** (coefficient, not knob),
  measured not guessed. It leaves a factor-3.2 discontinuity at the
  threshold, accepted: removing it needs unconditional `tanh`, which costs
  the shipped default 57 % of its level. Do not "finish" this by making the
  saturation unconditional.
- **MOTION's scatter on a sampler deck (F-04, variant a).** The hard clamp
  that folds away the lane's negative half — chosen over deleting the
  scatter — is still the accepted form, judged by ear from the
  `sampler_extremes` render, not an oversight. What changed since (voice-
  knobs-dpth-edge, task 1): the base used to be discarded on a sampler deck,
  with MOD as the only control; DPTH now writes that base and the sampler
  reads it halved (`sampler_cfg::kMotionBaseScale`), so DPTH moves the
  scatter too. That also makes the pulse-vs-breathe question DPTH-dependent
  rather than unconditional: at DPTH 0 the clamp still throws the whole
  negative half away and the scatter *pulses*; above DPTH 0 the sum sits off
  the floor and the clamp should bite less, softening toward a *breathe* —
  unmeasured where or how far. See the open-question comment above the
  `ENGINE_SAMPLER` block in `part.cpp` for the two named alternative
  formulas, still unresolved.
- **`kSpawnHeadroom = 2`.** Caps live grains at `ceil(overlap) + 2`, and
  through `len_ceil` it also caps how far tape mode may stretch a grain — one
  constant, two jobs. Bastian chose 2 over 1 to keep tape's smear out to ~4
  semitones down instead of ~2, paying 1.18 instead of 1.14 solo
  peak-to-mean. 1 is **not** the "better default"; it is the other side of
  the trade. Table and attribution sit at the constant in
  `sampler_config.h`.

## FEED

The engine shipped with every constant below the "by ear, first try" line in
`engine/feed/feed_config.h` at its first guess. These are the ones a listening
pass has since settled; the rest are still first guesses and still Bastian's
to confirm.

- **`kFbBaseCycles` 0.14, raised from 0.08** (2026-08-19). Chosen off a
  six-render A/B that crossed `kDampFixedHz` with `kFbBaseCycles` and stepped
  BOND 0 → 1 over 26 s. The brief was *"ruhig hart, filtern kann ich selbst
  dahinter"*: the escalation is what the engine is for, and its top end is the
  output FILT's problem. **It deliberately sits above the 0.08 at which the
  bank alone was measured to tip** — that ceiling is still a real
  measurement, and this value knowingly exceeds it. Do not reconcile the two.
- **The in-loop damp stays BRIGHT at `kDampFixedHz` 3200 Hz.** Darkening it
  (1200 Hz and 500 Hz were both rendered) was explicitly rejected: it removes
  the brightness for every patch instead of leaving it under a knob. If a
  later session wants a darker coupling, the answer is a knob, not this
  constant.
- **`kFiltRes` 0 is NOT a by-ear value** — it is measured, and it is in this
  list only so nobody moves it looking for "some resonance". At 0.15 the deck
  exceeds its own saturation ceiling by 4 dB. See
  [`gotchas.md`](gotchas.md) for the `r^0.25` trap behind that.

Still first-try and still open: `kIndexMaxCycles`, `kFbPitchSlope`,
`kFbAttenMin`, `kFbOffsetRange`, `kSpreadKneeCt`, `kSpreadMaxCt`,
`kRatioMagnetTop`, `kRatioMagnetExp`, `kRatioMax`, `kDampFixedHz` (confirmed
only against darker alternatives, not swept), `kFloorFoldStart`,
`kFlowFloorMin`, `kAccentVelFloor`, `kAccentDecFloor`, `kSatCeil`, `kSubMax`,
`kDepthBase`.

### FEED: DPTH and EDGE are yours to turn now

`kDepthBase` (0.5) and `kDampFixedHz` (3200 Hz) are both still **first-try**,
and since 2026-08-19 both have a knob — `DPTH` and `EDGE` in VOICE, one per
deck, on both panels. They boot on exactly those constants, so nothing has
changed until you move them.

What is worth knowing before the listening pass:

- **3200 Hz was only ever confirmed DOWNWARD.** Variants B and C at 1200 and
  500 Hz were rejected by ear on 2026-08-19; nobody has heard 3200 against
  anything brighter, which is why the trim reaches up as far as it reaches
  down.
- **EDGE is a TRIM, not a cutoff, since later the same day.** The 200–16000 Hz
  log rails are gone with the FEED-only setter that owned them: the knob is
  bipolar now, its centre IS `kDampFixedHz`, and its travel is
  `feed_cfg::kEdgeOctaves` — **2.0 octaves, a FIRST VALUE and yours** — either
  side, so ±1 reaches 800 Hz and 12.8 kHz. Widening it hands the player more
  of the aliasing guard; that is a legitimate thing to want to hear, and it is
  what this constant decides.
- **DEPTH at 0.5 is a defensive requirement, not a measurement.** Spec §4 asks
  that it be a good sound because the control had no knob; now that it has one,
  that requirement can be tested instead of assumed.

If a listening pass moves `kDepthBase`, move the CONSTANT in `feed_config.h` —
the DPTH knob default is derived from it and a gate recomputes the derivation.
`kDampFixedHz` needs no such care any more: EDGE boots at its centre whatever
that constant says.

### FEED level parity

- **`kDeckGain` 0.25** (2026-08-19). Not a taste setting — a parity target.
  Bastian's brief: *"die sollen gleich laut sein damit man auch im laufenden
  Betrieb die Engine wechseln kann ohne dass es krasse Lautstärke-Ausreißer
  gibt."* Do not raise it to make FEED "present"; the deck is meant to sit
  where SYNTH sits. Re-measure if `kPairs` changes. Full table in
  [`engine-map.md`](engine-map.md).
- **Open, not decided: BODY sits 10 dB (drone) to 29 dB (struck) below SYNTH**
  on the same probe settings. Under the same parity brief that is a defect,
  but BODY's level rides MATL and EXCIT hard and one setting is not a verdict.
  Needs its own pass before anything moves.

## DPTH and EDGE, on the other five engines

FEED's own DPTH/EDGE items are above, under "FEED: DPTH and EDGE are yours to
turn now" — that section already covers `kDepthBase` and `kDampFixedHz`. This
one covers the first values `docs/superpowers/specs/2026-08-19-voice-knobs-dpth-edge-design.md`
introduced on the other five engines, none of them confirmed by ear yet
(spec §9).

- **Every engine's EDGE octave span and neutral is a FIRST VALUE (spec §9
  items 1 and 5).**

  | Engine | Neutral | Span (octaves) | Constant |
  |---|---|---|---|
  | BODY | RESO-derived corner (`Exciter::_recompute_filter`) | 2.0 | `Exciter::kEdgeOctaves` — `engine/body/exciter.h`, **private** |
  | SYNTH, WAVE | 20 Hz | 3.0 | `SynthEngineT<V>::kEdgeHpNeutralHz` / `kEdgeHpOctaves` — `engine/synth/synth_engine.h` |
  | SAMPLER | 20 Hz | 3.0 | `sampler_cfg::kEdgeHpNeutralHz` / `kEdgeOctaves` — `engine/sampler/sampler_config.h` |
  | BBD | 20 Hz | 3.0 | anonymous-namespace `kEdgeHpNeutralHz` / `kEdgeOctaves` — `engine/parts/bbd_engine.cpp` |

  Spec §9 item 5 asks specifically whether SYNTH/WAVE/SAMPLER's shared
  3.0-octave span wants to split into three, or stay one control with one
  span copied across engines that merely happen to share the "output
  high-pass" shape. Still open. BODY's 2.0 octaves was matched to
  `feed_cfg::kEdgeOctaves` rather than measured for BODY specifically
  (`exciter.h`'s own comment says so) — the probe rule forbids justifying it
  with a number nobody printed for this engine.

- **`sampler_cfg::kMotionBaseScale = 0.5f`.** DPTH's base reaches a sampler
  deck halved, not at face value: knob 0.5 → base 0.25 (jitter window half a
  content length, ORGANIZE/SCAN stay audible), knob 1.0 → base 0.5 (the
  degenerate all-uniform state described under "MOTION's scatter on a
  sampler deck" above), knob 0.0 → base 0 (today's behaviour, the return
  ticket). Whether 0.25 — DPTH's shipped default of 0.5, halved — is the
  right factory point is unconfirmed (spec §9 item 2).

- **BODY's DPTH may be a whisper.** `body_voice.cpp`'s `kDriftDetuneCt = 3.f`
  caps the drift at ±3 cents, and `kDriftPanAmt = 0.25f` moves a pan fan that
  is pinned to centre on BODY's one voice (`BodyVoice::kEngineVoices == 1`)
  — so a full DPTH sweep on a BODY deck buys ±3 cents of pitch wander and
  nothing in the stereo field. If the listening pass finds `SWAY` too small
  to be worth a quarter of the VOICE row, `kDriftDetuneCt` is the ceiling to
  move, not the knob (spec §9 item 6).

- **DPTH at the top of its travel on a BBD deck reaches above unity before
  the loss pole.** `_fb_lane = clampf(t[LANE_MOTION], 0.f, 1.f) * 1.2f /
  bbd_drive_gain(_drive)` (`bbd_engine.cpp`), so DPTH 1.0 lands at
  `1.2 / bbd_drive_gain(DRIVE)` — above 1 before the loss pole eats into it.
  Not new territory (`MOD` could already drive the lane to 1.0 today), but
  newly reachable without modulation, under one finger, for the first time.
  A listening item, not a bug (spec §3.4); no probe has run yet.

## Panel & factory patch

- **The reduced panel's two contested legends stand** (confirmed 2026-08-09):
  the TIMING box heading (renamed from TIME so the delay knob could take that
  word) and COUPLE's `FREE|GRID`, at nine characters the longest label on the
  instrument. Do not "shorten for the hardware" — the footprint was measured
  and `res/test_hw_panel.py` guards it.
- **The factory patch is FF_hw_Init.vcvm (2026-08-09), not drone.vcvm.**
  Bastian replaced it wholesale after judging the old one unusable: 54 of 68
  values moved, deck A boots WAVE against SYNTH on deck B (no BODY deck any
  more), and both decks start inside the compressor zone. Comments and specs
  describing the drone.vcvm boot sound describe a closed lineage.
- **DETUNE has per-deck init values matching each deck's booted engine** —
  SYNTH and BODY read the same knob through different scales that only agree
  at full deflection, so there is no single correct number (see the
  control-merge init trap in [`gotchas.md`](gotchas.md)).
