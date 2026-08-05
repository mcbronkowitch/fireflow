# Flow listening notes

**Date opened:** 2026-08-05
**Scope:** the running logbook for the flow layer's listening phase (Plan A of
`docs/superpowers/specs/2026-08-05-flow-machine-design.md`, §2's
"Plan A first, Plan B does not start until Plan A's calm corner and house
seed have survived a listening pass"). Plan A itself
(`engine/flow/` — terrain generator, six-macro story layer, weather, NEW
gestures, render-host scenario wiring, audio sanity gates) is built and green
(954 test cases, `ctest` 4/4). Nothing below changes that; every item here is
an ear decision, not a bug.

## Open questions on arrival

Seeded from what Tasks 9 and 10 measured while building the audio gates, plus
item 8 from the final whole-branch review — found because they can't be
settled without ears.

1. **Calm corner may be too quiet.** Renders and an independent
   re-measurement both put it at RMS ≈ 0.00092, about −61 dBFS — roughly
   1.5 % of the `kCalmCornerRmsMax` ceiling. The spec (§3) wants a quiet
   background *presence*, not silence. `kCalmCornerRmsMin` in `taste.h` is
   deliberately only a **silence detector**, not a musical target — the
   musical question is open.

   *Amended 2026-08-05 (round-1 prep).* Two corrections to that number.
   First, it is partly an artifact of item 9: `flow_calm_corner.json` runs
   master `0xBEEF`, whose first note does not land until 14.1 s of a 20 s
   render, so 70 % of the measured window is digital silence. Measured from
   the first note onward the same render is **−55.4 dBFS**. Still very
   quiet, so the question stands — but the figure to argue about is −55,
   not −61. Second, the calm corner is **terrain-dependent**: at master
   `0x20` the same all-zero setting measures −41.9 dBFS over 90 s, ~13 dB
   above `0xBEEF`. So "how loud is the calm corner" has no single answer
   until item 2 is settled; the two questions are one question.

   *Answered by ear 2026-08-05 (round 1): inaudible.* See the log. The
   mechanism is worth writing down, because it constrains the fix: **the
   flow layer has no level parameter.** There is no `P_LEVEL_*` in
   `flow_params.h`, and `LANE_LEVEL` is never pushed — the ~30 dB between
   the calm corner and the mid setting is emergent, mostly from three
   bp[0] cells: `P_FILT_A/B` at −0.55…−0.4 (near-closed), `P_REVMIX_A` at
   0.75…0.9 (nearly all wet), and `P_DENSITY_A/B` at 0.02…0.08. So the two
   available fixes are not equivalent: lifting those cells makes the calm
   corner louder *and* brighter and drier, while a real level parameter
   (a new `P_LEVEL_A/B` → `set_target_base(part, LANE_LEVEL, v)`) raises it
   with its character intact. Round 2 finds the target level first, by ear,
   with plain gain on the rendered file — no engine change is needed to
   answer "how loud", and picking the mechanism is cheaper once the number
   is known.

2. **Terrain loudness spread ≈ 15.9 dB** at identical macro settings, no
   blend involved: min RMS 0.0158 (master `0x707`), max 0.0983 (master
   `0x101`). A NEW press can therefore land the player substantially louder
   or quieter. Owner's direction: **pull the very quiet patches up** —
   asymmetric, *not* full per-terrain normalization to a common target,
   which would flatten the loudness contrast that helps terrains feel
   different.

3. **Engine-switch blends dip to near-silence.** Worst measured case,
   master `0x101`: 65.16 dB below a no-press control run, with 4.00 s of the
   6 s blend spent more than 20 dB below it. **Known and accepted for now**
   — do not write this up as a defect. Mechanism: the outgoing engine is
   switched away, the incoming BBD delay line starts empty, and what decays
   is the reverb tail with nothing feeding it. Owner's idea for later: hand
   the transition to the reverb — a crossfade into a long reverb tail across
   the switch, so it covers the gap while the incoming engine primes, which
   costs no second engine instance. Note the constraint that motivated it:
   running two engines in parallel to cross-fade is expensive on the Daisy
   target.

4. **The texture deck's duck opens as a step.** It switches at blend phase
   0, so there is no time before the press for the duck to ramp in; the
   rising half is clipped and the send jumps to the duck peak. Audible.
   One-line fix if wanted: delay the texture switch by ~0.25 s in
   `switch_phase_for`.

5. **`kDuckWetTarget = 0.95` collides with the SPACE story's send ceiling**,
   so at SPACE = 1 the duck does nothing at all — the wettest setting is the
   one where the engine switch is least covered.

6. **A pending discrete is frozen against its own macro for up to 1.5 s** of
   a blend (the hold that keeps a re-press from dragging the carrier deck
   through a terrain it never played). If that reads as a dead knob, the
   answer is to shorten `kCarrierStaggerFrac`, **not** to reintroduce the
   jump.

7. **Discrete churn from quantizer hysteresis.** Under randomized macro
   sweeps `P_SCALE` was measured changing 258 times inside one 6 s blend —
   and 256 times with no button presses at all, so this is pre-existing
   hysteresis chatter, not a NEW-op effect. The committed gate bounds churn
   with macros held *static*; the hysteresis width itself is a listening
   number.

8. **Pressing NEW always changes archetype.** `draw_new` accepts a candidate
   when `distance() >= kDistanceMin`, but `distance()` adds a flat **+0.25**
   whenever the two archetypes differ while `kDistanceMin` is **0.18** — the
   bonus alone clears the bar, so the base patch almost never gets a say.
   Measured over 20 000 random terrain pairs: the base-patch term spans
   0.0711 / 0.1509 / 0.2491 (min / mean / max), and **6 775 of the 6 777
   same-archetype pairs fell below the threshold**; across 3 000 `draw_new`
   calls the result had the same archetype as its origin **0 times**. So from
   a drone you never get another drone, on an instrument whose archetype
   weights are 50 % drone — and the retry loop's 16th try and the
   farthest-candidate fallback are unreachable in practice. This may be
   exactly right for a gesture that means "show me somewhere else", or it may
   be why a drone never persists. The two knobs are `kDistanceMin` (`taste.h`)
   and the flat archetype bonus in `distance()` (`terrain.cpp`): raise the
   first above 0.25, or shrink the second, and the base patch decides again.
   Depends on item 2 — either change alters the reachable set of NEW targets,
   so the ~15.9 dB loudness spread wants re-measuring afterwards.

   *Corrected 2026-08-05 (round-3 prep): both of those knobs are wrong.*
   Raising `kDistanceMin` to 0.30 renders **byte-for-byte the same** eight
   NEW landings as 0.18, and so does shrinking the archetype bonus from 0.25
   to 0.06. Same-archetype rate over 400 origins, sweeping both:

   | bonus \ `kDistanceMin` | 0.18 | 0.14 | 0.10 | 0.06 |
   |---|---|---|---|---|
   | 0.25 | 0 % | 4.2 % | 32.8 % | 34.8 % |
   | 0.12 | 0 % | 4.2 % | 32.8 % | 34.8 % |
   | 0.06 | 0 % | 4.2 % | 32.8 % | 34.8 % |
   | 0.00 | 0 % | 5.0 % | 33.2 % | 34.8 % |

   The bonus column does nothing at all — because at a threshold of 0.18 the
   base-patch term (mean 0.1509) almost never clears the bar by itself, with
   or without a bonus, and below 0.10 nearly every candidate clears it
   whatever the archetype. **The only lever is lowering `kDistanceMin`,** and
   the earlier advice to raise it had the sign backwards.

   The lowering is not free. At 0.10 the mean number of retries inside
   `draw_new` falls to 1.03 — the first candidate is simply accepted, so the
   distance rule stops constraining anything and NEW may land somewhere
   nearly identical to where it started. 0.14 buys 4.2 % at a mean of 1.54
   tries; that is the setting that keeps the rule meaningful, and it is rare
   enough that eight presses from one origin produced no audible difference
   at all. So the real choice is between *a rule that works and almost never
   lets you stay* and *staying often, with no rule* — not a dial between
   them.

9. **A woken terrain can open with many seconds of digital silence.** Found
   while preparing round 1, not by ear. Time from `flow_wake` to the first
   audible sample, all six macros at a mid setting, measured over 17
   terrains: **0.008 s at best, 18.4 s at worst**, with roughly half the
   drone terrains above 4 s. It is flow-specific, not a host artifact — the
   non-flow `ambient_wash.json` renders its first sample at 0.004 s. It is
   also not a stuck level: the level lanes modulate normally throughout, the
   part simply has no voice yet.

   Musically a slow entrance may be exactly right for a drone. The problem
   is elsewhere: on a module whose promise is that something pleasant is
   always playing, **power-on and every NEW press can be followed by ten
   seconds of nothing**, with no way for the player to tell a slow terrain
   from a broken one. That makes it a Glow-panel question as much as a
   `taste.h` one — the cheapest fixes are a first-note-now nudge at wake, or
   an LED that shows the machine is alive before it is audible. Bears on
   item 3: the engine-switch dropout and this share a symptom, and a
   listener will not distinguish them.

The audio gates in `tests/test_flow_audio.cpp` are sanity bounds, not
musical judgements — no NaN, no clipping, RMS inside plausible ranges, a
silence floor. They do not say any of the above sounds right. The numbers
above are what a listening session should try to move.

## Log

**Correction, 2026-08-05: "digital zero" was my render pipeline, not the
engine.** `host/shared/wav_writer.h` writes 16-bit PCM and *truncates*
(`static_cast<int16_t>(v * 32767.f)`), so every sample under 3.05e-5 —
−90.3 dBFS — lands on exactly 0. The zero runs reported below are therefore
"below −90 dBFS", a floor of the tooling; what the engine actually produces
there is unmeasured. Inaudible either way, so no verdict changes, but the
evidence was overstated in three commit messages and in the rows below.

**And the mechanism is not the one those rows assume.** Read from the CSV
across the silent stretch at 75–82 s of `01-calm`: both decks hold four and
two voices the whole time, and the level lanes sit at −0.5 and +0.1, i.e.
nowhere near minimum. What falls is the per-voice envelope — `a_v0` decays
0.0655 → 0.0079 over six seconds, `a_v1` 0.2489 → 0.0300 — and the next
note does not arrive until 79.5 s. So the calm corner does not stop
producing notes and does not duck its level: **its notes decay below
audibility before the next one arrives, and at SPACE = 0 the room is too
small to bridge the gap.** The knobs that bear on it are `P_DENSITY_A/B`
at bp[0] (`{.02,.08}` — spacing ~3.8 s on deck A) against the drone
archetype's `P_DECAY_A/B` base span (`{.6,.95}`), which is already long.

**How to read the numbers in this log.** The dBFS figures beside an ear
verdict are what the file measured, not a threshold the ear resolved. Every
verdict below came from a single pass, on one playback chain, at a monitor
setting nobody wrote down — so what was actually judged is each file
*relative to its neighbours*, and the absolute values only carry meaning
against the round-1 mid setting (−25.6 dBFS) rendered in the same session.
Part B was auditioned in a fixed quiet→loud order, which is the easy way to
find a boundary and also the order most likely to bias where it lands.
Treat these as first bearings, good to maybe ±3 dB, to be re-checked once
the layer can actually hold a level — not as calibration.

| Date | Seed / terrain code | Verdict | `taste.h` change |
|---|---|---|---|
| 2026-08-05 | `F1-00000020-000000000000` | **Round 1.** Calm corner (all six macros at 0): inaudible over the full 90 s even at high monitor gain — item 1 is answered, and the answer is *silence*, not *quiet*. Mid, fully open and the per-macro ride: levels fine, no complaint. | none yet — see below |
| 2026-08-05 | `F1-00000020-000000000000` | **Round 2, part A.** On `06-calm-plus16` (the calm bed at +16 dB): audible to ~4 s, silent 4–9 s, tone 9–30 s, silent 30–39 s. The measurement agrees second for second — and shows worse than "silent": seconds **31–37 are digital zero**, exact 0.0 samples. Of 45 s, 18 are roughly audible and 15 sit at or below −70 dBFS. **The calm corner is not a level problem.** Gain cannot fill a hole. | none — gain hunt stopped |
| 2026-08-05 | `F1-00000020-000000000000` | **Calm-corner level: roughly 25 dB under the mid setting.** `06-calm-plus16` (−50.8 dBFS, against round 1's mid at −25.6) was judged "genau richtig". One file, one pass, no A/B against its neighbours — `05` and `07` were never heard, so the verdict says this level works, not that it beats the ones either side of it. The relationship is the finding; the absolute number is just what that file happened to measure. It also describes a *continuous* background, while the file judged has seven seconds of digital zero in it — so the level is only meaningful after the corner stops cutting out, and wants re-checking then. | direction recorded; no constant changed yet |
| 2026-08-05 | 6 terrains at one mid macro setting | **Round 2, part B — item 2 has a direction, not a threshold.** `10` (`0x12`, −41.8 dBFS) too quiet; OK from `12` (`0x05`, −26.5 dBFS) on, so `11` (`0x0F`, −30.4 dBFS) sits on the wrong side. Read that as *somewhere around −30 to −27 dBFS a terrain stops being acceptable* — the gap between the rejected and accepted file is 3.9 dB, finer than six files in a fixed order can resolve, and the listener said so. What is solid is the shape: the two quietest of six wanted lifting, the rest did not. Loud terrains stay untouched; the asymmetric rule holds. | direction recorded; no constant changed yet |
| 2026-08-05 | 8 terrains, all macros 0, 60 s each | Generalized, so the verdict does not rest on one draw: **6 of 8 terrains contain seconds of literal digital zero** in the calm corner (worst `0x0F` and `0xBEEF`, 12–13 s of 60); `0x26` and `0xC0DE` have none. Calm-corner RMS across the eight spans **−25.1 dBFS (`0xBEEF`) to −62.1 dBFS (`0xC0DE`) — 37 dB.** The `0xBEEF` figure also retires the old −61 dBFS: over 60 s rather than 20 that terrain is the *loudest* of the eight, not the quietest. | pending design decision |
| 2026-08-05 | `F1-00000020-000000000000` | Follow-up measurement on that verdict: the calm corner is **not empty**. 24 note starts on deck A and 6 on deck B in 90 s, a voice sounding 95.7 % / 90.0 % of all ticks, energy sitting in 160–640 Hz. Typical 10 s windows run −63 to −84 dBFS; the −41.9 dBFS whole-file figure is one isolated event near 34 s carrying the average. So it is a bed at roughly the right pitch and continuity, ~25–30 dB below audibility. | pending: target level to be found by ear (round 2) |
