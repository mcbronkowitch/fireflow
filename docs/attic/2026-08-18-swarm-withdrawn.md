# SWARM, withdrawn

**Withdrawn 2026-08-18, on a listening decision.** Not a technical failure, not
a schedule casualty: the engine worked, its tests were green, and Bastian did
not like how it sounded or how it played. That is a sufficient reason and the
only one that mattered.

Two rounds of work are behind this. Round 1 built the engine (merged as
`8b27466`, "merge: SWARM, the additive partial swarm"). Round 2 rebuilt its
character and its modulation after the first listening pass came back with
"it always sounds the same, and modulation does not make it breathe" — that
round never merged.

`main` was wound back to `42f9c79` (release 2.21.5), which is exactly the last
commit before SWARM. Nothing SWARM-related had ever been pushed, so no public
history was rewritten.

## What it was

An additive partial-swarm drone engine: `swarm_cfg::kPartials = 14` sine
partials per deck, retuned rather than re-voiced, running as `ENGINE_SWARM = 6`
— the last entry in the part-engine enum, which is why removing it renumbered
nothing.

## What round 2 changed, and what it measured

Kept here because the measurements outlive the engine.

- **Four spectral characters on the RES slot** — LADDER, VOWEL, BELL, CHOIR,
  selected by a zone reader with hysteresis. RES was chosen because Bastian
  could find no scenario in which resonance meaningfully changed the SWARM
  sound, so the slot was free.
- **HARM became physical inharmonicity**, `f_n = n·f0·√(1 + B·n²)`, replacing a
  detune spread. The complaint it answered was "HARM almost always sounds
  detuned": at the old scaling the 2nd overtone moved **+420 cents at HARM
  0.6**; afterwards it moves **+4.5 cents at HARM 1.0**.
- **FLOOR was folded into the top quarter of FALL**, freeing the slot.
- **The breath was not slow — it was frozen.** The engine's low-MOTION
  "breath" turned out to be float32 rounding: at MOTION 0.15 the bank's
  frequency was *exactly unchanged on 93.9 % of control ticks*, and the gate
  that was supposed to defend it passed with **more** margin when the drift
  constant was set to zero outright. MOTION 0.02–0.05 was completely inert.
  Replacing the shrunken step with a correlated walk (hold and interpolate a
  drift draw over H retargets) brought the frozen fraction down to ~42–47 %
  and the excursion at MOTION 0.02 from 0.00 to 13.2 cents.
- **The slow end never reached its design target.** The spec promised
  reversals of 1–3 s at the bottom of MOTION; the shipped mechanism reached
  **0.2419 s**. Longer holds do reach it — H = 256 gives 2.31 s — but at
  ~93 % frozen ticks, which buys back exactly the freeze the round removed.
  That trade is the honest reason the target was abandoned, and it is worth
  knowing before anyone designs a slow additive drift again.
- **MOTION is a modulation lane, not a knob** (`LANE_MOTION = 3`, lane 4).
  The VCV host never wrote its base, so it sat at Part's default of 0.5 and
  the only thing that moved it in Rack was MOD. **MOTION 0 — the exactly
  static state two fix rounds were spent guaranteeing — was not reachable
  while playing.** This was found on the day the engine was withdrawn and is
  a plausible part of why the breathing never convinced anyone at the panel.

## What was never established

Recorded so nobody inherits these as settled.

- **The hardware CPU gate never ran for round 2.** Both plans owed it. The
  round-1 figure (`2026-08-17-swarm-n-decision.md`, kept here) is what
  `kPartials = 14` rested on — measured at `-O2` with roughly 4× margin —
  and round 2 added two ring reads per partial per tick plus a third random
  walk on top of it, unmeasured.
- **Round 4's text corrections were never re-reviewed.** The fix loop was
  closed early, deliberately, once withdrawal became likely. The behavioural
  changes of rounds 2 and 3 *were* re-reviewed to completion; the round-4
  prose was not.
- **VOWEL cost about 2.5× a LADDER block through denormals** in the partial
  bank. Flush-to-zero collapsed it (10 398 ns → 4 440 ns), but no FTZ/DAZ
  call exists anywhere in `shell/`, `bench/` or `host/render/`. That finding
  is engine-independent and will recur.
- **A recurring documentation defect worth naming**, because it cost four
  rounds: every round's *correction* introduced at least one number measured
  on a different code state or at a different window than the sentence
  claiming it. The probe rule catches this only if the probe is re-run after
  the last edit, not before it.

## What is in the attic, and what is only in the tag

Kept here as documents: both specs, all three plans, and the round-1 CPU
decision.

Everything else — `engine/swarm/` (2 001 lines), the three test files
(2 655 lines), the bench workload rows, the two render scenarios, and the
`itcm-relief` bench infrastructure built for SWARM's `-O3` link pressure —
lives only in the tag.

**The `itcm-relief` work is the one piece worth salvaging separately.** It is
not SWARM-specific: it is a documented route from "the image does not link" to
"it links", with a placement guard and its tests, and the `full` bench profile
still has that problem. It was never verified on hardware.

## How to recover

**Tag:** `attic/swarm-2026-08-18`, at `d03bdbf` — the tip of the abandoned
`swarm-character-and-breath` branch, which contains round 1 as an ancestor.

```bash
git show attic/swarm-2026-08-18:engine/swarm/          # lists the tree
git show attic/swarm-2026-08-18:engine/swarm/swarm_engine.cpp
git log --oneline 42f9c79..attic/swarm-2026-08-18      # everything withdrawn
git diff 42f9c79 attic/swarm-2026-08-18 -- bench/      # the ITCM-relief work
```

To bring the whole thing back: `git checkout -b swarm-revival
attic/swarm-2026-08-18`.
